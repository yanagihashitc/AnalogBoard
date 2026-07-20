# R7 driver + re-arm telemetry validation build チェックリスト

対象プラン: [AnalogBoard 再構築プラン](../../plans/260710-analogboard-rebuild-plan.html)  
対象runbook: [実機セッションrunbook](./260706-field-session-runbook.html)
プロセスログ: [Process Log](2026-07-14-r7-rearm-telemetry-build-log.md)  
作成日: 2026-07-14

## Phase 1: telemetry契約とTDD

- [x] active cycleで最初の`DDR_RD_END=1`確定時刻をcycle ID付きで一度だけ記録し、host drain (unread=0)は別markerにする
- [x] 次trigger受付可能時刻と次の外部trigger検出時刻を別markerとして記録する
- [x] 固定長メモリを超えた場合は取得を止めずdrop数を集計する
- [x] 取得中にCSV／逐次ログ／動的確保を行わない
- [x] 正常、欠損、境界、容量超過をテストする

## Phase 2: buildとpackage

- [x] MSVC単体テストを実行する
- [x] 利用可能なTier1/2録画再生・障害注入回帰を実行する
- [x] Release x64をclean rebuildする
- [x] 既存R7 driver validation packageを変更せず、新build ID／manifest／SHA-256で別package化する

## Phase 3: docsとhandoff

- [x] runbookを「旧driver low×3＋high×30 → 同一buildで新driver A/B」へ更新する
- [x] legacy telemetryは今回baseline、最終判定は新engineで再実施と明記する
- [x] 親planと運用メモの表現を同期する
- [x] agent docs driftと中央mirror driftをread-only確認する（local skill valid、AnalogBoard中央mirror driftは中央workflow引渡し待ち）

## 完了条件

- [x] Unit test pass（10,698/10,698）
- [x] Release x64 build success（warning 0）
- [x] package manifestとchecksumsが自己整合
- [x] 実機で使うfull pathと実験順がrunbookに一意に記載されている
- [x] process logに検証結果と残る実機gateを記録する
