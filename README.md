# Acqiris Streaming Collection

当前操作入口见[统一采集模式与参数](CPP_IVIC_Streaming/ACQUISITION_MODES.md)：支持LINE单触发、LINE+激光门控，以及按帧数/固定时长两种停止方式；MATLAB自动识别有效扫描段与保护尾段。

本项目包含 Acqiris/SA230P 的 IVI-C++ 示例：`CPP_IVIC_SimpleAcquisition` 用于单次采集，`CPP_IVIC_Streaming` 用于触发式 Streaming。当前可执行入口是 [`CPP_IVIC_Streaming/CPP_IVIC_Streaming.cpp`](CPP_IVIC_Streaming/CPP_IVIC_Streaming.cpp)。

## 当前采集逻辑

程序使用 `AQMD3_VAL_STREAMING_MODE_TRIGGERED`，将 `External1` 配置为上升沿触发；单触发接LINE，双信号模式接激光并由IO2的LINE使能。每个接受的触发产生一条record；采集线程读取 `MarkersCh1` 和 `StreamCh1`，通过8块有限内存池交给独立写线程。输出位于 `D:\Acq_Storage`，文件名为BNU_Mark1前缀、时间、进程号和计时值，防止同秒覆盖；配套JSON记录模式及有效扫描段。

## 重要吞吐量限制

1 GS/s、16 bit 原始数据约为 2 GB/s，已经高于大多数单盘和文件系统的持续写入能力。程序会在内存池耗尽时报告错误，而不是静默覆盖数据。实际实验前应确认采集卡 DMA、PCIe、磁盘阵列和文件格式的持续吞吐；必要时降低采样率、使用硬件抽取/降采样，或只保存像素积分结果。

## 构建与运行

使用 Visual Studio 打开 `CPP_IVIC_Streaming/CPP_IVIC_Streaming.sln`，选择 x64 配置。运行前确认：

1. `resource` 与 `options` 指向实际 PXI 设备，并关闭 Simulate 模式。
2. 设备安装 CST/Streaming 选件。
3. 模式与实际接线匹配：single为LINE→TRG IN；dual为激光→TRG IN、LINE→IO2。阈值使用TRG IN实际50Ω电平。
4. `D:\Acq_Storage` 存在且有足够空间。

版本变更记录见 [`CPP_IVIC_Streaming/VERSION_HISTORY.md`](CPP_IVIC_Streaming/VERSION_HISTORY.md)。旧版本源文件不再放在活动目录中，后续版本应通过 Git 提交记录管理。

## LINE + 1 MHz激光同步

2026-09-23确认SA230P的IO2支持`In-TriggerEnable`，独立GateSyncProbe已完成实卡双信号测试：原窗口会出现非行同步记录，加长4.8µs后两次600条均无overflow且稳定段符合行/帧周期；启动边界和首个激光沿仍需验证。原生产入口未改。接线、API依据、原始测试日志及下一步见[同步调查与交接](CPP_IVIC_Streaming/LASER_LINE_SYNC.md)。
