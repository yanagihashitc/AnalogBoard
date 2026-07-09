# レビュー指摘フォローアップ タスクチェックリスト

対象プラン: [USB データ取得・書き込み安定性改善プラン](../../operations/usb-acquisition-stability/2026-03-02-usb-acquisition-stability.md)
プロセスログ: [Process Log](2026-03-09-review-followups-log.md)
作成日: 2026-03-09

---

## Phase 1: Red

依存: なし

- [x] mutex 解放ガードと EP6 buffer zero-fill の期待値をテストへ追加する
- [x] 追加したテストが未実装で失敗することを確認する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

---

## Phase 2: Green

依存: Phase 1

- [x] `UsbTransferHelpers` に条件付き mutex 解放と zero-fill helper を追加する
- [x] `AnalogBoard_Dll.cpp` の EP2/EP4/EP6 を helper ベースに修正する
- [x] `Dialog1_Main.cpp` の未使用引数削除と `size_t` cast を統一する
- [x] `AnalogBoard_Dll.h` に EP6 buffer のスレッド安全性注記を追加する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
```

---

## Phase 3: Verification

依存: Phase 2

- [x] UnitTest 一式を再実行して pass を確認する
- [x] Debug x64 Rebuild を実行してビルド成立を確認する
- [x] process_log と checklist を完了状態へ更新する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## Phase 4: Review Fixes

依存: Phase 3

- [x] `UsbTransferHelpers_test.cpp` で EP6 共有 mutex 必須の Red を追加し、失敗を確認する
- [x] `EP6_GetData` / `USBBoard_Disconnect` / デストラクタを共有 mutex で直列化する
- [x] UnitTest と Debug x64 Rebuild で修正後の回帰がないことを確認する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## Phase 5: Review Clarifications

依存: Phase 4

- [x] `RequiresEp2Ep4Mutex` に CyAPI 依存リスク回避の根拠コメントを追加する
- [x] `ReusableTransferBuffer` と `CycleMetrics` の単一スレッド/外部同期前提をコメントで明示する
- [x] UnitTest 一式と Debug x64 Rebuild を再実行して回帰がないことを確認する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat AnalogBoard_UnitTest\build_test.bat"
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
