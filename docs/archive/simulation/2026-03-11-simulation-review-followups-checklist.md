# simulation review followups タスクチェックリスト

対象プラン: [Acquisition Preflight Simulation Design](./2026-03-11-acquisition-preflight-simulation-design.md)
プロセスログ: [Process Log](./2026-03-11-simulation-review-followups-log.md)
作成日: 2026-03-11

---

## Phase 1: Review Findings Triage

依存: なし

- [x] 指摘内容と現行コードの差分を確認する
- [x] 対応対象と見送り対象を整理する
- [x] テスト観点を整理して Red ケースを決める

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## Phase 2: Test-First Coverage

依存: Phase 1

- [x] WriteFailed unit test を追加する
- [x] simulation integration test に write_fail preset を追加する
- [x] underflow / alignment 系の回帰テストを追加する

---

## Phase 3: Production Fixes And Refactor

依存: Phase 2

- [x] FakeUsbSession の EP4 register 生成ロジックを共通化する
- [x] ULONG アンダーフローを防止する
- [x] scenario/config の追加バリデーションを実装する
- [x] summary.json の文字列エスケープと runner.log の open/close 改善を実装する

---

## Phase 4: Verification And Logging

依存: Phase 3

- [x] 関連 unit test / integration test を実行する
- [x] checklist と process_log を更新する
- [x] 見送り項目があれば理由を明記する

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
