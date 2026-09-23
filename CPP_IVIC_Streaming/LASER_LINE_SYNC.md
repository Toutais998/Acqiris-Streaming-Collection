# LINE + 1 MHz 激光同步调查与交接（2026-09-23）

## 结论及当前验证状态

SA230P 支持 **IO2 电平使能 + External1 边沿触发**，不是任意 I/O 与 Trigger 的通用逻辑 AND。本机真实卡查询到 `ControlIO2` 的 `In-TriggerEnable`；实卡 `ApplySetup` 已接受 `Normal/Digitizer + Triggered Streaming + In-TriggerEnable` 组合。

随后用户完成接线，本轮已进行双信号实卡测试：原6.145216ms窗口的600条记录无overflow，但347个间隔短于行周期，不能直接用于一行一record成像。窗口增加4.8µs至**6.150016ms（6,150,016点）**后，600条全部取回，除启动时4个短间隔外，594个间隔约6.828ms，另一个约7.828ms（符合额外1ms帧间隔）。此为有余量门控方案的正面证据，**仍不是首个激光沿的示波器验证，也不是对外部LINE无漏失的独立计数证明**。

相同加余量窗口再测600条：启动6个短间隔，其余592个行间隔和1个帧间隔符合上述规律，仍无overflow。首条波形估算1000.06Hz，与接入1kHz相符，但触及ADC下限，模拟量程需另行调整；不能把本轮称为完整成像验证。

初期原窗口曾两次3秒零marker，后来原参数成功，根因未定位；不能事后把它们解释成已确认的阈值或窗口故障。完整结果见 `GateSyncProbe/VALIDATION_20260923.md`，600条原始时间戳已存档。生产代码保持不变，仅独立测试工程使用新参数。

用户最新要求：激光沿和 LINE 有效后开始一条记录，记录期间不再响应激光脉冲；之后的激光时刻拟按 1 MHz 推算。对此，IO2 门控是优先测试方案。CST 在采集期间同时传输，**板卡重新接受触发不以主机读取完成或磁盘写完为条件**。如果“读完才接受下一触发”是严格要求，不能宣称当前方案已满足：应增加外部 ready/ack 状态机，或另写有限单次采集、Fetch 后软件重新 Initiate 的程序，后者会引入 Windows 调度延迟，可能漏行，不推荐用于当前连续成像。

## 文档依据（可复查的主题）

优先检查了用户提供的 `MD3_SFP_Help.chm`。它是 **Software Front Panel 操作帮助**，并非完整 IVI-C API 帮助；其 Trigger panel 自己也要求参阅 Driver Help。随后补读本机安装的 `C:\Program Files\IVI Foundation\IVI\Drivers\AqMD3\AqMD3.chm`。UserManual 第72页明确指定这一文件为 IVI-C API 文档。

| 机制 | 证据与结论 |
|---|---|
| 外部触发 + enable/qualifier | UserManual 第70页表5.3：`In-TriggerEnable`，`Level`，`Digitizer mode`，`IO 2 only`。第68页 `TriggerCompare` 说明存在“条件满足但 enable 未置位所以不触发”的情况。支持电平门控，不是每行一次锁存器的承诺。 |
| I/O 选择 API | SDK `Html/AQMD3_ATTR_CONTROL_IO_SIGNAL.html`：设置后必须 `ApplySetup`。`Html/AQMD3_ATTR_CONTROL_IO_AVAILABLE_SIGNALS.html`：逗号分隔列表，具体信号语义参阅产品手册。`AqMD3_GetControlIOName` 从1开始枚举。 |
| SFP 的 I/O 设置 | `Topics/4_66ControlIOs.htm`：ControlIOs 选择端口、Signal 选择功能，可选信号依卡而异。没有规定“任意IO可以做AND”。 |
| 多源 AND/OR | SDK `Html/AqMD3_ConfigureMultiTrigger.html`、`AQMD3_ATTR_TRIGGER_SOURCE_OPERATOR.html` 和 `AQMD3_ATTR_TRIGGER_SOURCE_LIST.html` 明写 **Not Supported**。安装目录 `Readme.txt` 也列 `IviDigitizerMultiTrigger no`。不能调用头文件里的 AND 常量实现本任务。 |
| 独立 ARM 输入/API | SDK `AQMD3_ATTR_ARM_SOURCE_COUNT.html`、`AqMD3_GetArmSourceName.html`、`AqMD3_SendSoftwareArm.html` 均标 **Not Supported**。IO2 门控不是已证实的两级 arm/trigger 状态机。 |
| UserControl | `AQMD3_ATTR_USER_CONTROL_TRIGGER_ENABLE_SOURCE` 在 Segmentation 与 User Firmware 间选择；`USER_CONTROL_START_ON_TRIGGER_ENABLED` 涉及分段机制。没有文档说明可以拿它们直接把 LINE 和激光配成逐行双边沿同步。本次不设置。 |
| 实卡额外信号 | 三个 IO 都公开了 `In-EnableFirstTrigger`，但所查两份 CHM 和该型号手册没有解释其语义、重置条件及 CST 行为。不能把它自动解释为“每个LINE的第一激光脉冲”；需厂商书面说明或专门实验。 |
| AVG 门控 | 第70页的 `In-AccumulationEnable` 为 IO1、AVG 专用；不能替代正常 Digitizer/CST 的 IO2 enable。 |
| holdoff | SDK 有 `AQMD3_ATTR_TRIGGER_HOLDOFF`，但没有从所查文档确认本组合中能否实现所需逐行重置、具体范围/粒度。固定holdoff也不等价于等待主机读完。本次未使用。 |
| 外部时钟 | SFP `Topics/4_65SampleClock.htm` 明说 depends on model；SDK 通用 SAMPLE_CLOCK_SOURCE/EXTERNAL_FREQUENCY/DIVIDER 的存在不能证明 SA230P 可直接用1MHz作为采样时钟。型号 UserManual 第63页和 StartupGuide 的 REF IN 说明仅支持10/100MHz参考。 |

引用页码为 PDF 印刷页码。CHM 已用临时解包工具读取，未修改用户 CHM；提取目录为 `C:\Users\ADMIN\AppData\Local\Temp\sa230sync_b27cccf26f5e4268900201236bc3be01\sfp` 和 `sdk`。临时目录可失效，以上原始文档与主题名可用于重新查阅。

## 接线与时序

| 信号 | SA230P 端口 | 设置/电气要求 |
|---|---|---|
| LINE（有效时高） | **IO2**，API 实例 `ControlIO2` | `In-TriggerEnable`；测试程序选择高阻弱上拉。建议0–3.3V逻辑，端口实测低<0.8V、高>2.0V且在手册范围内。 |
| LASER_SYNC，持续1MHz | **TRG IN** | `External1`，上升沿；阈值按实际50Ω端高低电平中点确定。 |
| 待采模拟信号 | IN1 | 原通道配置，50Ω；确认幅值在量程内。 |
| 已接受触发观测 | TRG OUT → 示波器/逻辑分析仪 | `TriggerAccepted`，不是 `TriggerCompare`。不用输入信号驱动这个输出口。 |

源码里的精确配置入口是 `AQMD3_ATTR_CONTROL_IO_SIGNAL`；不能把 IO1 或 IO3 随意当作 IO2。IO2 同时不能输出 `Out-AcquisitionActive`，因为同一个端口已用作输入。

预期行为是：程序先 Initiate、LINE低时不接受激光触发；LINE高且卡ready时接受有效激光沿；整条record采样；record结束并重就绪后，若LINE仍高，**可能再次接受激光沿**。门控下降是否只禁止下一record、是否影响正在采集的record，所查文档未给出完整时序图，必须用短gate测试验证，不能当成已实测事实。

要保持一行一条，应使卡下一次可接受触发时gate已经低，不能只按名义占空比判断。用户现有90%高电平LINE已用6.150016ms窗口得到明显更稳定的结果；比有效行名义高宽6.1452ms多4.816µs，给边沿、相位及硬件时序留余量，不把这个实验余量当成厂商规定。原窗口只比名义高宽多16ns，实测反复出现短间隔。短至1.024ms的record同一高窗口可触发多次，也已实测确认。

另一候选是将LINE转换为起点后的短使能脉冲（例如10µs），足够覆盖若干激光周期而小于record；此方案仍需单独验证gate下降是否影响当前record，不能直接称已通过。由于本轮是在LINE已经持续运行时Initiate，记录可能从行中途开始，不能静默删除初始异常record凑帧。正式时序应先arm、确认等待后再开启扫描LINE，再检查第一条是否对应真实首行。

在LINE和LASER异步且gate通路无额外限制的理想模型中，等待下一个激光上升沿为0至小于1µs；实际还包含输入同步、比较器/逻辑流水线、线缆及触发至首样本延迟。临近同时变化可能错过本沿而接受下一沿。文档未给出本门控路径的 setup/hold、最短gate宽度、最坏延迟和抖动，不能承诺精确到“物理上紧随LINE的第一个沿”。测试须扫描两路相位。

UserManual 第15页给出 TRG IN **DC–3GHz** 带宽，1MHz不是比较器带宽瓶颈；这不等于1MHz长record接受率。6.145216ms记录在时间上已跨过约6145个激光周期，不能把这些激光脉冲各自当成完整record触发。

## 窗口、传输与激光时刻推算

当前生产工作树参数：`linePeriod=6828µs`、`lineActiveDuty=0.9`、`sampleRate=1GS/s`；record按64点取最近对齐值，**6,145,216点 / 6.145216ms**。独立门控测试候选参数改为6,150,016点。相对LINE延后至激光沿后，窗口末端也相应延后，通常约1µs加硬件路径差；加长窗口还多覆盖4.8µs，名义剩余飞返约677.984µs（还应扣除开始延迟）。不能误称窗口严格在原LINE的90%边界内。正式成像应单独定义“用于像素重建的有效段”和“用于防止重复触发而延长的记录尾段”，不能自动把新增尾部也平均分进512个像素；本轮未改MATLAB。

记录开始后由ADC连续采样，不需程序处理每个激光沿。理想激光事件为 `t_laser(k)=t_laser(0)+k/f_laser`，1GS/s与精确1MHz对应约1000样本/周期。**这只是推算，不是测量每个脉冲**：两时钟未同源时相对频偏会累计，激光抖动、漏脉冲不会被记录。例：相对频差10ppm，在6.145216ms内累计约61.45ns（1GS/s约61点）。若需要准确逐脉冲时间，考虑用共同10/100MHz参考锁定相关设备、外部时间标记或另一路同步采样/计数设备；当前单模拟通道不能同时独立记录两种模拟波形。

REF IN 是50Ω、AC耦合、10MHz或100MHz（容差±1kHz）、-3至+3dBm，手册给出约440–900mVpp正弦范围。不能直接接1MHz 0–3.3/5V方波。对应参考API为 `AQMD3_ATTR_REFERENCE_OSCILLATOR_SOURCE` 和 `AQMD3_ATTR_REFERENCE_OSCILLATOR_EXTERNAL_FREQUENCY`。若从激光倍频得到合适参考，需评估PLL抖动、相位及锁定，不是单纯改一个驱动常量。

CST采集、DMA读取和写线程是并行的，不能只在10%飞返时间内搬运整条数据。当前原始数据约1.8GB/s平均（未计帧间隔）；若硬要只在10%时段传输，瞬时需求约18GB/s，超过手册第44页给出的优化系统最高约6GB/s输出。D盘当前空间充足不改变持续吞吐及背压限制。

## 原程序工作方式

活动入口仍为 `CPP_IVIC_Streaming.cpp`，本轮未修改，其SHA256与备份相同。

1. 初始化 `PXI1::0::0::INSTR`，非模拟，确认CST选件；配置 `AQMD3_VAL_ACQUISITION_MODE_NORMAL` 与 `AQMD3_VAL_STREAMING_MODE_TRIGGERED`。
2. `External1` 使用LINE上升沿、实际50Ω端阈值1.5V，代码设置source/level/slope；没有ControlIO门控或第二激光信号配置。生产代码没有显式设置 `TRIGGER_TYPE`，沿用会话/驱动设置，意图为edge trigger。
3. `ApplySetup`、`SelfCalibrate` 后，创建8块有限缓冲池及写线程；`AqMD3_InitiateAcquisition` 一次启动持续等待/采集，不是每行软件arm。
4. 主循环先从 `MarkersCh1` 取16个int32元素解码记录索引/首样本时间，再从 `StreamCh1` 取一条record，每int32元素含两个int16样本。处理 `firstElement`；样本未到保留marker，不允许静默丢弃。软件日志时间代表主机获知时间，不是触发发生时间。
5. 数据排队给 `fileWriter` 的 `fwrite`，可与下一行采集重叠；池耗尽是可见背压。目标帧数/超时结束后Abort、排空队列、刷新并关闭文件，输出JSON和marker CSV。
6. `deadTime=32ns` 是计算常量，未配置硬件也不是当前实测死区。marker索引连续仅证明已输出记录序列连续，不能证明外部LINE没有漏掉；须与外部LINE计数/时间对照。

## 独立验证程序与编译运行

新增 `GateSyncProbe/GateSyncProbe.cpp`、`.vcxproj`、`.sln`；不加入原生产项目。默认 `--query` 不ApplySetup、不Initiate、不创建波形文件。`--validate` 配置并恢复IO门控、端接和TRG OUT但不采集。`--run` 自校准并启动门控短采集；明确不落盘波形，完整取回的样本计数，不能代替正式成像/磁盘吞吐测试。新增 `--run-ungated` 只隔离TRG IN是否正常，关闭gate的行为不适用于正式成像；`--io-levels` 根据SDK将IO2临时设为In-Software、1秒轮询状态后恢复，不启动采集。低速轮询不能测精确脉宽或硬件触发数。

修改前工作源码及原项目/solution已备份至：
`C:\Users\ADMIN\AppData\Local\AcqirisBackups\BeforeLaserSync_20260923_111116`

活动源码SHA256：`39493F810BD0342C7E7EF0F10E367DECF99F3E74CBF28A27516B3556E1D73EAD`。

仓库根目录PowerShell：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.com' 'CPP_IVIC_Streaming\GateSyncProbe\GateSyncProbe.sln' /Build 'Release|x64'
& '.\CPP_IVIC_Streaming\GateSyncProbe\x64\Release\GateSyncProbe.exe' --query
# 不启动采集，1.5V仅用来检查驱动接受配置，并非已测得激光的正确阈值：
& '.\CPP_IVIC_Streaming\GateSyncProbe\x64\Release\GateSyncProbe.exe' --validate 6145216 20 1.5 3
# 本轮用户实测激光50Ω高2V，按低约0V取1.0V；候选窗口增加4.8µs时序余量：
& '.\CPP_IVIC_Streaming\GateSyncProbe\x64\Release\GateSyncProbe.exe' --run 6150016 20 1.0 3
# LINE电平轮询（不计准确脉冲数、不驱动IO2）：
& '.\CPP_IVIC_Streaming\GateSyncProbe\x64\Release\GateSyncProbe.exe' --io-levels
```

`--run`参数依次为：samples（64倍数）、完整records目标、50Ω实际阈值V、总超时秒（最多10）。先采20条观察，不直接启动多帧大文件。每条打印记录索引、首样本硬件时间、相邻间隔、主机完成时间、样本数和firstElement；末尾打印marker/record计数及剩余元素峰值。首条额外以64点步进打印原始ADC最小/最大值和迟滞过阈估算频率，供确认是否有输入波形，不是校准幅值测量。退出码0表示达到目标（query/validate则代表成功）；3表示未在时限内达到目标；1表示API/连续性/数据边界等失败；2为参数错误。无LINE/无激光测试中超时可以是预期结果，不是程序通过的标志。

程序正常/异常退出尝试恢复门控、IO端接及TRG OUT原配置；采集窗口/触发等保留测试值，下次生产程序会重新配置其原有参数。强制杀进程可能来不及恢复；若发生，重新查询/用SFP确认IO2恢复Disabled后再接回原LINE触发。运行时不要同时打开另一采集程序。日志不含未接受的硬件触发事件；真实开始时间以marker和TRG OUT测量为准。

实卡执行结果见 `GateSyncProbe/VALIDATION_20260923.md`。

## 接线完成后的实验矩阵

本轮实际信号：LINE周期6828µs、90%高；激光模拟1MHz、5%占空比（名义50ns高）、1MΩ下0–4V，用户量测50Ω下高2V；IN1输入1kHz、4Vpp正弦（按1MΩ幅值）。两通道应共地；示波器同时观测LINE、LASER和TRG OUT，若只有两通道则分轮测量并保留同一参考。建议后续另测10µs短gate，并扫描LINE相位。

| 实验 | 操作与通过判据 |
|---|---|
| 仅激光 | IO2明确拉低，持续1MHz；3s内marker/record均0且无TriggerAccepted。不能拔掉IO2代替拉低。 |
| 仅LINE | TRG IN保持稳定低，输出LINE；不得出现记录。 |
| 两路正常 | 10µs gate、6.145216ms record；20条应对应20个LINE，间隔约6.828ms；每条样本数6145216。示波器检查每条对应gate内首个可接受激光沿。 |
| gate下降行为 | 用10µs gate确认record在gate下降后仍完整。若截断/异常，停止采用短gate方案并联系厂商，不能强行进入正式成像。 |
| 长gate/短record | 保持6.145ms高电平，将record改为1024000点（1.024ms）。观察同一LINE内是否出现多次Accepted，验证level enable不是one-shot。先短测，避免大量日志成为读取瓶颈。 |
| 相位边界 | 逐步改变LINE相对1MHz的相位，重点跨越激光上升沿附近；测Accepted延迟分布、是否推迟一周期；不凭主机输出时间判断纳秒抖动。 |
| 缺脉冲/停止扫描 | 跳过部分激光沿，或LINE持续低/停止；检查等待与超时，不允许无LINE记录。确保停止扫描时LINE真实回低。 |
| 原占空比及多帧 | 最后恢复真实LINE高宽，观察每行一次；每512行约1ms frame flyback应反映在marker间隔中。对齐外部计数后，才扩展到生产磁盘保存。 |

要测“漏LINE数量”，需示波器/逻辑分析仪独立计数LINE，并与同一观测时段的TriggerAccepted对照。不能拿成功marker的连续索引冒充外部触发无丢失。为免启停边界误计，比较持续运行中间的完整行。

## 电压约定与替代方案

用户所说输出幅值默认是**示波器1MΩ端测值**。TRG IN为固定50Ω，IO2本测试为高阻，必须分别实测。既有数据为1MΩ下5V→50Ω下3.1V、4V→3.1V、3V→2.867V，说明源存在非线性限流/钳位等可能，不能固定除2，也不能由这些数据唯一认定输出阻抗。新1MHz信号要重新量测负载高/低电平和边沿。若TRG端实测0–3.1V，中点为1.55V，可从约1.5V起测；此例不可直接套用未经测量的激光输出。

IO2手册描述3.3V CMOS、5V tolerant，StartupGuide列输入+5V max；设计建议0–3.3V，注意过冲。SDK `AQMD3_ATTR_CONTROL_IO_INPUT_TERMINATION`还提供弱上拉/50Ω下拉选项，不能笼统认为所有配置永远高阻。独立程序显式选择 `WEAK_PULL_UP`。用示波器测IO2应高阻探测，避免额外并上50Ω改变门控电平；TRG IN若再并一只50Ω示波器，源看到的是约25Ω，需要正确探测/分配方式。

若IO2门控实测满足上述条件，无需先加FPGA。若需要对任意LINE高宽都严格每行一次、或严格等待主机ready，最稳妥的是外部小型CPLD/FPGA状态机：

`等待LINE有效起点 → 置pending → 首个符合时序的LASER沿输出一个合规TRG脉冲并清pending → 忽略其余激光 → 下一LINE重新允许`。

输出接TRG IN，此时IO2可Disabled；严格等待读取完成时再加入主机ready/ack，并显式统计ready未就绪而错过的LINE。简单组合AND不够：LINE在LASER高时升高，会在LINE边沿本身产生输出沿；而且整个高窗口仍会通过后续脉冲。D触发器/单稳态可用于固定条件，但必须设计复位、脉宽、跨时钟setup/hold和亚稳态；不要仅用一只AND门承诺正确首沿同步。高频时钟同步状态机会增加确定/不确定的几个时钟延迟，仍须按实验允许的误差验证。

用户已完成接线；下一步先验证“卡先arm、LINE后启动”的首行时序，并用示波器量LINE/LASER/TriggerAccepted；再做仅激光、仅LINE、短gate及相位边界负测试。当前样本成功不等于这些测试已完成。还需核实初期零记录超时是否可复现。若边界不稳定，向Acqiris确认 `In-TriggerEnable` 时序、CST重触发约束，以及未公开说明的 `In-EnableFirstTrigger` 的重置语义，再决定外部状态机。
