# WaveDataFileIO Test Perspectives

Source: `AnalogBoard_UnitTest/WaveDataFileIO_test.cpp`

Total tests: 19

## Perspective Table

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_T0_SaveWaveDataToFileImpl_NullLowWriter_SkipsLow` | low writer `nullptr`, high writer valid, 2 frames | Boundary – disabled lane | low lane is skipped and high lane only is written | low lane optional contract |
| `Test_T0_SaveWaveDataToFileImpl_NullHighWriter_SkipsHigh` | high writer `nullptr`, low writer valid, 2 frames | Boundary – disabled lane | high lane is skipped and low lane only is written | high lane optional contract |
| `Test_T0_SaveWaveDataToFileImpl_NullLowWriter_ZeroLowFrameSize` | low writer `nullptr`, low frame size `0`, high lane active | Boundary – zero frame size | call succeeds and high lane is preserved | zero-sized disabled lane |
| `Test_T0_SaveWaveDataToFileImpl_LowWriterFailure_ReturnsLowError` | low writer fails on first write | Equivalence – write failure | low write error is returned | failure surfacing for PR-02 |
| `Test_T0_SaveWaveDataToFileImpl_HighWriterFailure_ReturnsHighError` | high writer fails on first write | Equivalence – write failure | high write error is returned | failure surfacing for PR-02 |
| `Test_T1_BinaryFormatUnchanged_AllPairs` | reconstructed read buffer for multiple real samples | Equivalence – regression | generated `fl/fh` binaries match source hashes | format compatibility regression test |
| `Test_T2_AtomicPublish_Success` | both `.tmp` files exist and final paths are free | Equivalence – normal publish | low/high rename succeeds and tmp files disappear | atomic publish happy path |
| `Test_T2_AtomicPublish_FlRenameFail_TmpRemains` | final low path locked before publish | Equivalence – low rename failure | publish fails and both tmp files remain | failure should not consume tmp |
| `Test_T2_AtomicPublish_FhRenameFail_RollbackLow` | final high path locked after low rename | Equivalence – high rename failure | low publish is rolled back and high tmp remains | rollback without existing low backup |
| `Test_T2_AtomicPublish_FhRenameFail_RestoreExistingLow` | existing low final file backed up before high rename fails | Equivalence – rollback restore | original low final file is restored | rollback with backup file |
| `Test_T2_AtomicPublish_FhRenameFail_BackupLocked_FinalLowRemains` | backup path gets locked during rollback | Equivalence – rollback failure | publish fails but final low remains published | degraded rollback scenario |
| `Test_T3_PseudoIntegration_TmpOnlyThenPublishedPair` | measuring state leaves only `.tmp`, then publish runs | Equivalence – downstream visibility | complete `.bin` pair appears only after publish | `.tmp` must stay hidden from consumer |
| `Test_T3_ForcedStop_TmpRemains_ExcludedFromDownstream` | forced stop leaves `.tmp` only | Equivalence – interrupted acquisition | downstream sees no `.bin`; tmp remains for cleanup | incomplete data isolation |
| `Test_T3_RestartCleanup_DeletesTargetPatternOnly` | startup folder contains target/non-target tmp and other files | Equivalence – cleanup filter | only `*_fl_*.bin.tmp` and `*_fh_*.bin.tmp` are deleted | safety against over-delete |
| `Test_T3_RestartCleanup_NoTarget_NoDelete` | startup folder has no cleanup target | Boundary – empty target set | nothing is deleted and no failure occurs | no-op cleanup path |
| `Test_T3_RestartCleanup_DeletesRollbackBackupPatternOnly` | rollback backup and tmp coexist with non-target files | Equivalence – cleanup filter | target rollback/tmp files are deleted, unrelated files survive | startup hygiene for rollback artifacts |
| `Test_T4_RenameTempFileWithRetry_MaxRetries3_AttemptCountMatches` | final path locked, rename always fails, max retries `3` | Boundary – retry count | attempt count is `1 + 3` and tmp remains | retry contract |
| `Test_T4_DownstreamPolling_ErrorRateReduced` | compare direct final write vs tmp+rename flow | Equivalence – consumer contention | atomic flow has no worse sharing violation rate | publish design rationale |
| `Test_T11_Performance_500Files_Measurement` | 500 direct writes and 500 atomic publishes | Equivalence – performance measurement | timing vectors are populated and degradation is observable | benchmark / observation, no hard threshold assert |
