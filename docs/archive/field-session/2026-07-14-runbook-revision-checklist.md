# Field-session runbook revision checklist

対象プラン: [AnalogBoard 再構築プラン](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](2026-07-14-runbook-revision-log.md)
作成日: 2026-07-14

## Phase 1: 証跡と正本の照合

- [x] transfer bundle の SHA-256 manifest を検証する
- [x] 7月14日の実機結果と現行runbookの前提差分を抽出する
- [x] 親プランと中央roadmapのphase／gate記述を照合する

## Phase 2: 文書改訂

- [x] `docs/260706-field-session-runbook.html` を残作業中心へ改訂する
- [x] 親プランへ実測結果、未成立gate、phase 0の残作業を反映する
- [x] 改訂概要と実行順を作成する
- [x] plan由来のagent guidance driftを同期する

## Phase 3: 検証

- [x] HTML構文と内部anchorを検証する
- [x] 相互参照・Pass／Fail／Skip／未判定・実行順の整合を検証する
- [x] 中央mirror／roadmapへのpending syncを記録する
- [x] `git diff --check` と変更範囲を確認する
- [x] process log と `tasks/todo.md` のReviewを完了する

## Review

- [x] Bundle manifest: 全件OK
- [x] HTML: タグ開閉整合、重複ID／欠落anchorなし
- [x] `Collect-FieldEvidence-v1.ps1`: Windows PowerShell parserでsyntax OK
- [x] R7-based driver validation package: checksum全件OK
- [x] `acquisition-hotpath-guard`: quick validation OK
- [x] `git diff --check`: OK
- [x] 残リスク: Gate Aの実データ確認、D4 owner gate、中央mirror／roadmap同期は未実施の次作業として明記
