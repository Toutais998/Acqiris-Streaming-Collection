# CPP_IVIC_Streaming Directory Guide

## Scope

This directory contains the active SA230P IVI-C AqMD3 triggered-streaming example, its Visual Studio solution, local AqMD3 header, manual, and troubleshooting records. `CPP_IVIC_Streaming.cpp` is the only active streaming source included by the project.

## Acquisition contract

The XY galvo emits one approximately 9,104 microsecond line period at about 109.8 Hz. The forward scan occupies 90% of the period. The example uses the rising edge of `External1`, 1 GS/s, a nominal 32 ns dead-time assumption, and an 8,193.6 microsecond active window. The deadTime constant does not configure hardware and was not measured by these tests. The 2.5 MHz pixel/40 clock is a synchronization reference, not the record trigger.

## Known constraints and issues

- SA230P channel and external-trigger inputs are fixed 50 ohm DC-terminated inputs; there is no 1 Mohm selection.
- Voltage values supplied in user discussions are 1 Mohm oscilloscope readings unless explicitly marked otherwise. Recalculate and annotate every trigger/level value for the actual 50 ohm load; never silently reuse an open-circuit amplitude.
- At 1 GS/s and the current record length, sustained raw data is approximately 1.8 GB/s. A stream overflow means data was not consumed quickly enough; it must not be hidden by dropping samples.
- The finite buffer pool is intentional back-pressure. Check RAM, sustained disk bandwidth, free space, marker counts, `firstElement`, `actualElements`, and `remainingElements` before changing it.
- Hardware-dependent streaming behavior requires a real SA230P with CST; simulated mode is not validation.

## Change requirements

For one complete image, use `--frame`: 512 complete records, default 8,193,600 samples/line, asynchronous disk writer, and a 10-second timeout. The raw file remains headerless int16; `.dat.markers.csv` stores timing separately. MATLAB must bin fractional samples-per-pixel using boundaries (16,003.125 average), not reshape into equal integer bins. See `../Matlab/FRAME_RECONSTRUCTION.md` for the latest end-to-end real-card result.

Read `HANDOFF.md` and `DIAGNOSTIC_RESULTS.txt` for the real-card tests. Unconditional `sleep_for(1us)` caused millisecond host stalls and stream backlog. Use marker timestamps to distinguish accepted triggers from host readout rate. Never assume `firstElement` is zero (8 was observed). The `--diag samples records legacySleep disk` mode explicitly consumes without saving when disk=0; it is not a production acquisition. Record size must be a multiple of 64 for this card. Voltage conversion is nonlinear for the user's source; use measured 50 ohm levels.

- Keep the active source named `CPP_IVIC_Streaming.cpp`; do not add numbered backup `.cpp` files.
- Record parameter rationale in `VERSION_HISTORY.md` and add Chinese explanatory comments when changing code or configuration.
- Build `CPP_IVIC_Streaming.sln` as x64 with AqMD3 libraries installed. Verify resource string, CST option, trigger level/slope, and output path before acquisition.
- After any change in this directory, use a Chinese Git commit message and push it to the configured remote. If push fails, report the reason explicitly.
