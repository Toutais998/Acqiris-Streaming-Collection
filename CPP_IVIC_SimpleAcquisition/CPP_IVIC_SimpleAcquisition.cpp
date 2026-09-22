#include "AqMD3.h"

#include <iostream>
using std::cerr;
using std::cout;
using std::hex;
#include <vector>
using std::vector;
#include <stdexcept>
using std::runtime_error;
#include <fstream>

#define checkApiCall(f)                                                        \
  do {                                                                         \
    ViStatus s = f;                                                            \
    testApiCall(s, #f);                                                        \
  } while (false)

// 采集参数
ViReal64 const sampleRate = 2.0e9;                // 采样率2GHz
ViReal64 const sampleInterval = 1.0 / sampleRate; // 采样间隔
ViInt64 const recordSize = 164e5;                    // 采样点数量
ViInt64 const numRecords = 512; // 默认选择512行，因此等待512个脉冲进行采集
// 采集时长 = recordSize / sampleRate =164e5/2e9=8.2ms

// 如果在非模拟模式下运行示例，则需要输入信号，否则采集会超时。
ViChar resource[] = "PXI1::0::0::INSTR"; // 设备资源字符串
ViChar options[] =
    "Simulate=false, DriverSetup= Model=SA230P"; // 配置选项，包括模拟模式和驱动程序设置

// 用于检查驱动程序 API 调用时的状态错误的实用函数。
void testApiCall(ViStatus status, char const *functionName) {
  ViInt32 ErrorCode;
  ViChar ErrorMessage[512];

  if (status > 0) // 警告发生
  {
    AqMD3_GetError(VI_NULL, &ErrorCode, sizeof(ErrorMessage), ErrorMessage);
    cerr << "** 警告发生在 " << functionName << ": 0x" << hex << ErrorCode
         << ", " << ErrorMessage << '\n';
  } else if (status < 0) // 错误发生
  {
    AqMD3_GetError(VI_NULL, &ErrorCode, sizeof(ErrorMessage), ErrorMessage);
    cerr << "** 错误发生在 " << functionName << ": 0x" << hex << ErrorCode
         << ", " << ErrorMessage << '\n';
    throw runtime_error(ErrorMessage); // 抛出运行时错误
  }
}

int main() {
  cout << "SimpleAcquisition\n\n";

  // 初始化驱动程序。有关更多信息，请参阅驱动程序帮助主题"初始化 IVI-C
  // 驱动程序"。
  ViSession session;
  ViBoolean const idQuery = VI_FALSE;
  ViBoolean const reset = VI_FALSE;
  checkApiCall(
      AqMD3_InitWithOptions(resource, idQuery, reset, options, &session));

  cout << "驱动程序已初始化 \n";

  // 读取并输出一些属性。
  ViChar str[128];
  checkApiCall(AqMD3_GetAttributeViString(
      session, "", AQMD3_ATTR_SPECIFIC_DRIVER_PREFIX, sizeof(str), str));
  cout << "驱动程序前缀:      " << str << '\n';
  checkApiCall(AqMD3_GetAttributeViString(
      session, "", AQMD3_ATTR_SPECIFIC_DRIVER_REVISION, sizeof(str), str));
  cout << "驱动程序版本:      " << str << '\n';
  checkApiCall(AqMD3_GetAttributeViString(
      session, "", AQMD3_ATTR_SPECIFIC_DRIVER_VENDOR, sizeof(str), str));
  cout << "驱动程序供应商:    " << str << '\n';
  checkApiCall(AqMD3_GetAttributeViString(
      session, "", AQMD3_ATTR_SPECIFIC_DRIVER_DESCRIPTION, sizeof(str), str));
  cout << "驱动程序描述:      " << str << '\n';
  checkApiCall(AqMD3_GetAttributeViString(
      session, "", AQMD3_ATTR_INSTRUMENT_MODEL, sizeof(str), str));
  cout << "仪器型号:          " << str << '\n';
  checkApiCall(AqMD3_GetAttributeViString(
      session, "", AQMD3_ATTR_INSTRUMENT_INFO_OPTIONS, sizeof(str), str));
  cout << "仪器选项:          " << str << '\n';
  checkApiCall(AqMD3_GetAttributeViString(
      session, "", AQMD3_ATTR_INSTRUMENT_FIRMWARE_REVISION, sizeof(str), str));
  cout << "固件版本:          " << str << '\n';
  checkApiCall(AqMD3_GetAttributeViString(
      session, "", AQMD3_ATTR_INSTRUMENT_INFO_SERIAL_NUMBER_STRING, sizeof(str),
      str));
  cout << "序列号:            " << str << '\n';

  ViBoolean simulate;
  checkApiCall(
      AqMD3_GetAttributeViBoolean(session, "", AQMD3_ATTR_SIMULATE, &simulate));
  cout << "\n模拟模式:          " << (simulate ? "True" : "False") << '\n';

  // 配置采样率
  checkApiCall(AqMD3_SetAttributeViReal64(session, "", AQMD3_ATTR_SAMPLE_RATE,
                                          sampleRate));

  // 配置采集。
  ViReal64 const range = 2.5;   // 输入量程2.5V
  ViReal64 const offset = 1.25; // 信号的偏移量1.25V
  ViInt32 const coupling =
      AQMD3_VAL_VERTICAL_COUPLING_DC; // 垂直耦合设置（DC耦合）
  cout << "\n配置采集\n";
  cout << "范围:              " << range << '\n';
  cout << "偏移量:            " << offset << '\n';
  cout << "耦合方式:          " << (coupling ? "DC" : "AC") << '\n';
  checkApiCall(AqMD3_ConfigureChannel(session, "Channel1", range, offset,
                                      coupling, VI_TRUE));
  cout << "记录数量:          " << numRecords << '\n';
  cout << "记录大小:          " << recordSize << '\n';
  checkApiCall(AqMD3_SetAttributeViInt64(
      session, "", AQMD3_ATTR_NUM_RECORDS_TO_ACQUIRE, numRecords));
  checkApiCall(AqMD3_SetAttributeViInt64(session, "", AQMD3_ATTR_RECORD_SIZE,
                                         recordSize));

  // 配置触发器。
  cout << "\n配置触发器\n";
  checkApiCall(AqMD3_SetAttributeViString(
      session, "", AQMD3_ATTR_ACTIVE_TRIGGER_SOURCE, "External1"));
  ViReal64 const triggerLevel = 2;                               // 触发电平2V
  ViInt32 const triggerSlope = AQMD3_VAL_TRIGGER_SLOPE_POSITIVE; // 触发沿
  checkApiCall(AqMD3_SetAttributeViReal64(
      session, "External1", AQMD3_ATTR_TRIGGER_LEVEL, triggerLevel));
  checkApiCall(AqMD3_SetAttributeViInt32(
      session, "External1", AQMD3_ATTR_TRIGGER_SLOPE, triggerSlope));

  // 校准仪器。
  cout << "\n执行自校准\n";
  checkApiCall(AqMD3_SelfCalibrate(session));

  // 执行采集。
  ViInt32 const timeoutInMs = 50; // 设置超时时间（毫秒）
  cout << "\n执行采集\n";
  for (ViInt64 i = 0; i < numRecords; ++i) {
    cout << "等待第" << (i + 1) << "次外部触发...\n";
    checkApiCall(AqMD3_InitiateAcquisition(session));
    checkApiCall(AqMD3_WaitForAcquisitionComplete(session, timeoutInMs));

    // 获取采集到的数据并存入数组。
    ViInt64 arraySize = 0;
    checkApiCall(AqMD3_QueryMinWaveformMemory(session, 16, 1, 0, recordSize,
                                              &arraySize));
    vector<ViInt16> dataArray(static_cast<size_t>(arraySize));
    ViInt64 actualPoints, firstValidPoint;
    ViReal64 initialXOffset[1], initialXTimeSeconds[1], initialXTimeFraction[1];
    ViReal64 xIncrement = 0.0, scaleFactor = 0.0, scaleOffset = 0.0;
    checkApiCall(AqMD3_FetchWaveformInt16(
        session, "Channel1", arraySize, &dataArray[0], &actualPoints,
        &firstValidPoint, initialXOffset, initialXTimeSeconds,
        initialXTimeFraction, &xIncrement, &scaleFactor, &scaleOffset));

    // 以二进制方式写入文件，文件名带编号
    char filename[128];
    snprintf(filename, sizeof(filename), "acq_data_%lld.bin",
             static_cast<long long>(i + 1));
    std::ofstream outfile(filename, std::ios::binary);
    outfile.write(reinterpret_cast<const char *>(dataArray.data()),
                  dataArray.size() * sizeof(ViInt16));
    outfile.close();
    cout << "第" << (i + 1) << "次采集完成并保存到 " << filename << "\n";
  }

  cout << "数据处理完成\n";

  // 关闭驱动程序。
  checkApiCall(AqMD3_close(session));
  cout << "\n驱动程序已关闭\n";

  return 0;
}
