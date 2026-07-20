# task_management synchronization prompt

このpromptはAnalogBoard workspaceではなく、sibling `task_management` repositoryをcurrent
workspaceとして実行する。AnalogBoard source planから中央mirror／roadmapへ一方向同期するための
通常taskであり、`/goal`ではない。

## Copyable prompt

`task_management` repositoryでAnalogBoardのsource plan mirrorとcross-repo roadmapを同期して
ください。最初にtask_management側の`AGENTS.md`、`README.md`、`plans/registry.tsv`、
`scripts/sync-active-plans.sh`、active roadmapを読み、各repositoryの正本/mirror境界を確認して
ください。AnalogBoard source planを中央から編集したり、中央mirrorを手修正したりしないでください。

開始前に次のpreconditionを機械確認してください。

1. AnalogBoardのrepository-closeout PRとUSBPcap/corpus PRが必要な順序でmerge済みで、
   `origin/main`上のmerge commit、source plan Draft/Last Updated、tests/evidenceが取得できる。
   open PRまたはpush済みbranchだけを`completed` evidenceにしない。
2. task_management worktreeがcleanで、最新`origin/main`から作成した短命task branch上にいる。
   2026-07-20のprompt作成時点ではtarget filesを含む既存dirty差分
   （`260710-cross-repo-execution-roadmap.html`、`index.md`、複数plan mirror、
   `gate_contract_alignment_plan_prompt.md`、untracked `.codex/hooks.json`）が観測されている。
   この状態が残っている場合は、既存差分をstage/revert/stash/worktree回避せず、所有者と意図を
   報告して停止する。別worktreeで古い`origin/main`から上書き同期してはならない。
3. `plans/registry.tsv`のAnalogBoard 2 entryが、source
   `../AnalogBoard/docs/plans/260710-analogboard-rebuild-plan.html`および
   `260703-downstream-modification-plan.html`からactive mirrorへ向く現行mappingのままである。

precondition成立後、複数step作業としてtask_management側のtracking規約に従い、次を実施して
ください。

1. `./scripts/sync-active-plans.sh --check`で初期driftを記録し、差分がAnalogBoard source planの
   expected updateだけか確認する。gcsa/sys_app/gain_scope mirror driftやregistry ambiguityが混ざる
   場合は同期せず停止する。
2. `./scripts/sync-active-plans.sh`をtask_management rootから実行し、registry経由でAnalogBoard
   mirrorを一方向同期する。mirrorを直接編集しない。
3. `260710-cross-repo-execution-roadmap.html`のAnalogBoard行、current Step/phase、last verified date、
   source plan version、branch/merged PR/commit/test evidence、next gate/blockerをsource planとmerge
   evidenceへ同期する。USBPcap/corpus PRがmerge済みなら「初期録画コーパス」を完了evidence付きで
   残件から外すが、Phase 0全体、Frozen v1、Zarr往復、partition sharding、scatter prototype、
   D17 golden regressionをcompletedにしない。表、Mermaid AB0 node、caption、summary、version、
   Last Updatedの全表現を同じ変更単位で一致させる。
4. `index.md`はactive roadmap/plan entryのversion/statusが変わる場合だけ同期する。
   registryはsource/archive stateが変わらない限り変更しない。roadmap自体は横断作業全体が
   completed/supersededではないためarchiveしない。
5. `./scripts/sync-active-plans.sh --check`、HTML parse、duplicate ID、TOC anchor、local link、
   Mermaid syntax/render可能性、plan version/Last Updated/status/evidence一致、`git diff --check`、
   complete diff reviewを実行する。sync後にAnalogBoard mirrorとsourceがbyte-identicalであることを
   確認する。
6. unrelated repositoryのstatus/evidenceを推測で変更せず、AnalogBoard同期に必要なfilesだけを
   stageする。staged file一覧とdiffstatを提示し、Conventional Commits形式で1 commitにまとめて
   task branchへpushする。同branchのPRがなければbase=`main`で作成し、既存なら更新する。
7. PR checksとfinal diffを報告する。PR mergeとbranch削除は自動実行せず、ownerの明示承認を待つ。

次の場合は停止してください: task_managementの既存dirty差分、AnalogBoard PR未merge、source planと
merge evidenceの不一致、AnalogBoard以外のmirror drift、registry変更の必要性、status/gateの推測、
central mirror手編集、force push、PR merge、branch削除、secret/credential、別repositoryへのwrite。
