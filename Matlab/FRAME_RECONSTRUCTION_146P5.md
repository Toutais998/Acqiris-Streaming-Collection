# 146.5 Hz 连续整帧采集与 MATLAB 重建

帧级飞返已确认：每512条line后额外约1 ms，正是 ScanImage 的 `Frame flyback=1 ms`。它不属于单行record；MATLAB 从采集生成的 `.config.json` 读取 `frameFlyback`，将帧周期计算为 `linesPerFrame×linePeriod+frameFlyback`，并只在帧边界允许这1 ms间隔。

现在只需在 C++ 配置区修改 `linePeriod`、`pixelsPerLine` 和 `framesToAcquire`。`linesPerFrame`自动等于像素数，`pixelPeriod`由有效行窗口/对齐record长度/像素数自动计算。默认程序运行采集`framesToAcquire`帧；`--frame`采1帧，`--frames 10`采10帧。

## 配置

- 行周期：6828 us，理论行频约146.45 Hz；External1上升沿仍是行触发。
- 像素时间：12000 ns；512像素对应6144 us。
- 采样率：1 GS/s；每条record为6,144,000点，即12,288,000字节。
- 模拟输入：用户设置100 kHz三角波、1MΩ示波器读数0–2 V、30%上升/下降设置。卡端固定50Ω，触发电平仍为代码1.5 V；电压不能从1MΩ设置值按固定比例换算。

## 真实卡结果

Release x64由Visual Studio 2022 `devenv.com`构建成功，真实SA230P AQ00071865运行：

| 模式 | 结果 |
|---|---|
| 单帧 `--frame` | 512条、6.291456GB、3.50466s、无overflow |
| 4帧 `--frames 4` | 2048条、25.165824GB、13.9914s、无overflow |
| 24帧 `--frames 24` | 12288条、150.994944GB、83.9295s、无overflow |

24帧性能报告为 `D:\Acq_Storage\BNU_Mark1_0922_172010.dat.config.json`：

- 队列峰值7/8，累计空闲池等待214.233 ms，最大单次136.429 ms；
- `remainingElements`峰值63,328,256个int32（约253 MB）；
- 写线程累计36.643 s，最大单次写入189.066 ms；
- 所有12288条record均写出，字节数完全吻合。

因此当前版本已验证约84秒/151GB连续采集。24帧不是硬件理论最大值；测试结束时D盘仅约22.1 GiB可用，程序的20 GiB保留策略会阻止继续扩大测试。没有删除已采集数据，也没有在安全余量不足时自动运行更多帧。

## 三角波重建

MATLAB 2025对24帧文件只重建第1、12、24帧，输出多帧MAT、预览和每帧PNG。迟滞交越估计输入为100 kHz，修正前的200 kHz是慢三角波在无迟滞阈值下重复穿越造成的倍频误报。当前每像素覆盖 `12 us × 100 kHz = 1.2` 个周期，因此图像不是均匀灰色，而是明显的周期性斜纹/三角形纹理；预览文件为 `D:\Acq_Storage\BNU_Mark1_0922_172010_multiframe_preview.png`。第1/12/24帧指标：

- 均值：-19701.45、-19704.90、-19710.82 ADC码；
- 像素标准差：1148.41、1142.56、1140.41 ADC码；
- 每帧估计频率均约100 kHz；
- marker最小间隔约6.82803 ms，最大约7.82806 ms，每512条周期中有一次多1 ms间隔，没有18 ms级隔行丢触发。

三角波是很好的测试信号：低频、斜率连续，可观察像素时间和行相位。推荐的其他测试信号：

1. 0–2 V、50%方波，频率2.5 MHz：每12 us约30个周期，重建应接近均匀灰度，用于验证吞吐和平均值。
2. 0–2 V、50%方波，频率10 kHz或50 kHz：每像素0.12或0.6周期，能检查相位和边界，但图像会有条纹。
3. 0–2 V三角波，频率100 kHz：当前测试信号，能检查斜率和像素时间。
4. 0–2 V慢斜坡/锯齿，频率约10–50 kHz：检查ADC线性和offset，不适合作为吞吐极限测试。

触发输入保持现有独立行方波；50Ω端直接测高低电平并将阈值设为中点。上述测试信号电压均沿用用户1MΩ测量约定，卡端不得机械折半；推荐让实际模拟输入低电平稍离ADC下限以留噪声余量，先核对源负载能力和offset。不要把2.5MHz像素参考接到External1，也不要同时并联示波器50Ω与卡50Ω。

24帧marker中较长间隔出现在第14、526、1038、1550……行，严格相隔512行；这支持“帧级时序额外1ms”的假设，尚需结合vDAQ帧同步确认。采集从任意line启动，文件中的第1帧只是首512行，不保证与扫描器物理帧边界对齐。

## 总时长和帧数参数

当前 C++ 默认运行由 `framesToAcquire` 控制，默认值为1；`--frame` 强制一帧，`--frames N` 临时覆盖为N帧。正式模式会收到 `N×linesPerFrame` 条完整record后停止，先停止板卡，再排空主机写队列并刷新文件。理论采集时间为：

```text
Ttotal ≈ N × (linesPerFrame × linePeriod + frameFlyback)
```

实际墙钟时间还包括第一次触发等待、fetch、写盘和停止排空。只需要修改 C++ 配置区的 `linePeriod`、`pixelsPerLine`、`framesToAcquire`；`pixelPeriod` 不再是独立输入，由 `recordSize / sampleRate / pixelsPerLine` 自动得到。记录长度会按64 samples向上/最近对齐，以满足SA230P单通道约束，因此实际像素时间可能比 `linePeriod×0.9/pixels` 有极小差异。

采样率提高不会增加“每帧包含的line数”，只会按比例增加每帧文件大小和持续吞吐：

```text
每帧字节数 = linesPerFrame × recordSize × 2
持续原始速率 ≈ sampleRate × lineActiveDuty × 2
可保存帧数 ≤ floor((D盘可用空间 - 20 GiB) / 每帧字节数)
```

当前1 GS/s约1.8 GB/s；2 GS/s约3.6 GB/s；4 GS/s约7.2 GB/s，超过手册所述优化系统约6 GB/s级别，不能仅靠改采样率保证连续采集。应先用 `--frames 1`、`--frames 10`，再逐级增加，并以 `remainingElements`、队列峰值、最大写入延迟和overflow作为边界判据。程序不自动扩大8块缓冲池或删除旧数据。

最大帧数还未测到失败边界，队列触顶后的恢复说明短时背压可被板端缓存吸收，不能据此定量声称已接近吞吐极限。当前可用空间再保留20GiB后不足新增一帧；若释放/迁移测试数据，可继续逐级延长。每帧6,291,456,000字节，空间上限=floor((可用字节-20GiB)/每帧字节)，这是容量预算而非保证可连续采集的性能上限。不要自动删除这些数据。

## 重复命令

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\devenv.com' 'CPP_IVIC_Streaming\CPP_IVIC_Streaming.sln' /Build 'Release|x64'
& '.\CPP_IVIC_Streaming\x64\Release\CPP_IVIC_Streaming.exe' --frame
& '.\CPP_IVIC_Streaming\x64\Release\CPP_IVIC_Streaming.exe' --frames 4
& '.\CPP_IVIC_Streaming\x64\Release\CPP_IVIC_Streaming.exe' --frames 24
$env:ACQ_DATA_FILE='D:\Acq_Storage\BNU_Mark1_0922_172010.dat'
$env:ACQ_FRAME_INDICES='1,12,24'
& 'E:\Software\Matlab2025\bin\matlab.exe' -wait -batch "run('Matlab/reconstruct_acqiris_scan.m')"
```
