# Lab semantics RD_WAIT + DDR_WSTOP タスクチェックリスト

対象プラン: [Lab Next](./lab_next.md)
プロセスログ: [Process Log](./process_log/2026-03-17-lab-semantics-rdwait-wstop-log.md)
作成日: 2026-03-17

---

## Phase 1: simulator semantics gap の再現

依存: なし

- [x] `FpgaDdrModel` に host-visible `DDR_WR_END` と internal `DDR_WSTOP` の差を表現できる状態を追加する
- [x] scenario / SimRunner から stale `DDR_WR_END` と `RD_WAIT` entry を preset で再現できるようにする
- [x] `WaveAcquisitionEngine_test` と `SimulationRunnerIntegration_test` に failing test を追加する
- [x] `stale_ddrwrend_rdwait` preset を追加し、summary / runner log で観測点を残す

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_simulation.bat stale_ddrwrend_rdwait"
```

---

## Phase 2: stale `WAVE_WR_CNT` と high-density timeout telemetry

依存: Phase 1

- [x] `AcquisitionCompletionLogic` が startup stale `WAVE_WR_CNT` を active cycle の readable upper bound として採用しないことを固定する
- [x] `FpgaDdrModel` / scenario / preset で stale `WAVE_WR_CNT` を SimRunner から再現できるようにする
- [x] `AcquisitionPerfMetrics` と `WaveAcquisitionEngine` に timeout snapshot telemetry を追加する
- [x] `high_density_timeout_active` preset を追加し、timeout 時の backlog / upper bound / terminal `DDR_RD_END=0` を summary と runner log に残す

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
cmd /d /c "scripts\run_simulation.bat stale_ddrwrend_rdwait"
cmd /d /c "scripts\run_simulation.bat high_density_timeout_active"
```

---

## Phase 3: baseline safe diff inventory

依存: Phase 2

- [x] baseline 既存 helper / metrics と lab 差分を比較し、direct backport candidate と lab-only asset を分離する
- [x] `AcquisitionCompletionLogic.h` の startup stale `WAVE_WR_CNT` guard と `AcquisitionCompletionLogic_test.cpp` の追加 test を first backport candidate として整理する
- [x] `AcquisitionPerfMetrics.h` の `drainingHintSeen` 保存は optional manual port として切り分ける
- [x] 全体 checklist と process log に inventory 状態を同期する

**確認内容:**

- 2026-03-17 の Phase 1 / Phase 2 verification 結果を根拠に safe diff inventory を作成
- main working tree に `docs/2026-03-17-lab-safe-diff-candidates.md` を追加

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
