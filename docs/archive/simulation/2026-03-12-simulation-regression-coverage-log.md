# Simulation Regression Coverage Process Log

## 対象プラン

- [2026-03-12-simulation-regression-coverage-design](./2026-03-12-simulation-regression-coverage-design.md)
- [チェックリスト](./2026-03-12-simulation-regression-coverage-checklist.md)
- [Process Log INDEX](../../process_log/INDEX.md)

## 記入ルール

- 本ログは実施都度、時系列で追記する（上書きしない）
- 各タスクの着手時/完了時に最低 1 エントリを記録する
- 「判断理由」がある変更（方針変更、スコープ調整、閾値変更）は必ず記録する
- 測定値は可能な限り実数で記録し、推測値は `estimate` と明記する
- **100行を超えたら分割**: 新ファイル (`-02`, `-03`...) を作成し、INDEX.md に追加する

## エントリ項目

- DateTime (JST)
- Phase / Task
- Activity
- Result
- Evidence (log path / test output / commit id)
- Risks / Issues
- Next Action

## Process Log Entries

| DateTime (JST) | Phase / Task | Activity | Result | Evidence | Risks / Issues | Next Action |
|---|---|---|---|---|---|---|
| 2026-03-12 14:10 | Phase 0 / setup | simulator 回帰カバレッジ拡張の design / checklist / process log を作成 | initialized | このファイル, `docs/2026-03-12-simulation-regression-coverage-checklist.md`, `docs/plans/2026-03-12-simulation-regression-coverage-design.md` | `slow_producer` の意味変更に伴い guide 更新が必要 | failing test を追加 |
| 2026-03-12 14:12 | Phase 1 / repro tests | `SimulationScenario_test.cpp` に non-aligned progress / zero-wave anomaly を追加し、`SimulationRunnerIntegration_test.cpp` に `empty_capture` integration test を追加して Red を確認 | red confirmed | `cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"` (`TC-N-04`, `TC-N-05` fail), `cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\SimulationRunnerIntegration_test.exe"` (`failed to load scenario file`) | `empty_capture.json` 未追加、validation 制約が再現を妨げている | simulator / preset / guide を更新 |
| 2026-03-12 14:15 | Phase 2 / simulator update | `SimulationScenario` validation を `32-byte` DDR address unit ベースへ調整し、`slow_producer.json` を非 `16KB` アライン進行に更新、`empty_capture.json` と exit code / guide を追加 | code updated | `AnalogBoard_SimRunner/SimulationScenario.cpp`, `AnalogBoard_SimRunner/SimulationRunnerCore.cpp`, `data/sim_scenarios/slow_producer.json`, `data/sim_scenarios/empty_capture.json`, `docs/SIMULATION.md` | `producer_step_bytes` は完全自由ではなく `32-byte` 単位に制約 | full verification を実行 |
| 2026-03-12 14:16 | Phase 3 / verification | `build_test.bat`、`AnalogBoard_SimRunner:Rebuild`、`slow_producer` / `empty_capture` simulation を実行して結果を確認 | verification complete | `cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"` (`SimulationScenario 35/35 pass`, `SimulationRunnerIntegration 90/90 pass`), `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"`, `logs\sim\slow_producer\20260312_141537_762\summary.json`, `logs\sim\empty_capture\20260312_141548_372\summary.json` | `empty_capture` preset は expected non-zero exit (`12`) | close out |
