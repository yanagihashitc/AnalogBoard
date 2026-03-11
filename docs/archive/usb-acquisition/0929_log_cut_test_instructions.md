# Current-version timing isolation test instructions

Purpose:
- Verify whether the waveform corruption in the current version is being triggered by EP6-side logging overhead.
- This is an isolation build, not a permanent fix.

Scope:
- Apply these changes only to the current version source tree in the other workspace.
- Do not change the comparison version.
- The comparison version is fixed to commit `aebf296adf72a8ac5a8355f7f539dca87521f724`.

Expected result:
- If the waveform becomes close to the comparison version after these changes, logging overhead is a strong trigger.
- If the waveform is still corrupted, the next step is raw EP6 dump comparison.

Terms used in this note:
- Current version: the version you are editing now, the one that shows the corrupted waveform.
- Comparison version: commit `aebf296adf72a8ac5a8355f7f539dca87521f724`.

## 1. Recommended test order

Do not start by disabling all 3 logging paths at once unless you only care about a quick yes/no answer.

Recommended staged order:

1. Version A:
   Disable only hot-path EP6 app logs in `Dialog1_Main.cpp`
2. Version B:
   Version A + disable persistent file logging in `AnalogBoard_TestAppDlg.cpp`
3. Version C:
   Version B + disable DLL-side EP6 `OutputDebugStringA(...)`

Reason:
- This makes it possible to see which logging layer is acting as the trigger.
- If you disable all 3 at once and the waveform improves, you still will not know which one mattered most.

Quick shortcut if you want fewer runs:

1. Version B first
2. If still corrupted, Version C

Likely impact order:

1. `Dialog1_Main.cpp` EP6 hot-path `PrintLog(...)`
2. file logging behind `PrintLog(...)`
3. DLL-side `OutputDebugStringA(...)`

## 2. Version A: disable hot-path EP6 logs in `Dialog1_Main.cpp`

Target file:
- `AnalogBoard_TestApp/Dialog1_Main.cpp`

Add this near the top, inside the anonymous namespace:

```cpp
// Timing-isolation switch for investigating the current-version EP6 issue.
// Keep high-frequency EP6 logs off to reduce UI/file I/O interference.
constexpr bool kEnableEp6HotPathLogs = false;
```

Wrap only the following high-frequency `PrintLog(...)` calls with:

```cpp
if (kEnableEp6HotPathLogs)
{
    ...
}
```

Targets to wrap:

1. `[PR01][FILE] ...`
2. `Fpga ddr write completed. %zu byte.`
3. `DDR data sized %zu byte.`
4. `[PR01][EP6] call=...`
5. `EP6 Read %u byte OK. Save into bin file...`
6. `Save OK.(Total size %zu byte)`
7. `[PR01][CYCLE] ...`
8. If the `#else` branch is used in your build, also wrap:
   `EP6 Read %u byte OK.Total size %zu byte.`

Do not suppress:
- error logs
- timeout/error logs
- `Remain size error.`
- `Usb read buffer index error.`
- start/end logs

Reason:
- These messages are inside or adjacent to the EP6 acquisition loop.
- In the current version, `PrintLog()` is synchronous to the UI and also writes to file, so these messages are much more expensive than in the comparison version.

## 3. Version B: disable persistent file logging in `AnalogBoard_TestAppDlg.cpp`

Target file:
- `AnalogBoard_TestApp/AnalogBoard_TestAppDlg.cpp`

Add near the globals:

```cpp
namespace
{
    // Timing-isolation switch for investigating the current-version EP6 issue.
    constexpr bool kEnablePersistentFileLog = false;
}
```

Wrap these operations with `if (kEnablePersistentFileLog)`:

1. `g_fileLogger.Init(...)`
2. `g_fileLogger.Append(...)`
3. `g_fileLogger.Flush()`
4. `g_fileLogger.Close()`

Keep the UI listbox logging as-is.

Reason:
- The current version writes every `PrintLog()` line to disk.
- The file write itself is not huge, but in combination with synchronous UI logging it increases timing pressure.

## 4. Version C: disable DLL-side EP6 debug output in `AnalogBoard_Dll.cpp`

Target file:
- `AnalogBoard_Dll/AnalogBoard_Dll.cpp`

Add near the EP6 diagnostic globals:

```cpp
namespace
{
    // Timing-isolation switch for investigating the current-version EP6 issue.
    constexpr bool kEnableEp6DllPerfLog = false;
}
```

Wrap the block that builds `perfLog` and calls `OutputDebugStringA(perfLog)` with:

```cpp
if (kEnableEp6DllPerfLog)
{
    ...
}
```

Target block:
- the block near the end of `USB_Lib_Info::EP6_GetData(...)`
- starts around:
  - `const LONG currentCallCount = ::InterlockedIncrement(&g_ep6CallCount);`
- ends at:
  - `::OutputDebugStringA(perfLog);`

Reason:
- `OutputDebugStringA` can become expensive when Visual Studio debugger or DebugView is attached.

## 5. Build and run conditions

Build:
- Prefer `x64 Release`

Run conditions:
- Run without Visual Studio debugger attached
- Make sure DebugView is not running
- Use the same machine
- Use the same sample
- Use the same parameters as comparison version `aebf296adf72a8ac5a8355f7f539dca87521f724`

## 6. Test procedure

Run each version as a separate build and save the resulting data separately.

Suggested run sequence:

1. Build and test Version A
2. If still corrupted, build and test Version B
3. If still corrupted, build and test Version C

For each run:

1. Build the modified current-version app and DLL.
2. Acquire data with the same settings used for:
   - the current version
   - comparison version `aebf296adf72a8ac5a8355f7f539dca87521f724`
3. Save one dataset.
4. Compare:
   - waveform appearance in the notebook
   - whether the periodic corrupted-channel windows still appear
   - whether `fl` and `fh` now resemble the comparison version

## 7. Interpretation

Case A:
- Version A already becomes normal or close to the comparison version

Meaning:
- The app-side EP6 hot-path `PrintLog(...)` is the strongest trigger.
- The deeper root cause is still probably fragile EP6/frame-boundary handling, but the logging load is what exposed it.

Case B:
- Version A is still corrupted, but Version B becomes normal or close to the comparison version

Meaning:
- File logging is contributing materially in addition to UI logging.
- The deeper root cause is still probably fragile EP6/frame-boundary handling, but the logging load is what exposed it.

Case C:
- Version B is still corrupted, but Version C becomes normal or close to the comparison version

Meaning:
- DLL-side `OutputDebugStringA(...)` is also contributing.
- The deeper root cause is still probably fragile EP6/frame-boundary handling, but the logging load is what exposed it.

Case D:
- Version C is still corrupted

Meaning:
- Logging is probably not the main trigger.
- Next step should be:
  - capture raw EP6 dump from both versions
  - compare raw buffer content before `fl/fh` split

## 8. Important note

This test does not prove that logging "directly corrupts values".
What it tests is:
- whether the added logging load changes acquisition timing enough to trigger the already-suspect EP6 boundary/reassembly problem.
