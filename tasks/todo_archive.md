# tasks/todo_archive.md

## Completed: feature/win11-driver-compat recreate - Batch 1

- [x] Create dedicated worktree/branch from `origin/dev`
- [x] Push `feature/win11-driver-compat` once immediately after branch creation
- [x] Bring in tracked CyLib `.gitignore` exceptions from `main`
- [x] Bring in `CyLib/x64/CyAPI.lib` and `CyLib/x86/CyAPI.lib`
- [x] Confirm `AnalogBoard_Dll.vcxproj` keeps the required CyAPI link settings
- [x] Review Batch 1 evidence and archive this batch to `tasks/todo_archive.md`

## Review

- Verification: `git push -u origin feature/win11-driver-compat` created the remote branch.
- Verification: `.gitignore` now includes `!CyLib/x64/`, `!CyLib/x86/`, `!CyLib/x64/CyAPI.lib`, and `!CyLib/x86/CyAPI.lib`.
- Verification: `CyLib/x64/CyAPI.lib` is 119946 bytes and `CyLib/x86/CyAPI.lib` is 82902 bytes in this worktree.
- Verification: `AnalogBoard_Dll/AnalogBoard_Dll.vcxproj` still links `CyAPI.lib;setupapi.lib;legacy_stdio_definitions.lib`, uses `..\CyLib\x64`, and ignores `LIBCMT` for Release.
- Residual risk: no MSVC build was run in Batch 1; Windows verification remains a Batch 3 stop-condition gate.

## Completed: feature/win11-driver-compat recreate - Batch 2

- [x] Read endpoint discovery implementation and existing unit-test style
- [x] Add failing focused tests for address-based endpoint discovery
- [x] Implement testable endpoint discovery logic in `AnalogBoard_Dll`
- [x] Wire `USBBoard_Connect` to resolve EP2/EP4/EP6 by address
- [x] Run focused verification where available
- [x] Review Batch 2 evidence and archive this batch to `tasks/todo_archive.md`

## Review

- Red: `g++ -std=c++17 -I. AnalogBoard_UnitTest/UsbEndpointDiscoveryPolicy_test.cpp -o /tmp/UsbEndpointDiscoveryPolicy_test` failed because `UsbEndpointDiscoveryPolicy.h` did not exist.
- Green: after adding `UsbEndpointDiscoveryPolicy.h`, the focused WSL compile/run passed: 16 tests, 16 passed, 0 failed.
- Verification: `git diff --check && git diff --cached --check` passed.
- Verification: `AnalogBoard_Dll.h` and `AnalogBoard_Dll.def` have no diff, so public API/export surface is unchanged.
- Scope check: implementation is limited to `AnalogBoard_Dll` endpoint discovery and the focused unit-test script/test.
- Residual risk: MSVC `cl` and Release|x64 rebuild are still pending for Batch 3 / Windows gate.

## Completed: feature/win11-driver-compat recreate - Batch 3

- [x] Run phase-level focused verification: `UsbEndpointDiscoveryPolicy_test`, existing EP6 focused suites, and `git diff --check`
- [x] Attempt Windows/MSVC focused build/test via `scripts\run_with_vsdevcmd.bat` if available
- [x] Confirm MSVC verification is available, so the Windows-verification-pending stop condition does not apply
- [x] Run scoped refactor pass from `.agent/refactor.md`
- [x] Run scoped review pass from `.agent/review.md`
- [x] Await peer review via agmsg/Claude or record skip reason after timeout/unavailability
- [x] Final diff inspection
- [x] Commit related changes only and push

## Review

- Verification: WSL `UsbEndpointDiscoveryPolicy_test` passed 27/27.
- Verification: MSVC `UsbEndpointDiscoveryPolicy_test` passed 27/27, `Ep6TransferRetryPolicy_test` passed 26/26, and `Ep6TransferTuningPolicy_test` passed 6/6.
- Verification: `Release|x64` rebuild passed for `AnalogBoard_Dll` and `AnalogBoard_TestApp`.
- Verification: `git diff --cached --check` passed before commit.
- Peer review: Claude replied at 2026-07-06T17:52:25Z with three low/verify findings. F1 and F2 were fixed; F3 is recorded as a required emulator/field-gate evidence item beyond unit tests.
- Commit/push: `a98b00d` was created and pushed, then amended to include task tracking while preserving one phase commit.
- Residual risk: full `AnalogBoard_UnitTest/build_test.bat` is blocked by pre-existing `FpgaRegisterLogic_test.cpp` compile errors before the new suite runs. Win11 new-driver first gate and Tier1/2 replay/fault-injection remain required before field acceptance.
