# Other Troubleshooting

## TestApp APPCRASH at startup after FileLogger integration (MSVCP140.dll / 0xc0000005)

**Date**: 2026-03-06
**Category**: other
**Severity**: major

### Symptoms

- `AnalogBoard_TestApp.exe` exits immediately after launch
- Windows Application log shows:
  - Faulting module: `MSVCP140.dll` (Debug build: `MSVCP140D.dll`)
  - Exception code: `0xc0000005`
- Reproduced for both `Debug|x64` and `Release|x64`

### Root Cause

`PrintLog()` could be invoked after dialog lifetime boundaries where member access was unsafe.  
`FileLogger` being a dialog member made `m_fileLogger` access path fragile in this state, triggering access violation during logging path.

### Failed Approaches

1. Guarding UTF-8 conversion edge cases in `Flush()` only - crash remained
2. Replacing file I/O backend only (`std::ofstream` -> `WriteFile`) without lifetime decoupling - crash remained

### Solution

1. Decouple logger lifetime from dialog instance:
   - Move logger storage from dialog member to `static FileLogger g_fileLogger` in `AnalogBoard_TestAppDlg.cpp`
2. Keep `PrintLog()/FlushLog()/OnClose()` using the static logger
3. Rebuild and verify:
   - `Debug|x64` and `Release|x64` run without immediate APPCRASH
   - Unit tests pass (`build_test.bat`)

### Related Files

- `AnalogBoard_TestApp/AnalogBoard_TestAppDlg.cpp`
- `AnalogBoard_TestApp/AnalogBoard_TestAppDlg.h`
- `AnalogBoard_TestApp/FileLogger.h`
- `AnalogBoard_UnitTest/FileLogger_test.cpp`

---

## New files under `docs/` do not appear in `git status`

**Date**: 2026-03-09
**Category**: other
**Severity**: minor

### Symptoms

- New Markdown files created under `docs/` do not appear in `git status`
- `git check-ignore -v <path>` reports `.gitignore: docs/`
- Existing tracked files under `docs/` remain visible, but newly created files stay hidden

### Root Cause

The repository `.gitignore` contains a broad `docs/` ignore rule. Previously tracked files under `docs/` remain tracked, but any newly created file is ignored unless an explicit negate rule is added later in `.gitignore`.

### Failed Approaches

1. Assuming the new file would behave like existing tracked docs - it remained ignored

### Solution

1. Confirm the ignore source with `git check-ignore -v <path>`
2. Add targeted negate rules for the specific new docs that must be versioned
3. Keep the broad `docs/` ignore in place for unrelated generated or scratch documentation

### Related Files

- `.gitignore`
- `docs/test-perspectives/INDEX.md`
- `docs/archive/code-quality/2026-03-09-test-perspective-catalog-checklist.md`
- `docs/archive/code-quality/2026-03-09-test-perspective-catalog-log.md`
