# 0.2.0 WaveAcquisitionEngine Fix Process Log

## 対象プラン

- [2026-03-12-waveacquisition-020-fix-design](./2026-03-12-waveacquisition-020-fix-design.md)
- [チェックリスト](./2026-03-12-waveacquisition-020-fix-checklist.md)
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
| 2026-03-12 14:00 | Phase 0 / setup | checklist / design / process log を作成し、`WaveAcquisitionEngine` 修正方針を整理 | initialized | このファイル, `docs/2026-03-12-waveacquisition-020-fix-checklist.md`, `docs/plans/2026-03-12-waveacquisition-020-fix-design.md` | 実機 symptom は複合要因の可能性あり | failing test を追加 |
| 2026-03-12 14:01 | Phase 1 / repro tests | `WaveAcquisitionEngine_test.cpp` に slow producer / sub-alignment / empty capture の 3 ケースを追加し、`build_test.bat` で Red を確認 | red confirmed | `cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"` (`EmptyCapture`, `kAcquisitionErrEmptyCapture` 未定義で compile fail) | test helper の前方参照不足を 1 回修正 | production fix を実装 |
| 2026-03-12 14:02 | Phase 2 / engine fix | `WaveAcquisitionEngine` に中間読み取りの 16KB 切り捨て、0-byte intermediate poll skip、empty capture guard、`EmptyCapture` status / error code を追加 | code updated | `AnalogBoard_TestApp/WaveAcquisitionEngine.cpp`, `AnalogBoard_TestApp/WaveAcquisitionEngine.h`, `AnalogBoard_UnitTest/WaveAcquisitionEngine_test.cpp` | 実機 stale status が長時間継続する場合は追加観測が必要 | full verification を実行 |
| 2026-03-12 14:03 | Phase 3 / verification | `build_test.bat`、Debug x64 rebuild、`slow_producer` simulation を実行して完走を確認 | verification complete | `build_test.bat` (`WaveAcquisitionEngine 114/114 pass`, `SimulationRunnerIntegration 84/84 pass`), `msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_Dll:Rebuild;AnalogBoard_TestApp:Rebuild;AnalogBoard_SimRunner:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1`, `scripts\run_simulation.bat slow_producer` (`status=success`, `logs\sim\slow_producer\20260312_140327_744`) | 実機での再確認は未実施 | close out |
