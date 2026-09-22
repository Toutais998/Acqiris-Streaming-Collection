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

The source of truth is `CPP_IVIC_Streaming.cpp`. Use Git commits/tags for future snapshots instead of copying `.cpp` files with version names.
