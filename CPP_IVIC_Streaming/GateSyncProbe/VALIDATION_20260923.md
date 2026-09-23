# 独立门控探测实卡验证记录

2026-09-23，先完成编译、功能查询和配置应用；用户随后确认已接线，本轮继续完成以下真实双信号采集。结果为：支持门控，但原窗口不能直接保证每行一条；增加4.8µs余量后两次600条测试得到稳定区间，首行/激光沿仍需示波器验证。

- Visual Studio 2022 `devenv.com`，`GateSyncProbe.sln /Build Release|x64`：1成功、0失败。
- `GateSyncProbe.exe --query`：退出0；未ApplySetup或Initiate。
- `GateSyncProbe.exe --validate 6145216 20 1.5 3`：退出0；只ApplySetup再恢复，未Initiate；阈值1.5V仅用于配置验证。
- 验证后再次 `--query` 退出0，三个IO均回读为 `Disabled`；活动生产源码SHA256仍与修改前备份一致。
- 首次构建发现独立目录头文件相对路径错误，已改为 `../../include/LibTool.h` 后重新构建成功。
- 接线前没有启动采集；接线后的实际采集结果如下。首沿相位、纳秒延迟/抖动、短gate下降行为、磁盘吞吐仍未验证。

实卡查询输出：

```text
MODEL SA230P OPTIONS CH1,MEA,EXC,DGT,CST SERIAL AQ00071865
DRIVER 3.9.20621.38
FIRMWARE Stage1 Version 3.7.393.0, FPGA Firmware Version 3.7.393.70110/AVG.CST.DGT/SFIR, FE CPLD 1/0.4.0.0
IO ControlIO1 CURRENT Disabled AVAILABLE Disabled,Out-LowLevel,Out-HighLevel,In-AccumulationEnable,Out-Software,In-Software,In-EnableFirstTrigger,In-StateMonitoring
IO ControlIO2 CURRENT Disabled AVAILABLE Disabled,Out-LowLevel,Out-HighLevel,Out-AcquisitionActive,In-TriggerEnable,Out-Software,In-Software,In-EnableFirstTrigger,In-StateMonitoring
IO ControlIO3 CURRENT Disabled AVAILABLE Disabled,Out-LowLevel,Out-HighLevel,Out-AnalyzerArmed,Out-Software,In-Software,In-EnableFirstTrigger
GATE_CAPABILITY ControlIO2 (does not prove first-edge timing or CST operation)
```

配置验证额外输出：

```text
SETUP_APPLIED CST=Triggered gate=ControlIO2 signal=In-TriggerEnable samples=6145216 rate=1e9 threshold_50ohm_V=1.5
RESTORED gate/termination/trigger-output; acquisition settings remain test values
```

`In-EnableFirstTrigger` 只是设备枚举到的名称；现有文档未证实其逐行重置行为，本程序没有使用它。详细依据、接线、电平及测试矩阵见 [同步调查报告](../LASER_LINE_SYNC.md)。

## 接线后条件和结果

用户确认：LINE周期6828µs，占空比90%，50Ω端高3.23V；1MHz占空比5%，1MΩ下0–4V，50Ω端高2V；待测1kHz/4Vpp正弦接IN1。IO2的实际高阻节点电压未实测，不能把3.23V当作IO2电压。激光低电平按约0V，后续阈值由旧1.5V改为50Ω端1.0V中点。

各模式都用真实SA230P、1GS/s，目标和timeout均有限；正常/异常退出恢复原IO门控、端接和TRG OUT。未保存波形文件，只完整读取、计数并记录时间戳。没有增大生产缓冲池或改生产采集代码。

| 次序 | 模式/窗口 | 结果 |
|---|---|---|
| 1 | gated，6145216点，1.5V，20条/3s | TIMEOUT，0 marker、0样本；无overflow报错。 |
| 2 | ungated，6145216点，1.0V，20条/3s | 20条成功，122904320样本；间隔约6.146004ms，证明激光输入可触发。 |
| 3 | IO2设In-Software，轮询1s | low=6592、high=59059、unknown=0、变化292次；读数约90%高，与LINE相符，但不是硬件计数或精确占空比测量。 |
| 4 | gated，6145216点，1.0V，20条/3s | TIMEOUT，0 marker、0样本，进程退出3。 |
| 5 | gated，1024000点，1.0V，20条/3s | 20条/20480000样本成功；多次间隔约1.025ms，同一高窗口会重复触发。 |
| 6 | gated，6000000点，1.0V，20条/3s | 20条/120000000样本成功，多数约6.001ms，尚非按行锁定。 |
| 7 | gated，6144000点，1.0V，30条/3s | 30条/184320000样本成功，多数约6.145ms，仍偏离6.828ms。 |
| 8 | gated，6145216点，1.0V，20条/3s复测 | 20条成功；初段约6.146ms，后段接近6.828ms，说明前面超时不能简单归因为窗口尺寸不受支持。 |
| 9 | gated，6145216点，1.0V，600条/6s | 600条/3687129600样本，约3.884s，无overflow；599间隔中251个接近行周期、347个偏短、1个长约7.828ms。**不满足逐行同步。** |
| 10 | gated，6150016点，1.0V，600条/6s | 600条/3690009600样本，约4.097s，无overflow；最初4个间隔偏短，594个约6.828ms、1个约7.828ms。 |
| 11 | 同次序10，新增首条波形预览后重复 | 600条/3690009600样本，约4.095s，无overflow；最初6个间隔偏短，592个约6.828ms、1个约7.828ms。 |

“接近行周期”的统计定义是 `abs(interval-0.006828)<2µs`。所有成功运行已取回record的索引连续；firstElement实测8。未用外部计数器同步计数LINE，不能从索引连续断言无漏行，也不能把600条自动等同于600条正确的扫描行。

原窗口仅比名义LINE高宽6145.2µs多0.016µs，实际阈值交叉时间、门控通路及激光相位会改变裕量。加长至6150.016µs后重复测试明显改善，支持保留额外4.8µs保护尾段的方案；并未单独测量各延迟分量，不把“边沿延迟”当作已经排他确认的根因。开机/初期零marker的确切原因仍未定位。

初始异常记录发生于LINE已经持续输出的情况下；应额外测试先arm再开启LINE，不可静默丢弃初始记录后声称首帧完整。gate只负责允许触发，不等价于每行one-shot，也不等待PC Fetch或磁盘写完。

## 首条模拟波形检查

最终程序以64点步进检查首record，原始ADC最小=-32768、最大=-6433，迟滞识别6次上升，估算频率1000.064004Hz，与输入1kHz一致。已看到真实变化数据；没有把“样本数非零”直接当成波形正确。

最小值触及ADC下限。当前量程沿用2.5V FSR、offset1.25的单极性配置，如果4Vpp正弦无DC偏置，则其负半周超出当前范围；需要量测IN1实际50Ω端高低电平，再改为适当中心/量程（例如实测约±1V时可测试offset0、2.5V FSR）。本次没有修改生产通道参数，也不声称已验证模拟幅度。该检查不是完整频谱分析或校准电压测量。

## 原始日志与后续

- [原窗口600条日志](GATE_600_RUN.txt)
- [加余量600条日志](GATE_MARGIN_600_RUN.txt)
- [加余量重复600条日志及波形预览](GATE_MARGIN_REPEAT_600_RUN.txt)

无波形落盘，不代表磁盘持续写入性能已验证。三份日志保留初始偏短记录，不做静默过滤。下一步是示波器同时验证LINE/LASER/TriggerAccepted、先arm后开启LINE、仅激光/仅LINE负测试和模拟量程检查。只有这些通过，才将可选门控模式接入生产程序，并在MATLAB明确区分有效扫描样本与新增保护尾段。
