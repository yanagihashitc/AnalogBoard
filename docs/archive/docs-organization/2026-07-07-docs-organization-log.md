# Docs Organization Process Log

## 対象

- `docs/` 直下の整理
- [Process Log INDEX](../../process_log/INDEX.md)

## Process Log Entries

| DateTime (JST) | Phase / Task | Activity | Result | Evidence | Risks / Issues | Next Action |
|---|---|---|---|---|---|---|
| 2026-07-07 17:49 | Docs organization | `docs/` の root 混在を調査し、正規プラン・現行運用ファイル・古い参照資料・完了済みログに分類 | completed | `find docs -maxdepth 3 -type f`, `rg` reference scan | `.gitignore` に既存の未コミット変更あり。今回は触らない | 参照資料と完了済みログを移動する |
| 2026-07-07 17:49 | Docs organization | 古い参照資料を `docs/reference/`、完了済みログを `docs/archive/` 配下へ移動 | completed | `docs/reference/`, `docs/archive/engine-context/`, `docs/archive/application-specification/` | 再構築プラン等から直参照される運用ファイルは root に残した | リンクと索引を更新する |
| 2026-07-07 17:49 | Docs organization | `docs/INDEX.md` を追加し、`process_log/INDEX.md` と移動後リンクを更新 | completed | `docs/INDEX.md`, `docs/process_log/INDEX.md` | ドキュメント整理のみのため build/test は未実施 | リンク検証を行う |
| 2026-07-07 17:49 | Docs organization | `docs/` 内の Markdown/HTML 相対リンクを検証し、既存 archive の古い相対リンクも修正 | passed | `No missing relative markdown/html file links under docs (templates excluded)` | テンプレートの `xxx.md` 例は意図的に検証対象外 | 完了 |
| 2026-07-07 17:49 | Docs root tightening | user request に従い、`docs/` 直下のファイルを `BUILD.md`, `INDEX.md`, `260706-field-session-runbook.html` のみに絞った | completed | `find docs -maxdepth 1 -type f` | `BUILD.md` は root に残し、USB取得系は当時`docs/operations/usb-acquisition-stability/`へ移動（2026-07-19に`docs/archive/usb-acquisition-stability/`へ退役）、長期ガイドは `docs/guides/` へ移動 | 参照リンクを更新する |
| 2026-07-07 17:49 | Link verification | 移動後の `docs/` 内 Markdown/HTML 相対リンクを再検証 | passed | `No missing relative markdown/html file links under docs (templates excluded)` | process log の過去 evidence 文字列には旧パスが残るが、履歴として保持 | 完了 |
| 2026-07-07 17:49 | Win11 first gate package cleanup | `win11_driver_first_gate_package` を実機 first gate 実行に必要な最小セットへ縮小 | completed | package size `27M` -> `444K`; remaining files: `bin/*`, runbook, checklist, result sheet, manifest, DFX scripts | `r18_dev_release/`, PDB, import lib, duplicate `baseline_next.md`/`driver_next.md` は source of truth ではないため削除 | checksums を検証する |
| 2026-07-07 17:49 | Rebuild-oriented root cleanup | rebuild 前提で root の旧 driver goal 補助・一時救出物を削除し、`tasks/todo.md` の stale active batch を解除 | completed | removed: `salvage/temp-260706/`, root `BUILD.md`, `memo.md`, `goal.md`, `prompt.md`, `.agent/`, `claudebox-codexbox-agmsg-setup.html` | 旧MFC/UnitTest/CyLib/FPGA_FW/logs/data は親プラン上の参照・検証資産なので保持 | 最終検証 |
