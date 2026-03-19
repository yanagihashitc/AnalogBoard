# AnalogBoard 改善プラン（binary原子的排出）

## 目的
- 書き込み途中ファイルを下流（sys_app）が読む事象を防止する。
- 計測中でも「完成済みペアのみ」公開できる状態にする。
- gain_adjust 連続取得時の読み取り失敗を根本的に減らす。

## 絶対制約
- **`.bin` ファイルの内容（バイト列・フォーマット）は一切変更しない。**
  `Write()` に渡すデータ・サイズ・順序は既存と完全同一とする。
- 最終ファイル名（`*_fl_N.bin` / `*_fh_N.bin`）は変更しない。

## 現状整理
- `*_fl_n.bin` / `*_fh_n.bin` を直接 `modeCreate | modeWrite` で開いて書き込む。
- ファイルは計測途中で存在し、クローズまで未完成状態。
- 下流が「最新」を取ると、書き込み中ファイルに当たる可能性がある。

## 変更対象
以下の関数・処理を改修する。すべて `Dialog1_Main.cpp` 内。

| 関数 / 処理 | 行付近 | 概要 |
|---|---|---|
| `CreateWaveDataFile()` | 1651-1686 | FL/FH ファイルを `modeCreate \| modeWrite` で開く |
| `LoopTestProcessThread_EP6_GetData()` | 1488-1575 | ファイル作成 → 書き込み → Close のループ |

**対象外:**
- `Dialog2_Debug::OnBnClickedButtonEp6rx()` — デバッグ用単発書き込み。下流連携なし。
- `SaveWaveDataToCHFile()` — 現在コメントアウト済み。

## 改善方針
1. 最終名へ直接書かない。
2. 一時ファイル（`*_fl_n.bin.tmp` / `*_fh_n.bin.tmp`）へ書き込み、`Close` 後に同一ディレクトリで `rename`。
3. FL/FH 両方の rename 成功をもってペア完成とする。

## 技術選定理由（rename方式）
- 採用: `MoveFileEx(tmpPath, finalPath, MOVEFILE_REPLACE_EXISTING)` を使用する。
- 理由:
  - 同一ディレクトリ内 rename のため、下流に「完成ファイルのみ見せる」設計と整合しやすい。
  - 既存 Win32/MFC 実装（`CFile`, `DeleteFile`）との親和性が高く、変更範囲を最小化できる。
- 非採用: `CopyFile + DeleteFile`
  - コピー途中の中間状態を別名で管理する必要があり、公開条件が複雑になる。
  - 大容量 `.bin` で余分なI/Oが増え、gain_adjust連続取得時の遅延要因になりやすい。

## 実装ステップ

### Step 1: `CreateWaveDataFile` の拡張
- 出力先を `.bin.tmp` 名で開く。
- 最終パス（`.bin`）を呼び出し元に返すため、引数に `CString* outFinalPath_l, CString* outFinalPath_h` を追加する。
- `Write()` に渡すデータ・バッファは一切変更しない。
- 返却契約:
  - `Ok`: FL/FH とも `.tmp` open 成功
  - `OpenLowFailed`: FL open 失敗
  - `OpenHighFailed`: FH open 失敗（FL が既に open 済みなら `Close` + `.tmp` 削除でロールバック）

### Step 2: `SaveWaveDataToFile` のヘッダ抽出（テスタビリティ確保）
- `SaveWaveDataToFile` のロジックを `WaveDataFileIO.h` にテンプレート関数として抽出する。
- ファイルI/O部分を型パラメータ化し、本番では `CFile`、テストでは `StdFileWriter`（fwrite wrapper）を使う。
- `Dialog1_Main.cpp` からは `#include "WaveDataFileIO.h"` で呼び出す（振る舞い変更なし）。
- 完了時に `Flush()` → `Close()` を必ず実行する。
- ループ内の if/else 両分岐で確実に Close されるよう整理する。

```cpp
// WaveDataFileIO.h - production and test share the same logic
template<typename FileWriter>
INT SaveWaveDataToFileImpl(FileWriter& fp_l, FileWriter& fp_h,
                           PBYTE WaveData, ULONG FrameSize_L,
                           ULONG FrameSize_H, INT WaveCnt);
// Production: SaveWaveDataToFileImpl<CFile>(...)
// Test:       SaveWaveDataToFileImpl<StdFileWriter>(...)
```

### Step 3: Close 後に rename
- `Close()` 成功後、`MoveFileEx(tmpPath, finalPath, MOVEFILE_REPLACE_EXISTING)` で rename する。
- rename 順序: FL → FH（順序は固定し、障害時の調査を容易にする）。

### Step 4: rename 失敗時のエラーハンドリング
- **FL rename 失敗:** `.tmp` をそのまま残す。ログに記録し、当該インデックスのデータは不完全として扱う。
- **FL 成功 → FH rename 失敗:** FL 側の `.bin` を `DeleteFile()` で削除し、不整合ペアを残さない。ログに記録。
- **リトライ:** rename 失敗時は 100ms wait 後に 1回だけリトライする。それでも失敗なら上記のエラー処理に進む。

### Step 5: 異常終了時の `.tmp` 掃除
- アプリ起動時（`LoopTestProcessThread_EP6_GetData` スレッド開始直前）に、保存先ディレクトリの `*.bin.tmp` を `DeleteFile()` で掃除する。
- `.tmp` は命名規則上、下流の探索パターン（`*_fl_*.bin` / `*_fh_*.bin`）に一致しないため、掃除前でも下流に影響しない。
- 掃除対象は `*_fl_*.bin.tmp` / `*_fh_*.bin.tmp` のみに限定する（誤削除防止）。
- 保存先は正規化済み絶対パスのみ許可し、起動中セッションで使用中の `.tmp` は削除対象から除外する。

## 失敗パス仕様（エラーマトリクス）

| 失敗点 | 検知方法 | リトライ | ロールバック | ログ必須項目 | 継続可否 |
|---|---|---|---|---|---|
| FL `.tmp` Open 失敗 | `CreateWaveDataFile` 戻り値 | なし | なし | `index`, `tmpPathL`, `GetLastError` | 当該 index はスキップ |
| FH `.tmp` Open 失敗 | `CreateWaveDataFile` 戻り値 | なし | FL が open 済みなら `Close` + `DeleteFile(tmpL)` | `index`, `tmpPathH`, `tmpPathL`, `GetLastError` | 当該 index はスキップ |
| Write 失敗 | 例外/戻り値 | なし | `Close` 後 `.tmp` を残置（調査用） | `index`, `bytesRequested`, `bytesWritten`, `api` | Error扱いで次サイクルへ |
| Flush 失敗 | 戻り値/例外 | 1回 | `Close` 後 `.tmp` 残置 | `index`, `tmpPath`, `GetLastError` | Error扱い |
| Close 失敗 | 戻り値/例外 | 1回 | `.tmp` 残置（rename禁止） | `index`, `tmpPath`, `GetLastError` | Error扱い |
| FL rename 失敗 | `MoveFileEx` 戻り値 | 100ms待機後1回 | `.tmp` 残置 | `index`, `tmpPathL`, `finalPathL`, `GetLastError` | 当該ペア不完全として扱う |
| FH rename 失敗（FL成功後） | `MoveFileEx` 戻り値 | 100ms待機後1回 | `DeleteFile(finalPathL)` でペア巻き戻し、FH `.tmp` 残置 | `index`, `finalPathL`, `tmpPathH`, `finalPathH`, `GetLastError` | 当該ペア不完全として扱う |
| startup cleanup 失敗 | `DeleteFile` 戻り値 | なし | なし | `path`, `GetLastError` | Warningログのみで継続 |

## 命名規約
- 最終名:
  - `{timestamp}_fl_{n}.bin`  (例: `250305_0105_fl_1.bin`)
  - `{timestamp}_fh_{n}.bin`  (例: `250305_0105_fh_1.bin`)
- 一時名:
  - `{timestamp}_fl_{n}.bin.tmp`
  - `{timestamp}_fh_{n}.bin.tmp`
- タイムスタンプ形式: `YYMMDD_HHMM` （重複時は `_1`, `_2` ... サフィックス付加）

## 互換性
- 最終ファイル名は変更しないため、sys_app 側の探索ロジックは無改修で利用可能。
- `.tmp` は既存パターンに一致しないため下流から自然に除外される。
- `.bin` ファイルの内容（バイナリフォーマット）は完全に同一。

## セキュリティ
- `SavePath` は起動時と実行前に正規化し、絶対パスであることを必須にする。
- 掃除処理で削除するのは `*_fl_*.bin.tmp` / `*_fh_*.bin.tmp` のみ。`*.bin` / `*.cfg` は削除対象外。
- パス結合は `timestamp + suffix` 方式を維持し、`..` を含む入力は拒否する。
- ログにはバイナリ内容を出力せず、パス・バイト数・エラーコード・`GetLastError` のみ記録する。

## 依存関係
- ヘッダ/API:
  - `Windows.h`（`MoveFileEx`, `DeleteFile`, `GetLastError`）
  - 既存 `CFile`（`Open`, `Write`, `Flush`, `Close`）
- 追加実装ファイル:
  - `WaveDataFileIO.h`（`SaveWaveDataToFileImpl` テンプレート）
- エラー記録ポリシー:
  - すべてのI/O失敗ログに `api`, `index`, `tmpPath/finalPath`, `GetLastError` を含める。
- エラーコード方針:
  - 既存 `USB_ERR_*` を優先利用し、新規コードが必要な場合のみ `USB_ERR_FILE_RENAME` などを追加定義する。

## パフォーマンス評価
- 計測KPI:
  - 1ファイルあたり `write + flush + rename` 所要時間（平均/95p/最大）
  - 連続500ファイル時の総処理時間
  - rename retry 発生率（%）
- 比較条件:
  - 改修前（直接 `.bin` 書き込み）と改修後（`.tmp + rename`）で同一データセット・同一PC条件で比較する。
- 判定基準:
  - 平均遅延の悪化が 10% 以内、かつ preview失敗率改善が有意であること。
  - 10%超悪化時は retry条件/ログ詳細度/flushタイミングを再調整する。

## テスト計画

### ビルド方法
```
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

### テストデータ
`data/` 配下に3パターンの実データを格納済み。全パターンともフレームパラメータは同一（FirFilterFC=0, 全13CH, TrgRange=60）。

| データセット | タイムスタンプ | WaveNum | ファイルペア数 |
|---|---|---|---|
| `data/sample_data/` | 251224_1007 | 500 | 1 |
| `data/sample_data2/` | 260220_1309 | 100 | 3 |
| `data/sample_data3/` | 251224_1406 | 500 | 2 |

共通パラメータ（cfg.txt より逆算・検証済み）:

| パラメータ | 値 |
|---|---|
| CHNum_L (CH1-8) | 8 |
| CHNum_H (CH9-13) | 5 |
| FirFilterFC | 0 → OneCHSize_H = 80 |
| TrgRange | 60 |
| OneWaveSize_L | 80 × 8 × 60 = 38,400 bytes |
| OneWaveSize_H | 80 × 5 × 60 = 24,000 bytes |

### T1: .bin フォーマット不変の検証（最重要）

既存の実データ `.bin` ペアから逆算で再構成済みの `ReadBuf` を、`SaveWaveDataToFileImpl<StdFileWriter>` に渡して出力した `.bin` と元ファイルのハッシュが一致することを確認する。**全3パターン×全ファイルペアを検証する。**

**テスト手順（各データセット・各ファイルペアに対して実行）:**
1. `data/sample_data*/reconstructed/*_readbuf_N.bin` (作成済み) をテスト入力として使用
2. `ReadBuf` を `SaveWaveDataToFileImpl<StdFileWriter>` に渡して `.bin.tmp` → rename → `.bin` を生成（出力先は一時ディレクトリ）
3. 生成された `.bin` と元の `.bin` の SHA256 を比較
4. テスト終了後、生成された `.bin` / `.bin.tmp` を削除

**テスト対象ファイルペア一覧:**

| データセット | ペア | FL サイズ | FH サイズ | WaveNum |
|---|---|---|---|---|
| sample_data | 251224_1007_{fl,fh}_1 | 19,200,000 | 12,000,000 | 500 |
| sample_data2 | 260220_1309_{fl,fh}_1 | 3,840,000 | 2,400,000 | 100 |
| sample_data2 | 260220_1309_{fl,fh}_2 | 3,840,000 | 2,400,000 | 100 |
| sample_data2 | 260220_1309_{fl,fh}_3 | 3,840,000 | 2,400,000 | 100 |
| sample_data3 | 251224_1406_{fl,fh}_1 | 19,200,000 | 12,000,000 | 500 |
| sample_data3 | 251224_1406_{fl,fh}_2 | 19,200,000 | 12,000,000 | 500 |

**ファイル配置:**
```
data/
  reconstruct_readbuf.py          # ReadBuf再構成スクリプト（全データセット対応）
  sample_data/
    251224_1007_{fl,fh}_1.bin     # 元データ（変更しない）
    251224_1007_cfg.txt
    reconstructed/
      251224_1007_readbuf_1.bin   # 作成済み
  sample_data2/
    260220_1309_{fl,fh}_{1,2,3}.bin
    260220_1309_cfg.txt
    reconstructed/
      260220_1309_readbuf_{1,2,3}.bin  # 作成済み
  sample_data3/
    251224_1406_{fl,fh}_{1,2}.bin
    251224_1406_cfg.txt
    reconstructed/
      251224_1406_readbuf_{1,2}.bin    # 作成済み
```

**ReadBuf の構造:**
```
ReadBuf = [Low_frame0 (38,400B) | High_frame0 (24,000B) | Low_frame1 | High_frame1 | ... ] × WaveNumフレーム
```
再生成が必要な場合は `data/` 内で `python reconstruct_readbuf.py` を実行する（全データセットを一括処理）。

**合格条件:** 全6ペアすべてで FL / FH のハッシュが元ファイルと完全一致。

### T2: 単体相当
| ID | 入力条件 | 期待結果 | 期待ログキー | 合格基準 |
|---|---|---|---|---|
| T2-1 | 正常系（lockなし） | `.tmp` 生成→書込→Flush/Close→FL/FH rename 成功 | `tmp open success`, `close success`, `rename success` | FL/FH `.bin` がペアで生成される |
| T2-2 | FL rename 失敗（lock模擬） | 100ms後1回リトライし、失敗時 `.tmp` 残置 | `rename fail`, `rename retry` | `.bin` が公開されない |
| T2-3 | FL成功/FH rename失敗 | FL `.bin` を削除して不整合ペアを残さない | `rename rollback`, `DeleteFile` | FLのみ/FHのみが残らない |

### T3: 擬似統合
| ID | 入力条件 | 期待結果 | 期待ログキー | 合格基準 |
|---|---|---|---|---|
| T3-1 | 計測中ディレクトリ監視 | `.tmp` のみ先行し、完成時のみ `.bin` 出現 | `tmp open`, `rename success` | 途中状態の `.bin` が観測されない |
| T3-2 | 強制終了（途中） | `.tmp` 残存でも下流探索から除外される | `close fail` or `thread exit` | sys_app 側誤読なし |
| T3-3 | 再起動時 cleanup | 残存 `.tmp` が対象パターンのみ削除される | `startup tmp cleanup count` | 対象外ファイルを削除しない |

### T4: 下流連携
| ID | 入力条件 | 期待結果 | 期待ログキー | 合格基準 |
|---|---|---|---|---|
| T4-1 | sys_app polling 同時実行 | `PermissionError` 発生率が改修前より低下 | `rename success/fail`, `preview fail count` | 改修前比で失敗率が有意に低下 |

## ログ・監視
- 記録するイベント:
  - tmp open success/fail
  - write bytes
  - close success/fail
  - rename success/fail (リトライ含む)
  - rename rollback (FL削除)
  - startup tmp cleanup count
- 目的:
  - 失敗点の切り分けを即時化し、運用調査時間を短縮する。

## ロールアウト
1. 検証機で `.tmp + rename` を先行導入。
2. 同条件で従来方式と比較し、読み取りエラー率を評価。
3. T1 で `.bin` フォーマット不変を確認。
4. 問題なければ本番適用し、一定期間は詳細ログを有効化。

## 移行・ロールバック
- 段階導入:
  1. 開発機で T1/T2/T3 を完了
  2. 検証機で T4 と性能KPI比較を実施
  3. 本番へ適用し、初期期間は詳細ログON
- ロールバック条件:
  - `PermissionError` 率が改修前より悪化
  - FL/FH の不整合ペアが1件でも発生
  - `.bin` ハッシュ一致（T1）が崩れる
- ロールバック手順:
  1. `.tmp + rename` 経路を無効化し、従来の直接 `.bin` 書き込みへ戻す
  2. 残存 `.tmp` は隔離フォルダへ退避して調査
  3. 直近ログ（rename/cleanup）を保存して原因解析へ引き渡す

## 関連ドキュメント
- `docs/plans/2026-03-02-usb-acquisition-stability.md`
- `docs/plans/review_AnalogBoard_improvement_plan_20260305.md`
- `docs/process_log/TEMPLATE.md`

## 受け入れ条件
- **`.bin` ファイルの内容が改修前後でバイト単位で同一である。**
- 書き込み途中の `.bin` が観測されない。
- sys_app 側の preview 失敗率（Permission系）が有意に低下する。
- 既存ワークフロー（ファイル命名・取り込み手順）を壊さない。
- rename 失敗時に不整合ペア（FL のみ or FH のみ）が残らない。
