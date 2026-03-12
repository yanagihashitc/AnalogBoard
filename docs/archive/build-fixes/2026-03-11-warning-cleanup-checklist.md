# Warning Cleanup Task Checklist

対象プラン: warning cleanup (C4819 / C4996 / C4189)
プロセスログ: [Process Log](./2026-03-11-warning-cleanup-log.md)
作成日: 2026-03-11

---

## Phase 1: Warning Root Cause Fix

依存: なし

- [x] `FpgaRegisterLogic_test.cpp` の `sprintf` を warning-free な実装へ置き換える
- [x] `FpgaRegisterLogic_test.cpp` の未使用変数を削除する
- [x] `build_test.bat` に UTF-8 source compile option を追加して `C4819` を解消する

**検証コマンド:**
```bat
cmd /d /c "AnalogBoard_UnitTest\build_test.bat"
```

---

## 全 Phase 共通チェック

- [x] 対象 warning が再現しない
- [x] UnitTest 全件 pass
- [x] process_log にエントリ追記
