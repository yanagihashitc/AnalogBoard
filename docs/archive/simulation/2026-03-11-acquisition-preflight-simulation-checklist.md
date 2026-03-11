# Acquisition Preflight Simulation タスクチェックリスト

対象プラン: [Acquisition Preflight Simulation Design](./2026-03-11-acquisition-preflight-simulation-design.md)
プロセスログ: [Process Log](./2026-03-11-acquisition-preflight-simulation-log.md)
作成日: 2026-03-11

---

## Phase 1: Baseline And Red

依存: なし

- [x] 関連コード、既存テスト、solution 構成を調査する
- [x] `WaveAcquisitionEngine` の観点表を整理する
- [x] `WaveAcquisitionEngine` contract test を追加し、未実装の失敗を確認する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

---

## Phase 2: Engine Extraction

依存: Phase 1

- [x] `WaveAcquisitionEngine` と関連 interface を追加する
- [x] `Dialog1_Main.cpp` から取得ループ主要ロジックを engine 経由へ移行する
- [x] UnitTest を green にする

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## Phase 3: Simulation Runner

依存: Phase 2

- [x] `AnalogBoard_SimRunner.exe` project を追加する
- [x] fake USB / sink / observer / scenario parser / preset JSON を追加する
- [x] simulation integration test を追加する
- [x] `scripts\run_simulation.bat` を追加する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_simulation.bat normal_complete"
```

---

## Phase 4: Docs And Final Verification

依存: Phase 3

- [x] `docs/BUILD.md` に build/run 導線を整理する
- [x] Debug x64 の UnitTest / TestApp / SimRunner を再検証する
- [x] チェックリストと process log を完了状態へ更新する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_simulation.bat publish_fail"
```

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
