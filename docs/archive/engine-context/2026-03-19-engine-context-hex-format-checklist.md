# Engine Context Hex Format タスクチェックリスト

対象プラン: N/A (direct bug fix request)
プロセスログ: [Process Log](2026-03-19-engine-context-hex-format-log.md)
作成日: 2026-03-19

---

## Phase 1: Reproduce and Lock Behavior

依存: なし

- [x] BuildEngineContextLog の現行挙動を確認する
- [x] 16進表記を要求する失敗テストを追加する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat cl /FS /EHsc /W4 /Zi /std:c++17 /I\"..\" /Fd:AcquisitionLogMessageFormatter_test.pdb AnalogBoard_UnitTest\AcquisitionLogMessageFormatter_test.cpp AnalogBoard_TestApp\WaveAcquisitionEngine.cpp /Fe:AnalogBoard_UnitTest\AcquisitionLogMessageFormatter_test.exe /link /DEBUG"
cmd /d /c "AnalogBoard_UnitTest\AcquisitionLogMessageFormatter_test.exe"
```

---

## Phase 2: Fix and Verify

依存: Phase 1

- [x] ENGINE_CONTEXT ログのアドレスを 16進で出力する
- [x] 変更したユニットテストが pass することを確認する

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 対象 pass
- [x] process_log にエントリ追記
