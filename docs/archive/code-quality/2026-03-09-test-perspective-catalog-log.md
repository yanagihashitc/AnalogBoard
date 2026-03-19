# Test Perspective Catalog Process Log

## 対象プラン

- [Test Perspectives Index](./test-perspectives/INDEX.md)
- [チェックリスト](./2026-03-09-test-perspective-catalog-checklist.md)

## 記入ルール

- 本ログは実施都度、時系列で追記する
- source inventory と docs の差分が出た場合は必ず理由を残す
- test 件数は `rg -n "^void Test_" AnalogBoard_UnitTest` の結果を基準にする

## Process Log Entries

| DateTime (JST) | Phase / Task | Activity | Result | Evidence | Risks / Issues | Next Action |
|---|---|---|---|---|---|---|
| 2026-03-09 17:30 | Phase 1 / Init | checklist / process log を作成し、観点表整理タスクの追跡を開始 | initialized | `docs/2026-03-09-test-perspective-catalog-checklist.md`, `docs/2026-03-09-test-perspective-catalog-log.md` | none | source inventory を取得 |
| 2026-03-09 17:33 | Phase 1 / Inventory | UnitTest ソースを棚卸しし、6 ファイル / 151 test を確認 | pass | `rg -n "^void Test_" AnalogBoard_UnitTest`, `FpgaRegisterLogic_test.cpp:86`, `WaveDataFileIO_test.cpp:19`, `SavePathValidation_test.cpp:24`, `AcquisitionPerfMetrics_test.cpp:6`, `FileLogger_test.cpp:8`, `UsbTransferHelpers_test.cpp:8` | 今後 test source が増えた場合は docs の更新漏れリスクあり | docs/test-perspectives を作成して観点表を配置 |
| 2026-03-09 17:42 | Phase 1 / Documentation | `docs/test-perspectives/` を新設し、INDEX とテストファイル別観点表を作成 | pass | `docs/test-perspectives/INDEX.md`, `docs/test-perspectives/*.md` | 観点表は現時点の test source ベース。将来の test 追加時は同フォルダ更新が必要 | source 件数とリンク整合を確認 |
| 2026-03-09 17:44 | Phase 1 / Tracking | 新規 docs が `.gitignore` の `docs/` ルールで `git status` に出ないことを確認し、今回作成した観点表 / checklist / log だけを例外で追跡対象に変更 | pass | `.gitignore`, `git check-ignore -v docs\\test-perspectives\\INDEX.md docs\\2026-03-09-test-perspective-catalog-checklist.md docs\\2026-03-09-test-perspective-catalog-log.md` | `docs/` 配下の新規ファイルは原則 ignore のままなので、将来追加時も同様に例外設定が必要 | source 件数とリンク整合を確認 |
| 2026-03-09 17:45 | Phase 1 / Verification | source inventory と docs index の件数・リンク構成を確認 | pass | `rg -n "^void Test_" AnalogBoard_UnitTest`, `docs/test-perspectives/INDEX.md` | build/test 自体は docs-only task のため未実施 | 今後 test 追加時は同じ整理ルールで追記 |
