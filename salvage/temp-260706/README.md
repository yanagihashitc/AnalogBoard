# temp/ (旧環境ワークツリー) からの退避 — 2026-07-06

`temp/` は旧環境（Windows 実機、プロジェクト名 `Sysmex_AnalogBoard_*` 世代）のワークツリーのコピー。
本フォルダはその照合結果と、origin/dev と差分のあったファイルの退避先。

## 照合結果（2026-07-06 実施）

- ソース・docs の 149 ファイルは **origin/dev と同一**（CRLF と `Sysmex_` 改名トークンを正規化して一致）
- 差分があった 19 ファイル（本フォルダに退避、`*.diff` は正規化後の差分）も、内容は
  **改名の残り（`SYSMEXUSBDRV_GUID`、`sysmex_wave_data_io_tests` 等）のみで実質的なコード差分なし**
- 結論: **旧環境のソースは git（origin/dev）から完全に復元可能。** 本フォルダは監査証跡として保持
- `CyLib`／`FX3`／`FPGA_FW`／`memo.md` は現ワークスペースと md5 一致（コピー不要だった）

## temp/ から現ワークスペースへ取り込んだもの

| 取り込み先 | 内容 | 方式 |
|---|---|---|
| `data/` (49GB) | 実機計測データ（録画コーパス素材・gcsa照合サンプル） | ハードリンク（容量消費なし） |
| `logs/` (8.9MB) | 実機ログ（Type A/B/C 証跡・4GB再アーム実測の一次データ） | ハードリンク |
| `branch_plan/` | ブランチ計画メモ（gitignore 対象） | コピー |
| `docs/` 10ファイル | IMPLEMENTATION_TRACKING.md・application_specification_initial.md・knowledge_inventory.md・project_direction.md・CyUSB.md・initial-commit-architecture.md・git-migrate-analogboard-linux.md・process_log 3件 | コピー |

## 取り込まなかったもの

- `Sysmex_AnalogBoard_*` ソースツリー（git から復元可能・改名のみ）
- `x64/`・各プロジェクトの `x64/Debug|Release/`（ビルド出力・再生成可能）
- `coverage/`（カバレッジ成果物・再生成可能）

注意: `data/`・`logs/` はハードリンクのため、`temp/` を削除してもファイル実体は残る。
逆に片方を**編集**すると両方に反映される（測定データは不変資産なので通常問題なし）。
