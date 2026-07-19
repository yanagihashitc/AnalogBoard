# Codex Branch Prompts

作成日: 2026-03-12  
最終更新: 2026-07-19

最新`main`起点の短命task branch、r7参照、remote branch退役監査を依頼するときの雛形。旧baseline／lab／dev worktree向けpromptは廃止した。

## Standard Task Branch

```text
AnalogBoardの最新mainをGit起点として、1 taskだけを扱う短命branchで進めてください。

開始前:
- git statusとorigin/mainとの差分を確認する
- mainをgit pull --ff-onlyで最新化する
- <type>/<phase>-<scope>形式の新branchをmainから作る
- mainへ直接実装しない

正規仕様:
- docs/plans/260710-analogboard-rebuild-plan.html
- ../task_management/260710-cross-repo-execution-roadmap.html（read-only）
- branch_plan/2026-03-12-two-branch-convergence-roadmap.md

今回のtask:
- <scope>

受け入れ条件:
- <tests / evidence / gate>

制約:
- D1〜D23を再議論・変更しない
- 該当project skillを必ず読む
- tasks/todo.mdとimplementation trackingを更新する
- phase/gateが進む場合はtask_management workflowへの同期引き渡しを残す
- commit/pushは依頼に明記されている場合だけ行う

完了時:
- test、git diff、残リスクを記録する
- mainへの統合単位をtask scope内に保つ
- 専用worktreeを作った場合は、差分保全後に撤去できる状態にする
```

## Phase 0 Example

```text
最新mainからfeature/phase0-zarr-roundtripを作り、Frozen v1前提のZarr往復prototypeだけを進めてください。

必須参照:
- docs/plans/260710-analogboard-rebuild-plan.html のD9/D12/D19/D21/D22とPhase 0 gate
- .claude/skills/zarr-store-output/SKILL.md

このbranchへscatter prototype、WPF scaffold、取得core本実装を混ぜないでください。暗号化込みgcsa往復とpartition sharding decisionに必要な証拠を分けて記録してください。gcsa/sys_appは直接改変せず、必要変更は下流改変promptとして残してください。
```

## Phase 1 Example（Frozen v1後のみ）

```text
Frozen v1のgate evidenceを確認してから、最新mainからfeature/phase1-rebuild-scaffoldを作ってください。Frozen v1未成立ならbranchを作らず停止し、未成立条件を報告してください。

新アプリはC#/.NET 8 WPF＋C++17 native core＋C ABI境界です。r7は移植baselineですが、validation branchや旧MFCアプリを一括mergeしません。Phase 1のscaffold受け入れ条件だけを実装し、取得hot-path移植は別task branchへ分離してください。
```

## r7 Asset Port

```text
最新main起点のtask branchで、validation/win11-driver-r7-acqのr7資産を選択移植してください。validation branch自体では開発を続けません。

移植元:
- commit d760e90
- artifacts/field-session/packages/r7-driver-ep4-polling-20260715T1618JST-source-package/

規則:
- 必要なpolicy／protocol semantics／focused testだけをfile単位で移植する
- Dialog1_Main god class、旧AnalogBoard_Dll実装、MFC resourceを移植しない
- 取得hot-pathはacquisition-hotpath-guardを適用する
- Tier 1/2回帰と移植元commit/pathをevidenceに残す
- dev限定policyを使う場合はheader単位＋Tier 2回帰を必須とする
- validation／dev／labをmainへ一括mergeしない
```

## Read-only Retirement Audit

```text
branch_plan/2026-03-12-temporary-branch-closure-checklist.mdに従い、指定remote branchの削除可否をread-only監査してください。

対象:
- <origin/branch>

実施:
- origin/mainとのahead/behind、merge-base、unique commit、unique fileを列挙する
- active docs／automationからの参照を検索する
- must-keep source／test／evidenceと、既存の移植先／保全先を照合する
- deletion candidateかkeepかを根拠付きで判定する

禁止:
- branch、tag、worktreeを削除しない
- merge、cherry-pick、commit、pushしない
- branch履歴からD1〜D23を変更しない

出力:
- keep／retire recommendation
- 未回収資産
- 復元手段
- 削除する場合のdry-run対象一覧（commandは実行しない）
```

## Short Prompt

```text
最新mainから1 taskだけの短命branchを作って進めて。mainへ直接実装せず、旧dev／feature／lab／validationは参照・選択移植専用として一括mergeしない。正規planと該当skillを読み、test/evidenceとtrackingを更新する。commit/pushは今回の依頼に明記されている場合だけ行う。
```
