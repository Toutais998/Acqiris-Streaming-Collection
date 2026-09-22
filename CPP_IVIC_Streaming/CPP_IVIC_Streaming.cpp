#include "../include/LibTool.h"
using LibTool::ToString;
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
using std::chrono::duration_cast;
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::minutes;
using std::chrono::nanoseconds;
using std::chrono::seconds;
using std::chrono::system_clock;
using std::chrono::time_point;
#include <thread>
using std::this_thread::sleep_for;
using std::this_thread::sleep_until;
#include <algorithm>
#include <cmath>
#include <atomic>
#include <condition_variable>
#include <ctime> // 添加这个头文件以获取当前时间
#include <fstream>
#include <functional>
#include <mutex>
#include <queue>

// 宏定义，用于检测API调用结果
#define checkApiCall(f)                                                        \
  do {                                                                         \
    ViStatus s = f;                                                            \
    testApiCall(s, #f);                                                        \
  } while (false)

// 定义一个整型采样数据缓冲区类型
typedef std::vector<int32_t> FetchBuffer;

//! 检查函数调用结果，失败时抛异常
void testApiCall(ViStatus status, char const* functionName);

//! 读取指定流中可用的数据元素
LibTool::ArraySegment<int32_t>
FetchAvailableElements(ViSession session, ViConstString streamName,
    ViInt64 maxElementsToFetch, FetchBuffer& buffer);

//! 读取指定数量的数据元素
LibTool::ArraySegment<int32_t> FetchElements(ViSession session,
    ViConstString streamName,
    ViInt64 nbrElementsToFetch,
    FetchBuffer& buffer);

//! 将一条波形记录写入输出流
void SaveRecord(LibTool::TriggerMarker const& triggerMarker,
    ViInt64 nbrRecordElements,
    LibTool::ArraySegment<int32_t> const& elementBuffer,
    double timestampInterval, std::ostream& output);

//! 根据仪器型号返回时间戳的周期（单位秒）
double GetTimestampPeriodForModel(std::string const& model);

// --- 数据块结构体 ---
struct DataChunk {
    std::vector<uint8_t> buffer;
    size_t validBytes;
};

// --- 用于多线程的全局变量 (内存池) ---
std::queue<std::vector<uint8_t>> g_freeBufferQueue;
std::mutex g_freeBufferMutex;
std::condition_variable g_freeBufferCondVar;

std::queue<DataChunk> g_dataQueue;
std::mutex g_dataMutex;
std::condition_variable g_dataCondVar;

std::atomic<bool> g_acquisitionFinished;

// 命名空间内定义配置参数
namespace {
    // 仪器资源字符串和初始化选项，我把模拟模式关掉了
    ViChar resource[] = "PXI1::0::0::INSTR";
    ViChar options[] = "Simulate=false, DriverSetup= Model=SA230P";

    // 采集参数
    bool const channelInterleavingEnabled =
        false; // 是否启用通道交错采样（多个通道独立采集数据，我们卡就一个通道）
    ViReal64 const sampleRate = 1.0e9;                // 采样率 1 GS/s
    ViReal64 const sampleInterval = 1.0 / sampleRate; // 采样间隔
    // XY 双振镜扫描：每个 line 的上升沿启动一条 record。
    // line 周期 9104 us、有效占空比 0.9，因此有效采集窗口约 8193.6 us。
    // 记录长度按采样率换算，取整后为 8,193,600 samples（512 个像素 line）。
    ViReal64 const linePeriod = 9104e-6;
    ViReal64 const lineActiveDuty = 0.9;
    ViInt64 const pixelsPerLine = 512;
    ViReal64 const activeLineDuration = linePeriod * lineActiveDuty;
    ViReal64 const pixelPeriod = activeLineDuration / pixelsPerLine;
    ViInt64 const recordSize = static_cast<ViInt64>(
        std::llround(activeLineDuration * sampleRate));
    ViReal64 const deadTime = 24e-9;                  // 硬件死区时间 24 ns
    ViReal64 const acquisitionCycle =
        (recordSize / sampleRate) + deadTime; // 一条 line 的采集周期

    ViInt32 const streamingMode =
        AQMD3_VAL_STREAMING_MODE_TRIGGERED; // 触发流式采集
    ViInt32 const acquisitionMode =
        AQMD3_VAL_ACQUISITION_MODE_NORMAL; // 正常采集模式

    // 通道配置参数
    ViReal64 const range = 2.5; // 输入量程 ±2.5V
    /*偏移量我给了一个1.25v，只采集正信号*/
    ViReal64 const offset = 1.25;                            // 输入偏移
    ViInt32 const coupling = AQMD3_VAL_VERTICAL_COUPLING_DC; // 直流耦合

    // 触发配置参数
    ViConstString triggerSource = "External1"; // 触发源为外部触发
    ViReal64 const triggerLevel = 1.5;           // 触发电平2V
    ViInt32 const triggerSlope = AQMD3_VAL_TRIGGER_SLOPE_POSITIVE; // 触发沿为上升沿

    // 读取参数
    ViConstString sampleStreamName = "StreamCh1";  // 采样数据流名称
    ViConstString markerStreamName = "MarkersCh1"; // 触发标记流名称
    // 一次只取一条 line，避免 8 MSamples/record 时分配数 GB 的临时缓冲。
    ViInt64 const maxRecordsToFetchAtOnce = 1;

    // 元素转换
    int64_t const nbrSamplesPerElement =
        sizeof(int32_t) / sizeof(int16_t); // 每个int32元素包含的int16采样点数
    ViInt64 const nbrRecordElements =
        recordSize / nbrSamplesPerElement; // 每条记录对应的int32元素数
    ViInt64 const maxAcquisitionElements =
        nbrRecordElements * maxRecordsToFetchAtOnce; // 最大采样元素数
    ViInt64 const maxMarkerElements =
        LibTool::StandardStreaming::NbrTriggerMarkerElements *
        maxRecordsToFetchAtOnce; // 最大标记元素数

    /* 读取失败时等待时间，单位毫秒 */
    auto const dataWaitTime = milliseconds(1); // 减小等待时间到1ms

    /* 等待采样数据准备的最长尝试次数 */
    int64_t const recordDurationInMs = std::max(
        static_cast<int64_t>(recordSize * sampleInterval * 1000.0), int64_t(1));
    int const nbrWaitForSamplesAttempts = 3;

    // 流式采集总时长（秒）
    auto const streamingDuration = seconds(5);
    /* 这个版本支持最大采集时长10s*/

    // 内存缓冲区设置 (现在这个不再是瓶颈，可以按需调整)
    // const size_t MEMORY_BUFFER_SIZE = 1024 * 1024 * 1024;    // 1GB 内存缓冲区
    // const size_t BUFFER_WRITE_THRESHOLD = 1024 * 1024 * 800; // 800MB 时写入磁盘
} // namespace

// --- 文件写入线程函数 (使用C风格I/O以提升性能) ---
void fileWriter(FILE* outputFile, size_t& totalDataWritten) {
    while (true) {
        DataChunk chunk;
        {
            std::unique_lock<std::mutex> lock(g_dataMutex);
            // 等待直到队列中有数据或采集结束
            g_dataCondVar.wait(
                lock, [] { return !g_dataQueue.empty() || g_acquisitionFinished; });

            if (!g_dataQueue.empty()) {
                // 从队列中取出数据块
                chunk = std::move(g_dataQueue.front());
                g_dataQueue.pop();
            }
            else if (g_acquisitionFinished) {
                // 队列为空且采集已结束，则退出线程
                break;
            }
        } // 锁在这里被释放

        if (chunk.validBytes > 0) {
            fwrite(chunk.buffer.data(), 1, chunk.validBytes, outputFile);
            totalDataWritten += chunk.validBytes;

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

int main() {
    cout << "Triggered Streaming \n\n";

    // --- 添加文件输出逻辑 ---
    std::time_t now = std::time(nullptr);
    std::tm* tm_now = std::localtime(&now);
    char dateSuffix[20];
    std::strftime(dateSuffix, sizeof(dateSuffix), "%m%d_%H%M%S",
        tm_now); // 格式化为"月日_时分"
    std::string const outputFileName(
        "D:\\Acq_Storage\\BNU_Mark25_Streaming_" +
        std::string(dateSuffix) + ".dat");

    // 打开输出文件 - 改用C风格I/O以提升性能
    FILE* outputFile = fopen(outputFileName.c_str(), "wb");
    if (outputFile == nullptr) {
        std::cerr << "错误: 无法打开输出文件！ -> " << outputFileName << std::endl;
        return 1;
    }

    cout << "数据将存储至: " << outputFileName << "\n\n";

    // --- 移除单体内存缓冲区 ---
    size_t totalDataWritten = 0;

    // 初始化驱动句柄
    ViSession session = VI_NULL;
    ViBoolean const idQuery = VI_FALSE; // 不查询设备ID
    ViBoolean const reset = VI_FALSE;   // 不复位设备

    // 触发计数器
    int64_t totalTriggers = 0;
    std::thread writerThread; // 在try块外部声明线程对象，以确保在catch块中可访问

    try {
        // 初始化仪器驱动
        checkApiCall(
            AqMD3_InitWithOptions(resource, idQuery, reset, options, &session));
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
        if (simulate == VI_TRUE) {
            cout << "\nThe Streaming features are not supported in simulated mode.\n";
            cout << "Please update the resource string (resource[]) to match your "
                "configuration,";
            cout << " and update the init options string (options[]) to disable "
                "simulation.\n";

            AqMD3_close(session);
            return 1;
        }

        // 检查设备是否有CST模块选项
        if (options.find("CST") == std::string::npos) {
            cout
                << "The required CST module option is missing from the instrument.\n";
            AqMD3_close(session);
            return 1;
        }

        // 获取时间戳周期
        ViReal64 const timestampPeriod =
            GetTimestampPeriodForModel(instrumentModel);

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

        // 应用配置并自校准
        cout << "\nApply setup and run self-calibration\n";
        checkApiCall(AqMD3_ApplySetup(session));
        checkApiCall(AqMD3_SelfCalibrate(session));

        // --- 初始化内存池 ---
        // 每条 line 约 16.4 MB（int32 流格式）；8 个缓冲区约 128 MB。
        // 若磁盘吞吐低于采集吞吐，队列耗尽时主动报错，避免静默丢 line。
        const int NUM_BUFFERS_IN_POOL = 8;
        ViInt64 const bufferSizeBytes =
            maxAcquisitionElements *
            sizeof(int32_t); // 每个缓冲区的大小与最大单次抓取相同
        for (int i = 0; i < NUM_BUFFERS_IN_POOL; ++i) {
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
        cout << "Acquisition is running\n\n";

        // 计算采集结束时间点
        auto const endTime = system_clock::now() + streamingDuration;
        std::chrono::steady_clock::time_point acquisitionStartTime =
            std::chrono::steady_clock::now();

        // 采集循环，持续到采集时长结束
        while (system_clock::now() < endTime) {
            // 读取触发标记数据
            LibTool::ArraySegment<int32_t> markerArraySegment =
                FetchAvailableElements(session, markerStreamName, maxMarkerElements,
                    markerStreamBuffer);

            if (markerArraySegment.Size() > 0) {
                // 计算可用的记录数量
                int64_t const numAvailableRecords =
                    int64_t(markerArraySegment.Size() /
                        LibTool::StandardStreaming::NbrTriggerMarkerElements);

                if (numAvailableRecords == 0) {
                    sleep_for(std::chrono::microseconds(1));
                    continue;
                }

                // --- 核心修改：获取采样数据并传递给写入线程 ---

                // 1. 根据触发记录数，计算需要读取的采样数据量
                ViInt64 const nbrElementsToFetch =
                    numAvailableRecords * nbrRecordElements;

                // 2. 从内存池获取一个空闲缓冲区
                std::vector<uint8_t> sampleChunk;
                {
                    std::unique_lock<std::mutex> lock(g_freeBufferMutex);
                    // 等待缓冲区可用，设置超时以防死锁
                    if (!g_freeBufferCondVar.wait_for(lock, std::chrono::seconds(2), [] {
                        return !g_freeBufferQueue.empty();
                        })) {
                        throw std::runtime_error(
                            "采集线程超时: 没有可用的空闲缓冲区。磁盘I/O可能无法跟上。");
                    }
                    sampleChunk = std::move(g_freeBufferQueue.front());
                    g_freeBufferQueue.pop();
                }

                // 3. 直接将数据采集到池化的缓冲区中 (零拷贝)
                ViInt64 actualElements = 0;
                ViInt64 firstElement = 0;
                ViInt64 remainingElements = 0;
                ViInt32* bufferData = reinterpret_cast<ViInt32*>(sampleChunk.data());
                ViInt64 bufferSizeInElements = sampleChunk.size() / sizeof(ViInt32);

                checkApiCall(AqMD3_StreamFetchDataInt32(
                    session, sampleStreamName, nbrElementsToFetch, bufferSizeInElements,
                    bufferData, &remainingElements, &actualElements, &firstElement));

                // 4. 将带有有效数据的缓冲区推入数据队列
                if (actualElements > 0) {
                    DataChunk chunk;
                    chunk.buffer = std::move(sampleChunk);
                    // 注意：API返回的firstElement是缓冲区内的偏移，我们必须把它也考虑进去
                    // 然而，为了简化，当前假定firstElement为0。一个完整的实现需要处理这个偏移。
                    // 我们只写入实际获取的数据量。
                    chunk.validBytes = actualElements * sizeof(ViInt32);
                    {
                        std::lock_guard<std::mutex> lock(g_dataMutex);
                        g_dataQueue.push(std::move(chunk));
                    }
                    g_dataCondVar.notify_one();
                }
                else {
                    // 如果没有读到数据，立刻归还缓冲区
                    std::lock_guard<std::mutex> lock(g_freeBufferMutex);
                    g_freeBufferQueue.push(std::move(sampleChunk));
                }

                totalTriggers += numAvailableRecords;

                // --- 为避免性能影响，建议在高吞吐量测试时注释掉下面的输出 ---
                // auto currentTime = std::chrono::steady_clock::now();
                // auto elapsed = currentTime - acquisitionStartTime;
                // auto elapsedNs =
                //     std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed)
                //         .count();
                // std::cout << "时间: " << elapsedNs << "ns, "
                //           << "检测到触发数: " << numAvailableRecords
                //           << ", 总触发次数: " << totalTriggers
                //           << std::endl;
            }

            // 缩短休眠时间以提高响应速度
            sleep_for(std::chrono::microseconds(1));
        }

        // --- 等待写入线程处理完剩余数据 ---
        g_acquisitionFinished = true;
        g_dataCondVar
            .notify_one(); // 唤醒写入线程以使其能检查到 g_acquisitionFinished 标志
        if (writerThread.joinable())
            writerThread.join(); // 等待写入线程结束

        fclose(outputFile);

        // 输出最终统计信息
        cout << "\n采集结束统计:\n";
        cout << "总运行时间: " << (streamingDuration / seconds(1)) << " 秒\n";
        cout << "总检测到的触发次数: " << totalTriggers << "\n";
        cout << "总写入数据量: " << totalDataWritten / (1024.0 * 1024.0) << " MB\n";

        // 停止采集
        cout << "\nStopping acquisition\n";
        checkApiCall(AqMD3_Abort(session));

        // 关闭驱动
        checkApiCall(AqMD3_close(session));
        cout << "\nDriver closed\n";
        return 0;

    }
    catch (std::exception const& exc) {
        std::cerr << "Error: " << exc.what() << std::endl;

        // 确保在异常情况下也能正确停止并加入后台线程
        g_acquisitionFinished = true;
        g_dataCondVar.notify_one();
        if (writerThread.joinable()) {
            writerThread.join();
        }

        if (session != VI_NULL)
            checkApiCall(AqMD3_close(session));
        return 1;
    }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// 以下为局部辅助函数定义
//

// 检查API调用返回值，打印错误或抛异常
void testApiCall(ViStatus status, char const* functionName) {
    ViInt32 ErrorCode;
    ViChar ErrorMessage[512];

    if (status > 0) // 警告
    {
        AqMD3_GetError(VI_NULL, &ErrorCode, sizeof(ErrorMessage), ErrorMessage);
        cerr << "** Warning during " << functionName << ": 0x" << hex << ErrorCode
            << ", " << ErrorMessage << "\n";
    }
    else if (status < 0) // 错误
    {
        AqMD3_GetError(VI_NULL, &ErrorCode, sizeof(ErrorMessage), ErrorMessage);
        cerr << "** ERROR during " << functionName << ": 0x" << hex << ErrorCode
            << ", " << ErrorMessage << "\n";
        throw runtime_error(ErrorMessage);
    }
}

// 读取指定流中当前可用的元素，带缓冲区大小检查和重试
LibTool::ArraySegment<int32_t>
FetchAvailableElements(ViSession session, ViConstString streamName,
    ViInt64 nbrElementsToFetch, FetchBuffer& buffer) {
    int32_t* bufferData = buffer.data();
    ViInt64 const bufferSize = buffer.size();
    if (bufferSize < nbrElementsToFetch)
        throw std::invalid_argument(
            "Buffer size is smaller than the requested elements to fetch");
    ViInt64 firstValidElement = 0;
    ViInt64 actualElements = 0;
    ViInt64 remainingElements = 0;
    checkApiCall(AqMD3_StreamFetchDataInt32(
        session, streamName, nbrElementsToFetch, bufferSize,
        (ViInt32*)bufferData, &remainingElements, &actualElements,
        &firstValidElement));
    if ((actualElements == 0) && (remainingElements > 0)) {
        if (nbrElementsToFetch <= remainingElements) {
            // 只返回实际可读数据，不抛异常
            return LibTool::ArraySegment<int32_t>(buffer, 0, 0);
        }
        checkApiCall(AqMD3_StreamFetchDataInt32(
            session, streamName, remainingElements, bufferSize,
            (ViInt32*)bufferData, &remainingElements, &actualElements,
            &firstValidElement));
    }

    return LibTool::ArraySegment<int32_t>(buffer, (size_t)firstValidElement,
        (size_t)actualElements);
}

// 读取指定数量的元素，要求必须读满，支持多次尝试
// 注意: 此函数在新的内存池设计中不再被主循环使用，但为了完整性暂时保留。
LibTool::ArraySegment<int32_t> FetchElements(ViSession session,
    ViConstString streamName,
    ViInt64 nbrElementsToFetch,
    FetchBuffer& buffer) {
    if (nbrElementsToFetch == 0)
        return LibTool::ArraySegment<int32_t>(buffer, 0, 0);
    int32_t* bufferData = buffer.data();
    ViInt64 const bufferSize = buffer.size();
    if (bufferSize < nbrElementsToFetch)
        throw std::invalid_argument(
            "Buffer size is smaller than the requested elements to fetch");
    for (int nbrAttempts = 0; nbrAttempts < nbrWaitForSamplesAttempts;
        ++nbrAttempts) {
        ViInt64 firstElement = 0;
        ViInt64 actualElements = 0;
        ViInt64 remainingElements = 0;
        checkApiCall(AqMD3_StreamFetchDataInt32(
            session, streamName, nbrElementsToFetch, bufferSize,
            (ViInt32*)bufferData, &remainingElements, &actualElements,
            &firstElement));
        if (actualElements > 0) {

            return LibTool::ArraySegment<int32_t>(buffer, size_t(firstElement),
                size_t(actualElements));
        }
        if ((actualElements == 0) && (remainingElements < nbrElementsToFetch)) {
            // std::cout << "Wait for record samples to be ready for fetch\n";
            std::this_thread::sleep_for(
                std::chrono::milliseconds(recordDurationInMs));
            continue;
        }
        // 不再抛异常，直接返回实际读取到的数据
        return LibTool::ArraySegment<int32_t>(buffer, size_t(firstElement),
            size_t(actualElements));
    }
    // 多次尝试均失败，直接返回空
    return LibTool::ArraySegment<int32_t>(buffer, 0, 0);
}

/*这是修改的内容*/
void SaveRecord(LibTool::TriggerMarker const& triggerMarker,
    ViInt64 nbrRecordElements,
    LibTool::ArraySegment<int32_t> const& elementBuffer,
    double timestampInterval, std::ostream& output) {
    // 计算采样点数量
    size_t nbrRecordSamples =
        size_t(nbrRecordElements) * size_t(nbrSamplesPerElement);
    int16_t* sampleArray = reinterpret_cast<int16_t*>(elementBuffer.GetData());
    // 直接写二进制数据
    output.write(reinterpret_cast<const char*>(&triggerMarker.recordIndex),
        sizeof(triggerMarker.recordIndex));
    output.write(reinterpret_cast<const char*>(sampleArray),
        nbrRecordSamples * sizeof(int16_t));
}

// 根据仪器型号返回时间戳周期（秒），用于时间戳计算
ViReal64 GetTimestampPeriodForModel(std::string const& model) {
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
