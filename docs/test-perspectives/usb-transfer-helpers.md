# UsbTransferHelpers Test Perspectives

Source: `AnalogBoard_UnitTest/UsbTransferHelpers_test.cpp`

Total tests: 22

## Perspective Table

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_TC_N_01_Ep2RequiresSharedMutex` | endpoint role is EP2 | Equivalence – normal | shared EP2/EP4 mutex is required | command endpoint contract |
| `Test_TC_N_02_Ep4RequiresSharedMutex` | endpoint role is EP4 | Equivalence – normal | shared EP2/EP4 mutex is required | register-read endpoint contract |
| `Test_TC_N_03_Ep6RequiresSharedMutex` | endpoint role is EP6 | Equivalence – normal | shared EP2/EP4 mutex is required | current `0.1.5` contract |
| `Test_TC_B_01_MutexWaitTimeoutIsFixedToFiveSeconds` | helper timeout constant | Boundary – fixed config | timeout is `5000ms` | replaces `INFINITE` wait |
| `Test_TC_N_04_ResetOverlappedPreservesOnlyEventHandle` | stale `OVERLAPPED` with a fresh event handle | Equivalence – reinitialization | all fields except `hEvent` are zeroed | `ZeroMemory` contract |
| `Test_TC_N_05_ScopedHandleClosesExactlyOnce` | scoped handle owns one valid handle | Equivalence – resource cleanup | closer runs exactly once | event handle leak prevention |
| `Test_TC_N_06_ReusableEp6BufferReusesAllocation` | same reusable buffer size requested twice | Equivalence – buffer reuse | pointer and capacity are reused | existing helper contract |
| `Test_TC_B_02_ReusableEp6BufferGrowsWhenNeeded` | reusable buffer grows from small allocation to 4MB | Boundary – capacity growth | capacity expands to requested size | no shrink requirement |
| `Test_TC_N_07_ReleaseMutexIfOwned_ReleasesExactlyOnce` | owned valid mutex handle | Equivalence – cleanup | release callback runs exactly once | shared mutex teardown |
| `Test_TC_N_08_ReleaseMutexIfOwned_SkipsWhenNotOwned` | valid mutex handle but ownership false | Equivalence – cleanup guard | callback is skipped | ownership contract |
| `Test_TC_B_03_ReleaseMutexIfOwned_NullHandleSkipsRelease` | ownership true with null handle | Boundary – null handle | callback is skipped | invalid handle guard |
| `Test_TC_N_09_ReusableEp6BufferZeroFillClearsExistingBytes` | reusable buffer contains stale bytes | Equivalence – buffer reset | requested byte range is zeroed | existing helper contract |
| `Test_TC_B_04_ReusableEp6BufferZeroFill_ZeroBytesIsNoOp` | reusable buffer with zero-length clear | Boundary – zero | success with no modification | zero-length guard |
| `Test_TC_B_05_ReusableEp6BufferZeroFill_RejectsOutOfRangeSize` | clear size exceeds reusable buffer capacity | Boundary – out of range | request is rejected | capacity guard |
| `Test_TC_N_10_ScopedHeapBufferAllocatesZeroedMaxTransfer` | local heap buffer requested for 4MB transfer | Equivalence – normal | allocation succeeds and bytes are zero-initialized | fix helper for EP6 local scratch buffer |
| `Test_TC_N_11_ScopedHeapBufferAllocatesZeroedMinimumPositiveSize` | local heap buffer requested for 1 byte | Boundary – min positive | allocation succeeds and the byte is zero | smallest meaningful size |
| `Test_TC_B_06_ScopedHeapBufferRejectsZeroSize` | local heap buffer requested for 0 bytes | Boundary – zero | allocation fails and state remains empty | invalid size guard |
| `Test_TC_B_07_ScopedHeapBufferZeroSizeClearsPreviousAllocation` | valid allocation followed by 0-byte request | Boundary – zero after valid state | request fails and previous allocation is released | avoids stale scratch buffer reuse |
| `Test_TC_N_12_ScopedHeapBufferMoveConstructorTransfersOwnership` | populated scratch buffer is move-constructed | Equivalence – normal | ownership transfers and source becomes empty | explicit move support for unique ownership |
| `Test_TC_N_13_ScopedHeapBufferMoveAssignmentTransfersOwnership` | populated destination takes ownership from populated source | Equivalence – normal | destination takes source buffer and source becomes empty | existing allocation must be released |
| `Test_TC_B_08_ScopedHeapBufferMoveConstructorHandlesEmptySource` | empty scratch buffer is move-constructed | Boundary – empty source | both helpers remain empty and valid | no allocation to transfer |
| `Test_TC_B_09_ScopedHeapBufferMoveAssignmentHandlesEmptySource` | populated destination takes ownership from empty source | Boundary – empty source | destination releases old allocation and becomes empty | validates empty-source move assignment |
