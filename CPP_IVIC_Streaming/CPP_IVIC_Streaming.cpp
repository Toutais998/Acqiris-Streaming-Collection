#include "../include/LibTool.h"
#include "AqMD3.h"
// #include "../include/IviVisaType.h"

#include <iomanip>
#include <iostream>
using std::cerr;
using std::cout;
using std::hex;
#include <vector>
using std::vector;
#include <stdexcept>
using std::runtime_error;
#include <chrono>
using std::chrono::microseconds;
using std::chrono::seconds;
#include <thread>
using std::this_thread::sleep_for;
#include <algorithm>
#include <cmath>
#include <atomic>
#include <condition_variable>
#include <ctime> // 添加这个头文件以获取当前时间
#include <fstream>
#include <functional>
#include <mutex>
#include <queue>
#define NOMINMAX
#include <Windows.h> // 中文：用Win32查询剩余空间，兼容当前Release的C++标准。
#include <io.h> // 中文：_commit在测试结束时把文件刷新到系统存储层。

// 宏定义，用于检测API调用结果
#define checkApiCall(f)     \
    do                      \
    {                       \
        ViStatus s = f;     \
        testApiCall(s, #f); \
    } while (false)

// 定义一个整型采样数据缓冲区类型
typedef std::vector<int32_t> FetchBuffer;

//! 检查函数调用结果，失败时抛异常
void testApiCall(ViStatus status, char const *functionName);

//! 读取指定流中可用的数据元素
LibTool::ArraySegment<int32_t>
FetchAvailableElements(ViSession session, ViConstString streamName,
                       ViInt64 maxElementsToFetch, FetchBuffer &buffer);

//! 读取指定数量的数据元素

//! 将一条波形记录写入输出流

//! 根据仪器型号返回时间戳的周期（单位秒）
double GetTimestampPeriodForModel(std::string const &model);

// --- 数据块结构体 ---
struct DataChunk
{
    std::vector<uint8_t> buffer;
    size_t validBytes;
    size_t offsetBytes=0; // 中文：驱动 firstElement 是当前缓冲内偏移，不是样本编号。
};

// --- 用于多线程的全局变量 (内存池) ---
std::queue<std::vector<uint8_t>> g_freeBufferQueue;
std::mutex g_freeBufferMutex;
std::condition_variable g_freeBufferCondVar;

std::queue<DataChunk> g_dataQueue;
std::mutex g_dataMutex;
std::condition_variable g_dataCondVar;

std::atomic<bool> g_acquisitionFinished;
std::atomic<bool> g_writeFailed{false};
std::atomic<uint64_t> g_writtenBytes{0};
double g_writeMs=0, g_maxWriteMs=0; // 中文：仅写线程修改，join后读取。
ViInt64 g_markerRemainingMax=0;

// 命名空间内定义配置参数
namespace
{
    // ================= 用户参数区：一般只修改这一处 =================
    enum class TriggerMode { LineOnly, LineLaser };
    TriggerMode triggerMode = TriggerMode::LineOnly; // 默认单触发；双信号改LineLaser，或--trigger dual。
    bool fixedDurationEnabled = false; // false按帧数；true固定秒数，两者互斥。
    double fixedDurationSeconds = 5.0; // 从Initiate成功起计时，包括等触发；写队列随后排空。
    ViInt64 framesToAcquire = 1; // 帧模式目标帧数。
    ViInt64 pixelsPerLine = 512; // 方形图像，同时决定每帧行数。
    double linePeriod = 6828e-6; // ScanImage行周期，秒。
    double lineActiveDuty = 0.9; // 有效扫描占比，不能仅凭linePeriod推断。
    double frameFlyback = 1e-3; // 每帧额外间隔，秒。
    double sampleRate = 1e9; // 提高采样率前须重新验证PCIe和磁盘。
    double dualGuardSeconds = 4.8e-6; // 双信号保护尾部，不参与MATLAB像素积分。
    double lineTriggerLevel = 1.5; // TRG IN实际50Ω阈值，原LINE约0–3.1/3.23V。
    double laserTriggerLevel = 1.0; // 激光50Ω约0–2V中点；1MΩ输出0–4V不可直接使用。
    double range = 2.5; // IN1满量程跨度V。
    double offset = 1.25; // 原单极性配置；零中心正弦测50Ω幅值后可考虑0。
    double startWaitSeconds = 30.0; // 帧模式arm后等待首条record的超时。
    double completionMarginSeconds = 10.0; // 首条后理论扫描时长之外的超时裕量。
    double timingToleranceSeconds = 2e-6; // 行/帧间隔允许误差，异常仍保存并标记。
    double deadTime = 32e-9; // 名义参考，不配置硬件、不是实测死区。
    char const* outputDirectory = "D:\\Acq_Storage";
    // ================= 参数区结束；以下为系统设置 =================
    int const bufferPoolCount = 8; // 有限背压，禁止盲目增加缓冲。
    double const ioTimeoutSeconds = 2.0;
    ViChar resource[] = "PXI1::0::0::INSTR";
    ViChar options[] = "Simulate=false, DriverSetup= Model=SA230P";
    ViInt32 const streamingMode = AQMD3_VAL_STREAMING_MODE_TRIGGERED;
    ViInt32 const acquisitionMode = AQMD3_VAL_ACQUISITION_MODE_NORMAL;
    ViInt32 const coupling = AQMD3_VAL_VERTICAL_COUPLING_DC;
    ViConstString triggerSource = "External1";
    ViInt32 const triggerSlope = AQMD3_VAL_TRIGGER_SLOPE_POSITIVE;
    ViConstString sampleStreamName = "StreamCh1";
    ViConstString markerStreamName = "MarkersCh1";
    ViInt64 const maxMarkerElements = LibTool::StandardStreaming::NbrTriggerMarkerElements;
    ViSession g_session = VI_NULL; // API报错使用当前会话，不读无关全局错误队列。
} // namespace

// --- 文件写入线程函数 (使用C风格I/O以提升性能) ---
void fileWriter(FILE *outputFile, size_t &totalDataWritten)
{
    while (true)
    {
        DataChunk chunk;
        {
            std::unique_lock<std::mutex> lock(g_dataMutex);
            // 等待直到队列中有数据或采集结束
            g_dataCondVar.wait(
                lock, []
                { return !g_dataQueue.empty() || g_acquisitionFinished; });

            if (!g_dataQueue.empty())
            {
                // 从队列中取出数据块
                chunk = std::move(g_dataQueue.front());
                g_dataQueue.pop();
            }
            else if (g_acquisitionFinished)
            {
                // 队列为空且采集已结束，则退出线程
                break;
            }
        } // 锁在这里被释放

        if (chunk.validBytes > 0)
        {
            auto writeStart=std::chrono::steady_clock::now();
            size_t written=fwrite(chunk.buffer.data()+chunk.offsetBytes, 1, chunk.validBytes, outputFile);
            double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-writeStart).count();
            g_writeMs+=ms; g_maxWriteMs=std::max(g_maxWriteMs,ms);
            g_writtenBytes+=written;
            totalDataWritten += written;
            if(written!=chunk.validBytes) g_writeFailed=true; // 中文：短写必须可见，不能报告为成功。

            // 将用完的缓冲区归还给空闲池
            {
                std::lock_guard<std::mutex> lock(g_freeBufferMutex);
                g_freeBufferQueue.push(std::move(chunk.buffer));
            }
            g_freeBufferCondVar.notify_one();
        }
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 主程序入口
//////////////////////////////////////////////////////////////////////////////////////////////////////////////

// 中文诊断入口：参数为样本数、完整记录数、每轮是否复现旧版 Sleep(1us)、是否落盘。
// 显式不落盘仅用于诊断，所有取回样本均计数；不可把诊断结果当正式保存数据。
void Diagnose(ViSession session, ViInt64 samples, int target, bool legacySleep,
              bool disk, FILE* file, double timestampPeriod)
{
    using Clock = std::chrono::steady_clock;
    ViInt64 grain=0, mg=0;
    checkApiCall(AqMD3_GetAttributeViInt64(session, sampleStreamName, AQMD3_ATTR_STREAM_GRANULARITY_IN_BYTES, &grain));
    checkApiCall(AqMD3_GetAttributeViInt64(session, markerStreamName, AQMD3_ATTR_STREAM_GRANULARITY_IN_BYTES, &mg));
    FetchBuffer data(size_t(samples/2 + samples/4 + grain/4)), markers(size_t(16+mg/4));
    std::vector<LibTool::TriggerMarker> decoded;
    std::vector<double> completed;
    decoded.reserve(target); completed.reserve(target);
    ViInt64 bytes=0, maxRemaining=0, maxMarkerRemaining=0, maxFirst=0;
    int empty=0, count=0;
    double fetchMs=0, sleepMs=0, writeMs=0;
    bool pending=false;
    cout << "DIAG samples=" << samples << " target=" << target
         << " legacy_sleep=" << legacySleep << " disk=" << disk
         << " (disk=0: DIAGNOSTIC DATA NOT SAVED)\n";
    auto begin=Clock::now();
    checkApiCall(AqMD3_InitiateAcquisition(session));
    while(count<target && std::chrono::duration<double>(Clock::now()-begin).count()<10.0)
    {
        if(!pending)
        {
            ViInt64 remain=0, actual=0, first=0;
            checkApiCall(AqMD3_StreamFetchDataInt32(session, markerStreamName,16,markers.size(),reinterpret_cast<ViInt32*>(markers.data()),&remain,&actual,&first));
            maxMarkerRemaining=std::max(maxMarkerRemaining,remain);
            if(actual!=0 && actual!=16) throw runtime_error("Incomplete marker: diagnostic stopped");
            if(first<0 || actual<0 || first+actual>ViInt64(markers.size())) throw runtime_error("Marker bounds");
            if(actual==16)
            {
                LibTool::ArraySegment<int32_t> segment(markers,size_t(first),size_t(actual));
                decoded.push_back(LibTool::StandardStreaming::DecodeTriggerMarker(segment));
                pending=true;
            }
        }
        if(pending)
        {
            ViInt64 remain=0, actual=0, first=0;
            auto t=Clock::now();
            checkApiCall(AqMD3_StreamFetchDataInt32(session,sampleStreamName,samples/2,data.size(),reinterpret_cast<ViInt32*>(data.data()),&remain,&actual,&first));
            fetchMs+=std::chrono::duration<double,std::milli>(Clock::now()-t).count();
            maxRemaining=std::max(maxRemaining,remain); maxFirst=std::max(maxFirst,first);
            if(first<0 || actual<0 || first+actual>ViInt64(data.size())) throw runtime_error("Sample bounds");
            if(actual && actual!=samples/2) throw runtime_error("Partial record: diagnostic stopped without consuming next marker");
            if(actual)
            {
                bytes+=actual*4;
                if(disk)
                {
                    t=Clock::now();
                    if(fwrite(data.data()+first,4,size_t(actual),file)!=size_t(actual)) throw runtime_error("Short disk write");
                    writeMs+=std::chrono::duration<double,std::milli>(Clock::now()-t).count();
                }
                ++count; pending=false;
                completed.push_back(std::chrono::duration<double>(Clock::now()-begin).count());
            }
            else ++empty; // 中文：marker 已到、样本未到时必须保留 pending，不能消费下一个 marker。
        }
        if(legacySleep)
        {
            auto t=Clock::now(); sleep_for(microseconds(1));
            sleepMs+=std::chrono::duration<double,std::milli>(Clock::now()-t).count();
        }
        else std::this_thread::yield();
    }
    double elapsed=std::chrono::duration<double>(Clock::now()-begin).count();
    checkApiCall(AqMD3_Abort(session)); // 中文：到达诊断上限立即停止，尚未读取的板端尾部明确不纳入此测试。
    if(fflush(file)!=0) throw runtime_error("Flush failed");
    cout << std::dec << std::setprecision(12) << "DIAG_RESULT records=" << count << " markers=" << decoded.size()
         << " bytes=" << bytes << " elapsed_s=" << elapsed << " fetch_ms=" << fetchMs
         << " sleep_ms=" << sleepMs << " write_ms=" << writeMs << " empty_fetch=" << empty
         << " max_first=" << maxFirst << " max_sample_remaining=" << maxRemaining
         << " max_marker_remaining=" << maxMarkerRemaining << '\n';
    for(size_t i=0;i<completed.size();++i)
        cout << "MARKER n=" << i << " index=" << decoded[i].recordIndex
             << " tick=" << decoded[i].absoluteSampleIndex
             << " dt_ms=" << (i ? (decoded[i].absoluteSampleIndex-decoded[i-1].absoluteSampleIndex)*timestampPeriod*1000 : 0)
             << " host_s=" << completed[i] << '\n';
    if(count!=target) throw runtime_error("Diagnostic timeout: requested record count not reached");
}

int main(int argc, char** argv)
{
    bool frameMode = !fixedDurationEnabled, discard = false, planOnly = false;
    int64_t requestedFrames = framesToAcquire;
    bool diagnostic = argc > 1 && std::string(argv[1]) == "--diag";
    ViInt64 diagnosticSamples = 0;
    int target = 10; bool legacySleep = false, diagnosticDisk = false;
    try {
        bool stopSpecified = false;
        auto number = [](const std::string& text) {
            size_t end = 0; double v = std::stod(text, &end);
            if(end != text.size() || !std::isfinite(v)) throw runtime_error("Invalid numeric value");
            return v;
        };
        auto integer = [&](const std::string& text) {
            double v = number(text);
            if(v != std::floor(v) || v < 1 || v > 1000000) throw runtime_error("Invalid integer");
            return static_cast<ViInt64>(v);
        };
        if(diagnostic) {
            if(argc != 6) throw runtime_error("Usage: --diag samples records legacySleep disk");
            diagnosticSamples = std::stoll(argv[2]); target = std::stoi(argv[3]);
            legacySleep = std::stoi(argv[4]) != 0; diagnosticDisk = std::stoi(argv[5]) != 0;
            if(diagnosticSamples < 1024 || diagnosticSamples > 8193600 || diagnosticSamples % 64 || target < 1 || target > 600)
                throw runtime_error("Invalid diagnostic size");
            discard = !diagnosticDisk; frameMode = false;
        } else for(int i = 1; i < argc; ++i) {
            std::string a(argv[i]);
            auto value = [&]() -> std::string { if(++i >= argc) throw runtime_error("Missing argument"); return argv[i]; };
            if(a == "--help") {
                cout << "Usage: [--trigger single|dual] [--frames N | --frame | --seconds S]\n"
                     << " [--pixels N] [--line-us U] [--flyback-ms M] [--offset V] [--discard] [--plan]\n";
                return 0;
            }
            if(a == "--trigger") {
                auto v = value();
                if(v != "single" && v != "dual") throw runtime_error("Unknown trigger mode");
                triggerMode = v == "dual" ? TriggerMode::LineLaser : TriggerMode::LineOnly;
            } else if(a == "--frames" || a == "--frame" || a == "--seconds") {
                if(stopSpecified) throw runtime_error("Choose frames OR seconds, not both");
                stopSpecified = true; frameMode = a != "--seconds";
                if(a == "--seconds") fixedDurationSeconds = number(value());
                else requestedFrames = a == "--frame" ? 1 : integer(value());
            } else if(a == "--pixels") pixelsPerLine = integer(value());
            else if(a == "--line-us") linePeriod = number(value()) * 1e-6;
            else if(a == "--flyback-ms") frameFlyback = number(value()) * 1e-3;
            else if(a == "--offset") offset = number(value());
            else if(a == "--discard") discard = true;
            else if(a == "--plan") planOnly = true;
            else throw runtime_error("Unknown argument: " + a);
        }
        if(requestedFrames < 1 || requestedFrames > 100000 || pixelsPerLine < 1 || pixelsPerLine > 16384
            || !std::isfinite(sampleRate) || sampleRate <= 0 || sampleRate > 4e9
            || !std::isfinite(linePeriod) || linePeriod <= 0 || linePeriod > 1
            || !std::isfinite(lineActiveDuty) || lineActiveDuty <= 0 || lineActiveDuty >= 1
            || !std::isfinite(frameFlyback) || frameFlyback < 0 || frameFlyback > 60
            || !std::isfinite(dualGuardSeconds) || dualGuardSeconds < 0
            || !std::isfinite(fixedDurationSeconds) || fixedDurationSeconds <= 0 || fixedDurationSeconds > 86400
            || startWaitSeconds <= 0 || completionMarginSeconds <= 0)
            throw runtime_error("Configuration outside supported limits");
    } catch(const std::exception& e) { cerr << e.what() << "\nUse --help\n"; return 2; }
    bool const dual = triggerMode == TriggerMode::LineLaser;
    ViInt64 const linesPerFrame = pixelsPerLine;
    ViInt64 const activeLineSamples = static_cast<ViInt64>(std::llround(linePeriod * lineActiveDuty * sampleRate / 64.0)) * 64;
    ViInt64 const guardSamples = dual ? static_cast<ViInt64>(std::ceil(dualGuardSeconds * sampleRate / 64.0)) * 64 : 0;
    ViInt64 const recordSize = diagnostic ? diagnosticSamples : activeLineSamples + guardSamples;
    if(recordSize < 1024 || activeLineSamples < pixelsPerLine || recordSize / sampleRate >= linePeriod) {
        cerr << "Record must fit inside line period; active samples must cover all pixels\n"; return 2;
    }
    ViInt64 const nbrRecordElements = recordSize / 2;
    ViInt64 const targetRecords = requestedFrames * linesPerFrame;
    double const pixelPeriod = activeLineSamples / sampleRate / pixelsPerLine;
    double const framePeriod = linesPerFrame * linePeriod + frameFlyback;
    double const nominalDuration = requestedFrames * framePeriod;
    // 中文：首条触发至最后record结束，不含最后一帧之后的flyback。
    double const expectedCaptureSeconds = (targetRecords - 1) * linePeriod
        + (requestedFrames - 1) * frameFlyback + recordSize / sampleRate;
    double const triggerLevel = dual ? laserTriggerLevel : lineTriggerLevel;
    cout << std::setprecision(12) << "CONFIG trigger=" << (dual ? "dual" : "single")
         << " stop=" << (frameMode ? "frames" : "seconds") << " frames=" << requestedFrames
         << " pixels=" << pixelsPerLine << " line_s=" << linePeriod << " active_samples=" << activeLineSamples
         << " guard_samples=" << guardSamples << " record_samples=" << recordSize
         << " nominal_scan_s=" << nominalDuration << " first_trigger_to_last_end_s=" << expectedCaptureSeconds
         << " fixed_s=" << fixedDurationSeconds << " threshold_50ohm_V=" << triggerLevel << '\n';
    if(planOnly) return 0; // 中文：只计算，不连接硬件、不创建文件。
    if(!discard) {
        ULARGE_INTEGER freeBytes{};
        bool spaceOk = GetDiskFreeSpaceExA(outputDirectory, &freeBytes, nullptr, nullptr) != 0;
        // 中文：固定时长按连续满采样上界预算，而非假定一定是每行一次。
        double estimate = diagnostic ? double(target)*recordSize*2 :
            frameMode ? double(targetRecords)*recordSize*2 :
            (fixedDurationSeconds + ioTimeoutSeconds)*sampleRate*2 + recordSize*2;
        uint64_t need = static_cast<uint64_t>(std::ceil(estimate));
        if(!spaceOk || freeBytes.QuadPart < need + 20ull*1024*1024*1024) {
            cerr << "Insufficient space (including 20 GiB reserve)\n"; return 2;
        }
        cout << "SPACE_CHECK needed_bytes=" << need << " available_bytes=" << freeBytes.QuadPart << '\n';
    }

    // --- 添加文件输出逻辑 ---
    std::time_t now = std::time(nullptr);
    std::tm tm_now{};
    if(localtime_s(&tm_now, &now) != 0) { cerr << "Local time conversion failed\n"; return 1; }
    char dateSuffix[20];
    std::strftime(dateSuffix, sizeof(dateSuffix), "%m%d_%H%M%S",
                  &tm_now); // 格式化为"月日_时分"
    // 中文：进程号+毫秒防止同秒重跑覆盖文件；独占创建仍检查碰撞。
    std::string const outputFileName(std::string(outputDirectory) + "\\BNU_Mark1_" +
        std::string(dateSuffix) + "_" + std::to_string(GetCurrentProcessId()) + "_" +
        std::to_string(GetTickCount64()) + (discard ? "_discard.dat" : ".dat"));

    // 打开输出文件 - 改用C风格I/O以提升性能
    FILE *outputFile = nullptr;
    fopen_s(&outputFile, discard ? "NUL" : outputFileName.c_str(), discard ? "wb" : "wbx");
    if (outputFile == nullptr)
    {
        std::cerr << "错误: 无法打开输出文件！ -> " << outputFileName << std::endl;
        return 1;
    }

    cout << "Output: " << (discard || (diagnostic && !diagnosticDisk) ? "NONE (explicit diagnostic discard)" : outputFileName) << "\n\n";

    // --- 移除单体内存缓冲区 ---
    size_t totalDataWritten = 0;

    // 初始化驱动句柄
    ViSession session = VI_NULL;
    std::string oldGateSignal;
    ViInt32 oldGateTermination = 0;
    bool gateChanged = false;
    auto restoreGate = [&]() {
        if(!gateChanged || session == VI_NULL) return;
        checkApiCall(AqMD3_SetAttributeViString(session, "ControlIO2", AQMD3_ATTR_CONTROL_IO_SIGNAL, oldGateSignal.c_str()));
        checkApiCall(AqMD3_SetAttributeViInt32(session, "ControlIO2", AQMD3_ATTR_CONTROL_IO_INPUT_TERMINATION, oldGateTermination));
        checkApiCall(AqMD3_ApplySetup(session));
        gateChanged = false;
        cout << "IO2 restored\n";
    };
    ViBoolean const idQuery = VI_FALSE; // 不查询设备ID
    ViBoolean const reset = VI_FALSE;   // 不复位设备

    // 触发计数器
    int64_t totalTriggers = 0;
    std::thread writerThread; // 在try块外部声明线程对象，以确保在catch块中可访问
    using Clock=std::chrono::steady_clock;
    Clock::time_point acquisitionStartTime{};
    bool started=false;
    ViInt64 timingAnomalies = 0;
    double timestampPeriod = 250e-12;
    double actualSeconds=0, flushSeconds=0, poolWaitMs=0, maxPoolWaitMs=0, fetchMs=0, maxFetchMs=0;
    ViInt64 fetchedBytes=0, maxFirstElement=0, maxRemaining=0;
    size_t queueHighWater=0;
    std::vector<LibTool::TriggerMarker> frameMarkers;
    // 中文：成功/失败都保存配置和性能，避免长采集失败后丢失定位信息。
    auto saveReport=[&](bool complete)
    {
        std::ofstream report(outputFileName+".config.json");
        report << std::setprecision(15) << "{\n\"status\":\"" << (complete ? "complete" : "failed")
            << "\",\n\"dataSaved\":" << (discard ? "false" : "true")
            << ",\n\"sampleRate\":" << sampleRate << ",\n\"recordSize\":" << recordSize
            << ",\n\"schemaVersion\":2"
            << ",\n\"triggerMode\":\"" << (dual ? "line_laser" : "line_only") << "\""
            << ",\n\"durationMode\":\"" << (frameMode ? "frames" : "seconds") << "\""
            << ",\n\"fixedDurationSeconds\":" << fixedDurationSeconds
            << ",\n\"nominalScanSeconds\":" << nominalDuration
            << ",\n\"expectedCaptureSeconds\":" << expectedCaptureSeconds
            << ",\n\"activeLineSamples\":" << activeLineSamples
            << ",\n\"guardSamples\":" << guardSamples
            << ",\n\"activeSampleOffset\":0"
            << ",\n\"timingAnomalies\":" << timingAnomalies
            << ",\n\"timingToleranceSeconds\":" << timingToleranceSeconds
            << ",\n\"alignmentVerified\":false"
            << ",\n\"nominalDeadTimeSeconds\":" << deadTime
            << ",\n\"triggerLevelAt50Ohm\":" << triggerLevel
            << ",\n\"range\":" << range << ",\n\"offset\":" << offset
            << ",\n\"partialFrameLines\":" << totalTriggers%linesPerFrame
            << ",\n\"linePeriod\":" << linePeriod << ",\n\"frameFlyback\":" << frameFlyback
            << ",\n\"framePeriod\":" << framePeriod << ",\n\"pixelPeriod\":" << pixelPeriod
            << ",\n\"activeDuty\":" << lineActiveDuty << ",\n\"pixelsPerLine\":" << pixelsPerLine
            << ",\n\"linesPerFrame\":" << linesPerFrame << ",\n\"requestedFrames\":" << requestedFrames
            << ",\n\"completedRecords\":" << totalTriggers << ",\n\"completedFrames\":" << totalTriggers/linesPerFrame
            << ",\n\"fetchedBytes\":" << fetchedBytes << ",\n\"writtenBytes\":" << (discard ? 0 : totalDataWritten)
            << ",\n\"elapsedSeconds\":" << actualSeconds << ",\n\"flushSeconds\":" << flushSeconds
            << ",\n\"poolWaitMs\":" << poolWaitMs << ",\n\"maxPoolWaitMs\":" << maxPoolWaitMs
            << ",\n\"writeMs\":" << g_writeMs << ",\n\"maxWriteMs\":" << g_maxWriteMs
            << ",\n\"fetchMs\":" << fetchMs << ",\n\"maxFetchMs\":" << maxFetchMs
            << ",\n\"queueHighWater\":" << queueHighWater << ",\n\"maxRemainingElements\":" << maxRemaining
            << ",\n\"maxMarkerRemaining\":" << g_markerRemainingMax
            << ",\n\"maxFirstElement\":" << maxFirstElement << "\n}\n";
        report.close();
        std::ofstream metadata(outputFileName+".markers.csv");
        metadata << "line,record_index,timestamp_s,interval_s\n" << std::setprecision(15);
        // 中文：SA230P时间戳250ps，仅写已成功取样的marker。
        for(size_t i=0;i<size_t(totalTriggers) && i<frameMarkers.size();++i)
            metadata << i+1 << ',' << frameMarkers[i].recordIndex << ','
                     << frameMarkers[i].GetInitialXTime(timestampPeriod) << ','
                     << (i ? (frameMarkers[i].absoluteSampleIndex-frameMarkers[i-1].absoluteSampleIndex)*timestampPeriod : 0) << '\n';
        metadata.close();
        cout << std::dec << "PERF_RESULT status=" << (complete?"complete":"failed") << " records=" << totalTriggers
             << " frames=" << totalTriggers/linesPerFrame << " elapsed_s=" << actualSeconds << " flush_s=" << flushSeconds
             << " pool_wait_ms=" << poolWaitMs << " max_pool_wait_ms=" << maxPoolWaitMs
             << " write_ms=" << g_writeMs << " max_write_ms=" << g_maxWriteMs
             << " fetch_ms=" << fetchMs << " max_fetch_ms=" << maxFetchMs
             << " queue_high=" << queueHighWater << " max_remaining=" << maxRemaining
             << " saved_bytes=" << (discard?0:totalDataWritten) << " config=" << outputFileName << ".config.json\n";
        if(!report || !metadata) throw runtime_error("Report/marker write failed");
    };

    try
    {
        // 初始化仪器驱动
        checkApiCall(
            AqMD3_InitWithOptions(resource, idQuery, reset, options, &session));
        g_session = session;
        cout << "Driver initialized \n";

        // 读取并显示驱动和设备信息
        ViChar str[128];
        checkApiCall(AqMD3_GetAttributeViString(
            session, "", AQMD3_ATTR_SPECIFIC_DRIVER_PREFIX, sizeof(str), str));
        cout << "Driver prefix:      " << str << '\n';
        checkApiCall(AqMD3_GetAttributeViString(
            session, "", AQMD3_ATTR_SPECIFIC_DRIVER_REVISION, sizeof(str), str));
        cout << "Driver revision:    " << str << '\n';
        checkApiCall(AqMD3_GetAttributeViString(
            session, "", AQMD3_ATTR_SPECIFIC_DRIVER_VENDOR, sizeof(str), str));
        cout << "Driver vendor:      " << str << '\n';
        checkApiCall(AqMD3_GetAttributeViString(
            session, "", AQMD3_ATTR_SPECIFIC_DRIVER_DESCRIPTION, sizeof(str), str));
        cout << "Driver description: " << str << '\n';
        checkApiCall(AqMD3_GetAttributeViString(
            session, "", AQMD3_ATTR_INSTRUMENT_MODEL, sizeof(str), str));
        cout << "Instrument model:   " << str << '\n';
        std::string const instrumentModel(str);
        checkApiCall(AqMD3_GetAttributeViString(
            session, "", AQMD3_ATTR_INSTRUMENT_INFO_OPTIONS, sizeof(str), str));
        cout << "Instrument options: " << str << '\n';
        std::string const options(str);
        checkApiCall(AqMD3_GetAttributeViString(
            session, "", AQMD3_ATTR_INSTRUMENT_FIRMWARE_REVISION, sizeof(str),
            str));
        cout << "Firmware revision:  " << str << '\n';
        checkApiCall(AqMD3_GetAttributeViString(
            session, "", AQMD3_ATTR_INSTRUMENT_INFO_SERIAL_NUMBER_STRING,
            sizeof(str), str));
        cout << "Serial number:      " << str << '\n';
        cout << '\n';

        // 检查是否仍处于模拟模式，模拟模式不支持流式采集
        ViBoolean simulate;
        checkApiCall(AqMD3_GetAttributeViBoolean(session, "", AQMD3_ATTR_SIMULATE,
                                                 &simulate));
        if (simulate == VI_TRUE)
        {
            cout << "\nThe Streaming features are not supported in simulated mode.\n";
            cout << "Please update the resource string (resource[]) to match your "
                    "configuration,";
            cout << " and update the init options string (options[]) to disable "
                    "simulation.\n";

            AqMD3_close(session);
            return 1;
        }

        // 检查设备是否有CST模块选项
        if (options.find("CST") == std::string::npos)
        {
            cout
                << "The required CST module option is missing from the instrument.\n";
            AqMD3_close(session);
            return 1;
        }

        // 获取时间戳周期
        timestampPeriod = GetTimestampPeriodForModel(instrumentModel);

        // 配置采集参数
        cout << "Configuring Acquisition\n";
        cout << "  Record size :        " << recordSize << '\n';
        cout << "  SampleRate:          " << sampleRate << '\n';
        checkApiCall(AqMD3_SetAttributeViInt32(
            session, "", AQMD3_ATTR_STREAMING_MODE, streamingMode));
        checkApiCall(AqMD3_SetAttributeViReal64(session, "", AQMD3_ATTR_SAMPLE_RATE,
                                                sampleRate));
        checkApiCall(AqMD3_SetAttributeViInt32(
            session, "", AQMD3_ATTR_ACQUISITION_MODE, acquisitionMode));
        checkApiCall(AqMD3_SetAttributeViInt64(session, "", AQMD3_ATTR_RECORD_SIZE,
                                               recordSize));

        // 配置通道参数
        cout << "Configuring Channel1\n";
        cout << "  Range:              " << range << '\n';
        cout << "  Offset:             " << offset << '\n';
        cout << "  Coupling:           " << (coupling ? "DC" : "AC") << '\n';
        checkApiCall(AqMD3_ConfigureChannel(session, "Channel1", range, offset,
                                            coupling, VI_TRUE));

        // 配置触发参数
        cout << "Configuring Trigger\n";
        cout << "  ActiveSource:       " << triggerSource << '\n';
        cout << "  Level:              " << triggerLevel << "\n";
        cout << "  Slope:              " << (triggerSlope ? "Positive" : "Negative")
             << "\n";
        checkApiCall(AqMD3_SetAttributeViString(
            session, "", AQMD3_ATTR_ACTIVE_TRIGGER_SOURCE, triggerSource));
        checkApiCall(AqMD3_SetAttributeViReal64(
            session, triggerSource, AQMD3_ATTR_TRIGGER_LEVEL, triggerLevel));
        checkApiCall(AqMD3_SetAttributeViInt32(
            session, triggerSource, AQMD3_ATTR_TRIGGER_SLOPE, triggerSlope));
        checkApiCall(AqMD3_SetAttributeViInt32(session, triggerSource, AQMD3_ATTR_TRIGGER_TYPE, AQMD3_VAL_EDGE_TRIGGER));
        checkApiCall(AqMD3_SetAttributeViReal64(session, "", AQMD3_ATTR_TRIGGER_DELAY, 0.0));
        ViChar gateSignals[4096]{}, gateSignal[128]{};
        checkApiCall(AqMD3_GetAttributeViString(session, "ControlIO2", AQMD3_ATTR_CONTROL_IO_AVAILABLE_SIGNALS, sizeof(gateSignals), gateSignals));
        if(dual && ("," + std::string(gateSignals) + ",").find(",In-TriggerEnable,") == std::string::npos)
            throw runtime_error("IO2 does not advertise In-TriggerEnable");
        checkApiCall(AqMD3_GetAttributeViString(session, "ControlIO2", AQMD3_ATTR_CONTROL_IO_SIGNAL, sizeof(gateSignal), gateSignal));
        oldGateSignal = gateSignal;
        checkApiCall(AqMD3_GetAttributeViInt32(session, "ControlIO2", AQMD3_ATTR_CONTROL_IO_INPUT_TERMINATION, &oldGateTermination));
        gateChanged = true;
        checkApiCall(AqMD3_SetAttributeViString(session, "ControlIO2", AQMD3_ATTR_CONTROL_IO_SIGNAL, dual ? "In-TriggerEnable" : "Disabled"));
        if(dual) checkApiCall(AqMD3_SetAttributeViInt32(session, "ControlIO2", AQMD3_ATTR_CONTROL_IO_INPUT_TERMINATION, AQMD3_VAL_CONTROL_IO_INPUT_TERMINATION_WEAK_PULL_UP));
        cout << "WIRING " << (dual ? "LASER->TRG IN; LINE->IO2 (high impedance, actively drive LOW when idle)" : "LINE->TRG IN; IO2 gate disabled") << '\n';

        // 应用配置并自校准
        cout << "\nApply setup and run self-calibration\n";
        checkApiCall(AqMD3_ApplySetup(session));
        checkApiCall(AqMD3_SelfCalibrate(session));
        ViReal64 readRate = 0; ViInt64 readSize = 0;
        checkApiCall(AqMD3_GetAttributeViReal64(session, "", AQMD3_ATTR_SAMPLE_RATE, &readRate));
        checkApiCall(AqMD3_GetAttributeViInt64(session, "", AQMD3_ATTR_RECORD_SIZE, &readSize));
        if(readSize != recordSize || std::abs(readRate-sampleRate) > sampleRate*1e-9)
            throw runtime_error("Driver coerced sample rate/record size; refusing mismatched metadata");

        if(diagnostic)
        {
            Diagnose(session,recordSize,target,legacySleep,diagnosticDisk,outputFile,timestampPeriod);
            fclose(outputFile); outputFile=nullptr;
            restoreGate(); checkApiCall(AqMD3_close(session)); return 0;
        }

        // --- 初始化内存池 ---
        // 中文：每块按实际record计算，额外空间用于驱动展开/对齐，不写入文件。
        // 若磁盘吞吐低于采集吞吐，队列耗尽时主动报错，避免静默丢 line。
        const int NUM_BUFFERS_IN_POOL = bufferPoolCount;
        ViInt64 sampleGrain=0;
        checkApiCall(AqMD3_GetAttributeViInt64(session,sampleStreamName,AQMD3_ATTR_STREAM_GRANULARITY_IN_BYTES,&sampleGrain));
        ViInt64 const bufferSizeBytes =
            (nbrRecordElements + nbrRecordElements/2) *
            sizeof(int32_t) + sampleGrain; // 中文：手册第44页单通道需要50%展开余量，并非增加排队深度。
        MEMORYSTATUSEX memory{}; memory.dwLength = sizeof(memory);
        if(!GlobalMemoryStatusEx(&memory) || uint64_t(bufferSizeBytes)*NUM_BUFFERS_IN_POOL > memory.ullAvailPhys/4)
            throw runtime_error("Buffer pool exceeds 25% of available RAM");
        cout << "POOL bytes=" << bufferSizeBytes*NUM_BUFFERS_IN_POOL << " buffers=" << NUM_BUFFERS_IN_POOL << '\n';
        for (int i = 0; i < NUM_BUFFERS_IN_POOL; ++i)
        {
            g_freeBufferQueue.push(std::vector<uint8_t>(bufferSizeBytes));
        }

        // --- 启动写入线程 ---
        g_acquisitionFinished = false;
        writerThread =
            std::thread(fileWriter, outputFile, std::ref(totalDataWritten));

        // 准备读取缓冲区
        ViInt64 markerStreamGrain = 0;
        checkApiCall(AqMD3_GetAttributeViInt64(
            session, markerStreamName, AQMD3_ATTR_STREAM_GRANULARITY_IN_BYTES,
            &markerStreamGrain));
        ViInt64 const markerStreamGrainElements =
            markerStreamGrain / sizeof(int32_t);
        ViInt64 const markerStreamBufferSize =
            maxMarkerElements                // 需要的标记元素数量
            + markerStreamGrainElements - 1; // 对齐开销
        FetchBuffer markerStreamBuffer(static_cast<size_t>(markerStreamBufferSize));

        // 启动采集
        cout << "\nInitiating acquisition\n";
        checkApiCall(AqMD3_InitiateAcquisition(session));
        cout << "ARMED: now start LINE/scanning; already-running LINE may produce misaligned initial records\n" << std::flush;

        // 计算采集结束时间点
        acquisitionStartTime = Clock::now(); started=true;
        auto endTime = acquisitionStartTime + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(frameMode ? startWaitSeconds : fixedDurationSeconds));

        // 采集循环，持续到采集时长结束
        frameMarkers.reserve(size_t(frameMode ? std::min<int64_t>(targetRecords,65536) : 1024));
        while (Clock::now() < endTime && (!frameMode || totalTriggers<targetRecords))
        {
            if(g_writeFailed) throw runtime_error("Disk short write");
            // 读取触发标记数据
            LibTool::ArraySegment<int32_t> markerArraySegment =
                FetchAvailableElements(session, markerStreamName, maxMarkerElements,
                                       markerStreamBuffer);

            if (markerArraySegment.Size() > 0)
            {
                if(frameMode && frameMarkers.empty())
                    endTime = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(expectedCaptureSeconds + completionMarginSeconds));
                // 计算可用的记录数量
                int64_t const numAvailableRecords =
                    int64_t(markerArraySegment.Size() /
                            LibTool::StandardStreaming::NbrTriggerMarkerElements);

                if (numAvailableRecords == 0)
                {
                    throw runtime_error("Incomplete trigger marker");
                }
                if(numAvailableRecords!=1 || markerArraySegment.Size()!=maxMarkerElements)
                    throw runtime_error("Unexpected marker count");
                // 中文：所有正式模式都解码marker，持续检查记录索引。
                {
                    auto marker=LibTool::StandardStreaming::DecodeTriggerMarker(markerArraySegment);
                    if(!frameMarkers.empty() && marker.recordIndex!=((frameMarkers.back().recordIndex+1)&LibTool::TriggerMarker::RecordIndexMask))
                        throw runtime_error("Frame marker index discontinuity");
                    if(!frameMarkers.empty()) {
                        double dt = (marker.absoluteSampleIndex-frameMarkers.back().absoluteSampleIndex)*timestampPeriod;
                        double expected = linePeriod + (frameMarkers.size()%linesPerFrame == 0 ? frameFlyback : 0.0);
                        if(std::abs(dt-expected) > timingToleranceSeconds) {
                            ++timingAnomalies;
                            if(timingAnomalies <= 5) cerr << "TIMING_WARNING record=" << frameMarkers.size()+1
                                << " dt_s=" << dt << " expected_s=" << expected << " (saved, not dropped)\n";
                        }
                    }
                    frameMarkers.push_back(marker);
                }

                // --- 核心修改：获取采样数据并传递给写入线程 ---

                // 1. 根据触发记录数，计算需要读取的采样数据量
                ViInt64 const nbrElementsToFetch =
                    numAvailableRecords * nbrRecordElements;

                // 2. 从内存池获取一个空闲缓冲区
                std::vector<uint8_t> sampleChunk;
                {
                    auto waitStart=Clock::now();
                    std::unique_lock<std::mutex> lock(g_freeBufferMutex);
                    // 等待缓冲区可用，设置超时以防死锁
                    if (!g_freeBufferCondVar.wait_for(lock, std::chrono::duration<double>(ioTimeoutSeconds), []
                                                      { return !g_freeBufferQueue.empty() || g_writeFailed; }))
                    {
                        throw std::runtime_error(
                            "采集线程超时: 没有可用的空闲缓冲区。磁盘I/O可能无法跟上。");
                    }
                    if(g_writeFailed) throw runtime_error("Writer failed while waiting for buffer");
                    sampleChunk = std::move(g_freeBufferQueue.front());
                    g_freeBufferQueue.pop();
                    double ms=std::chrono::duration<double,std::milli>(Clock::now()-waitStart).count();
                    poolWaitMs+=ms; maxPoolWaitMs=std::max(maxPoolWaitMs,ms);
                }

                // 3. 直接将数据采集到池化的缓冲区中 (零拷贝)
                ViInt64 actualElements = 0;
                ViInt64 firstElement = 0;
                ViInt64 remainingElements = 0;
                ViInt32 *bufferData = reinterpret_cast<ViInt32 *>(sampleChunk.data());
                ViInt64 bufferSizeInElements = sampleChunk.size() / sizeof(ViInt32);

                auto sampleDeadline=std::chrono::steady_clock::now()+std::chrono::duration<double>(ioTimeoutSeconds);
                do
                {
                    auto fetchStart=Clock::now();
                    checkApiCall(AqMD3_StreamFetchDataInt32(
                        session, sampleStreamName, nbrElementsToFetch, bufferSizeInElements,
                        bufferData, &remainingElements, &actualElements, &firstElement));
                    double ms=std::chrono::duration<double,std::milli>(Clock::now()-fetchStart).count();
                    fetchMs+=ms; maxFetchMs=std::max(maxFetchMs,ms);
                    if(firstElement<0 || actualElements<0 || firstElement+actualElements>bufferSizeInElements)
                        throw runtime_error("Sample buffer bounds");
                    if(actualElements==0)
                    {
                        if(std::chrono::steady_clock::now()>sampleDeadline) throw runtime_error("Samples timeout after marker");
                        std::this_thread::yield(); // 中文：保留当前marker，避免样本未到就错配下一条。
                    }
                } while(actualElements==0);
                if(actualElements!=nbrElementsToFetch)
                {
                    cerr << std::dec << "PARTIAL_RECORD requested=" << nbrElementsToFetch << " actual=" << actualElements
                         << " first=" << firstElement << " remaining=" << remainingElements << '\n';
                    throw runtime_error("Partial record returned");
                }
                fetchedBytes+=actualElements*4;
                maxFirstElement=std::max(maxFirstElement,firstElement);
                maxRemaining=std::max(maxRemaining,remainingElements);

                // 4. 将带有有效数据的缓冲区推入数据队列
                if (actualElements > 0)
                {
                    DataChunk chunk;
                    chunk.buffer = std::move(sampleChunk);
                    // 中文：从 firstElement 开始保存实际样本，不能从缓冲区首地址写入。
                    chunk.offsetBytes=size_t(firstElement)*sizeof(ViInt32);
                    chunk.validBytes = actualElements * sizeof(ViInt32);
                    {
                        std::lock_guard<std::mutex> lock(g_dataMutex);
                        g_dataQueue.push(std::move(chunk));
                        queueHighWater=std::max(queueHighWater,g_dataQueue.size());
                    }
                    g_dataCondVar.notify_one();
                }

                totalTriggers += numAvailableRecords;
                if(totalTriggers%linesPerFrame==0)
                    cout << std::dec << "FRAME_PROGRESS frames=" << totalTriggers/linesPerFrame
                         << " seconds=" << std::chrono::duration<double>(Clock::now()-acquisitionStartTime).count()
                         << " sink_bytes=" << g_writtenBytes.load() << " remaining=" << remainingElements << std::endl;

            }

            // 中文：让出CPU但不请求定时休眠，防止Windows定时粒度造成周期性积压。
            std::this_thread::yield(); // 中文：Windows微秒sleep实测可休眠约13ms，造成流式积压。
        }

        actualSeconds=std::chrono::duration<double>(Clock::now()-acquisitionStartTime).count();
        checkApiCall(AqMD3_Abort(session)); // 中文：先停卡再排空主机写队列，避免等待写盘时继续采集。

        // --- 等待写入线程处理完剩余数据 ---
        g_acquisitionFinished = true;
        g_dataCondVar
            .notify_one(); // 唤醒写入线程以使其能检查到 g_acquisitionFinished 标志
        if (writerThread.joinable())
            writerThread.join(); // 等待写入线程结束

        auto flushStart=Clock::now();
        int flushResult=fflush(outputFile);
        int commitResult=discard ? 0 : _commit(_fileno(outputFile));
        int closeResult=fclose(outputFile); outputFile=nullptr;
        flushSeconds=std::chrono::duration<double>(Clock::now()-flushStart).count();
        if(g_writeFailed || flushResult || commitResult || closeResult) throw runtime_error("Disk write/flush/close failed");
        if(frameMode)
        {
            if(totalTriggers!=targetRecords || int64_t(frameMarkers.size())!=targetRecords) throw runtime_error("Incomplete requested frames");
        }
        if(totalTriggers == 0) throw runtime_error("No complete records acquired");
        if(uint64_t(fetchedBytes) != totalDataWritten) throw runtime_error("Fetched/written byte count mismatch");
        saveReport(true);
        cout << "TIMING_RESULT anomalies=" << timingAnomalies << " alignment_verified=false (external first-line verification required)\n";
        cout << std::dec << "RUN_RESULT records=" << totalTriggers << " elapsed_s=" << actualSeconds
             << " fetched_bytes=" << fetchedBytes << " written_bytes=" << totalDataWritten
             << " max_first=" << maxFirstElement << " max_remaining=" << maxRemaining
             << " tail_policy=unread_card_tail_not_saved\n";

        // 输出最终统计信息
        cout << "\n采集结束统计:\n";
        cout << "实际采集时间: " << actualSeconds << " 秒\n"; // 中文：整帧模式按512条停止，不再打印固定5秒。
        cout << "总检测到的触发次数: " << totalTriggers << "\n";
        cout << "总写入数据量: " << totalDataWritten / (1024.0 * 1024.0) << " MB\n";

        // 停止采集
        cout << "\nStopping acquisition\n";
        // 中文：板卡已在写线程排空前停止。

        // 关闭驱动
        restoreGate();
        checkApiCall(AqMD3_close(session));
        cout << "\nDriver closed\n";
        return 0;
    }
    catch (std::exception const &exc)
    {
        std::cerr << "Error: " << exc.what() << std::endl;
        if(started) actualSeconds=std::chrono::duration<double>(Clock::now()-acquisitionStartTime).count();
        if(session!=VI_NULL) AqMD3_Abort(session);

        // 确保在异常情况下也能正确停止并加入后台线程
        g_acquisitionFinished = true;
        g_dataCondVar.notify_one();
        if (writerThread.joinable())
        {
            writerThread.join();
        }

        if(outputFile)
        {
            auto flushStart=Clock::now();
            fflush(outputFile); if(!discard) _commit(_fileno(outputFile)); fclose(outputFile); outputFile=nullptr;
            flushSeconds=std::chrono::duration<double>(Clock::now()-flushStart).count();
        }
        if(started) { try { saveReport(false); } catch(...) { cerr << "Failure report could not be saved\n"; } }
        if (session != VI_NULL) {
            try { restoreGate(); } catch(...) { cerr << "IO2 restore failed; check SFP before next run\n"; }
            AqMD3_close(session);
        }
        return 1;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// 以下为局部辅助函数定义
//

// 检查API调用返回值，打印错误或抛异常
void testApiCall(ViStatus status, char const *functionName)
{
    if(status == VI_SUCCESS) return;
    ViChar message[1024]{};
    AqMD3_error_message(g_session, status, message);
    cerr << (status < 0 ? "ERROR " : "WARNING ") << functionName
         << " status=0x" << hex << status << std::dec << " " << message << '\n';
    if(status < 0) throw runtime_error(message);
}

// 读取指定流中当前可用的元素，带缓冲区大小检查和重试
LibTool::ArraySegment<int32_t>
FetchAvailableElements(ViSession session, ViConstString streamName,
                       ViInt64 nbrElementsToFetch, FetchBuffer &buffer)
{
    int32_t *bufferData = buffer.data();
    ViInt64 const bufferSize = buffer.size();
    if (bufferSize < nbrElementsToFetch)
        throw std::invalid_argument(
            "Buffer size is smaller than the requested elements to fetch");
    ViInt64 firstValidElement = 0;
    ViInt64 actualElements = 0;
    ViInt64 remainingElements = 0;
    checkApiCall(AqMD3_StreamFetchDataInt32(
        session, streamName, nbrElementsToFetch, bufferSize,
        (ViInt32 *)bufferData, &remainingElements, &actualElements,
        &firstValidElement));
    g_markerRemainingMax=std::max(g_markerRemainingMax,remainingElements);
    // 中文：不消费半条marker，样本与marker必须一一对应。
    if(firstValidElement < 0 || actualElements < 0 || firstValidElement + actualElements > bufferSize)
        throw runtime_error("Marker buffer bounds");
    if(actualElements != 0 && actualElements != nbrElementsToFetch)
        throw runtime_error("Incomplete marker");

    return LibTool::ArraySegment<int32_t>(buffer, (size_t)firstValidElement,
                                          (size_t)actualElements);
}

// 根据仪器型号返回时间戳周期（秒），用于时间戳计算
ViReal64 GetTimestampPeriodForModel(std::string const &model)
{
    if (model == "SA220P" || model == "SA220E")
        return 500e-12;
    else if (model == "SA230P" || model == "SA230E")
        return 250e-12;
    else if (model == "SA240P" || model == "SA240E")
        return 250e-12;
    else if (model == "SA217P" || model == "SA217E")
        return 250e-12;
    else if (model == "SA248P" || model == "SA248E")
        return 125e-12;
    else
        throw std::invalid_argument(
            "Cannot deduce timestamp period for instrument: " + model);
}
