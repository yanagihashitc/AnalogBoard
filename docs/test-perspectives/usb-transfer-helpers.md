# UsbTransferHelpers Test Perspectives

Source: `AnalogBoard_UnitTest/UsbTransferHelpers_test.cpp`

Total tests: 8

## Perspective Table

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_TC_N_01_Ep2RequiresSharedMutex` | endpoint role is EP2 | Equivalence – normal | shared EP2/EP4 mutex is required | command endpoint contract |
| `Test_TC_N_02_Ep4RequiresSharedMutex` | endpoint role is EP4 | Equivalence – normal | shared EP2/EP4 mutex is required | register-read endpoint contract |
| `Test_TC_N_03_Ep6SkipsSharedMutex` | endpoint role is EP6 | Equivalence – normal | shared EP2/EP4 mutex is not required | PR-02 core change |
| `Test_TC_B_01_MutexWaitTimeoutIsFixedToFiveSeconds` | helper timeout constant | Boundary – fixed config | timeout is `5000ms` | replaces `INFINITE` wait |
| `Test_TC_N_04_ResetOverlappedPreservesOnlyEventHandle` | stale `OVERLAPPED` with a fresh event handle | Equivalence – reinitialization | all fields except `hEvent` are zeroed | `ZeroMemory` contract |
| `Test_TC_N_05_ScopedHandleClosesExactlyOnce` | scoped handle owns one valid handle | Equivalence – resource cleanup | closer runs exactly once | event handle leak prevention |
| `Test_TC_N_06_ReusableEp6BufferReusesAllocation` | same EP6 buffer size requested twice | Equivalence – buffer reuse | pointer and capacity are reused | removes repeated allocation |
| `Test_TC_B_02_ReusableEp6BufferGrowsWhenNeeded` | buffer grows from small allocation to 4MB | Boundary – capacity growth | capacity expands to requested size | no shrink requirement |
