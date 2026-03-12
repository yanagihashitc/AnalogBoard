# WaveAcquisition stale DDR_WR_END follow-up タスクチェックリスト

対象プラン: [2026-03-12-waveacquisition-stale-ddrwrend-followup-design](./2026-03-12-waveacquisition-stale-ddrwrend-followup-design.md)
プロセスログ: [Process Log](./2026-03-12-waveacquisition-stale-ddrwrend-followup-log.md)
作成日: 2026-03-12

---

## Phase 1: Test First

依存: なし

- [x] stale `DDR_WR_END` 実機近似ケースの観点表を整理する
- [x] `measTrg=1` / `limit-1` / `limit` / persistent stale のテストを追加する
- [x] 追加テストを Red または既存差分では未保証の状態で固定する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

---

## Phase 2: Production Update

依存: Phase 1

- [x] `AcquisitionSummary` に settling 観測値を追加する
- [x] stale 判定ロジックを最小限 refactor して summary に反映する
- [x] cycle summary log に settling 情報を追加する

---

## Phase 3: Verification

依存: Phase 2

- [x] `build_test.bat` を実行して全 UnitTest pass を確認する
- [x] 必要最小限の build 回帰確認を行う
- [x] checklist / process_log を完了状態に更新する

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] process_log にエントリ追記
