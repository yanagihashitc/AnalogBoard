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

---

## LNK4098 in `Debug|x64` because `CyAPI.lib` pulls `LIBCMT`

**Date**: 2026-03-11
**Category**: build
**Severity**: minor

### Symptoms

- `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"` succeeds but emits:
  - `LINK : warning LNK4098: defaultlib 'LIBCMT' conflicts with use of other libs`
- Warning is reported from `AnalogBoard_Dll.vcxproj`

### Root Cause

`CyLib\x64\CyAPI.lib` contains the linker directive `/DEFAULTLIB:LIBCMT`.  
`AnalogBoard_Dll` Debug build uses the dynamic debug CRT (`/MDd`), so the linker sees a static CRT request from `CyAPI.lib` and reports `LNK4098`.

### Failed Approaches

1. Treating the warning as originating from the DLL project itself without inspecting dependency directives

### Solution

1. Confirm the dependency directive with:
   - `cmd /d /c "scripts\run_with_vsdevcmd.bat dumpbin /directives CyLib\x64\CyAPI.lib"`
2. Mirror the existing Release workaround in `AnalogBoard_Dll.vcxproj`
3. Add `LIBCMT` to `IgnoreSpecificDefaultLibraries` for `Debug|x64`
4. Re-run the full `Debug|x64` rebuild and confirm it finishes with `0 warning / 0 error`

### Related Files

- `CyLib/x64/CyAPI.lib`
- `AnalogBoard_Dll/AnalogBoard_Dll.vcxproj`
- `AnalogBoard_TestApp.sln`

---

## C4996 / STL4017 when simulation code uses `<codecvt>` in VS2022

**Date**: 2026-03-11
**Category**: build
**Severity**: minor

### Symptoms

- `msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1` fails in `SimulationRunnerCore.cpp` / `SimulationScenario.cpp`
- Compiler reports `error C4996` for `std::codecvt_utf8_utf16`
- The nested warning text is `STL4017` about `<codecvt>` deprecation in C++17

### Root Cause

The VS2022 STL marks `<codecvt>` helpers such as `std::codecvt_utf8_utf16` as deprecated in C++17. In this repository, the standalone simulation project treats that deprecation as `C4996`, so using locale facets based on `<codecvt>` breaks the build.

### Failed Approaches

1. Using `std::wifstream` / `std::wofstream` with `std::locale(..., new std::codecvt_utf8_utf16<wchar_t>)`

### Solution

1. Remove `<codecvt>` usage from the simulation code
2. Read JSON as UTF-8 bytes with `std::ifstream`
3. Convert UTF-8 to UTF-16 with `MultiByteToWideChar`
4. Convert UTF-16 to UTF-8 with `WideCharToMultiByte` when writing `runner.log` / `summary.json`
5. Re-run the SimRunner rebuild and confirm it succeeds

### Related Files

- `AnalogBoard_SimRunner/SimulationScenario.cpp`
- `AnalogBoard_SimRunner/SimulationRunnerCore.cpp`
- `AnalogBoard_SimRunner/AnalogBoard_SimRunner.vcxproj`

---

## C2589/C4003 when `std::numeric_limits::min/max` collides with Windows macros

**Date**: 2026-03-11
**Category**: build
**Severity**: minor

### Symptoms

- A standalone `cl` build of `SimulationScenario.cpp` fails after adding `std::numeric_limits<...>::min()` / `max()`
- The compiler reports:
  - `warning C4003: 関数に似たマクロ呼び出し 'max' の引数が不足しています`
  - `error C2589: '(' : スコープ解決演算子 (::) の右側にあるトークンは使えません`

### Root Cause

`windows.h` defines `min` and `max` as macros unless `NOMINMAX` is set.  
Those macros expand inside `std::numeric_limits<T>::min()` / `max()` and break parsing in MSVC.

### Failed Approaches

1. Calling `std::numeric_limits<T>::min()` / `max()` directly in a translation unit that already includes `windows.h`

### Solution

1. Keep using `std::numeric_limits`
2. Call the functions with macro-safe syntax:
   - `(std::numeric_limits<T>::min)()`
   - `(std::numeric_limits<T>::max)()`
3. Re-run the standalone `cl` build or project rebuild to confirm the errors disappear

### Related Files

- `AnalogBoard_SimRunner/SimulationScenario.cpp`
- `AnalogBoard_UnitTest/SimulationScenario_test.cpp`

### References

- none
