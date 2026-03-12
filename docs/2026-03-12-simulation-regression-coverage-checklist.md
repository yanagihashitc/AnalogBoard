# Simulation Regression Coverage タスクチェックリスト

対象プラン: [2026-03-12-simulation-regression-coverage-design](./plans/2026-03-12-simulation-regression-coverage-design.md)
プロセスログ: [Process Log](./process_log/2026-03-12-simulation-regression-coverage-log.md)
作成日: 2026-03-12

---

## Phase 1: Repro Tests

依存: なし

- [x] `SimulationScenario_test.cpp` に non-aligned progress / zero-wave anomaly の Red test を追加する
- [x] `SimulationRunnerIntegration_test.cpp` に `empty_capture` の Red test を追加する
- [x] Red を確認する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

---

## Phase 2: Simulator Update

依存: Phase 1

- [x] `SimulationScenario` validation を anomaly simulation 向けに調整する
- [x] `slow_producer.json` と `empty_capture.json` を更新/追加する
- [x] simulator exit code と guide を更新する

---

## Phase 3: Verification

依存: Phase 2

- [x] UnitTest 全件 pass を確認する
- [x] `slow_producer` と `empty_capture` の simulation 実行結果を確認する
- [x] checklist / process log / index を完了状態に更新する

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
