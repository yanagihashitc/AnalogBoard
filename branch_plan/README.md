# Branch Plan

最終更新: 2026-07-19

AnalogBoard再構築中のbranch作成、旧branchからの資産移植、worktree、remote branch退役を管理する。

## Current Rule

- 新規開発は最新`main`から短命task branchを作る。
- 直接`main`へ実装しない。
- 長期`dev` branchは新設・再利用しない。
- D4のr7移植baselineはコード／挙動の移植元であり、Git branchの起点は`main`のまま。
- `validation/win11-driver-r7-acq`、旧`dev`、`feature/win11-driver-compat`、`lab/0.2.2-engine-semantics`は参照・資産回収専用。一括mergeしない。
- 通常はmain worktree 1本。並行taskで必要な場合だけ専用worktreeを作り、完了時に撤去する。

次の開発gateはFrozen v1。Phase 0残作業はZarr往復、partition sharding、scatter prototype、D17 golden regression等に分けて、それぞれ最新`main`からbranchを作る。Phase 1 branchはFrozen v1後に作成する。

## Documents

- strategy／branch snapshot／移植規則: [roadmap](./2026-03-12-two-branch-convergence-roadmap.md)
- remote branch退役条件: [closure checklist](./2026-03-12-temporary-branch-closure-checklist.md)
- Codexへ作業依頼するときの雛形: [prompts](./2026-03-12-codex-worktree-prompts.md)

## Authority

- AnalogBoardの設計・D4・phase gate: `docs/plans/260710-analogboard-rebuild-plan.html`
- 横断phase／gate: `../task_management/260710-cross-repo-execution-roadmap.html`
- 本フォルダ: 上記をbranch運用へ落とした派生文書

上位planと差異が生じた場合は、branch履歴から推測して決めず差異を報告し、本フォルダを同期する。
