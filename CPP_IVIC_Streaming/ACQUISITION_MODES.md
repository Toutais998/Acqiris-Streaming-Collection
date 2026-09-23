# 统一采集模式、参数和MATLAB重建

## 常用参数与默认值

在 `CPP_IVIC_Streaming.cpp` 搜索 **用户参数区**，所有常用配置集中在该区。默认是单触发、按1帧采集；固定时长默认关闭。双信号接线时必须选择 `LineLaser` 或命令行 `--trigger dual`，软件不能识别你实际插了哪根线。

| 参数 | 默认值 | 用途 |
|---|---|---|
| `triggerMode` | `TriggerMode::LineOnly` | `LineOnly`单触发；`LineLaser`双信号门控 |
| `fixedDurationEnabled` | `false` | false按帧数；true按固定秒数 |
| `fixedDurationSeconds` | 5.0 | 固定时长模式的秒数 |
| `framesToAcquire` | 1 | 所需帧数 |
| `pixelsPerLine` | 512 | 方形图像宽度，同时决定每帧行数 |
| `linePeriod` | 6828e-6 | ScanImage行周期，秒 |
| `lineActiveDuty` | 0.9 | 有效扫描占比；必须符合扫描设置 |
| `frameFlyback` | 1e-3 | 每帧额外间隔，秒 |
| `sampleRate` | 1e9 | 样本/秒 |
| `dualGuardSeconds` | 4.8e-6 | 双信号记录保护尾段，MATLAB不参与像素平均 |
| `lineTriggerLevel` | 1.5 | 单触发，TRG IN实际50Ω电压 |
| `laserTriggerLevel` | 1.0 | 双信号，激光50Ω约0–2V的中点 |
| `range` / `offset` | 2.5 / 1.25 | IN1量程跨度与偏移；零中心正弦应实测后考虑offset=0 |
| `startWaitSeconds` | 30 | 帧模式arm后等待首条marker的超时 |
| `completionMarginSeconds` | 10 | 首条后理论采集时间之外的超时裕量 |
| `timingToleranceSeconds` | 2e-6 | 行/帧间隔检测容差 |
| `deadTime` | 32e-9 | 保留参考，未设置到硬件、非实测 |
| `outputDirectory` | D:\Acq_Storage | 原始文件与侧文件目录 |

本次交付参数区为源码第84–104行：模式86，固定时长开关87、秒数88，帧数89，像素数90，行周期91，有效比例92，帧间隔93，采样率94，双信号余量95，阈值96/97，量程98、偏移99。代码变更后优先搜索上述变量名。参数改源码后必须重新编译，命令行覆盖则无需重新编译。

`pixelPeriod` 无需单独填写，但**不能仅由linePeriod无条件推算真实像素驻留时间**。本程序在“有效扫描比例已知、正向扫描均分为N像素”的模型下，用 `activeLineSamples/sampleRate/pixelsPerLine` 算像素平均窗口。ScanImage存在额外line flyback、非线性扫描、过扫描或不同有效像素定义时，须调整模型，不能把保护尾段算进像素。

## 单触发与双信号门控

| 模式 | TRG IN | IO2 | 采样记录 |
|---|---|---|---|
| single / LineOnly | LINE上升沿 | 显式Disabled | 一次LINE触发一条有效扫描record |
| dual / LineLaser | 1MHz激光上升沿 | LINE，In-TriggerEnable，高阻弱上拉 | LINE高时允许激光触发；有效段+保护尾段 |

两种模式均使用 Normal Digitizer + Triggered Streaming（CST），显式设置edge、上升沿、零trigger delay；先ApplySetup、自校准、读回采样率与记录长度，若驱动强制改变则拒绝启动，避免文件布局和MATLAB不一致。

双信号在启动前查询IO2可用信号；不使用SDK标为Not Supported的多源AND/ARM，也不使用未明确说明的In-EnableFirstTrigger。程序保存原IO2信号/端接，结束或异常时尝试恢复；强制杀进程后需用SFP核查。单触发显式关闭IO2门控，避免继承前次设置导致收不到LINE。

采集卡应先arm，然后扫描才开始输出LINE。看到 `ARMED` 后开启扫描。LINE已经输出时再启动程序，可能首行错位；软件不删除初始记录，也不保证接收第一个物理激光沿。间隔错误会写 `TIMING_WARNING`、累计到JSON；真正首行/帧相位和首个激光沿仍需外部验证。IO2不能悬空当低：弱上拉会使能；需主动低电平。用户幅值默认1MΩ读数，TRG IN固定50Ω和IO2高阻的电平需分别实测，不机械除2。

双信号保护尾段来自先前两次600条实测的候选配置，不是厂商保证适用于所有扫描条件的常数。当前有效段6145216点，额外4800点，record=6150016点/6.150016ms。必须小于行周期；改变行频或占空比后需重新验证。采集、DMA和写盘并行，**不会等主机读取或磁盘写完才接受下一次触发**。严格ready/ack需要额外设计。

## 两种停止方式和时间定义

旧 `auto const streamingDuration = seconds(5)` 是遗留定时分支，默认帧模式不使用；本版本已移除，改为明确的互斥选项。

帧模式：

```text
N = pixelsPerLine = linesPerFrame（当前支持方形图像）
目标完整record数量 = framesToAcquire × N
名义扫描总时长 = framesToAcquire × (N × linePeriod + frameFlyback)
首个触发至最后record末端 = (目标record数−1) × linePeriod
                          + (framesToAcquire−1) × frameFlyback
                          + recordSize/sampleRate
```

默认512、6828µs、1ms额外帧间隔：1帧名义3.496936s，10帧34.96936s。双信号1帧首个触发至最后record末端约3.495258016s。最后一帧之后的flyback不需要等待。程序按完整record数量停止，不以名义时长截断一帧。实际总运行时间还含初始化、自校准、等待首次触发、读盘背压及最终flush。

固定时长：`fixedDurationEnabled=true`或`--seconds 5`。计时从Initiate返回成功开始，包含等待触发；到时不再消费新marker，已开始读取的那条尽量完整取回（最多额外样本等待2s），Abort停止板卡，然后排空主机队列并flush。它是主机墙钟定时，不是精确硬件门控的5.000000s；最后可能存在未读取板端尾部，不声称保存了停止时板上的每一条记录。此策略在输出 `tail_policy` 中明确说明。无完整记录则失败，不把空文件标为成功。

固定时长不保证整数帧；JSON写完整帧数和 `partialFrameLines`，原始尾行保留，MATLAB仅重建完整帧。`status=complete`只指本模式读取/写盘完成，不代表触发与真实扫描行一一对应；`alignmentVerified=false`保留这一限制。

## 编译和运行

仓库根目录PowerShell，Visual Studio选择Release x64：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.com' 'CPP_IVIC_Streaming\CPP_IVIC_Streaming.sln' /Build 'Release|x64'
$exe = '.\CPP_IVIC_Streaming\x64\Release\CPP_IVIC_Streaming.exe'
# 无参数使用源码默认值；--plan只计算、不连接卡、不建数据文件
& $exe --trigger single --frames 10 --pixels 512 --line-us 6828 --plan
& $exe --trigger single --frames 1
& $exe --trigger dual --frames 10 --pixels 512 --line-us 6828
& $exe --trigger dual --seconds 5
# 正弦测试可覆盖offset，务必先确认实际50Ω幅度符合2.5V跨度
& $exe --trigger dual --frames 1 --offset 0
```

`--frame`等于`--frames 1`；`--frames`和`--seconds`不可同时出现。`--flyback-ms`覆盖额外帧间隔；`--discard`为显式不保存样本的吞吐诊断，不能重建图像；旧 `--diag samples records legacySleep disk`只为复现旧读取问题保留，不与新CLI混用。该旧诊断默认使用源码triggerMode，不自动猜测接线。`--help`列出入口。

输出仍为无头小端int16 `.dat`，配套 `.config.json` 和 `.markers.csv`；文件名保留BNU_Mark1前缀，增加进程号/计时值并独占创建，防止同秒运行覆盖。采集前检查容量并保留20GiB余量；固定时长按满采样上界预算，而不是假设始终每行一次。

## MATLAB兼容及错误处理

`Matlab/reconstruct_acqiris_scan.m` 自动读取两种触发和两种停止方式的侧文件，无需同步手改record长度。新字段：`recordSize`决定文件步进；`activeLineSamples`与`activeSampleOffset`决定像素有效段；`guardSamples`是记录保护尾部。旧侧文件缺少新字段时按原布局兼容。

```powershell
$env:ACQ_DATA_FILE = 'D:\Acq_Storage\实际文件名.dat'
$env:ACQ_FRAME_INDICES = '1' # all或逗号分隔帧号
& 'E:\Software\Matlab2025\bin\matlab.exe' -batch "run('Matlab/reconstruct_acqiris_scan.m')"
```

MATLAB检查完整记录、marker索引和行/帧间隔，包括跨帧第一行的1ms额外间隔。有时序异常默认拒绝正式重建；只需诊断时可设 `ACQ_ALLOW_TIMING_ANOMALIES=1`，输出名添加 `_DIAGNOSTIC`，不会删行、重排帧或掩盖失配。零完整帧、失败采集、discard均不能生成正式图像。即使未发现间隔异常，首行物理相位仍需外部确认。

合成回归测试：`matlab.exe -batch "addpath('Matlab'); test_scan_reconstruction"`。验证保护尾部不污染像素、帧步进、尾部非整帧处理、跨帧间隔和异常拒绝；不替代实卡Streaming测试。

## 本轮整理与备份

- 常用参数集中、CLI覆盖、两种停止方式、两种触发配置、驱动配置读回、IO恢复。
- 删除闲置FetchElements/SaveRecord实现与等待常量；保留有效读取诊断入口和deadTime参考。
- 继续使用独立写线程/8块池/背压；补齐手册单通道50%展开余量，并检查池总量不超过可用RAM的25%。本机修改前可用RAM约45GiB，当前池约147.6MB；不是盲目增加排队深度。
- 保留firstElement（实测8）及完整marker/sample检查；写失败唤醒读取端、最终校验读写字节一致，错误不可被静默吞掉。
- 编译显式UTF-8，修复中文源文件在系统代码页936下的编译错误。
- MATLAB修复保护尾段混入、跨帧第一行漏检、多帧JSON结构数组问题；首条频率检查扩展到低频正弦。

修改前完整工作源码、原sln/vcxproj及MATLAB备份：
`C:\Users\ADMIN\AppData\Local\AcqirisBackups\BeforeUnifiedModes_20260923_114628`

C++ SHA256：`39493F810BD0342C7E7EF0F10E367DECF99F3E74CBF28A27516B3556E1D73EAD`

MATLAB SHA256：`F88250F817418F5183459D67035C09EC49E2A16F4C183D09F90C900B89F3E559`

没有删除D盘测试数据。本轮优化属于功能/数据完整性整理，不代表提高了采样率性能上限；真实可用性以本轮验证记录为准，旧版600条测试不能代替新生产版本的验证。
