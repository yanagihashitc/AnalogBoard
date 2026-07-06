# application_specification 再編 Process Log

## 対象プラン

- [レビュー結果](../review_application_specification_20260321.md)
- [チェックリスト](../2026-03-21-application-specification-restructure-checklist.md)
- [Process Log INDEX](../process_log/INDEX.md)

## 記入ルール

- 本ログは実施都度、時系列で追記する（上書きしない）
- 各タスクの着手時/完了時に最低 1 エントリを記録する
- 「判断理由」がある変更（方針変更、スコープ調整、閾値変更）は必ず記録する
- 測定値は可能な限り実数で記録し、推測値は `estimate` と明記する
- **100行を超えたら分割**: 新ファイル (`-02`, `-03`...) を作成し、INDEX.md に追加する

## Process Log Entries

| DateTime (JST) | Phase / Task | Activity | Result | Evidence | Risks / Issues | Next Action |
|---|---|---|---|---|---|---|
| 2026-03-21 23:35 | Phase 1 / tracking setup | チェックリストとプロセスログを作成 | initialized | このファイル、`docs/2026-03-21-application-specification-restructure-checklist.md` | none | 仕様書骨格の再編に着手 |
| 2026-03-21 23:48 | Phase 1-3 / restructure | `application_specification.md` を再編し、`source-backed / derived / migration guidance` を分離 | done | `docs/application_specification.md` | 7章は derived 情報を多く含むため、今後も provenance 更新が必要 | low-level 不足情報の追記と最終確認 |
| 2026-03-21 23:54 | Phase 2 / fact fixes | `EP6 timeout` 記述、CSV / `_cfg.txt` 分離、gain switch / trigger range 詳細を修正 | done | `docs/application_specification.md` | Initial commit と後続知見の境界は明確化したが、RTL 由来の詳細は今後の裏取り余地あり | 追加セクションの整合確認 |
| 2026-03-21 23:58 | Phase 3 / completion | テスト方針、セキュリティ、依存関係、互換性、実装計画、未確定事項を追加。見出し整合を確認 | done | `docs/application_specification.md`, `rg -n "^## |^### " docs/application_specification.md` | ドキュメント作業のため build/test は未実施 | ユーザーへ変更内容を共有 |
