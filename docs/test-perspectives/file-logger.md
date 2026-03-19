# FileLogger Test Perspectives

Source: `AnalogBoard_UnitTest/FileLogger_test.cpp`

Total tests: 8

## Perspective Table

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_Init_CreatesLogsDirAndFile` | empty temp root, logger initialized | Equivalence – normal init | logs directory and target file are created | filesystem bootstrap |
| `Test_Append_BuffersMessages` | initialized logger, append called before flush | Equivalence – buffered write | message is buffered in memory | non-immediate disk I/O |
| `Test_Flush_WritesBufferToFile` | buffered messages exist | Equivalence – flush | buffered messages are written to disk | persistence path |
| `Test_Flush_ClearsBuffer` | buffered messages flushed once | Equivalence – post-flush state | in-memory buffer is cleared | avoid duplicate writes |
| `Test_MultipleFlushCycles` | append/flush repeated across cycles | Equivalence – repeated operation | each cycle appends and flushes correctly | lifecycle stability |
| `Test_Close_FlushesRemaining` | messages remain buffered at close | Equivalence – close semantics | close flushes remaining messages | safe shutdown |
| `Test_Init_NonExistentParent_Fails` | parent directory path does not exist | Equivalence – invalid path | init fails cleanly | negative path |
| `Test_AppendWithoutInit_NoCrash` | append/flush/close called before init | Boundary – uninitialized object | no crash occurs | defensive API behavior |
