# Gate B/C field result and EP4 polling remediation checklist

Target plan: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)  
Process log: [Gate B/C EP4 polling remediation log](2026-07-15-gate-bc-ep4-polling-remediation-log.md)  
Created: 2026-07-15

## Evidence preservation and classification

- [x] Preserve the returned inventory, logs, and generated evidence as the old-build field record.
- [x] Record Gate B as log-condition success with missing returned data/telemetry evidence.
- [x] Record Gate C as inventory Pass, idle EP4 10/10, and automatic EP4 failure 3/3 after Set.

## TDD and implementation

- [x] Add a unit test that requires a bounded delay only while external-trigger EP4 polling continues.
- [x] Observe the expected Red result before adding production code.
- [x] Implement a 10 ms idle cadence and log the existing USB status when EP4 transfer fails.
- [x] Keep transfer failure fatal; do not retry or alter the EP6 acquisition/read loop.
- [x] Run focused and full unit tests.

## Review and distribution

- [x] Run `claude-review-fixer` before Release build and resolve required findings.
- [x] Clean-rebuild matched Release x64 EXE/DLL.
- [x] Create a new build identity and preserve the previous fixed build as rollback evidence.
- [x] Refresh the returned field package, immutable checksums, inventory expected build ID, and package guidance.

## Documentation and final validation

- [x] Fill the returned result sheet and separate old-build evidence from remediation-build rerun status.
- [x] Update canonical/package runbooks, rebuild plan, source-plan mirror, and active roadmap.
- [x] Validate unit tests, build, package verifier, PowerShell syntax/contracts, HTML, local links, docs sync, and complete diffs.
- [x] Archive this checklist and process log after every item is complete.

Hardware acceptance was intentionally not marked complete in this historical checklist. The next field action at that time was the non-counting new-driver fail-fast described in [the archived HTML task](2026-07-15-new-driver-fail-fast-next-task.html).
