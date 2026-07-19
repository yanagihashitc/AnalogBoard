# Remote Branch Retirement Checklist

作成日: 2026-03-12  
最終更新: 2026-07-19

対象roadmap: [Branch Strategy and Retirement Roadmap](./2026-03-12-two-branch-convergence-roadmap.md)

このchecklistはremote branchの削除可否を判断するためのもの。**本checklistを更新するだけではbranchを削除しない。** remote削除時は対象、unique commit、復元手段を提示し、オーナーの明示承認を得る。

## Shared Safety Gate

- [x] 現在のGit起点は`main`、日常開発は短命task branchと明示した
- [x] worktreeがrepository rootの1本だけであることを確認した（2026-07-19、current branch=`docs/branch-plan-refresh`）
- [x] 全remote branchのmain比ahead／behindと直近commitを取得した
- [ ] branch固有のmust-keep source／test／docs／evidenceを列挙した
- [ ] keep資産をmain起点の新実装、tracked docs、または`artifacts/`へ保全した
- [ ] branch名を参照するactive runbook／automation／CIがないことを確認した
- [ ] tag／commit hash／artifact等の復元手段を記録した
- [ ] 削除対象とremote delete commandのdry-runを提示した
- [ ] オーナーがremote削除を明示承認した

## Fully-contained `min_*`

対象:

- `origin/min_change`
- `origin/min_debug`
- `origin/min_refactor`

現状: 3本ともcommit `2b9b44a`を指し、`origin/main`のancestor。main比`42 / 0`。

- [x] branch-only commitが0件である
- [x] mainからcommit `2b9b44a`へ到達可能である
- [ ] active docs／automationがbranch名を使用していない
- [ ] オーナーが3 remote branchの削除を承認した
- [ ] remote削除後に`git fetch --prune`で消失を確認した

判定: **最優先のdeletion candidate。未承認のため保持。**

## `feature/win11-driver-compat`

現状: main比`7 / 10`。r18系`dev`履歴にendpoint discovery hardeningとtestを加えたbranch。D23以前の実験枝で、worktreeは撤去済み。

- [x] 日常開発／新driver構成の正本ではないと明示した
- [x] worktree未commit差分をlocal patchへ保全した
- [x] field package／diagnostic sourceを`artifacts/field-session/`とvalidation branchへ保全した
- [ ] unique commit 10件について、validation source／新coreへの取り込み状況をfile単位で照合した
- [ ] endpoint discovery testsの移植先とtest結果を記録した
- [ ] active docsの実行指示をsupersededへ更新した
- [ ] オーナーがremote削除を承認した

判定: **supersededだがmust-keep audit未完。まだ削除しない。**

## `dev`

現状: main比`13 / 7`。`WaveAcquisitionEngine`、回復policy、旧branch運用docsを含む。D4でcode mainlineから除外済み。

- [x] `dev`を新規task branchの起点にしないと明示した
- [x] dev全体mergeを禁止し、D4のheader単位＋Tier 2回帰規則を記録した
- [ ] `Ep6TimeoutRecoveryPolicy.h`等、移植候補headerと対応testを列挙した
- [ ] 移植候補ごとに採用／不採用理由とTier 2 evidenceを記録した
- [ ] `WaveAcquisitionEngine`実装を移植対象から除外したことを確認した
- [ ] 旧branch運用docsのactive参照を解消した
- [ ] オーナーがremote削除を承認した

判定: **critical path外。選択移植監査が終わるまでreferenceとして保持。**

## `lab/0.2.2-engine-semantics`

現状: main比`18 / 22`。SimRunner／simulation／RD_WAIT・stale DDR semanticsと旧engine実装を含む。

- [x] labを日常開発／release branchとして使わないと明示した
- [x] labの`WaveAcquisitionEngine`実装はdiscard対象と再確認した
- [ ] SimRunner／FpgaDdrModel／fault-injection JSONの完全な所在とunique commitを列挙した
- [ ] レジスタ意味論と対応testをmain起点の新core Tier 1へ移植した
- [ ] keep資産のgolden／fault regressionを新coreでPassした
- [ ] discard部分と理由をprocess logへ記録した
- [ ] オーナーがremote削除を承認した

判定: **移植必須資産が残る可能性が高い。validationに次いで慎重に保持。**

## `validation/win11-driver-r7-acq`

現状: main比`13 / 1`。remote commit `d760e90`にfinal field diagnostic source、focused tests、build再現資材を保全。専用worktreeは撤去済み。

- [x] final diagnostic sourceをcommit／pushした
- [x] native全suiteとRelease x64 rebuildをPassした
- [x] branchの役割をreference-onlyと明示した
- [ ] Phase 1の新native coreへr7 policy／protocol semantics／testを選択移植した
- [ ] D17 golden regressionと初期録画corpusの参照をmain側へ固定した
- [ ] field sourceをcommit hash／tag／artifactから再現できることを最終確認した
- [ ] 保持継続かtag化後削除かをオーナー決定した

判定: **r7移植が完了するまでkeep。最も遅く閉じる。**

## End-state Check

- [x] 恒久integration branchは`main`のみと決定した
- [x] 長期`dev`ではなく短命task branchを使うと決定した
- [x] 現在のworktreeをrepository rootの1本へ整理した
- [ ] `min_*`を承認後に削除した
- [ ] `feature`／`dev`／`lab`／`validation`のmust-keep資産を回収した
- [ ] reference-only remote branchを承認後に順次削除した
- [ ] active branchが`main`＋作業中task branchだけであることを確認した
