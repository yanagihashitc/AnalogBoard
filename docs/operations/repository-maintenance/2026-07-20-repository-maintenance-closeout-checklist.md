# Repository-maintenance closeout checklist

対象プラン: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](../../process_log/2026-07-20-repository-maintenance-closeout-log.md)
作成日: 2026-07-20

## Preflight

- [x] `AGENTS.md`、`.cursor/rules/`、tracking guide、branch plan、retirement checklistを読む
- [x] `agent-docs-sync`、commit、PR publish関連skillを読む
- [x] Git status、worktree、fetch後refs、current PR、`origin/main...HEAD`を再取得する
- [x] 既存dirty差分がprompt setup由来の関連変更だけであることを確認する

## Archive and plan correction

- [x] archive対象3件と全参照元を`rg`で列挙し、target／参照数／移動先／link影響を記録する
- [x] 対象3件だけを`git mv`し、tracked referencesと壊れた相対linkを更新する
- [x] remote validation commit `d760e90`のsource／tests／build再現資材を確認する
- [x] 親planのstale evidenceとDraft／Last Updated／footerを一貫して更新する
- [x] D1〜D23、gate結果、Phase 0 statusが不変であることを確認する

## Agent guidance and prompt setup

- [x] `agent-docs-sync`のdrift checklistを実行し、必要なAnalogBoard内guidanceだけを同期する
- [ ] 中央mirror／roadmap向けhandoff evidenceを記録し、`../task_management`を変更しない
- [x] `.agent` checkpoint、2 prompt、対応trackingを内容reviewして関連変更に含める
- [x] ignoredなroot `goal.md`／`prompt.md`をstageしない

## Validation and publish

- [x] HTML parse、duplicate IDs、TOC anchors、全local links、plan metadataを検証する
- [x] central syncをread-onlyで確認し、driftを記録する
- [x] `git diff --check`、status、staged files／sizes、禁止資材非混入を検証する
- [ ] checklist／process log／todo Reviewを完了してarchiveする
- [ ] 最大2 Conventional Commitsでcurrent branchへcommit／pushする
- [ ] base=`main`のPRを作成または更新し、checksを確認する
- [ ] PR merge／branch deleteを実行せず、owner承認待ちのdry-runを提示する

## Review

- [ ] 変更、検証結果、残リスク、中央handoff、未実施操作を記録する
