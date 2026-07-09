# Test Perspective Catalog タスクチェックリスト

対象プラン: [Test Perspectives Index](../../test-perspectives/INDEX.md)
プロセスログ: [Process Log](./2026-03-09-test-perspective-catalog-log.md)
作成日: 2026-03-09

---

## Phase 1: テスト棚卸しと docs 配置整理

依存: なし

- [x] 現在の UnitTest ソースを棚卸しし、対象が 6 ファイル / 151 test であることを確認
- [x] `docs/test-perspectives/` フォルダを新設し、参照起点となる `INDEX.md` を配置
- [x] `.gitignore` に例外を追加し、今回作成した観点表 / checklist / log を repo 管理対象として扱える状態にした
- [x] `FpgaRegisterLogic_test.cpp` の観点表を機能群ごとに整理して配置
- [x] `WaveDataFileIO_test.cpp` の観点表を配置
- [x] `SavePathValidation_test.cpp` の観点表を配置
- [x] `AcquisitionPerfMetrics_test.cpp` の観点表を配置
- [x] `FileLogger_test.cpp` の観点表を配置
- [x] `UsbTransferHelpers_test.cpp` の観点表を配置
- [x] source 側 test 件数と docs 側観点表の構成が一致することを確認

**検証コマンド:**
```bat
rg -n "^void Test_" AnalogBoard_UnitTest
```

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] 対象 test 151 件の観点表が `docs/test-perspectives/` 配下に配置されている
- [x] `INDEX.md` のリンクと test 件数サマリが source inventory と整合している
- [x] process_log にエントリ追記
