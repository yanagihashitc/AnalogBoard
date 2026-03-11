# EP6 Local Buffer Fix Process Log

## 対象プラン

- [EP6 Local Buffer Fix Design](./plans/2026-03-11-ep6-local-buffer-fix-design.md)
- [チェックリスト](./2026-03-11-ep6-local-buffer-fix-checklist.md)

## Process Log Entries

| DateTime (JST) | Phase / Task | Activity | Result | Evidence | Risks / Issues | Next Action |
|---|---|---|---|---|---|---|
| 2026-03-11 16:25 | Phase 1 / design | comparison build の成功条件を現行へ移植する設計メモと tracking 文書を作成 | initialized | `docs/plans/2026-03-11-ep6-local-buffer-fix-design.md` | 現行 `0.1.5` は comparison build より mutex/diagnostic 変更が多い | red test を追加する |
| 2026-03-11 16:26 | Phase 2 / red test | `ScopedHeapBuffer` を前提にした test 4件を追加し、単体 compile failure で red を確認 | expected failure | `error C2039: 'ScopedHeapBuffer'`, `AnalogBoard_UnitTest\\UsbTransferHelpers_test.cpp` | helper 未実装のため compile error で停止 | helper 実装へ進む |
| 2026-03-11 16:27 | Phase 3 / implementation | `UsbTransferHelpers` に `ScopedHeapBuffer` を追加し、`EP6_GetData()` を per-call local buffer 化。`USBBoard_Connect()` の reusable buffer 事前確保を削除した | implemented | `AnalogBoard_Dll\\UsbTransferHelpers.h`, `AnalogBoard_Dll\\AnalogBoard_Dll.cpp` | ABI 崩れを避けるため `m_ep6TransferBuffer` は未使用のまま保持 | targeted test と build を実行する |
| 2026-03-11 16:27 | Phase 3 / targeted test | `UsbTransferHelpers_test` を compile/run し、18 case / 50 assertion が全件 pass | passed | `=== Results: 50 tests, 50 passed, 0 failed ===` | none | Release build を確認する |
| 2026-03-11 16:28 | Phase 3 / release build | `Release x64` の DLL と TestApp を rebuild し、どちらも成功 | passed | `x64\\Release\\AnalogBoard_Dll.dll`, `x64\\Release\\AnalogBoard_TestApp.exe` | none | unit test 一括を確認する |
| 2026-03-11 16:29 | Phase 3 / full unit test | `AnalogBoard_UnitTest\\build_test.bat` を実行し、全テストが pass | passed | `WaveDataFileIO 9422 pass`, `UsbTransferHelpers 50 pass` | `FpgaRegisterLogic_test.cpp` に既存 warning は残る | 実機確認とユーザー共有へ進む |
| 2026-03-11 16:32 | Phase 4 / cleanup | comparison worktree `AnalogBoard_cmp_76b2b2a_ep6_local_buffer` を削除 | completed | `git worktree list` | comparison branch 自体は残っている | version update と branch 整理へ進む |
| 2026-03-11 16:32 | Phase 4 / version bump | `AnalogBoardTestApp.rc` の表示/version info を `0.1.6` に更新 | completed | `CAPTION ... Ver 0.1.6`, `FILEVERSION 0,1,6,0` | About 表示は updater の仕様で `0.1` を使用 | Release TestApp を rebuild する |
| 2026-03-11 16:33 | Phase 4 / rebuild after version bump | `Release x64` の TestApp を rebuild し、`0.1.6` resource を含む exe を再生成 | passed | `x64\\Release\\AnalogBoard_TestApp.exe` | none | commit と merge を行う |
| 2026-03-11 16:49 | Phase 5 / red test | `ScopedHeapBuffer` move semantics を固定する test 4件を追加し、compile failure で red を確認 | expected failure | `error C2280`, `AnalogBoard_UnitTest\\UsbTransferHelpers_test.cpp` | move ctor / move assign 未定義のため copy delete にフォールバック | helper 実装へ進む |
| 2026-03-11 16:50 | Phase 5 / implementation | `ScopedHeapBuffer` を `new[]/delete[]` へ統一し、`noexcept` move ctor / move assign を追加。`EP6_GetData()` の到達不能 null check を削除した | implemented | `AnalogBoard_Dll\\UsbTransferHelpers.h`, `AnalogBoard_Dll\\AnalogBoard_Dll.cpp` | 4MB per-call allocation overhead は info 指摘のみで、実測課題が出るまでは現状維持 | targeted/full test と Debug build を確認する |
| 2026-03-11 16:50 | Phase 5 / targeted test | `UsbTransferHelpers_test` を compile/run し、22 case / 70 assertion が全件 pass | passed | `=== Results: 70 tests, 70 passed, 0 failed ===` | none | test perspective と process log を更新する |
| 2026-03-11 16:51 | Phase 5 / docs sync | `usb-transfer-helpers` test perspective に move case 4件を追加し、既存 process log の Phase 3 timestamp を実行順へ修正 | completed | `docs\\test-perspectives\\usb-transfer-helpers.md`, `docs\\2026-03-11-ep6-local-buffer-fix-log.md` | none | full unit test を実行する |
| 2026-03-11 16:52 | Phase 5 / full unit test | `AnalogBoard_UnitTest\\build_test.bat` を実行し、全テストが pass | passed | `FpgaRegisterLogic 417 pass`, `WaveDataFileIO 9422 pass`, `UsbTransferHelpers 70 pass` | `FpgaRegisterLogic_test.cpp` の既存 warning C4819/C4996/C4189 は残る | Debug x64 rebuild を確認する |
| 2026-03-11 16:53 | Phase 5 / debug rebuild | `Debug x64` の solution rebuild を実行し、DLL/TestApp とも成功 | passed | `x64\\Debug\\AnalogBoard_Dll.dll`, `x64\\Debug\\AnalogBoard_TestApp.exe` | `AnalogBoard_Dll.vcxproj` の既存 `LNK4098` warning は残る | 変更内容をユーザーへ共有する |
