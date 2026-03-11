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
| 2026-03-11 16:22 | Phase 3 / release build | `Release x64` の DLL と TestApp を rebuild し、どちらも成功 | passed | `x64\\Release\\AnalogBoard_Dll.dll`, `x64\\Release\\AnalogBoard_TestApp.exe` | none | unit test 一括を確認する |
| 2026-03-11 16:23 | Phase 3 / full unit test | `AnalogBoard_UnitTest\\build_test.bat` を実行し、全テストが pass | passed | `WaveDataFileIO 9422 pass`, `UsbTransferHelpers 50 pass` | `FpgaRegisterLogic_test.cpp` に既存 warning は残る | 実機確認とユーザー共有へ進む |
| 2026-03-11 16:32 | Phase 4 / cleanup | comparison worktree `AnalogBoard_cmp_76b2b2a_ep6_local_buffer` を削除 | completed | `git worktree list` | comparison branch 自体は残っている | version update と branch 整理へ進む |
| 2026-03-11 16:32 | Phase 4 / version bump | `AnalogBoardTestApp.rc` の表示/version info を `0.1.6` に更新 | completed | `CAPTION ... Ver 0.1.6`, `FILEVERSION 0,1,6,0` | About 表示は updater の仕様で `0.1` を使用 | Release TestApp を rebuild する |
| 2026-03-11 16:33 | Phase 4 / rebuild after version bump | `Release x64` の TestApp を rebuild し、`0.1.6` resource を含む exe を再生成 | passed | `x64\\Release\\AnalogBoard_TestApp.exe` | none | commit と merge を行う |
