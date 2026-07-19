# Branch Strategy and Retirement Roadmap

作成日: 2026-03-12  
最終更新: 2026-07-19  
状態: active（Phase 0、次gate = Frozen v1）

## 結論

日常開発の起点と統合先は最新の`main`に一本化する。長期`dev` branchは新設・再利用せず、作業ごとに最新`main`から短命task branchを作り、検証後に`main`へ戻して削除する。

D4で承認された「r7移植baseline」は、旧アプリの実証済み資産を選択移植するための**コード／挙動baseline**であり、新規branchのGit起点を`validation/win11-driver-r7-acq`へ変更する意味ではない。`validation`、旧`dev`、`feature`、`lab`は参照・資産回収用であり、日常開発や一括mergeの起点にしない。

## Authority

1. 設計・移植判断: `docs/plans/260710-analogboard-rebuild-plan.html`（D4、資産の移植・破棄、Phase 0〜4）
2. 横断phase／gate: `../task_management/260710-cross-repo-execution-roadmap.html`
3. branch運用: 本文書と`branch_plan/2026-03-12-temporary-branch-closure-checklist.md`

上位planと本文書が食い違う場合は上位planを優先し、本文書を同期する。branchの履歴だけを根拠に確定済みD1〜D23を変更しない。

## Current Snapshot（2026-07-19）

main HEAD: `12428b7`（`origin/main`一致）  
current local branch: `docs/branch-plan-refresh`（`main`の`12428b7`から作成。本plan更新の作業branch）  
worktree: repository rootの1本のみ

`main...branch`は「mainだけのcommit数 / branchだけのcommit数」。削除候補はこの表だけで削除せず、closure checklistとオーナー承認を通す。

| Remote branch | main...branch | 現在の役割 | 運用状態 |
|---|---:|---|---|
| `origin/main` | `0 / 0` | 正規plan、統合済みコード、次作業の唯一のGit起点 | **active / keep** |
| `origin/validation/win11-driver-r7-acq` | `13 / 1` | r7取得系＋最終field diagnostic source（`d760e90`）の再現可能な参照 | **reference-only / keep until ported** |
| `origin/feature/win11-driver-compat` | `7 / 10` | r18系＋Win11 endpoint実験の履歴。実機単変数評価には不採用 | **superseded / retirement candidate** |
| `origin/dev` | `13 / 7` | r18系回復policyの履歴。D4によりcode mainlineではない | **superseded / selective-source only** |
| `origin/lab/0.2.2-engine-semantics` | `18 / 22` | SimRunner／FpgaDdrModel／レジスタ意味論の回収元。engine実装自体は不採用 | **reference-only / drain before retirement** |
| `origin/min_change` | `42 / 0` | initial commitを指す旧比較branch | **fully contained / deletion candidate** |
| `origin/min_debug` | `42 / 0` | initial commitを指す旧比較branch | **fully contained / deletion candidate** |
| `origin/min_refactor` | `42 / 0` | initial commitを指す旧比較branch | **fully contained / deletion candidate** |

旧planにあった`baseline/0.1.4-hw-recovery`、`investigate/ep6-regression`、`port/engine-reintro`は現在remoteに存在しない。active branchとして復元しない。

## Development Workflow

### 1. Branchを作る

必ず最新`main`から作成する。

```bash
git switch main
git pull --ff-only
git switch -c <type>/<phase>-<scope>
```

命名例:

- `feature/phase0-zarr-roundtrip`
- `spike/phase0-partition-sharding`
- `feature/phase0-scatter-prototype`
- `test/phase0-d17-golden`
- Frozen v1後: `feature/phase1-rebuild-scaffold`
- Frozen v1後: `feature/phase1-native-core`

`feature/phase0-frozen-v1`のような巨大branchへ全残作業を詰めず、Zarr／scatter／golden regression／shardingを独立した受け入れ単位に分ける。

### 2. 作業する

- 1 branch = 1つの明確なtask／受け入れ条件とする。
- 直接`main`へ実装しない。
- 並行作業が本当に必要な場合だけworktreeを追加し、用途・branch・削除条件を作業checklistへ記録する。
- 取得ホットパス、Zarr、gate、WPF等は該当project skillのguardrailを適用する。
- phase／gateが動く変更はローカルplanとevidenceを更新し、中央task_management workflowへ引き渡す。

### 3. 統合する

- taskのテスト／レビュー／受け入れ条件を通してから`main`へ統合する。
- 原則としてreview済みPRを使用する。
- 統合後はtask branchと専用worktreeを閉じ、長期feature branch化しない。

## Legacy Asset Import Rules

### r7 validation

- `validation/win11-driver-r7-acq`は直接開発を続けるbranchではなく、実機で使用した最終sourceの参照点。
- Phase 1では最新`main`起点のtask branchへ、必要なpolicy、test、protocol semanticsを小さく移植する。
- `d760e90`の一括mergeで旧MFCアプリ全体を新mainlineへ戻さない。
- 取得hot-path資産はTier 1/2回帰を添え、移植元file／commitをevidenceに残す。

### old dev

- 旧`dev`をcheckoutして日常開発を再開しない。
- D4で許可されたdev限定回復policyは、header単位かつTier 2回帰つきでのみ選択移植できる。
- `WaveAcquisitionEngine`、god class、旧DLL実装をbranchごとmergeしない。

### lab

- keep候補はSimRunner、FpgaDdrModel、fault-injection scenario、レジスタ意味論と対応test。
- labの`WaveAcquisitionEngine`実装そのものは現場退行歴があるため移植しない。
- keep候補を新coreのTier 1へ移し、golden／fault testが成立するまでbranchをreference-onlyで保持する。

### win11-driver feature

- `feature/win11-driver-compat`はD23以前の実験履歴であり、新driver構成の正本ではない。
- endpoint discovery等のunique changeはvalidation source／新coreへ取り込み済みかをclosure時に照合する。
- worktree未commit差分は`artifacts/field-session/superseded-worktree-patches/`へ保全済み。branchを日常作業へ再利用しない。

## Worktree Policy

- 通常はmain worktree 1本で運用する。
- 同時並行が必要なtaskに限り、明示的なtask branchと1対1でworktreeを作る。
- 作成時にowner、目的、保存すべき未commit差分、削除条件をchecklistへ記録する。
- 撤去前に`HEAD == remote`またはcommit／patch保全を確認し、固有build／evidenceを`artifacts/`へ退避する。
- worktree削除後も必要なbranch履歴は、retirement checklist完了とオーナー承認まではremoteに保持する。

## Retirement Order

branch削除は本更新の対象外。実行時は`temporary-branch-closure-checklist.md`を使い、削除対象を明示してオーナー承認を得る。

推奨順:

1. `min_change`／`min_debug`／`min_refactor`（mainに完全包含）
2. `feature/win11-driver-compat`（unique差分の回収確認後）
3. `dev`（許可されたpolicy／testの回収確認後）
4. `lab/0.2.2-engine-semantics`（Tier 1資産の移植確認後）
5. `validation/win11-driver-r7-acq`（Phase 1移植とgolden固定後。最も遅く閉じる）

## End State

- 恒久branchは`main`のみ。
- 開発中だけ短命task branchが存在し、統合後に削除される。
- reference-only branchは未移植資産がある期間だけ保持する。
- remote branchの数を減らすこと自体を目的にせず、must-keep source／test／evidenceがmain・docs・artifactへ移り切ったことをclosure条件とする。
