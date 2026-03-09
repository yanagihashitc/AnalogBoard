# [機能名] タスクチェックリスト

対象プラン: [プランへのリンク](../plans/xxx.md)
プロセスログ: [Process Log](../YYYY-MM-DD-feature-name-log.md)
作成日: YYYY-MM-DD

---

## Phase 1: [フェーズ名]

依存: なし

- [ ] タスク1
- [ ] タスク2
- [ ] タスク3

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## Phase 2: [フェーズ名]

依存: Phase 1

- [ ] タスク1
- [ ] タスク2

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [ ] UnitTest 全件 pass
- [ ] Debug x64 Rebuild 成功
- [ ] process_log にエントリ追記
