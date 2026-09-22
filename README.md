# Acqiris Streaming Collection

本项目包含 Acqiris/SA230P 的 IVI-C++ 示例：`CPP_IVIC_SimpleAcquisition` 用于单次采集，`CPP_IVIC_Streaming` 用于触发式 Streaming。当前可执行入口是 [`CPP_IVIC_Streaming/CPP_IVIC_Streaming.cpp`](CPP_IVIC_Streaming/CPP_IVIC_Streaming.cpp)。

## 当前采集逻辑

程序使用 `AQMD3_VAL_STREAMING_MODE_TRIGGERED`，将 `External1` 配置为上升沿触发。每个触发产生一条 record；采集线程先读取 `MarkersCh1`，再从 `StreamCh1` 取出对应数量的数据。数据通过固定大小的内存池交给独立写线程，以降低磁盘 I/O 对 DMA 读取的影响。默认输出为 `D:\Acq_Storage\BNU_Mark25_Streaming_MMDD_HHMMSS.dat`。

## XY 双振镜参数

- line 周期：9104 µs（约 109.8 Hz）
- 有效扫描占空比：0.9，回位区约 10%
- 每 line：512 像素
- 有效 line 时间：8193.6 µs
- 当前采样率：1 GS/s
- 一条 record：8,193,600 samples（约 16.4 MiB 的 int32 Streaming 数据）
- 硬件 dead time：24 ns

因此当前推荐使用 **109.8 Hz line 上升沿**作为 `External1` 触发。每个 line 采集一条完整 record，record 结束后利用振镜回位时间和硬件 dead time 写入/转移数据。2.5 MHz（pixel/40）信号不适合作为启动触发：它会产生约 250 万次触发/秒，触发标记和数据搬运会立即溢出；它应保留为像素时序或后处理同步参考。

## 重要吞吐量限制

1 GS/s、16 bit 原始数据约为 2 GB/s，已经高于大多数单盘和文件系统的持续写入能力。程序会在内存池耗尽时报告错误，而不是静默覆盖数据。实际实验前应确认采集卡 DMA、PCIe、磁盘阵列和文件格式的持续吞吐；必要时降低采样率、使用硬件抽取/降采样，或只保存像素积分结果。

## 构建与运行

使用 Visual Studio 打开 `CPP_IVIC_Streaming/CPP_IVIC_Streaming.sln`，选择 x64 配置。运行前确认：

1. `resource` 与 `options` 指向实际 PXI 设备，并关闭 Simulate 模式。
2. 设备安装 CST/Streaming 选件。
3. `External1` 接收到 line 上升沿，触发电平与电气标准匹配。
4. `D:\Acq_Storage` 存在且有足够空间。

版本变更记录见 [`CPP_IVIC_Streaming/VERSION_HISTORY.md`](CPP_IVIC_Streaming/VERSION_HISTORY.md)。旧版本源文件不再放在活动目录中，后续版本应通过 Git 提交记录管理。

## 信号发生器测试建议

建议使用双通道信号发生器：

- CH1（接 `External1`，作为 line 触发）：109.8 Hz、方波/脉冲，0–5 V TTL 电平，占空比 90%，上升沿触发。若设备不允许 5 V，使用 0–3.3 V，并将 C++ 中 `triggerLevel` 调整到约 1.0–1.5 V。
- CH2（接 `Channel1`，模拟 PMT/SPAD）：先用 2.5 MHz 方波、50% 占空比、0–1.0 V（0.5 V 直流偏置、1 Vpp）验证采集链路。2.5 MHz 在 16 µs 像素窗口内约有 40 个周期，像素平均后应得到近似均匀的灰度图。
- 做空间图案测试时，不要只输出固定正弦波；应让 CH2 的幅值按 line 或外部包络变化。例如使用任意波形/AM，在 4.66 s（约 512 条 line）周期内设置 512 个 line 的幅度包络，或先用每行不同幅度的阶梯波验证 Y 方向重建。

CH1 的 line 触发与 CH2 的模拟信号必须共地；输入端先确认不会超过当前通道量程。当前通道配置为 ±2.5 V、offset 1.25 V，建议所有测试波形保持在约 0–2 V 范围内。不要把 2.5 MHz 信号接到 `External1` 作为触发。

采集完成后运行 `Matlab/reconstruct_acqiris_scan.m`。脚本从与 C++ 相同的 `D:\Acq_Storage` 中选择最新 `.dat` 文件，按每条 line 的 8,193,600 个 int16 样本分成 512 个像素并求均值，生成 512×512 图像。
