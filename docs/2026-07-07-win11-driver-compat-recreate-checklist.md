# Win11 Driver Compatibility Recreate Checklist

対象プラン:

- [AnalogBoard rebuild plan](plans/260703-analogboard-rebuild-plan.html)
- [Driver Next](driver_next.md)

プロセスログ: [Process Log](process_log/2026-07-07-win11-driver-compat-recreate-log.md)
作成日: 2026-07-07

---

## Phase: feature/win11-driver-compat recreate

- [x] Batch 1: Create `feature/win11-driver-compat` from `origin/dev`, push branch, and prepare CyAPI library tracking
- [x] Batch 2: Add focused endpoint discovery tests first
- [x] Batch 2: Implement endpoint discovery by endpoint address in `AnalogBoard_Dll` without public API drift
- [x] Batch 3: Run focused unit tests and `git diff --check`
- [x] Batch 3: Run Windows `Release|x64` rebuild or record Windows verification pending at the stop condition
- [x] Batch 3: Complete refactor pass, review pass, peer review, final diff inspection, commit, and push

**Focused verification commands:**

```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat cl /FS /EHsc /W4 /Zi /std:c++17 /I. /Fd:AnalogBoard_UnitTest\UsbEndpointDiscoveryPolicy_test.pdb AnalogBoard_UnitTest\UsbEndpointDiscoveryPolicy_test.cpp /Fe:AnalogBoard_UnitTest\UsbEndpointDiscoveryPolicy_test.exe /link /DEBUG"
cmd /d /c "AnalogBoard_UnitTest\UsbEndpointDiscoveryPolicy_test.exe"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_Dll:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild /p:Configuration=Release /p:Platform=x64 /m:1"
```
