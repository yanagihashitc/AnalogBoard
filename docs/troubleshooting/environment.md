# Environment Troubleshooting

## Batch file output empty when invoked via bash cd + cmd

**Date**: 2026-03-06
**Category**: environment
**Severity**: minor

### Symptoms

- Running `cd "d:/path/to/dir" && cmd /d /c "build_test.bat"` from bash produces no output
- Running `cmd //c "call build_test.bat"` after `cd` also produces no output
- The batch file exists and is visible via `cmd //c "dir build_test.bat"`
- Error message (when redirected): `'build_test.bat' is not recognized as an internal or external command` (in Japanese CP932)

### Root Cause

When bash `cd` changes to a directory using Unix-style paths (e.g. `/d/ubuntu/...`), the resulting working directory may not be correctly inherited by nested `cmd.exe` processes. The cmd subprocess cannot resolve the batch file name because its working directory is not set to the expected Windows path.

### Failed Approaches

1. **`cd "d:/path" && cmd /d /c "build_test.bat"`** - cmd subprocess does not inherit bash's cwd correctly
2. **`cmd //c "cd /d D:\path && build_test.bat"`** - Still fails with "not recognized" error
3. **`cmd //c "cd /d D:\path && call build_test.bat"`** - Same failure

### Solution

Use the full Windows-native absolute path to invoke the batch file directly:

```bash
cmd //c "D:\ubuntu\jupyter\sys_analyzer\AnalogBoard\AnalogBoard_UnitTest\build_test.bat" 2>&1 | cat
```

Key points:
- Use `//c` (double slash) in bash to pass `/c` to cmd.exe
- Pipe through `| cat` to capture output in bash
- The batch file's internal `%~dp0` resolves correctly when invoked by absolute path

### Related Files

- `AnalogBoard_UnitTest/build_test.bat`
- `scripts/run_with_vsdevcmd.bat`

---

## std::filesystem unavailable in MFC TestApp project (no C++17)

**Date**: 2026-03-06
**Category**: environment
**Severity**: major

### Symptoms

- `#include <filesystem>` in `FileLogger.h` compiles fine in UnitTest (`/std:c++17`) but fails in TestApp MSBuild:
  - `error C2653: 'fs': is not a class or namespace name`
  - `error C2065: 'path': undeclared identifier`
- 48 errors total, all originating from `std::filesystem` usage

### Root Cause

The `AnalogBoard_TestApp.vcxproj` does not set `<LanguageStandard>` — defaults to C++14. `std::filesystem` requires C++17. UnitTest builds via `build_test.bat` with explicit `/std:c++17` flag, masking the incompatibility.

### Solution

Replace `std::filesystem` usage with Win32 API equivalents:
- `fs::create_directories()` → `CreateDirectoryW()` (single-level, sufficient for `logs/` under existing exe dir)
- `fs::path` concatenation → `std::wstring` concatenation with `L'\\'`

This avoids requiring C++17 in the MFC project and keeps the header self-contained.

### Related Files

- `AnalogBoard_TestApp/FileLogger.h`
- `AnalogBoard_TestApp/AnalogBoard_TestApp.vcxproj`
