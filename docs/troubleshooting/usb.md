# USB Troubleshooting

## EP6 local scratch buffer regressed when `ScopedHeapBuffer` switched to `new[]/delete[]`

**Date**: 2026-03-11
**Category**: usb
**Severity**: major

### Symptoms

- `0.1.4` logs show normal completion with `ep6Timeouts=0` and `DDR_RD_END=1`
- `0.1.5` and raw `76b2b2a` logs stop around acquisition start and the app appears to restart into a new log file
- `cmp_76b2b2a` succeeds with `ep6Timeouts=0` and `DDR_RD_END=1`
- `0.1.6` follow-up log (`2603111710.log`) shows `USB Timeout, Error Code : -10`, `ep6Timeouts=1`, and `DDR_RD_END=0`

### Root Cause

The successful comparison build used a per-call EP6 local scratch buffer backed by CRT `malloc/free`.  
`0.1.6` later changed `ScopedHeapBuffer` to `new[]/delete[]` during a review follow-up. Versioned field logs narrowed the regression back down to that allocator backend change.

### Failed Approaches

1. Assuming the allocator backend was interchangeable because unit tests still passed - field timeout regression remained
2. Treating shared mutex policy as the immediate `0.1.5 -> 0.1.6` fix target - comparison logs showed the successful build still kept the shared mutex

### Solution

1. Compare versioned field logs across `0.1.4`, `0.1.5`, `76b2b2a`, `cmp_76b2b2a`, and `0.1.6`
2. Add a test-first backend contract to `UsbTransferHelpers_test.cpp`
3. Restore `ScopedHeapBuffer` to CRT `malloc/free`
4. Expose the backend contract in `UsbTransferHelpers.h` so future follow-up changes cannot silently switch it back

### Related Files

- `AnalogBoard_Dll/UsbTransferHelpers.h`
- `AnalogBoard_UnitTest/UsbTransferHelpers_test.cpp`
- `docs/archive/build-fixes/2026-03-11-ep6-local-buffer-fix-design.md`
- `docs/process_log/2026-03-02-usb-acquisition-stability-log.md`
