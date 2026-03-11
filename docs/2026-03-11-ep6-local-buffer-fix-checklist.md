# EP6 Local Buffer Fix タスクチェックリスト

対象プラン: [EP6 Local Buffer Fix Design](./plans/2026-03-11-ep6-local-buffer-fix-design.md)
プロセスログ: [Process Log](./2026-03-11-ep6-local-buffer-fix-log.md)
作成日: 2026-03-11

---

## Phase 1: 設計固定と tracking

依存: なし

- [x] fix 設計メモを作成する
- [x] process_log に着手内容を記録する

---

## Phase 2: テスト追加

依存: Phase 1

- [x] `ScopedHeapBuffer` の test case を追加する
- [x] test perspective 文書を更新する
- [x] red を確認する

**検証コマンド:**
```bat
cmd /d /c "D:\ubuntu\jupyter\sys_analyzer\AnalogBoard\scripts\run_with_vsdevcmd.bat cl /EHsc /W4 /Zi /std:c++17 /I.. UsbTransferHelpers_test.cpp /Fe:UsbTransferHelpers_test.exe /link /DEBUG"
```

---

## Phase 3: 実装と検証

依存: Phase 2

- [x] `UsbTransferHelpers` に local heap buffer helper を実装する
- [x] `EP6_GetData()` を per-call local buffer へ変更する
- [x] `USBBoard_Connect()` の reusable buffer 事前確保を外す
- [x] targeted test を pass させる
- [x] `Release x64` の DLL/TestApp build を確認する

---

## 全 Phase 共通チェック

- [x] process_log にエントリ追記
- [x] 既知の未解決検証制約を記録
