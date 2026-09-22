# Agent Guide

## Scope

This repository controls an Acqiris/SA230P digitizer through the IVI-C++ AqMD3 driver. `CPP_IVIC_SimpleAcquisition` is a finite single-shot example. `CPP_IVIC_Streaming/CPP_IVIC_Streaming.cpp` is the only active streaming entry point and is the file included by the Visual Studio project.

## Acquisition contract

The XY galvo scanner emits a 9,104 µs line period at approximately 109.8 Hz. Ninety percent of each period is the forward scan; the final ten percent is galvo flyback. The active streaming configuration uses the rising edge of `External1` (one trigger per line), a 1 GS/s sample rate, a 24 ns hardware dead time, and a record length derived from the 8,193.6 µs active line window. Do not replace the line trigger with the 2.5 MHz pixel/40 clock: that rate is a synchronization reference, not a viable record trigger.

## Safety and data integrity

Do not silently discard samples or increase the buffer pool without checking RAM and sustained disk bandwidth. At 1 GS/s, raw 16-bit data is about 2 GB/s. A buffer-pool timeout is intentional back-pressure and must remain visible in logs/console output. Check `firstElement` and marker counts whenever changing the fetch path.

## Change policy

- Keep the active source named `CPP_IVIC_Streaming.cpp`.
- Record rationale and parameter changes in `CPP_IVIC_Streaming/VERSION_HISTORY.md` and in Git commit messages.
- Do not reintroduce numbered backup `.cpp` files into the active project directory.
- Hardware-dependent changes must be tested with a real card; simulated mode does not validate Streaming behavior.

## Build

Build `CPP_IVIC_Streaming/CPP_IVIC_Streaming.sln` as x64 with the vendor AqMD3 libraries installed. Verify the instrument resource string, CST option, trigger level, and output path before acquisition.
