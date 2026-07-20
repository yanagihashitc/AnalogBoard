# Branch plan最新化チェックリスト

対象: `branch_plan/`
正規仕様: [AnalogBoard再構築プラン](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](./2026-07-19-branch-plan-refresh-log.md)
作成日: 2026-07-19

## Audit

- [x] local／remote branch、worktree、mainとの差分を棚卸しする
- [x] D4、Frozen v1、移植資産の正規仕様を再確認する
- [x] 旧branch planと現状のdriftを特定する

## Update

- [x] branch plan READMEとroadmapを現在の運用へ更新する
- [x] closure checklistを実在branchと保全条件へ更新する
- [x] Codex promptsをmain起点の短命task branch運用へ更新する
- [x] AGENTSと関連する旧USB運用メモのbranch表現を同期する
- [x] 現行運用から退役した旧USB安定化文書一式をarchiveし、参照を更新する

## Review

- [x] branch名・ahead/behind・worktree事実を再検証する
- [x] Markdown linkとAGENTS/CLAUDE parityを検証する
- [x] 中央roadmap／mirror driftをread-only確認する
- [x] 残るbranch削除候補と未監査資産を記録する

## Review

- 開発起点を最新`main`、統合対象を短命task branchへ一本化し、D4のr7はGit起点ではなく選択移植baselineと固定した。
- `min_*`はmain完全包含の削除候補。`feature`／`dev`／`lab`／`validation`はmust-keep資産監査または移植が終わるまで保持する。
- branch／worktree snapshot、変更Markdown 25件・相対link 155件、HTML 2件、AGENTS/CLAUDE parity、branch planのGit追跡可否を検証済み。
- 更新差分は最新`main`起点の短命branch `docs/branch-plan-refresh`へ移した。worktreeは1本のみ。
- 旧USB安定化文書8件を`docs/archive/usb-acquisition-stability/`へ移し、現行`docs/operations/`から除外した。
- branch削除、commit、pushは本作業では実施していない。
- 親planのvalidation source未commit記述と中央roadmapのAnalogBoard HEADは現状より古い。中央mirror driftと併せ、別の正規plan／task_management同期作業へ引き渡す。
