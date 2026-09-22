# CPP_IVIC_Streaming Directory Guide

## Scope

This directory contains the active SA230P IVI-C AqMD3 triggered-streaming example, its Visual Studio solution, local AqMD3 header, manual, and troubleshooting records. `CPP_IVIC_Streaming.cpp` is the only active streaming source included by the project.

## Acquisition contract

The current test configuration uses a 6,828 microsecond line period at about 146.45 Hz. The requested pixel time is 12 microseconds, so each 512-pixel record is 6,144,000 samples (6.144 ms); this deliberately differs from the 90% period calculation (6,145,200 samples at 1 GS/s) by 1.2 microseconds. The rising edge of `External1` remains the line trigger. The nominal 32 ns dead-time constant does not configure hardware. The 2.5 MHz pixel/40 clock remains a synchronization reference, not the record trigger.

## Known constraints and issues

- SA230P channel and external-trigger inputs are fixed 50 ohm DC-terminated inputs; there is no 1 Mohm selection.
- Voltage values supplied in user discussions are 1 Mohm oscilloscope readings unless explicitly marked otherwise. Recalculate and annotate every trigger/level value for the actual 50 ohm load; never silently reuse an open-circuit amplitude.
- At 1 GS/s and the current record length, sustained raw data is approximately 1.8 GB/s. A stream overflow means data was not consumed quickly enough; it must not be hidden by dropping samples.
- The finite buffer pool is intentional back-pressure. Check RAM, sustained disk bandwidth, free space, marker counts, `firstElement`, `actualElements`, and `remainingElements` before changing it.
- Hardware-dependent streaming behavior requires a real SA230P with CST; simulated mode is not validation.

## Change requirements

For one complete image, use `--frame`: 512 complete records, currently 6,144,000 samples/line, asynchronous disk writer, and a timeout. For repeated frames use `--frames N`; it performs a disk-space check, writes `.config.json` and `.markers.csv`, and reports queue and write timing. The raw file remains headerless int16. MATLAB reads the record parameters from `.config.json`; for the current 12 microsecond pixel time it can reshape 12,000 samples/pixel. See `../Matlab/FRAME_RECONSTRUCTION_146P5.md`.

Read `HANDOFF.md` and `DIAGNOSTIC_RESULTS.txt` for the real-card tests. Unconditional `sleep_for(1us)` caused millisecond host stalls and stream backlog. Use marker timestamps to distinguish accepted triggers from host readout rate. Never assume `firstElement` is zero (8 was observed). The `--diag samples records legacySleep disk` mode explicitly consumes without saving when disk=0; it is not a production acquisition. Record size must be a multiple of 64 for this card. Voltage conversion is nonlinear for the user's source; use measured 50 ohm levels.

- Keep the active source named `CPP_IVIC_Streaming.cpp`; do not add numbered backup `.cpp` files.
- Record parameter rationale in `VERSION_HISTORY.md` and add Chinese explanatory comments when changing code or configuration.
- Build `CPP_IVIC_Streaming.sln` as x64 with AqMD3 libraries installed. Verify resource string, CST option, trigger level/slope, and output path before acquisition.
- After any change in this directory, use a Chinese Git commit message and push it to the configured remote. If push fails, report the reason explicitly.
