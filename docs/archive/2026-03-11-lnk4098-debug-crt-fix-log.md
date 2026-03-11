# LNK4098 Debug CRT Fix Process Log

## 対象プラン

- Debug build の `LNK4098`（`LIBCMT` 競合）解消
- [チェックリスト](./2026-03-11-lnk4098-debug-crt-fix-checklist.md)

## Process Log Entries

| DateTime (JST) | Phase / Task | Activity | Result | Evidence | Risks / Issues | Next Action |
|---|---|---|---|---|---|---|
| 2026-03-11 17:47 | Phase 1 / investigate | `Debug|x64` rebuild と `dumpbin /directives CyAPI.lib` で `LNK4098` の原因を調査開始 | initialized | `msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1`, `dumpbin /directives CyLib\\x64\\CyAPI.lib` | `CyAPI.lib` が `/DEFAULTLIB:LIBCMT` を要求している可能性 | `AnalogBoard_Dll.vcxproj` の Debug linker 設定を確認する |
| 2026-03-11 17:49 | Phase 1 / isolate root cause | `AnalogBoard_Dll.vcxproj` と `CyAPI.lib` の defaultlib 指示を照合し、Debug build だけ `LIBCMT` ignore が欠けていることを確認 | analyzed | `CyAPI.lib` dump: `/DEFAULTLIB:LIBCMT`, `AnalogBoard_Dll.vcxproj` Release には `IgnoreSpecificDefaultLibraries=LIBCMT` あり / Debug にはなし | `CyAPI.lib` 自体は外部依存のため置換コストが高い | Debug linker に同じ ignore を追加する |
| 2026-03-11 18:04 | Phase 2 / implementation | `AnalogBoard_Dll.vcxproj` Debug link に `IgnoreSpecificDefaultLibraries=LIBCMT` を追加 | implemented | `AnalogBoard_Dll\\AnalogBoard_Dll.vcxproj` | `CyAPI.lib` が static CRT 依存を持つ前提は継続 | Debug rebuild で warning 消滅を確認する |
| 2026-03-11 18:05 | Phase 3 / verify rebuild | `Debug|x64` solution rebuild を再実行し、`LNK4098` が消えたことを確認 | passed | `msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1` -> `0 warning / 0 error` | Release 側の設定は未変更 | UnitTest 一括を実行する |
| 2026-03-11 18:06 | Phase 3 / verify tests | `AnalogBoard_UnitTest\\build_test.bat` を実行し、全テスト pass を確認 | passed | `FpgaRegisterLogic 417 pass`, `WaveDataFileIO 9422 pass`, `SavePathValidation 39 pass`, `AcquisitionPerfMetrics 21 pass`, `FileLogger 19 pass`, `UsbTransferHelpers 73 pass` | `FpgaRegisterLogic_test.cpp` の既存 warning C4819/C4996/C4189 は残る | troubleshooting/build.md に知見を追記してタスク完了 |
