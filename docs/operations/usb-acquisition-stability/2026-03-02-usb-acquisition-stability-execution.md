# USB Acquisition Stability Execution Notes

対象プラン: [USB データ取得・書き込み安定性改善プラン](./2026-03-02-usb-acquisition-stability.md)

この文書は phase / PR / test / rollback の実行詳細をまとめる。現在どこを進めるかは checklist / next docs を参照する。

## Phase Summary

| Phase | Goal | Main Focus |
| --- | --- | --- |
| 0 | lightweight instrumentation | hot-path summary metrics |
| 1 | recovery baseline hardening | completion semantics, buffer/backend, save path |
| 1.5 | short-term stabilization | publish non-fatal, preview tolerance |
| 2 | architecture split | Reader / Writer / Publisher, queue boundary |
| 3 | robustness | retry, atomic flags, stop behavior |
| 4 | state clarity | engine state machine, cleanup, degraded paths |
| 5 | testing | unit / integration / soak |
| 6 | KPI judgment | field sessions, acceptance, optional next step |

## PR Breakdown

| PR | Purpose | Key Outputs |
| --- | --- | --- |
| PR-01 | instrumentation | cycle summary, timeout counters, DDR metrics |
| PR-02 | DLL hardening | handle cleanup, buffer/backend fix, timeout waits |
| PR-02b | save path validation | path checks, UI trigger policy |
| PR-03a | preview tolerance | publish failure non-fatal, `.tmp` / `.bin` contract |
| PR-03 | queue and contracts | `WaveChunk`, `BlockingQueue`, phase-2 error codes |
| PR-04 | path split | engine path, writer thread, publish visibility |
| PR-05 | robustness | atomic flags, retry/backoff, stop wait |
| PR-06 | state machine | `Start/Stop/GetStatus`, cleanup, error paths |
| PR-07 | tests | soak / regression / injected timeout coverage |
| PR-08 | KPI decision | field result synthesis and next-step decision |

## Test Matrix

| ID | Focus | Expected Result |
| --- | --- | --- |
| T1 | normal queue handoff | no loss, `USB_SUCCESS` |
| T2 | queue full timeout | `USB_ERR_QUEUE_FULL_TIMEOUT` |
| T3 | retry recovery | completes within retry budget |
| T4 | retry budget exceeded | deterministic `Error` path |
| T5 | publish failure | acquisition continues, failure recorded |
| T6 | stop during draining | no publish of partial data |
| T7 | 8h soak | timeout-rate within KPI |
| T8 | `wave_file_publish` regression | output order preserved |
| T9-T13 | save path validation | path errors surfaced before acquisition |
| T14 | EP6 FIFO pressure | no data loss, counters converge |
| T15 | auto-mode DDR exhaustion | error path explicit |
| T16 | preview consumer | acquisition continues, `.tmp` hidden |
| T17 | `last_n + 1` visibility | only completed pair becomes visible |

## Verification Commands

```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat x64\Debug\AnalogBoard_UnitTest.exe"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_Dll:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

## Rollback Policy

### Rollback triggers

- timeout-rate degrades materially from baseline
- low/high pair loss or corruption appears
- stop path exceeds timeout budget
- new path introduces new regression outside known Type A / Type B

### Rollback steps

1. switch back to previous stable path or binary
2. preserve logs and retained `.tmp`
3. quarantine incomplete or uncertain outputs
4. return to baseline field recipe and compare signatures

## Security / Data Integrity Notes

- reject traversal and reserved names in save path / file naming
- never expose `.tmp` as completed output
- log sizes / counters / error codes, not raw waveform payload
- keep retained `.tmp` or quarantine material for postmortem instead of silently deleting

## Related References

- architecture and contracts: [USB acquisition stability architecture notes](./2026-03-02-usb-acquisition-stability-architecture.md)
- field signatures and session bundle: [USB acquisition stability field reference](./2026-03-02-usb-acquisition-stability-field-reference.md)
- execution status and history: [process log](../../process_log/2026-03-02-usb-acquisition-stability-log.md)
