# Repository-maintenance closeout checklist

対象プラン: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](./2026-07-20-repository-maintenance-closeout-log.md)
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
- [x] 中央mirror／roadmap向けhandoff evidenceを記録し、`../task_management`を変更しない
- [x] `.agent` checkpoint、2 prompt、対応trackingを内容reviewして関連変更に含める
- [x] ignoredなroot `goal.md`／`prompt.md`をstageしない

## Validation and publish

- [x] HTML parse、duplicate IDs、TOC anchors、全local links、plan metadataを検証する
- [x] central syncをread-onlyで確認し、driftを記録する
- [x] `git diff --check`、status、staged files／sizes、禁止資材非混入を検証する
- [x] checklist／process log／todo Reviewを完了してarchiveする
- [x] 最大2 Conventional Commitsでcurrent branchへcommit／pushする
- [x] base=`main`のPRを作成または更新し、checksを確認する
- [x] PR merge／branch deleteを実行せず、owner承認待ちのdry-runを提示する

## Review

- [x] 変更、検証結果、残リスク、中央handoff、未実施操作を記録する

- archive: 指定3件だけを移動し、tracked referencesと相対linkを更新した。移動本文は壊れたlink以外を保持した。
- plan: remote validation commit `d760e90`を確認し、Draft 3.6へsource凍結／reference-only keep条件を反映。D1〜D23、gate status、Phase 0 statusは不変。
- agent docs: AGENTS/CLAUDEとproject skillsに今回起因のdriftはなく、変更不要。中央mirror／roadmapはread-only handoffとした。
- validation: final PRの現存58 documents、4 HTML、215 repo-local links、37 TOC anchors、duplicate ID 0、metadata／decision／gate status、whitespace、stage scopeをPass。既存historical absolute references 2件は別集計。source/build変更なしのためMSVC buildは未実施。
- publish: commit `9866494`をpushし、draft PR #1を作成。initial check 1/1 green。完了trackingは第2 commitとして追加する。
- remaining: PR mergeはowner明示承認待ち。merge commitとunique commit=0が未成立のためbranch delete dry-runはmerge後に実施し、remote branchは保持する。中央task_managementは既存dirty解消＋PR merge後に別workflowで同期する。
