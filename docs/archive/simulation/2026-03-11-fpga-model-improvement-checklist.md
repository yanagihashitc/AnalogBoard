# FPGA Model Improvement Task Checklist

対象プラン: [FPGA Model Improvement Design](./2026-03-11-fpga-model-improvement-design.md)
プロセスログ: [Process Log](./2026-03-11-fpga-model-improvement-log.md)
作成日: 2026-03-11

---

## Phase 1: Level 1 Register Encoding Fidelity

依存: なし

- [x] `FpgaRegisterEncoding.h` を追加し、FPGA 仕様ベースの EP4 register encoding を実装する
- [x] `FakeUsbSession` 実装を SimRunner / UnitTest で共通の encoding 経路へ置き換える
- [x] Level 1 の round-trip / bit isolation テストを追加する
- [x] UnitTest を実行して既存ケースとの後方互換を確認する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## Phase 2: Level 2 MEAS_CTRL State Machine Model

依存: Phase 1

- [x] `FpgaDdrModel.h` を追加し、INIT/MEAS/WAIT/RD_WAIT の状態遷移を実装する
- [x] `SimulationScenario` / loader を拡張し、state-machine 用の設定値を追加する
- [x] `FakeUsbSession` を `FpgaDdrModel` ベースへ差し替える
- [x] slow producer 系の unit/integration test と preset を追加する
- [x] UnitTest / IntegrationTest を実行して状態遷移の整合を確認する

---

## Phase 3: Level 3 Burst-Aligned DDR Counter Model

依存: Phase 2

- [x] `FpgaDdrModel` を burst-aligned counter へ拡張する
- [x] burst boundary stress 用の unit/integration test と preset を追加する
- [x] backlog / last burst / read completion の境界条件を検証する
- [x] Debug x64 Rebuild と関連テストを再実行する

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
