# startup SavePath validation タスクチェックリスト

対象プラン: レビュー指摘対応（起動時 SavePath 検証）
プロセスログ: [Process Log](2026-03-06-startup-savepath-validation-log.md)
作成日: 2026-03-06

---

## Phase 1: 回帰の固定と修正

依存: なし

- [x] 起動時 SavePath 検証の判定をヘルパーとしてテスト化する
- [x] `default_config.csv` 読み込み失敗時は起動時 SavePath 検証をスキップする実装へ修正する
- [x] UnitTest Rebuild と `SavePathValidation_test.exe` 実行で回帰がないことを確認する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
