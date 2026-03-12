# review-findings-fix タスクチェックリスト

対象プラン: [2026-03-11-acquisition-preflight-simulation-design](../simulation/2026-03-11-acquisition-preflight-simulation-design.md)
プロセスログ: [Process Log](./2026-03-11-review-findings-fix-log.md)
作成日: 2026-03-11

---

## Phase 1: Repro Tests

依存: なし

- [x] SimulationScenario の負値入力を再現する failing test を追加する
- [x] EP4 poll の yield 挙動を固定化する test を追加する
- [x] 追加テストが修正前コードで失敗することを確認する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

---

## Phase 2: Production Fixes

依存: Phase 1

- [x] `Dialog1_Main.cpp` から未使用 EP6 バッファと関連 dead code を削除する
- [x] `WaveAcquisitionEngine` で `ep4PollSleepMs == 0` を `Sleep(0)` として扱う
- [x] `SimulationScenario` で unsigned field の負値を validation error にする

---

## Phase 3: Verification

依存: Phase 2

- [x] UnitTest を実行して全件 pass を確認する
- [x] チェックリストと process log を更新する

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] process_log にエントリ追記
