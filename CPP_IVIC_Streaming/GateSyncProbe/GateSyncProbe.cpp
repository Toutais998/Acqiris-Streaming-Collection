// 中文：依据 SA230P 手册第70页的 IO2 In-TriggerEnable 做独立验证。
// 默认只查询；--validate 只应用配置再恢复；--run 才会采集。不保存波形，仅计数。
#include "../../include/LibTool.h"
#include "../AqMD3.h"
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

ViSession session = VI_NULL;
void check(ViStatus status, const char* name)
{
    if (status == VI_SUCCESS) return;
    char message[1024]{};
    AqMD3_error_message(session, status, message);
    std::cerr << "API " << name << " status=0x" << std::hex << status
              << std::dec << " " << message << '\n';
    if (status < VI_SUCCESS) throw std::runtime_error(name);
}
#define CALL(x) check((x), #x)
std::string getString(const char* rep, ViAttr attr)
{
    char value[4096]{};
    CALL(AqMD3_GetAttributeViString(session, rep, attr, sizeof(value), value));
    return value;
}

int main(int argc, char** argv)
{
    using Clock = std::chrono::steady_clock;
    std::string mode = argc > 1 ? argv[1] : "--query";
    // 中文：阈值必须是 TRG IN 的实际50Ω负载电压；不自动沿用旧LINE的1.5V。
    double level = 0, timeout = 3;
    ViInt64 samples = 6145216;
    int target = 20;
    try {
        if (mode == "--query" || mode == "--io-levels") { if (argc > 2) throw std::runtime_error("args"); }
        else {
            if ((mode != "--validate" && mode != "--run" && mode != "--run-ungated") || argc != 6)
                throw std::runtime_error("args");
            samples = std::stoll(argv[2]); target = std::stoi(argv[3]);
            level = std::stod(argv[4]); timeout = std::stod(argv[5]);
            if (samples < 1024 || samples > 8193600 || samples % 64 || target < 1 || target > 600
                || !std::isfinite(level) || level < -5 || level > 5
                || !std::isfinite(timeout) || timeout <= 0 || timeout > 10)
                throw std::runtime_error("range");
        }
    } catch (...) {
        std::cerr << "Usage: --query OR --io-levels OR --validate/--run/--run-ungated samples records threshold_V_at_50ohm timeout_s(<=10)\n";
        return 2;
    }
    std::string gate, oldSignal, oldOutput;
    ViInt32 oldTermination = 0;
    ViBoolean oldOutputEnabled = VI_FALSE;
    bool changed = false, armed = false;
    int result = 0;
    try {
        CALL(AqMD3_InitWithOptions("PXI1::0::0::INSTR", VI_TRUE, VI_FALSE,
             "Simulate=false", &session));
        auto model = getString("", AQMD3_ATTR_INSTRUMENT_MODEL);
        auto options = getString("", AQMD3_ATTR_INSTRUMENT_INFO_OPTIONS);
        std::cout << "MODEL " << model << " OPTIONS " << options
                  << " SERIAL " << getString("", AQMD3_ATTR_INSTRUMENT_INFO_SERIAL_NUMBER_STRING)
                  << "\nDRIVER " << getString("", AQMD3_ATTR_SPECIFIC_DRIVER_REVISION)
                  << "\nFIRMWARE " << getString("", AQMD3_ATTR_INSTRUMENT_FIRMWARE_REVISION) << '\n';
        ViInt32 count = 0;
        CALL(AqMD3_GetAttributeViInt32(session, "", AQMD3_ATTR_CONTROL_IO_COUNT, &count));
        for (int i = 1; i <= count; ++i) {
            char name[128]{};
            CALL(AqMD3_GetControlIOName(session, i, sizeof(name), name));
            auto available = getString(name, AQMD3_ATTR_CONTROL_IO_AVAILABLE_SIGNALS);
            std::cout << "IO " << name << " CURRENT " << getString(name, AQMD3_ATTR_CONTROL_IO_SIGNAL)
                      << " AVAILABLE " << available << '\n';
            if (("," + available + ",").find(",In-TriggerEnable,") != std::string::npos) gate = name;
        }
        std::cout << "GATE_CAPABILITY " << (gate.empty() ? "NOT_ADVERTISED" : gate)
                  << " (does not prove first-edge timing or CST operation)\n";
        if (mode != "--query") {
            if (model != "SA230P" || options.find("CST") == std::string::npos || gate.empty())
                throw std::runtime_error("Required SA230P/CST/TriggerEnable capability absent");
            oldSignal = getString(gate.c_str(), AQMD3_ATTR_CONTROL_IO_SIGNAL);
            CALL(AqMD3_GetAttributeViInt32(session, gate.c_str(), AQMD3_ATTR_CONTROL_IO_INPUT_TERMINATION, &oldTermination));
            oldOutput = getString("", AQMD3_ATTR_TRIGGER_OUTPUT_SOURCE);
            CALL(AqMD3_GetAttributeViBoolean(session, "", AQMD3_ATTR_TRIGGER_OUTPUT_ENABLED, &oldOutputEnabled));
            changed = true;
            if (mode == "--io-levels") {
                // 中文：SDK仅允许在In-Software下读取此状态；这是低速轮询，不能测精确脉宽或计数。
                CALL(AqMD3_SetAttributeViString(session, gate.c_str(), AQMD3_ATTR_CONTROL_IO_SIGNAL, "In-Software"));
                CALL(AqMD3_SetAttributeViInt32(session, gate.c_str(), AQMD3_ATTR_CONTROL_IO_INPUT_TERMINATION,
                     AQMD3_VAL_CONTROL_IO_INPUT_TERMINATION_WEAK_PULL_UP));
                CALL(AqMD3_ApplySetup(session));
                int low = 0, high = 0, unknown = 0, transitions = 0;
                ViInt32 previousState = -1;
                auto start = Clock::now();
                while (std::chrono::duration<double>(Clock::now() - start).count() < 1.0) {
                    ViInt32 state = 0;
                    CALL(AqMD3_GetAttributeViInt32(session, gate.c_str(), AQMD3_ATTR_CONTROL_IO_IN_SOFTWARE_STATE, &state));
                    if (state == AQMD3_VAL_CONTROL_IO_STATE_LOW) ++low;
                    else if (state == AQMD3_VAL_CONTROL_IO_STATE_HIGH) ++high;
                    else ++unknown;
                    if (previousState >= 0 && state != previousState) ++transitions;
                    previousState = state;
                }
                std::cout << "IO_POLL low_reads=" << low << " high_reads=" << high << " unknown_reads=" << unknown
                          << " observed_changes=" << transitions << " (not a hardware edge counter)\n";
            } else {
            CALL(AqMD3_SetAttributeViInt32(session, "", AQMD3_ATTR_ACQUISITION_MODE, AQMD3_VAL_ACQUISITION_MODE_NORMAL));
            CALL(AqMD3_SetAttributeViInt32(session, "", AQMD3_ATTR_STREAMING_MODE, AQMD3_VAL_STREAMING_MODE_TRIGGERED));
            CALL(AqMD3_SetAttributeViReal64(session, "", AQMD3_ATTR_SAMPLE_RATE, 1e9));
            CALL(AqMD3_SetAttributeViInt64(session, "", AQMD3_ATTR_RECORD_SIZE, samples));
            CALL(AqMD3_ConfigureChannel(session, "Channel1", 2.5, 1.25, AQMD3_VAL_VERTICAL_COUPLING_DC, VI_TRUE));
            CALL(AqMD3_SetAttributeViString(session, "", AQMD3_ATTR_ACTIVE_TRIGGER_SOURCE, "External1"));
            CALL(AqMD3_SetAttributeViInt32(session, "External1", AQMD3_ATTR_TRIGGER_TYPE, AQMD3_VAL_EDGE_TRIGGER));
            CALL(AqMD3_SetAttributeViInt32(session, "External1", AQMD3_ATTR_TRIGGER_SLOPE, AQMD3_VAL_TRIGGER_SLOPE_POSITIVE));
            CALL(AqMD3_SetAttributeViReal64(session, "External1", AQMD3_ATTR_TRIGGER_LEVEL, level));
            CALL(AqMD3_SetAttributeViReal64(session, "", AQMD3_ATTR_TRIGGER_DELAY, 0.0));
            // 中文：ungated只用于隔离TRG IN故障，绝不作为正式成像接线/模式。
            CALL(AqMD3_SetAttributeViString(session, gate.c_str(), AQMD3_ATTR_CONTROL_IO_SIGNAL,
                 mode == "--run-ungated" ? "Disabled" : "In-TriggerEnable"));
            // 中文：显式高阻弱上拉；LINE源必须主动拉低，不能以拔线代替低电平。
            CALL(AqMD3_SetAttributeViInt32(session, gate.c_str(), AQMD3_ATTR_CONTROL_IO_INPUT_TERMINATION,
                 AQMD3_VAL_CONTROL_IO_INPUT_TERMINATION_WEAK_PULL_UP));
            CALL(AqMD3_SetAttributeViString(session, "", AQMD3_ATTR_TRIGGER_OUTPUT_SOURCE, "TriggerAccepted"));
            CALL(AqMD3_SetAttributeViBoolean(session, "", AQMD3_ATTR_TRIGGER_OUTPUT_ENABLED, VI_TRUE));
            CALL(AqMD3_ApplySetup(session));
            std::cout << "SETUP_APPLIED CST=Triggered gate=" << gate
                      << " signal=" << getString(gate.c_str(), AQMD3_ATTR_CONTROL_IO_SIGNAL)
                      << " samples=" << samples << " rate=1e9 threshold_50ohm_V=" << level << '\n';
            }
        }
        if (mode == "--run" || mode == "--run-ungated") {
            CALL(AqMD3_SelfCalibrate(session));
            ViInt64 grain = 0, markerGrain = 0;
            CALL(AqMD3_GetAttributeViInt64(session, "StreamCh1", AQMD3_ATTR_STREAM_GRANULARITY_IN_BYTES, &grain));
            CALL(AqMD3_GetAttributeViInt64(session, "MarkersCh1", AQMD3_ATTR_STREAM_GRANULARITY_IN_BYTES, &markerGrain));
            // 中文：手册第44页要求单通道附加50%的展开空间，另加DMA对齐余量。
            std::vector<int32_t> data(size_t(samples / 2 + samples / 4 + grain / 4)), markers(size_t(16 + markerGrain / 4));
            LibTool::TriggerMarker marker{}, previous{};
            int records = 0, markerCount = 0;
            ViInt64 totalSamples = 0, maxRemaining = 0, maxFirst = 0;
            bool pending = false;
            auto begin = Clock::now();
            CALL(AqMD3_InitiateAcquisition(session)); armed = true;
            std::cout << "ARMED: waiting for hardware-qualified edges; DIAGNOSTIC ONLY, WAVEFORMS NOT SAVED\n"
                      << "record,index,first_sample_s,interval_s,host_elapsed_s,samples,firstElement\n";
            std::cout << std::setprecision(15);
            while (records < target && std::chrono::duration<double>(Clock::now() - begin).count() < timeout) {
                ViInt64 remain = 0, actual = 0, first = 0;
                if (!pending) {
                    CALL(AqMD3_StreamFetchDataInt32(session, "MarkersCh1", 16, markers.size(),
                         reinterpret_cast<ViInt32*>(markers.data()), &remain, &actual, &first));
                    if (first < 0 || actual < 0 || first + actual > ViInt64(markers.size()) || (actual && actual != 16))
                        throw std::runtime_error("Partial marker / bounds error");
                    if (actual) {
                        LibTool::ArraySegment<int32_t> segment(markers, size_t(first), size_t(actual));
                        marker = LibTool::StandardStreaming::DecodeTriggerMarker(segment);
                        ++markerCount; pending = true;
                        if (records && marker.recordIndex != ((previous.recordIndex + 1) & LibTool::TriggerMarker::RecordIndexMask))
                            throw std::runtime_error("Record index discontinuity");
                    }
                }
                if (pending) {
                    CALL(AqMD3_StreamFetchDataInt32(session, "StreamCh1", samples / 2, data.size(),
                         reinterpret_cast<ViInt32*>(data.data()), &remain, &actual, &first));
                    if (first < 0 || actual < 0 || first + actual > ViInt64(data.size()) || (actual && actual != samples / 2))
                        throw std::runtime_error("Partial record / bounds error");
                    maxRemaining = std::max(maxRemaining, remain); maxFirst = std::max(maxFirst, first);
                    if (actual) {
                        if (records == 0) {
                            // 中文：仅首条做64点步进预览，不改波形、不丢读取计数；频率只是信号检查。
                            auto valueAt = [&](ViInt64 sample) {
                                uint32_t word = static_cast<uint32_t>(data[size_t(first + sample / 2)]);
                                return int(static_cast<int16_t>(word & 0xffff));
                            };
                            int rawMin = 32767, rawMax = -32768;
                            for (ViInt64 i = 0; i < samples; i += 64) {
                                int v = valueAt(i); rawMin = std::min(rawMin, v); rawMax = std::max(rawMax, v);
                            }
                            int low = rawMin + (rawMax - rawMin) / 4, high = rawMin + 3 * (rawMax - rawMin) / 4;
                            bool below = false;
                            int rises = 0; ViInt64 firstRise = 0, lastRise = 0;
                            if (rawMax > rawMin) for (ViInt64 i = 0; i < samples; i += 64) {
                                int v = valueAt(i);
                                if (v <= low) below = true;
                                if (below && v >= high) {
                                    if (!rises) firstRise = i;
                                    lastRise = i; ++rises; below = false;
                                }
                            }
                            std::cout << "WAVE_PREVIEW stride=64 raw_min=" << rawMin << " raw_max=" << rawMax
                                      << " rises=" << rises << " approx_frequency_Hz="
                                      << (rises > 1 ? (rises - 1) * 1e9 / double(lastRise - firstRise) : 0)
                                      << " (raw units, not calibrated amplitude; 0 means insufficient crossings)\n";
                        }
                        double delta = records ? (marker.absoluteSampleIndex - previous.absoluteSampleIndex) * 250e-12 : 0;
                        ++records; totalSamples += actual * 2;
                        std::cout << records << ',' << marker.recordIndex << ',' << marker.GetInitialXTime(250e-12)
                                  << ',' << delta << ',' << std::chrono::duration<double>(Clock::now() - begin).count()
                                  << ',' << actual * 2 << ',' << first << '\n';
                        previous = marker; pending = false;
                    }
                }
                std::this_thread::yield();
            }
            CALL(AqMD3_Abort(session)); armed = false;
            std::cout << "RESULT " << (records == target ? "TARGET_REACHED" : "TIMEOUT")
                      << " markers=" << markerCount << " records=" << records << " samples=" << totalSamples
                      << " pending_marker=" << pending << " max_remaining_elements=" << maxRemaining
                      << " max_first_element=" << maxFirst
                      << "\nMISSED_LINE_COUNT=UNKNOWN (external LINE counter required)"
                      << "\nSTOP: board tail beyond diagnostic target/time limit is not included\n";
            if (records != target) result = 3;
        }
    } catch (const std::exception& e) { std::cerr << "FAILED " << e.what() << '\n'; result = 1; }
    // 中文：正常和异常退出均恢复门控/端接/TRG OUT，避免污染原程序；强制杀进程无法保证恢复。
    if (session != VI_NULL) {
        try {
            if (armed) CALL(AqMD3_Abort(session));
            if (changed) {
                CALL(AqMD3_SetAttributeViString(session, gate.c_str(), AQMD3_ATTR_CONTROL_IO_SIGNAL, oldSignal.c_str()));
                CALL(AqMD3_SetAttributeViInt32(session, gate.c_str(), AQMD3_ATTR_CONTROL_IO_INPUT_TERMINATION, oldTermination));
                CALL(AqMD3_SetAttributeViString(session, "", AQMD3_ATTR_TRIGGER_OUTPUT_SOURCE, oldOutput.c_str()));
                CALL(AqMD3_SetAttributeViBoolean(session, "", AQMD3_ATTR_TRIGGER_OUTPUT_ENABLED, oldOutputEnabled));
                CALL(AqMD3_ApplySetup(session));
                std::cout << "RESTORED gate/termination/trigger-output; acquisition settings remain test values\n";
            }
        } catch (const std::exception& e) { std::cerr << "RESTORE_FAILED " << e.what() << '\n'; result = 1; }
        ViStatus closeStatus = AqMD3_close(session);
        if (closeStatus < 0) { std::cerr << "CLOSE_FAILED " << closeStatus << '\n'; result = 1; }
    }
    return result;
}
