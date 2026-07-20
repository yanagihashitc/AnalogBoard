# Phase 0 USBPcap goal prompt setup checklist

対象プラン: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](2026-07-20-usbpcap-goal-setup-log.md)
作成日: 2026-07-20

## Scope

- [x] 正規plan、既存branch plan、capture実体から実行単位と前提条件を固定する
- [x] `/goal` の対象をUSBPcap解析と初期録画corpus化に限定する
- [x] repository closeoutとtask_management同期を別promptへ分離する

## Generated files

- [x] `goal.md` と `prompt.md` を生成する
- [x] `.agent/refactor.md` と `.agent/review.md` をAnalogBoard向けに生成する
- [x] repository closeout promptを生成する
- [x] task_management sync promptを生成する

## Verification and Review

- [x] required section、placeholder、local linkを検証する
- [x] scope外操作と停止条件が明示されていることを確認する
- [x] `git diff --check` とtracked/untracked状態を確認する
- [x] process logと`tasks/todo.md`を完了・archiveする

## Review

- [x] 生成物、検証結果、残る事前条件を記録する
