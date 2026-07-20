# Repository closeout prompt

このpromptはAnalogBoard repositoryで実行する。`/goal`ではなく通常のCodex taskとして使う。
目的は、現在のdocs branchをmerge可能な状態へ閉じ、完了済み資料をarchiveし、親planの
stale記述を修正すること。新機能実装やUSBPcap解析は行わない。

## Copyable prompt

以下のcode block内を先頭から末尾まで、そのまま新しいCodex sessionへ貼り付ける。

~~~text
AnalogBoard repositoryのrepository-maintenance closeoutを進めてください。最初に
`AGENTS.md`、`.cursor/rules/`、`docs/guides/IMPLEMENTATION_TRACKING.md`、
`branch_plan/README.md`、`branch_plan/2026-03-12-temporary-branch-closure-checklist.md`を読み、
親plan変更が発生するため`agent-docs-sync` skillも読んで適用してください。

2026-07-20時点の観測snapshotは、branch=`docs/branch-plan-refresh`、HEAD=`f3a007d`、
tracking=`origin/docs/branch-plan-refresh`、worktreeはprompt生成前にはclean、専用worktreeなしです。
このsnapshotを信用せず、`git status --short --branch`、`git worktree list --porcelain`、
`git fetch origin`、current PR、`origin/main...HEAD`のcommit/diffを再取得してください。
他人または別taskの差分があれば変更・stage・commitせず停止してください。

複数step作業なので`tasks/todo.md`へactive batchを作り、checklistとprocess logを
`docs/operations/repository-maintenance/`と`docs/process_log/`へ作成してください。
以下をこの順で実施してください。

1. archive対象と全参照元を`rg`で列挙し、move前のdry-runとしてtarget、参照数、移動先、
   local-link影響をprocess logへ記録する。対象は次の3 filesだけです。
   - `docs/260706-field-session-runbook.html` →
     `docs/archive/field-session/260706-field-session-runbook.html`
   - `docs/process_log/2026-03-02-usb-acquisition-stability-log.md` →
     `docs/archive/usb-acquisition-stability/2026-03-02-usb-acquisition-stability-log.md`
   - `docs/process_log/2026-03-02-usb-acquisition-stability-log-02.md` →
     `docs/archive/usb-acquisition-stability/2026-03-02-usb-acquisition-stability-log-02.md`
2. 上記だけを`git mv`し、`docs/INDEX.md`、`docs/process_log/INDEX.md`、その他のtracked
   referencesを新pathへ更新する。archive文書本文は、moveで壊れた相対linkの修正以外は変更しない。
   同名fileが移動先に存在する、想定外の参照がcurrent operationとして使っている、またはlinkの
   意味が曖昧な場合は停止する。
3. `docs/plans/260710-analogboard-rebuild-plan.html`の旧記述
   「source差分はmanifest作成時点でlocal uncommitted patch…製品採用前にcommitで凍結する」
   を現在の証跡へ更新する。事実はremote
   `validation/win11-driver-r7-acq` commit `d760e90`にfinal field diagnostic source、focused
   tests、build再現資材がcommit/push済みで、branchはreference-only、Phase 1の選択移植完了まで
   keepであること。commit内容を`git show`で再確認してから書き、planのDraft/Last Updated/footerと
   関連する同一記述を一貫して更新する。D1〜D23、gate結果、Phase 0 statusは変更しない。
4. `agent-docs-sync` skillに従い、親plan更新でdriftするAnalogBoard内のagent guidanceだけを同期する。
   `../task_management`はこのworkspaceから編集せず、中央mirror/roadmap更新に必要なsource plan
   version、AnalogBoard merge commit、PR、validation結果をhandoffとして記録する。
5. このprompt setupで生成済みのtracked files（`.agent/refactor.md`、`.agent/review.md`、
   `docs/operations/**/2026-07-20-*-prompt.md`および対応tracking）が残っている場合は、内容をreviewし、
   本closeoutの関連変更として扱う。root `goal.md`と`prompt.md`はlocal operational filesで
   `.gitignore`対象のままにし、force-addしない。
6. HTML parse、duplicate IDs、TOC anchors、全local links、plan metadata整合、
   `./scripts/sync-active-plans.sh --check`相当のcentral read-only drift確認（実行できる範囲）、
   `git diff --check`、`git status --short`、staged file一覧/sizeを検証する。raw capture、artifact、
   build binary、unrelated fileがstageされていないことを確認する。
7. checklist/process log/Reviewを完了し、完了trackingを規約どおりarchiveする。diffを論理単位で
   reviewし、Conventional Commits形式で最大2 commitにまとめてcurrent branchへcommit/pushする。
   untracked/ignored fileや無関係なuser差分を「すべて」の名目で含めない。
8. GitHub上に同branchのPRがなければbase=`main`でPRを作成し、既存なら本文をcurrent evidenceへ
   更新する。PR本文にはarchive対象、stale-plan修正、agent docs sync、validation、central sync
   handoffを記録する。checksを確認し、失敗時は原因を調べてscope内だけ修正する。

PR mergeはmainへの外部状態変更なので、checks greenとfinal diffを提示してownerの明示承認を
得るまで実行しないでください。merge後もlocal/remote branchを自動削除しないでください。
削除候補は`docs/branch-plan-refresh`だけを対象に、merge commit、unique commit=0、復元可能な
remote commitを示すdry-runを提示して停止してください。`min_*`、`feature`、`dev`、`lab`、
`validation` branchesはretirement checklistの未完了gateがあるため、このtaskでは削除しません。

次の場合は即停止して確認してください: dirtyなunrelated差分、archive対象のcurrent利用、
parent planと`d760e90`の事実不一致、central mirrorへのwriteが必要、force push、history rewrite、
branch delete、PR merge、secret/credential、driver/registry/firmware/実機操作。
~~~
