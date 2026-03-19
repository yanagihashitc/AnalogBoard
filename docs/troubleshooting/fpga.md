# FPGA Troubleshooting

## Immediate empty capture when startup sees stale `DDR_RD_END=1`

**Date**: 2026-03-18
**Category**: fpga
**Severity**: major

### Symptoms

- Field log shows `FPGA start sampling.` immediately followed by `Read over, saved wave count 0.`
- No `USB Timeout`, no `[PR01][TIMEOUT]`, and no meaningful waveform files are produced
- The same symptom was previously observed in `0.2.x`

### Root Cause

The host completion helper treated an initial stale EP4 snapshot with `DDR_RD_END=1` and no readable bytes as a real active cycle. Because `savedBytes==0` and `unreadBytes==0`, the helper could mark acquisition complete before the new measurement actually exposed any data.

### Failed Approaches

1. Guarding only the `DDR_WR_END=1 && DDR_RD_END=1` startup-stale case

### Solution

1. Add focused regression tests for `startup stale DDR_RD_END only`:
   - `DDR_WR_END=0, DDR_RD_END=1, WAVE_WR_CNT=0`
2. Update the startup stale completion guard in `AcquisitionCompletionLogic.h` to ignore:
   - `savedBytes==0 && DDR_RD_END==1 && (WAVE_WR_CNT==0 || DDR_WR_END==1)`
3. Re-run:
   - focused `AcquisitionCompletionLogic_test.exe`
   - full `AnalogBoard_UnitTest\\build_test.bat`
   - `Release|x64` DLL / TestApp rebuild
4. Resume field validation with a low-density smoke run before returning to the 100-cycle gate

### Related Files

- `AnalogBoard_TestApp/AcquisitionCompletionLogic.h`
- `AnalogBoard_UnitTest/AcquisitionCompletionLogic_test.cpp`
- `logs/0.1.4r8/logs/2603181853.log`
