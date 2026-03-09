# Test Perspectives Index

現在の UnitTest 群に対する観点表の索引。`AnalogBoard_UnitTest/` 配下の既存 test source を対象に、test ごとの入力条件・観点・期待結果を docs 化している。

## Coverage Summary

| Source Test File | Test Count | Perspective Document |
|---|---:|---|
| `FpgaRegisterLogic_test.cpp` | 86 | [fpga-register-logic.md](./fpga-register-logic.md) |
| `WaveDataFileIO_test.cpp` | 19 | [wave-data-file-io.md](./wave-data-file-io.md) |
| `SavePathValidation_test.cpp` | 24 | [save-path-validation.md](./save-path-validation.md) |
| `AcquisitionPerfMetrics_test.cpp` | 6 | [acquisition-perf-metrics.md](./acquisition-perf-metrics.md) |
| `FileLogger_test.cpp` | 8 | [file-logger.md](./file-logger.md) |
| `UsbTransferHelpers_test.cpp` | 8 | [usb-transfer-helpers.md](./usb-transfer-helpers.md) |
| **Total** | **151** | - |

## Organization Rule

- 観点表は source test file 単位で分割する
- 各行は 1 test function に対応させる
- `Case ID / Source Test` には source 上の test 関数名をそのまま使う
- source 側に test が追加・削除された場合は、同名の観点表ファイルも同時更新する

## Test Execution

- Command: `cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"`
- Inventory command: `rg -n "^void Test_" AnalogBoard_UnitTest`
- Coverage note: 現状は 151 の named test function を観点表で管理している。自動 branch coverage レポートは未導入のため、意図した観点の網羅性は本フォルダで補完する
