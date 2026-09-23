# Streaming version history

## 2026-09-23：统一生产采集模式

备份当前工作C++与MATLAB后，将用户参数集中，新增单LINE触发/LINE+激光IO2门控选择、默认按帧数/可选固定秒数停止，删除遗留streamingDuration与闲置读取实现。按完整记录计帧、从行周期/帧间隔估算时长，新增CLI与只计算模式，明确时序异常和首行未验证状态。保留8块池及背压，补齐单通道展开余量、配置读回、IO恢复及文件完整性校验。MATLAB按完整记录步进，仅对有效段重建，检查跨帧时间并拒绝未显式允许的异常时序；测试修复多帧JSON结构问题。使用说明及备份哈希见ACQUISITION_MODES.md；实测与未完成项见UNIFIED_MODES_VALIDATION.md。

The numbered and named backup sources that previously lived beside the active entry point were removed from the build directory. Their progression is preserved here as a lightweight index; exact source snapshots should be recovered from Git commits when available.

| Range | Summary |
|---|---|
| 原版 backup, 第1–6版 | Initial triggered streaming, overflow and buffer experiments |
| 第7–12版 | Dual-trigger/line-trigger tests, marker and readout fixes |
| 第13–18版 | DIY gating, trigger counting, dead-time experiments and optimization |
| 第19–21.2版 | vDAQ changes, half-line tests, overflow mitigation and 10-second test |
| 第22–25版 | Follow-up backups and BNU parameter updates |
| current | Line-triggered full-line record configuration; one record per 109.8 Hz line |

## 2026-09-22

补充帧级飞返和参数化采集：每512行的约1ms间隔按ScanImage的Frame flyback建模；总帧周期为`linesPerFrame×linePeriod+frameFlyback`。C++默认由`framesToAcquire`决定帧数，`--frames N`可覆盖；像素时间由对齐后的record长度、采样率和像素数计算，不再单独配置。已加入多帧配置/性能报告字段。

切换测试配置为line period 6828us、像素时间12000ns、每行6144000点，新增--frames N连续整帧模式、磁盘20GiB安全余量检查、配置/性能报告、marker CSV和写盘/队列耗时。真实三角波100kHz、1MΩ设置0–2V（50Ω端电压须以实测为准）下，1帧512行成功；4帧25.17GB成功；24帧150.995GB、83.93s成功且无overflow，但队列峰值7/8、最大缓冲等待136ms，存在短时写盘背压但恢复正常，未测到失败上限。D盘测试后约22.1GiB可用，继续加压会突破安全余量，未再自动采集。MATLAB支持多帧选取并加入迟滞频率估计，实测100kHz、1.2周期/像素，三帧图像统计稳定。详见Matlab/FRAME_RECONSTRUCTION_146P5.md。

新增--frame整帧采集模式：保持8193600点/line、1GS/s和1.5V上升沿，收到512条完整记录后停止，输出独立marker CSV并验证索引连续。真实卡完成512条、4.67028秒、8390246400字节、无overflow/积压。MATLAB原脚本因8193600/512=16003.125而无法整除重建，现改为整数边界分箱，不丢样；调用MATLAB2025完成512×512重建，输出MAT、固定灰度PNG、增强PNG、预览和JSON。方波估计2.5MHz，图像近均匀；8.77%样本达到ADC下限、一次marker间隔10.104ms，已记录于Matlab/FRAME_RECONSTRUCTION.md。

新增命令行诊断模式及交接报告：真实SA230P上完成1/2/10/110/550条记录、完整/半/1ms窗口、旧休眠/取消休眠及落盘对照。旧版每轮sleep_for(1us)实测产生毫秒级等待和流积压，marker仍连续；正常路径改用yield，修复firstElement偏移和对齐余量，样本未到时保留marker，短写/部分记录显式报错，停卡后再排空写线程。8块池、1GS/s、8193600点及50Ω端1.5V上升沿保持；5秒正常实测548条、8980185600字节、无overflow。32ns仅为计算假设，非实测；550条诊断中有一次10.104ms间隔待查。详见HANDOFF.md及DIAGNOSTIC_RESULTS.txt。

Added `Trouble_Shot.md` and this directory-local `AGENTS.md`. The troubleshooting note records the manual-confirmed fixed 50 ohm termination, the recommended 2.5 V rising-edge trigger for a 0–5 V line signal, and the approximately 1.8 GB/s sustained-stream calculation behind the overflow investigation.

补充电压换算约定：用户给出的幅值默认是示波器 1 MΩ 端口读数；接入固定 50 Ω 负载后必须以实测负载电压重新计算，并在代码注释中注明。

新增 MATLAB 重建脚本，按当前 `D:\Acq_Storage` 二进制输出读取每条 line，并以 512 个像素的积分/平均值生成 512×512 图像。同步补充了信号发生器测试建议和中文提交规范。

The source of truth is `CPP_IVIC_Streaming.cpp`. Use Git commits/tags for future snapshots instead of copying `.cpp` files with version names.

## 2026-09-23：LINE与激光同步调查

依据SFP CHM、安装的AqMD3 SDK CHM及SA230P手册确认IO2的In-TriggerEnable；多源AND/OR和独立ARM标为Not Supported。新增独立GateSyncProbe并用devenv.com编译，完成真实卡功能查询、配置应用和用户接线后的触发测试。原6145216点窗口600条无overflow但347个间隔偏短；测试窗口增加4.8µs至6150016点后，两次600条均无overflow，除初始4/6个偏短间隔外为正常行/帧间隔。50Ω激光高2V时阈值用1.0V；首条模拟波形约1000.06Hz但触及ADC下限。保留原始日志、初期零记录超时及未验证项，不将样本成功等同于首沿/首帧验证。生产源码未改并已备份；CST不会等待主机读完才重触发。详见LASER_LINE_SYNC.md及GateSyncProbe/VALIDATION_20260923.md。
