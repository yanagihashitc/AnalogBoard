# SavePathValidation Test Perspectives

Source: `AnalogBoard_UnitTest/SavePathValidation_test.cpp`

Total tests: 24

## Perspective Table

| Case ID / Source Test | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
|---|---|---|---|---|
| `Test_T9_ValidateSavePath_ExistingDirectory_Succeeds` | existing writable directory | Equivalence – normal | validation succeeds | baseline valid path |
| `Test_T10_ValidateSavePath_NotFound_ReturnsExpectedError` | non-existent directory | Equivalence – invalid path | not-found error is returned | existence check |
| `Test_T11_ValidateSavePath_NotWritable_ReturnsExpectedError` | directory without write permission | Equivalence – permission failure | not-writable error is returned | write probe failure |
| `Test_T12_ValidateSavePath_PathTraversal_ReturnsExpectedError` | path contains `..` | Boundary – path traversal | invalid-path error is returned | security rule |
| `Test_T13_WarningClears_WhenPathBecomesValid` | path transitions from invalid to valid | Equivalence – state transition | warning message clears after path becomes valid | UI recovery path |
| `Test_T14_ContainsControlCharacter_Boundary31And32` | path contains character `0x1F` or `0x20` boundary | Boundary – control character edge | control char is rejected, printable boundary survives | control character guard |
| `Test_T15_DefaultWriteProbeWithSeed_RetryOnFileExists` | generated probe file collides once or more | Equivalence – retry on collision | probe retries until unique candidate succeeds | write-probe collision handling |
| `Test_T16_DefaultWriteProbeWithSeed_AllAttemptsCollide` | every generated probe file collides | Boundary – retry exhaustion | probe returns failure after max attempts | upper retry bound |
| `Test_T17_ShouldValidateForUiTrigger_Startup` | UI trigger is startup | Equivalence – trigger policy | validation is required | startup validation policy |
| `Test_T18_ShouldValidateForUiTrigger_FolderDialogCancel` | UI trigger is folder dialog cancel | Equivalence – trigger policy | validation is required | cancel should re-check current path |
| `Test_T19_ShouldNotValidateForUiTrigger_TextChanged` | UI trigger is text changed | Equivalence – trigger policy | validation is skipped | avoid intrusive validation on every keystroke |
| `Test_T20_ShouldShowDialogForUiTrigger_Startup` | invalid path during startup | Equivalence – dialog policy | warning dialog is shown | user-visible startup error |
| `Test_T21_ShouldShowDialogForUiTrigger_FolderDialogCancel` | invalid path after folder dialog cancel | Equivalence – dialog policy | warning dialog is shown | cancel path feedback |
| `Test_T22_ShouldShowDialogForUiTrigger_FolderDialogConfirmed` | invalid path after folder dialog confirm | Equivalence – dialog policy | warning dialog is shown | explicit selection feedback |
| `Test_T23_ShouldValidateForUiTrigger_SetParameters` | Set Parameters action | Equivalence – trigger policy | validation is required | must fail before acquisition starts |
| `Test_T24_ShouldValidateForUiTrigger_FolderDialogConfirmed` | folder dialog confirm trigger | Equivalence – trigger policy | validation is required | confirm path should be revalidated |
| `Test_T25_ShouldShowDialogForUiTrigger_SetParameters` | invalid path during Set Parameters | Equivalence – dialog policy | warning dialog is shown | operation-blocking validation |
| `Test_T26_ShouldNotShowDialogForUiTrigger_TextChanged` | text changed trigger with invalid interim input | Equivalence – dialog policy | no dialog is shown | UX guard for typing |
| `Test_T27_ShouldValidateStartupAfterSuccessfulConfigImport` | config import succeeds before startup validation | Equivalence – startup sequencing | startup validation runs | imported config becomes validation target |
| `Test_T28_ShouldNotValidateStartupAfterFailedConfigImport` | config import fails before startup validation | Equivalence – startup sequencing | startup validation is skipped | avoid false alarm on bad import state |
| `Test_T29_ResolveSavePathForSetParameters_UseNormalizedWhenValidated` | validation succeeded and normalized path exists | Equivalence – path resolution | normalized path is used | canonical assignment policy |
| `Test_T30_ResolveSavePathForSetParameters_FallbackToUiPathWhenValidationSkipped` | validation skipped, raw UI path exists | Equivalence – path resolution | raw UI path is preserved | defensive fallback |
| `Test_T31_ResolveSavePathForSetParameters_FallbackToUiPathWhenNormalizedEmpty` | validation succeeded but normalized path is empty | Boundary – empty normalized path | raw UI path is used | avoid blank assignment |
| `Test_T32_ResolveSavePathForSetParameters_KeepEmptyUiPathWhenValidationSkipped` | validation skipped and UI path is empty | Boundary – empty UI path | empty UI path is preserved | no silent substitution |
