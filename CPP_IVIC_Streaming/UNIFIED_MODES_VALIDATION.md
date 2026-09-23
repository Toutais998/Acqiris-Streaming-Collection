# 统一生产模式验证记录（2026-09-23）

本文件记录新生产代码验证，不能用之前独立GateSyncProbe的600条结果代替。硬件为SA230P AQ00071865，driver 3.9.20621.38，FPGA 3.7.393.70110，CST选件真实启用。

## 已通过的检查

- Visual Studio 2022 `devenv.com`，Release x64与Debug x64均编译成功；最终构建没有C4819编码错误，显式使用UTF-8。实卡运行验证使用Release版。
- `--plan` 不连接硬件：single、10帧、512像素、6828µs，输出名义34.96936s；dual、1帧，record6150016、有效6145216、guard4800、名义3.496936s。
- `--seconds 5 --pixels 256 --line-us 9104 --plan` 输出固定时长模式和8193600点有效窗口。
- 同时指定 `--frames 1 --seconds 5`，启动前拒绝，退出2。
- MATLAB2025 `test_scan_reconstruction` 退出0，输出RECONSTRUCTION_TEST_PASS。合成10条记录、4行/帧、1000点有效段+24点高码值保护尾部：第二帧准确得到5/6/7/8行值，保护尾段未污染；2帧外的2条尾行被显式报告；跨帧1ms间隔检查正确；错误时序默认拒绝，显式诊断输出带DIAGNOSTIC后缀。测试日志在 `D:\Acq_Storage\matlab_modes_test.log`。

## 实卡负测试与输入定位

新程序运行 `--trigger dual --seconds 0.2 --offset 0`，0.204896s、0条，正确报告失败；随后 `--trigger dual --seconds 5 --offset 0`，5.0049461s、0条，退出1，无overflow。

用此前独立GateSyncProbe对照：IO2在1秒轮询中low=64931、high=0、unknown=0、变化0次；关gate、1.0V阈值后1MHz能触发10条记录，间隔约6.151ms。说明当时LINE未在IO2形成有效高电平，而激光仍存在。已请求用户重新开启LINE；不能把零记录误判为新版吞吐或磁盘故障，也不能报告双信号正向测试通过。

固定5秒失败文件：`D:\Acq_Storage\BNU_Mark1_0923_115755_10740_7125500.dat.config.json`。波形文件为空，侧文件status=failed，MATLAB不允许当完整采集重建。未删除这些排查文件。

帧模式缺LINE负测试：`--trigger dual --frames 1 --offset 0` 在30.0049006s首条等待超时，0条，退出1、status=failed，IO2已恢复。对应侧文件 `BNU_Mark1_0923_120408_5836_7498109.dat.config.json`。验证不会因为理论1帧时长到达就错误报告完整帧。

## 实卡单触发代码分支（当前TRG仍为激光，非LINE接线验收）

`--trigger single --seconds 0.2 --discard --offset 0`：32条完整记录、393293824字节读取、0.2000091s、无overflow、firstElement=8、队列峰值1、最大buffer等待0.0005ms。固定时间按墙钟停止并排空写线程。由于TRG IN实际仍为1MHz，记录间隔约6.146ms，不等于LINE周期6.828ms；31个异常间隔正确标记，alignmentVerified=false。此例只验证关闭gate后的数据通路/定时控制，**不是LINE单触发成像测试**；NUL诊断没有保存原始波形。

对应侧文件：`D:\Acq_Storage\BNU_Mark1_0923_120340_28780_7469953_discard.dat.config.json`。

## 帧数停止、实际写盘和真实文件MATLAB链路

继续使用现有TRG IN的激光信号，仅检查程序数据链路：`--trigger single --frames 1 --pixels 32 --offset 0`，恰好32条，0.1986911s采集、0.1180384s flush，写入393293824字节，无overflow，最大buffer等待0.0006ms，队列峰值1。文件大小、JSON的record数及32行marker CSV匹配。31个时序异常正确保留（仍非LINE接线，不能宣称真实扫描1帧成功）。

文件：`D:\Acq_Storage\BNU_Mark1_0923_120520_9480_7570531.dat`。

显式设置 `ACQ_ALLOW_TIMING_ANOMALIES=1` 后用MATLAB2025处理此文件，输出32×32诊断图、MAT、JSON，耗时4.379s，频率估计1000.126Hz。`_DIAGNOSTIC`后缀标记结果不能当同步成像验收。输入原始最大码32767、上限占比约41.53%，说明本次offset0配置存在削顶；此前按正弦可能零中心作的假设未被证实，需实测IN1高/低电平及DC偏置。默认源码offset1.25未因该实验而更改。

输出示例：`D:\Acq_Storage\BNU_Mark1_0923_120520_9480_7570531_frame0001_DIAGNOSTIC.png`。日志 `D:\Acq_Storage\matlab_modes_real.log`。

## 尚需完成

1. 开启LINE，当前双信号接线下验证新版完整帧保存与有效段MATLAB重建；先arm后开启扫描，避免启动错位。
2. 用户将LINE接TRG IN、移除1MHz后，验证真实LINE单触发模式。软件不能代替物理换线。
3. 两种接线下的首行/首激光沿仍需示波器或逻辑分析仪同步观察；marker间隔正常不证明首沿。

正向测试尚未完成时，代码交付应标注“已编译、部分实卡验证，待信号恢复/换线验收”，不能声称两模式全部验证通过。
