# binary原子的排出 Process Log

## 対象プラン

- [AnalogBoard_improvement_plan.md](../plans/AnalogBoard_improvement_plan.md)
- [チェックリスト](2026-03-05-binary-atomic-publish-checklist.md)

## 記入ルール

- 本ログは実施都度、時系列で追記する（上書きしない）
- 各タスクの着手時/完了時に最低 1 エントリを記録する
- 「判断理由」がある変更（方針変更、スコープ調整、閾値変更）は必ず記録する
- 測定値は可能な限り実数で記録し、推測値は `estimate` と明記する

## エントリ項目

- DateTime (JST)
- Phase / Task
- Activity
- Result
- Evidence (log path / test output / commit id)
- Risks / Issues
- Next Action

## Process Log Entries

| DateTime (JST) | Phase / Task | Activity | Result | Evidence | Risks / Issues | Next Action |
|---|---|---|---|---|---|---|
| 2026-03-05 | - | ログ・チェックリスト作成 | initialized | このファイル | none | Phase 1 実装開始 |
| 2026-03-05 21:05 | Phase 1-3 | `WaveDataFileIO.h` 新規作成、`SaveWaveDataToFileImpl`/`StdFileWriter` 実装、`Dialog1_Main.cpp` を `.tmp` オープン仕様へ移行 | 完了 | `AnalogBoard_TestApp/WaveDataFileIO.h`, `AnalogBoard_TestApp/Dialog1_Main.cpp` | 既存 UnitTest プロジェクト未整備のため `msbuild /t:AnalogBoard_UnitTest:Rebuild` は利用不可 | Phase 4-6 実装へ |
| 2026-03-05 21:18 | Phase 4-6 | Close後 rename、FL->FH 固定、rename retry/rollback、startup `.tmp` cleanup、SavePath 正規化を実装 | 完了 | `AnalogBoard_TestApp/Dialog1_Main.cpp` (`FlushCloseAndPublishWavePair`, `CleanupResidualTmpFiles`, `NormalizeSavePath`) | `build_test.bat` 実行環境で出力が途切れるケースがあり、個別コマンドで検証を補完 | Phase 2/T2 テスト実装へ |
| 2026-03-05 21:26 | Phase 2 / Phase 8 | T1/T2 用 `WaveDataFileIO_test.cpp` を追加。6ペア SHA256一致、rename失敗系(FL失敗/FH失敗rollback)をテスト化 | 完了 | `AnalogBoard_UnitTest/WaveDataFileIO_test.cpp`, `AnalogBoard_UnitTest/build_test.bat` | `_wfopen` の C4996 warning は既存ポリシー上許容 | Debug Rebuild/最終確認へ |
| 2026-03-05 21:31 | Verification | Debug x64 Rebuild 実施、UnitTest 2本の pass を確認 | 成功 | `cmd /d /c "scripts\\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"`, `FpgaRegisterLogic_test.exe: 413/413 pass`, `WaveDataFileIO_test.exe: 117/117 pass` | Phase 9以降（擬似統合/性能/ロールアウト）は未着手 | Phase 9 以降の計画実行 |
| 2026-03-05 21:36 | Phase 10 | `SavePath` 正規化/絶対パス必須、`..` 拒否、`*_fl_*.bin.tmp`/`*_fh_*.bin.tmp` のみ cleanup を再確認 | 完了 | `Dialog1_Main.cpp` (`NormalizeSavePath`, `ContainsPathTraversalToken`, `CleanupResidualTmpFiles`) | cleanup の実環境検証（実機での再起動時確認）は Phase 9/T3 と合わせて継続 | 擬似統合テストへ |
| 2026-03-05 21:42 | Phase 9 | T3 擬似統合テストを `WaveDataFileIO_test.cpp` に追加（tmp先行/強制終了相当/再起動cleanup）。cleanup ロジックを `WaveDataFileIO.h` に共通化して本番実装と同一経路で検証 | 完了 | `WaveDataFileIO_test.exe: 147/147 pass`, `msbuild Rebuild Debug|x64 成功`, `WaveDataFileIO::CleanupResidualBinTmpFiles` | `build_test.bat` のワンショット実行は環境依存で途中終了するため個別コマンド実行を継続 | Phase 11 性能評価へ |
| 2026-03-05 21:46 | Phase 11 | 500ファイル性能測定テストを追加し、direct書き込み vs `.tmp + rename` を同条件比較。平均/p95/最大/rename retry率を取得 | 部分完了（閾値未達） | `WaveDataFileIO_test.exe` ログ: `direct avg=1.343ms, atomic avg=1.522ms, degradation=13.31%, retryRate=0.00000` | 判定基準（平均遅延悪化 <=10%）は未達。I/O条件・flush戦略・ログ粒度の再調整が必要 | Phase 11 再調整 or Phase 13前に実機再計測 |
| 2026-03-05 21:46 | Phase 12 | 下流polling同時実行相当の擬似テストを追加し、direct/atomicで sharing violation 率を比較 | 完了（擬似環境） | `WaveDataFileIO_test.exe` ログ: `direct fail rate=1.0000, atomic fail rate=0.0000` | 実 sys_app 連携では未計測（擬似テスト結果） | 実機でT4再確認後ロールアウト判断 |
| 2026-03-05 21:49 | Phase 11 (rerun) | 実データ相当サイズ（FL=19,200,000B / FH=12,000,000B）へ計測条件を変更して再測定 | 部分完了（閾値未達） | `WaveDataFileIO_test.exe` ログ: `direct avg=10.146ms, atomic avg=21.817ms, degradation=115.04%, retryRate=0.00000` | 開発PCの疑似計測では <=10% を満たせず。実機ディスク条件での再評価が必須 | Phase 13は実機作業へ |
| 2026-03-05 22:24 | Phase 5 / Phase 8 follow-up | Review指摘対応として `PublishWavePairAtomic` のFH rename失敗時ロールバックを改修。low既存ファイルがある場合は事前退避し、失敗時に復元する方式へ変更。再現テスト `Test_T2_AtomicPublish_FhRenameFail_RestoreExistingLow` を追加 | 完了 | `WaveDataFileIO_test.exe`: 追加前 `9381/9383`（2 fail）、修正後 `9383/9383`（0 fail） | 退避ファイル削除はベストエフォート（成功系で削除失敗時は処理継続） | 実機でrename失敗時ログの監視確認を継続 |
| 2026-03-05 22:38 | Phase 14 | Review指摘の追加対応。SaveWaveDataToFileImpl に writer pointer overload を追加し NULL 側スキップ互換を復元。Dialog1_Main.cpp で終端未publishペアの FlushCloseAndPublishWavePair 実行を追加。rename retry回数を引数化（呼び出し側は 3 回設定）。DetectRepoRoot を repo marker 検出へ改善。 | 完了 | WaveDataFileIO_test.exe: 9387/9387 pass、msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 成功 | build_test.bat は環境依存で出力途切れが再発するため、個別コマンド実行で検証 | PR説明に非対応判断（atomic名称/goto/ヘッダ分離/LIBCMT）を記載 |
| 2026-03-05 23:48 | Phase 14 follow-up | Review指摘対応として、EP6 取得失敗 (`iRet != USB_SUCCESS`) 分岐で `ErrExit = TRUE` を設定してから `break` するよう修正。これによりループ終端の pending pair publish 条件 (`ErrExit == FALSE`) が正しく機能し、失敗サイクルで `.tmp` が誤って finalize される経路を遮断。 | 完了 | `AnalogBoard_TestApp/Dialog1_Main.cpp` (around L1837-L1843), `git diff` | 回帰確認は実機/結合経路での EP6 エラー注入が未実施 | 次回: EP6 timeout 注入で pending pair が publish されないことを確認 |
| 2026-03-05 23:58 | Phase 14 verification | 回帰確認として Debug Rebuild と UnitTest 3本を個別実行。`build_test.bat` は環境依存でログ欠落/失敗終了するため、既存方針どおり各 exe を直接実行して判定。 | 完了（自動検証範囲） | `msbuild ... /t:AnalogBoard_TestApp:Rebuild` 成功、`FpgaRegisterLogic_test.exe: 417/417 pass`、`WaveDataFileIO_test.exe: 9387/9387 pass`、`SavePathValidation_test.exe: 11/11 pass` | 実機なしのため EP6 timeout 注入シナリオは未検証 | 実機で timeout 注入し pending pair の未publishを確認 |
| 2026-03-06 00:06 | Phase 14 verification prep | 実機確認用に EP6 timeout 注入シナリオの手順書を作成。操作手順、期待ログ、ファイル期待状態、合否判定、証跡項目を定義。 | 完了 | `docs/2026-03-05-ep6-timeout-field-verification.md` | 実機での実行結果は未取得 | 手順書に従って実機試験を実施し、結果を追記 |
| 2026-03-06 01:02 | Phase 15 | Review指摘対応として `WaveDataFileIO.h` を更新。rollback backup path 生成の独立関数化、`*.bin.rollback.*` cleanup対応、rename試行回数(`attemptCount`)の可視化、`SaveWaveDataToFileImpl` pointer版の意図コメント追加を実施。あわせて `WaveDataFileIO_test.cpp` に cleanup/リトライ回数/nullptr+zero-size/backup-lock耐性のテストを追加。`SavePathValidation.h` に drive-root 判定コメントを追加。DLL側は EP6 診断カウンタを connect/disconnect でリセットするよう変更。 | 完了 | `msbuild AnalogBoard_TestApp.sln /t:Rebuild` 成功、`WaveDataFileIO_test.exe: 9420/9420 pass`、`SavePathValidation_test.exe: 23/23 pass`、`FpgaRegisterLogic_test.exe: 417/417 pass` | `build_test.bat` は環境依存で途中失敗するため個別実行で判定継続 | 実機で EP6 timeout 注入手順の実施結果を追記 |
| 2026-03-06 01:19 | Phase 16 | `build_test.bat` の途中終了を調査。原因は `if (...)` ブロック内 `echo` 文の未エスケープ括弧で、バッチ構文が崩れていたこと。`^(`/`^)` へ修正し、`if errorlevel 1` へ統一。 | 完了 | `scripts\\run_with_vsdevcmd.bat AnalogBoard_UnitTest\\build_test.bat` 成功、3テスト完走 (`417/417`, `9420/9420`, `23/23`) | 末尾に `cl` banner が追加で出るが、終了コード/判定には影響なし | 必要なら banner抑制（`/nologo`）を別タスク化 |

