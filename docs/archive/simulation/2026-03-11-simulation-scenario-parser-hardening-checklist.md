# SimulationScenario parser hardening タスクチェックリスト

対象プラン: [simulation-scenario-parser-hardening-design](./2026-03-11-simulation-scenario-parser-hardening-design.md)
プロセスログ: [Process Log](./2026-03-11-simulation-scenario-parser-hardening-log.md)
作成日: 2026-03-11

---

## Phase 1: Reproduce and define expected behavior

依存: なし

- [x] 設計メモと実装ログを作成する
- [x] 数値範囲超過と複数行 `ep6_results` の失敗再現テストを追加する
- [x] 追加テストが現状実装で失敗することを確認する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat cl /EHsc /W4 /Zi /std:c++17 /utf-8 /I\"..\" SimulationScenario_test.cpp ..\AnalogBoard_SimRunner\SimulationScenario.cpp /Fe:SimulationScenario_test.exe /link /DEBUG"
```

---

## Phase 2: Harden parser and verify

依存: Phase 1

- [x] 数値パースで missing / negative / out-of-range を区別する
- [x] `ep6_results` の複数行配列を許容する
- [x] 追加テストと既存テストを通す

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
