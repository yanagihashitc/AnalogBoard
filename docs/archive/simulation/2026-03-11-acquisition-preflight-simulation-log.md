# Acquisition Preflight Simulation Process Log

## 対象プラン

- [Acquisition Preflight Simulation Design](./2026-03-11-acquisition-preflight-simulation-design.md)
- [チェックリスト](./2026-03-11-acquisition-preflight-simulation-checklist.md)

## 記入ルール

- 本ログは実施都度、時系列で追記する（上書きしない）
- 各タスクの着手時/完了時に最低 1 エントリを記録する
- 「判断理由」がある変更（方針変更、スコープ調整、閾値変更）は必ず記録する
- 測定値は可能な限り実数で記録し、推測値は `estimate` と明記する

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
| 2026-03-11 18:42 | Phase 1 / kickoff | 設計書、workspace ルール、関連 skill、既存取得ループ、UnitTest、build 構成を確認し、実装方針と対象ファイルを洗い出し | initialized | `docs/2026-03-11-acquisition-preflight-simulation-design.md`, `.cursor/rules/*.md`, `.claude/skills/brainstorming/SKILL.md`, `.claude/skills/test-strategy/SKILL.md`, `AnalogBoard_TestApp/Dialog1_Main.cpp`, `AnalogBoard_UnitTest/build_test.bat`, `docs/BUILD.md` | `Dialog1_Main.cpp` の取得ループが大きく、UI と file I/O に密結合 | checklist 作成後、engine contract の観点表と Red テストを追加する |
| 2026-03-11 18:50 | Phase 1 / Red | `WaveAcquisitionEngine_test.cpp` と `build_test.bat` 更新で contract test を追加し、未実装 header 参照により Red を確認 | Red confirmed | `AnalogBoard_UnitTest/WaveAcquisitionEngine_test.cpp`, `AnalogBoard_UnitTest/build_test.bat`, `cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"` (`fatal error C1083: '../AnalogBoard_TestApp/WaveAcquisitionEngine.h': No such file or directory`) | 既存 `FpgaRegisterLogic_test.cpp` warning は継続 | engine 本体と fake contract を実装して Green にする |
| 2026-03-11 18:55 | Phase 2 / Green | `WaveAcquisitionEngine.h/.cpp` を追加し、fake USB/sink/observer による contract test を実装。単体ビルドと test 実行で Green を確認 | pass | `AnalogBoard_TestApp/WaveAcquisitionEngine.h`, `AnalogBoard_TestApp/WaveAcquisitionEngine.cpp`, `AnalogBoard_UnitTest/WaveAcquisitionEngine_test.cpp`, `cmd /d /c "scripts\run_with_vsdevcmd.bat cl /EHsc /W4 /Zi /std:c++17 /I. AnalogBoard_UnitTest\WaveAcquisitionEngine_test.cpp AnalogBoard_TestApp\WaveAcquisitionEngine.cpp /Fe:AnalogBoard_UnitTest\WaveAcquisitionEngine_test.exe /link /DEBUG && AnalogBoard_UnitTest\WaveAcquisitionEngine_test.exe"` | `Dialog1_Main.cpp` への実機統合と SimRunner 追加は未着手 | real adapter / observer / sink を作成して取得ループを engine 経由へ移行する |
| 2026-03-11 19:10 | Phase 2 / Integrate real app | `Dialog1_Main.cpp` に real USB adapter / sink / observer を追加し、取得ループ主要部分を `RunAcquisitionCycleWithEngine()` 経由へ置換。TestApp project に engine source を組み込み | pass | `AnalogBoard_TestApp/Dialog1_Main.cpp`, `AnalogBoard_TestApp/AnalogBoard_TestApp.vcxproj`, `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"` | manual/auto 停止時の legacy loop 差異は実機での最終確認が必要 | SimRunner project と preset 実行系を追加する |
| 2026-03-11 19:18 | Phase 3 / SimRunner | `AnalogBoard_SimRunner` project、scenario loader、runner core、fake USB、scripted sink、preset JSON、`run_simulation.bat`、integration test を追加。`codecvt` 由来の `C4996/STL4017` build error を WinAPI UTF-8 変換へ切り替えて解消し、troubleshooting に追記 | pass | `AnalogBoard_SimRunner/*`, `data/sim_scenarios/*.json`, `scripts/run_simulation.bat`, `AnalogBoard_UnitTest/SimulationRunnerIntegration_test.cpp`, `docs/troubleshooting/build.md`, `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"`, `cmd /d /c "scripts\run_simulation.bat normal_complete"` | `run_simulation.bat publish_fail` は expected non-zero 終了のため shell 上は failure 扱いに見える | BUILD.md 更新と総合検証を実施する |
| 2026-03-11 19:23 | Phase 4 / Verification | `docs/BUILD.md` を整理し、`build_test.bat`、TestApp Rebuild、SimRunner Rebuild、`run_simulation.bat publish_fail` を実行して最終確認 | completed | `docs/BUILD.md`, `cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"`, `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_TestApp:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"`, `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"`, `cmd /d /c "scripts\run_simulation.bat publish_fail"` (`status=publish_failed exit_code=4`) | `FpgaRegisterLogic_test.cpp` の既存 warning C4819/C4996/C4189 は残存 | 実機 3 本確認（正常1 cycle / timeout or disconnect 1 cycle / 長時間1 cycle）へ |
