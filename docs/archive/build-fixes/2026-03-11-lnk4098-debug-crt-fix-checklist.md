# LNK4098 Debug CRT Fix タスクチェックリスト

対象プラン: Debug build の `LNK4098`（`LIBCMT` 競合）解消
プロセスログ: [Process Log](./2026-03-11-lnk4098-debug-crt-fix-log.md)
作成日: 2026-03-11

---

## Phase 1: 原因切り分け

依存: なし

- [x] `LNK4098` を Debug x64 Rebuild で再現する
- [x] `dumpbin /directives` で `CyAPI.lib` の defaultlib 指示を確認する
- [x] 修正方針を process_log に記録する

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat dumpbin /directives CyLib\x64\CyAPI.lib"
```

---

## Phase 2: Debug linker 設定修正

依存: Phase 1

- [x] `AnalogBoard_Dll.vcxproj` の Debug link に `IgnoreSpecificDefaultLibraries=LIBCMT` を追加する
- [x] 変更理由を troubleshooting/build.md に残す

---

## Phase 3: 検証

依存: Phase 2

- [x] Debug x64 Rebuild で `LNK4098` が消えることを確認する
- [x] process_log に最終結果を追記する

---

## 全 Phase 共通チェック

- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
