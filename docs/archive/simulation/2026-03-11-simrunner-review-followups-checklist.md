# simrunner-review-followups タスクチェックリスト

対象プラン: [SimRunner Review Follow-ups Design](./2026-03-11-simrunner-review-followups-design.md)
プロセスログ: [Process Log](./2026-03-11-simrunner-review-followups-log.md)
作成日: 2026-03-11

---

## Phase 1: Repro Tests

依存: なし

- [x] scenario parser の負値 / overflow を再現する failing test を追加する
- [x] optional simulation field omission を再現する failing test を追加する
- [x] SimRunner の current-directory 依存を再現する test を追加する
- [x] integration test の side effect を固定する test を追加する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

---

## Phase 2: Production Fixes

依存: Phase 1

- [x] `SimulationScenario` に non-negative / overflow validation と optional field default 適用を追加する
- [x] `main.cpp` で repo root を executable path から解決する
- [x] simulation integration test の後始末を追加する

---

## Phase 3: Verification

依存: Phase 2

- [x] UnitTest を実行して全件 pass を確認する
- [x] SimRunner を `x64\Debug` 直下から起動して preset load を確認する
- [x] チェックリストと process log を更新する

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] process_log にエントリ追記
