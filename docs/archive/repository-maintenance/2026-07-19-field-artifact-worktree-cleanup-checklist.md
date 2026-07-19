# 実機資材・worktree整理チェックリスト

対象プラン: [AnalogBoard再構築プラン](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](2026-07-19-field-artifact-worktree-cleanup-log.md)
作成日: 2026-07-19

---

## Phase 1: 保全と重複除去

依存: ユーザーによる削除対象の明示承認

- [x] 実機データとportable source packageを`artifacts/field-session/`へ移動する
- [x] validation worktree固有の旧build packageを保全する
- [x] 完全一致するroot Gate packageを削除する

## Phase 2: worktree回収

依存: Phase 1

- [x] final diagnostic sourceをvalidation branchへ回収する
- [x] native unit testとRelease x64 buildを通す
- [x] validation branchをcommit/pushする
- [x] superseded feature差分をpatch保全して両worktreeを削除する

## Phase 3: main整理

依存: Phase 2

- [x] artifact配置規約と参照pathを更新する
- [x] 完了済みfield-session運用文書・process logをarchiveする
- [x] plan/runbook/agent設定/中央mirrorの整合を検証する
- [x] mainをcommit/pushする

## Review

- [x] Git status、worktree、remote追跡状態を確認する
- [x] 実行した検証、残リスク、ローカルのみのartifactを記録する
- [x] checklist/process logをarchiveし、active batchを`tasks/todo_archive.md`へ移す
