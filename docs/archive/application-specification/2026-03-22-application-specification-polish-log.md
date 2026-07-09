# application_specification 仕上げ改善 Process Log

## 対象プラン

- 再採点レポート: このリポジトリ snapshot には未収録
- チェックリスト: このリポジトリ snapshot には未収録
- [Process Log INDEX](../../process_log/INDEX.md)

## 記入ルール

- 本ログは実施都度、時系列で追記する（上書きしない）
- 各タスクの着手時/完了時に最低 1 エントリを記録する
- 「判断理由」がある変更（方針変更、スコープ調整、閾値変更）は必ず記録する
- 測定値は可能な限り実数で記録し、推測値は `estimate` と明記する
- **100行を超えたら分割**: 新ファイル (`-02`, `-03`...) を作成し、INDEX.md に追加する

## Process Log Entries

| DateTime (JST) | Phase / Task | Activity | Result | Evidence | Risks / Issues | Next Action |
|---|---|---|---|---|---|---|
| 2026-03-22 00:30 | Phase 1 / tracking setup | 仕上げ改善用のチェックリスト・ログを作成 | initialized | このファイル、`docs/2026-03-22-application-specification-polish-checklist.md` | none | 出典補強と具体例追加に着手 |
| 2026-03-22 00:38 | Phase 1 / provenance hardening | `sys_app` と FPGA RTL の実コード参照を抽出し、5章・7章の derived 節へ追記 | done | `docs/application_specification.md`, `../sys_app/apps/api/src/services/dataset_import_steps.py`, `../sys_app/apps/api/src/schemas/job.py`, `../sys_app/apps/web/src/lib/import/localSamplePairing.ts`, `FPGA_FW/SYSMEX_ANA_20250129_restored/FPGA_TOP.qsf` | downstream/RTL 参照は今後も実装進化で更新余地あり | 具体例追加 |
| 2026-03-22 00:41 | Phase 2 / examples | golden example、レジスタ操作例、DLL 呼び出しシーケンス例を追加 | done | `docs/application_specification.md` | golden example は最小セットであり、将来は実ファイル添付例があるとさらに強い | 変更内容を共有 |
| 2026-03-21 23:48 | Phase 3 / rescore | 改善後の `application_specification.md` を再採点し、新版レポートを作成 | done | `docs/rescore_application_specification_20260321_v2.md`, `docs/application_specification.md`, `docs/rescore_application_specification_20260321.md` | 実ファイル fixture と技術選定の確定は未完 | ユーザーへ新スコアと残課題を共有 |
