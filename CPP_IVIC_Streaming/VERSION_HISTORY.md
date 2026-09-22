# Streaming version history

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

Added `Trouble_Shot.md` and this directory-local `AGENTS.md`. The troubleshooting note records the manual-confirmed fixed 50 ohm termination, the recommended 2.5 V rising-edge trigger for a 0–5 V line signal, and the approximately 1.8 GB/s sustained-stream calculation behind the overflow investigation.

补充电压换算约定：用户给出的幅值默认是示波器 1 MΩ 端口读数；接入固定 50 Ω 负载后必须以实测负载电压重新计算，并在代码注释中注明。

新增 MATLAB 重建脚本，按当前 `D:\Acq_Storage` 二进制输出读取每条 line，并以 512 个像素的积分/平均值生成 512×512 图像。同步补充了信号发生器测试建议和中文提交规范。

The source of truth is `CPP_IVIC_Streaming.cpp`. Use Git commits/tags for future snapshots instead of copying `.cpp` files with version names.
