# r7 telemetry graceful-stop build checklist

対象プラン: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](2026-07-16-telemetry-graceful-stop-build-log.md)
作成日: 2026-07-16

## Phase 1: 契約固定とTDD

- [x] package内の設計記録に、1セッション1回終了・非同期finalize・hot-path非変更を固定する
- [x] 正常、反復close、自然終了、thread開始失敗、再開始のtest caseを固定する
- [x] header不在によるTDD Redを記録する
- [x] coordinator unit testをGreenにする

## Phase 2: app統合と回帰

- [x] 最初のcloseで停止要求だけを出し、loggerとwindowを保持する
- [x] telemetry CSV、summary、終了logのflush後だけ`WM_CLOSE`を再送する
- [x] thread開始競合と開始失敗を安全側へ処理する
- [x] targeted unit testとfull deterministic unit suiteを実行する
- [x] acquisition hot-pathの転送長、retry、buffer、publish契約が未変更であることを差分確認する

## Phase 3: review、build、配布

- [x] `claude-review-fixer`でRelease artifact build前reviewを完了する
- [x] Blocker/Major指摘を解消し、必要なtestを再実行する
- [x] 新build IDの隔離directoryへRelease x64 clean buildする
- [x] CSV取得手順書をMarkdownとoffline HTMLで同梱する
- [x] manifest、checksums、source diff、test/review/build evidenceを同梱する
- [x] `field_package/`と`build_evidence/`が未変更であることを確認する
- [x] 実機未実施、D23 NoDfx採用済み、D4 gate未成立を明記する
- [x] checklist、process log、`tasks/todo.md`のReviewを完了してarchiveする

## Test strategy

| ID | 区分 | Given | When | Then |
|---|---|---|---|---|
| TC-N-01 | 正常 | EP6 threadが未起動 | window closeを要求 | 即時close可能 |
| TC-N-02 | 正常 | EP6 threadが実行中 | 最初のwindow closeを要求 | 停止要求を返しwindowを保持 |
| TC-N-03 | 正常 | 停止要求済み | 再度window closeを要求 | finalize完了までwindowを保持 |
| TC-N-04 | 正常 | close起因の停止要求済み | thread finalizeを通知 | 自動close要求が1回必要 |
| TC-N-05 | 正常 | close要求なしでthread実行中 | threadが自然終了 | 自動close要求なし |
| TC-A-01 | 異常 | thread開始予約済み | `AfxBeginThread`が失敗 | idleへ戻り即時close可能 |
| TC-A-02 | 異常 | thread実行中 | 2回目の開始予約 | 拒否し二重workerを作らない |
| TC-A-03 | 異常 | closeによる停止要求済み | 新しい開始予約 | finalize完了まで拒否 |
| TC-A-04 | 異常 | close待ちfinalize済み | finalizeを重複通知 | close pendingを保持し重複closeを出さない |
| TC-B-01 | 境界 | 自然終了後 | 次のthread開始を予約 | 再度runningへ遷移可能 |
| TC-B-02 | 境界 | 自然終了済み | finalizeを重複通知 | idleを維持し次sessionを開始可能 |
| TC-ETP-A-07 | 競合 | close要求と外部triggerが同時 | poll decisionを評価 | close cancelを優先し次cycleへ入らない |
| TC-ETP-A-08 | 異常 | close要求中にEP4 transfer失敗 | poll decisionを評価 | transfer failureを隠さずFail扱い |

数値の0/min/max、NULL pointerは状態機械の公開契約に数値引数・pointerがないため対象外。thread開始失敗を異常系境界として固定する。
