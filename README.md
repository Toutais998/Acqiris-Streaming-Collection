# Acqiris Streaming Collection

本项目包含 Acqiris/SA230P 的 IVI-C++ 示例：`CPP_IVIC_SimpleAcquisition` 用于单次采集，`CPP_IVIC_Streaming` 用于触发式 Streaming。当前可执行入口是 [`CPP_IVIC_Streaming/CPP_IVIC_Streaming.cpp`](CPP_IVIC_Streaming/CPP_IVIC_Streaming.cpp)。

## 当前采集逻辑

程序使用 `AQMD3_VAL_STREAMING_MODE_TRIGGERED`，将 `External1` 配置为上升沿触发。每个触发产生一条 record；采集线程先读取 `MarkersCh1`，再从 `StreamCh1` 取出对应数量的数据。数据通过固定大小的内存池交给独立写线程，以降低磁盘 I/O 对 DMA 读取的影响。默认输出为 `D:\Acq_Storage\BNU_Mark25_Streaming_MMDD_HHMMSS.dat`。

## 重要吞吐量限制

1 GS/s、16 bit 原始数据约为 2 GB/s，已经高于大多数单盘和文件系统的持续写入能力。程序会在内存池耗尽时报告错误，而不是静默覆盖数据。实际实验前应确认采集卡 DMA、PCIe、磁盘阵列和文件格式的持续吞吐；必要时降低采样率、使用硬件抽取/降采样，或只保存像素积分结果。

## 构建与运行

使用 Visual Studio 打开 `CPP_IVIC_Streaming/CPP_IVIC_Streaming.sln`，选择 x64 配置。运行前确认：

1. `resource` 与 `options` 指向实际 PXI 设备，并关闭 Simulate 模式。
2. 设备安装 CST/Streaming 选件。
3. `External1` 接收到 line 上升沿，触发电平与电气标准匹配。
4. `D:\Acq_Storage` 存在且有足够空间。

版本变更记录见 [`CPP_IVIC_Streaming/VERSION_HISTORY.md`](CPP_IVIC_Streaming/VERSION_HISTORY.md)。旧版本源文件不再放在活动目录中，后续版本应通过 Git 提交记录管理。

## LINE + 1 MHz激光同步

2026-09-23确认SA230P的IO2支持`In-TriggerEnable`，独立GateSyncProbe已完成实卡双信号测试：原窗口会出现非行同步记录，加长4.8µs后两次600条均无overflow且稳定段符合行/帧周期；启动边界和首个激光沿仍需验证。原生产入口未改。接线、API依据、原始测试日志及下一步见[同步调查与交接](CPP_IVIC_Streaming/LASER_LINE_SYNC.md)。
