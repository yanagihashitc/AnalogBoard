# binary原子的排出 タスクチェックリスト

対象プラン: [AnalogBoard_improvement_plan.md](../plans/archive/AnalogBoard_improvement_plan.md)
プロセスログ: [Process Log](2026-03-05-binary-atomic-publish-log.md)
作成日: 2026-03-05

---

## Phase 1: WaveDataFileIO.h 抽出 (Step 2)

依存: なし

- [x] `SaveWaveDataToFile` の現行ロジックを読み解き、入出力仕様を確定する
- [x] `WaveDataFileIO.h` を作成し、`SaveWaveDataToFileImpl<FileWriter>` テンプレート関数を実装する
- [x] `StdFileWriter`（fwrite wrapper）をテスト用に実装する
- [x] `Dialog1_Main.cpp` から `#include "WaveDataFileIO.h"` で呼び出すよう差し替える
- [x] Flush() -> Close() の確実な実行を保証する（if/else 両分岐）
- [x] Debug x64 Rebuild 成功を確認

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## Phase 2: T1 テスト (.bin フォーマット不変検証)

依存: Phase 1

- [x] T1 テストコードを作成（`SaveWaveDataToFileImpl<StdFileWriter>` で .bin 生成 -> SHA256 比較）
- [x] sample_data (251224_1007, 500 waves, 1 pair) で検証
- [x] sample_data2 (260220_1309, 100 waves, 3 pairs) で検証
- [x] sample_data3 (251224_1406, 500 waves, 2 pairs) で検証
- [x] 全6ペアで FL/FH の SHA256 が元ファイルと完全一致することを確認
- [x] UnitTest Rebuild 成功を確認

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## Phase 3: CreateWaveDataFile 拡張 (Step 1)

依存: Phase 1

- [x] `CreateWaveDataFile()` の出力先を `.bin.tmp` 名で開くよう変更する
- [x] 引数に `CString* outFinalPath_l, CString* outFinalPath_h` を追加する
- [x] エラー時のロールバック処理を実装する（FH open失敗時に FL を Close + .tmp 削除）
- [x] 戻り値の契約を実装する（Ok / OpenLowFailed / OpenHighFailed）
- [x] Debug x64 Rebuild 成功を確認

---

## Phase 4: Close 後 rename (Step 3)

依存: Phase 1, Phase 3

- [x] `LoopTestProcessThread_EP6_GetData()` 内で Close 後に `MoveFileEx(tmpPath, finalPath, MOVEFILE_REPLACE_EXISTING)` を呼び出す
- [x] rename 順序を FL -> FH に固定する
- [x] Debug x64 Rebuild 成功を確認

---

## Phase 5: rename 失敗時のエラーハンドリング (Step 4)

依存: Phase 4

- [x] FL rename 失敗時: .tmp 残置 + ログ記録を実装する
- [x] FH rename 失敗時（FL成功後）: 新規 FL を取り消し、既存 FL があれば退避ファイルから復元して保全する
- [x] 100ms wait 後 1回リトライを実装する
- [x] ログ出力: api, index, tmpPath/finalPath, GetLastError を含める
- [x] エラーコード方針: 既存 `USB_ERR_*` を優先利用、必要に応じ `USB_ERR_FILE_RENAME` 等を追加する
- [x] Debug x64 Rebuild 成功を確認

---

## Phase 6: 異常終了時の .tmp 掃除 (Step 5)

依存: Phase 4

- [x] アプリ起動時（スレッド開始直前）に保存先ディレクトリの `*.bin.tmp` を掃除する処理を実装する
- [x] 掃除対象を `*_fl_*.bin.tmp` / `*_fh_*.bin.tmp` のみに限定する
- [x] 保存先を正規化済み絶対パスのみ許可する
- [x] 使用中 `.tmp` を削除対象から除外する
- [x] 掃除失敗時は Warning ログのみで継続する
- [x] Debug x64 Rebuild 成功を確認

---

## Phase 7: ログ・監視

依存: Phase 5, Phase 6

- [x] 全 I/O イベントのログ出力を確認する
  - [x] tmp open success/fail
  - [x] write bytes
  - [x] close success/fail
  - [x] rename success/fail (リトライ含む)
  - [x] rename rollback (FL削除)
  - [x] startup tmp cleanup count
- [x] ログに `api`, `index`, `tmpPath/finalPath`, `GetLastError` が含まれることを確認する
- [x] バイナリ内容がログに出力されないことを確認する

---

## Phase 8: T2 単体相当テスト

依存: Phase 5

- [x] T2-1: 正常系 - .tmp 生成 -> 書込 -> Flush/Close -> FL/FH rename 成功を検証する
- [x] T2-2: FL rename 失敗（lock模擬）- 100ms後リトライ、失敗時 .tmp 残置を検証する
- [x] T2-3: FL成功/FH rename失敗 - 新規 FL を取り消し、既存 FL がある場合は内容が復元されることを検証する
- [x] UnitTest 全件 pass を確認

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## Phase 9: T3 擬似統合テスト

依存: Phase 6, Phase 8

- [x] T3-1: 計測中ディレクトリ監視 - .tmp のみ先行し、完成時のみ .bin 出現を検証する
- [x] T3-2: 強制終了（途中）- .tmp 残存でも下流探索から除外されることを検証する
- [x] T3-3: 再起動時 cleanup - 残存 .tmp が対象パターンのみ削除されることを検証する

---

## Phase 10: セキュリティ確認

依存: Phase 6

- [x] `SavePath` の正規化・絶対パス必須チェックを実装する
- [x] パス結合で `..` を含む入力を拒否することを確認する
- [x] 掃除処理で `*.bin` / `*.cfg` が削除対象外であることを確認する

---

## Phase 11: パフォーマンス評価

依存: Phase 9

- [x] 1ファイルあたり write + flush + rename 所要時間を計測する（平均/95p/最大）
- [x] 連続500ファイル時の総処理時間を計測する
- [x] rename retry 発生率を計測する
- [x] 改修前（直接 .bin 書き込み）との比較を実施する
- [ ] 平均遅延悪化が 10% 以内であることを確認する
  - 注記: 現在の開発PC上の疑似計測では `degradation=115.04%`（`WaveDataFileIO_test`）で未達。実機I/O条件での再計測が必要。

---

## Phase 12: T4 下流連携テスト

依存: Phase 11

- [x] T4-1: sys_app polling 同時実行で PermissionError 発生率を計測する
- [x] 改修前比で失敗率が有意に低下することを確認する

---

## Phase 13: ロールアウト

依存: Phase 12

- [ ] 検証機で `.tmp + rename` を先行導入する
- [ ] 従来方式と同条件で読み取りエラー率を比較する
- [ ] T1 で .bin フォーマット不変を再確認する
- [ ] 本番適用し、詳細ログを有効化する
- [ ] ロールバック手順の動作確認を実施する
  - 注記: 本Phaseは検証機/本番機が必要なため、実機なしでは未実施。

---

## Phase 14: Review指摘フォローアップ (2026-03-05)

依存: Phase 5, Phase 8

- [x] `SaveWaveDataToFile` の `NULL` writer 互換を復元（`NULL` 側はスキップ継続）
- [x] ループ終了時に未publishの最終 `.tmp` ペアを publish する処理を追加
- [x] `RenameTempFileWithRetry` のリトライ回数を引数化し、呼び出し側で設定可能にした
- [x] `DetectRepoRoot` を `data/sample_data` 依存から repo marker 依存へ改善
- [x] EP6 読み取り失敗時に `ErrExit` を必ず立て、未完了 `.tmp` の誤 publish を防止
- [x] 回帰確認: `AnalogBoard_TestApp` Debug Rebuild 成功
- [x] 回帰確認: UnitTest 3本（`FpgaRegisterLogic_test` / `WaveDataFileIO_test` / `SavePathValidation_test`）pass
- [x] 実機確認手順書を作成（`docs/2026-03-05-ep6-timeout-field-verification.md`）
- [ ] 実機確認: EP6 timeout 注入時に pending pair が publish されないことを確認
- [x] 変更不要判断: `PublishWavePairAtomic` 名称（厳密原子性は不可、rollback実装で運用上許容）
- [x] 変更不要判断: `goto FINALIZE_THREAD`（既存構造維持、将来RAII化は別PR）
- [x] 変更不要判断: `WaveDataFileIO.h` の `.cpp` 分離（本PRのスコープ外）
- [x] 変更不要判断: 大量ホワイトスペース（今回は機能修正のみ実施）
- [x] 変更不要判断: `LIBCMT` 追加（リンク警告継続のため現状維持、説明はPR本文で補足）

---

## Phase 15: Review指摘フォローアップ (2026-03-06)

依存: Phase 14

- [x] `PublishWavePairAtomic` の rollback backup 名生成ロジックを独立関数へ抽出
- [x] startup cleanup 対象に `*_fl_*.bin.rollback.*` / `*_fh_*.bin.rollback.*` を追加し、失敗rollback残骸の掃除漏れを防止
- [x] `RenameTempFileWithRetry` の試行回数を結果構造体で可視化し、`maxRetries=3` 相当の検証テストを追加
- [x] `Test_T2_AtomicPublish_FhRenameFail_BackupLocked_FinalLowRemains` のタイミング依存を緩和（観測リトライ方式）
- [x] `SaveWaveDataToFileImpl` ポインタ版の `nullptr` 設計意図コメントを追加し、`nullptr + frameSizeLow==0` ケースのテストを追加
- [x] `ContainsReservedNameSegment` の `segment.back() != L':'` 判定に意図コメントを追加
- [x] DLL側 EP6 診断カウンタを connect/disconnect 時にリセットしてログ解釈の混乱を緩和
- [x] 回帰確認: `AnalogBoard_TestApp` Debug Rebuild 成功
- [x] 回帰確認: UnitTest 3本（`FpgaRegisterLogic_test` / `WaveDataFileIO_test` / `SavePathValidation_test`）pass

---

## Phase 16: build_test.bat 安定化 (2026-03-06)

依存: Phase 15

- [x] `build_test.bat` の `if (...)` ブロック内メッセージに含まれる括弧をエスケープし、途中終了を解消
- [x] `build_test.bat` のエラー判定を `if errorlevel 1` に統一
- [x] `build_test.bat` を `scripts\\run_with_vsdevcmd.bat` 経由で実行し、3テストが完走することを確認

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] process_log にエントリ追記
