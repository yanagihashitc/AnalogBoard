# Build Troubleshooting

## MSB4057 when invoking `AnalogBoard_UnitTest:Rebuild` on the solution

**Date**: 2026-03-06
**Category**: build
**Severity**: minor

### Symptoms

- `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"` fails immediately
- MSBuild reports `error MSB4057: ターゲット "AnalogBoard_UnitTest:Rebuild" はプロジェクト内に存在しません`

### Root Cause

`AnalogBoard_UnitTest` is not a project target inside `AnalogBoard_TestApp.sln`. The unit tests in this repository are built by `AnalogBoard_UnitTest/build_test.bat`, which compiles the standalone test executables directly with `cl`.

### Failed Approaches

1. Running `msbuild` with `/t:AnalogBoard_UnitTest:Rebuild` against the solution file

### Solution

Use one of the following verified commands depending on the goal:

- Unit tests only:
  `cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"`
- Full Debug rebuild:
  `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"`

### Related Files

- `AnalogBoard_UnitTest/build_test.bat`
- `AnalogBoard_TestApp.sln`
- `AGENTS.md`

---

## Parallel `cl` invocations hit C1041 on shared `vc140.pdb`

**Date**: 2026-03-09
**Category**: build
**Severity**: minor

### Symptoms

- Two standalone `cl` invocations are launched in parallel from the repository root
- One compile fails with `fatal error C1041: プログラム データベース '...\\vc140.pdb' を開けません`
- The message suggests `/FS` because multiple `CL.EXE` processes are writing the same PDB

### Root Cause

Standalone `cl` commands default to the same `vc140.pdb` output when no explicit `/Fd` is provided. Parallel compilation from separate commands therefore races on the shared PDB file.

### Failed Approaches

1. Running multiple standalone `cl` commands in parallel without `/FS` or distinct `/Fd`

### Solution

Use one of these verified approaches:

- Run the standalone `cl` commands serially
- Or assign a unique `/Fd` per compile when parallelism is necessary
- The repository's `AnalogBoard_UnitTest\\build_test.bat` already avoids this by compiling test executables sequentially

### Related Files

- `AnalogBoard_UnitTest/build_test.bat`
- `AnalogBoard_UnitTest/UsbTransferHelpers_test.cpp`

---

## MSB4184 when `msbuild` cannot access `C:\Users\...\Microsoft SDKs`

**Date**: 2026-03-09
**Category**: build
**Severity**: minor

### Symptoms

- `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"` fails immediately
- MSBuild reports `error MSB4184` while evaluating `GetLatestSDKTargetPlatformVersion(Windows, 10.0)`
- The inner message says `Access to the path 'C:\Users\chiccho\AppData\Local\Microsoft SDKs' is denied`

### Root Cause

The solution rebuild touches the Windows SDK discovery path under the user profile. In the sandboxed agent environment, that path is readable only when the command is run with elevated permissions, so the regular `msbuild` invocation fails before project evaluation finishes.

### Failed Approaches

1. Running the full solution rebuild inside the default sandbox

### Solution

Re-run the exact same `msbuild` command with escalated permissions enabled for the agent session. The rebuild then succeeds without any source changes.

### Related Files

- `scripts/run_with_vsdevcmd.bat`
- `AnalogBoard_TestApp.sln`
- `AnalogBoard_TestApp/AnalogBoard_TestApp.vcxproj`
- `AnalogBoard_Dll/AnalogBoard_Dll.vcxproj`
