# EP6 logging 負荷切り分け実施チェックリスト

作成日: 2026-03-09
元資料: `0929_log_cut_test_instructions.md`

## 1. 目的

現在版で発生している waveform 異常が、EP6 周辺の logging 負荷によって誘発されているかを段階的に切り分ける。

この試験では、比較版と current 版の差を一気に潰すのではなく、logging を A/B/C の順に止めて影響の大きい層を特定する。

## 2. 基本方針

- 比較版 1 本を基準にする
- current 無改造版でも再現データを 1 セット取る
- current 改変版は `Version A` → `Version B` → `Version C` の順に試す
- `Version C` は DLL も変わるため、`exe` 単体ではなく `exe + dll` の組で管理する

## 3. 固定条件

以下はすべての run で固定する。

- Build: `x64 Release`
- 実行環境: 同じ PC
- sample: 同じ sample
- parameter: 同じ parameter
- 実行条件: Visual Studio debugger なし
- 実行条件: DebugView なし
- 比較版 commit: `aebf296adf72a8ac5a8355f7f539dca87521f724`

## 4. 各 Version の内容

### 4.1 Version A

対象:

- `AnalogBoard_TestApp/Dialog1_Main.cpp`

内容:

- EP6 ホットパスの高頻度 `PrintLog(...)` を止める

主な対象ログ:

- `[PR01][FILE] ...`
- `Fpga ddr write completed. %zu byte.`
- `DDR data sized %zu byte.`
- `[PR01][EP6] call=...`
- `EP6 Read %u byte OK. Save into bin file...`
- `Save OK.(Total size %zu byte)`
- `[PR01][CYCLE] ...`

止めないもの:

- error log
- timeout/error log
- `Remain size error.`
- `Usb read buffer index error.`
- start/end log

### 4.2 Version B

対象:

- `AnalogBoard_TestApp/AnalogBoard_TestAppDlg.cpp`

内容:

- Version A に加えて persistent file logging を止める

止める対象:

- `g_fileLogger.Init(...)`
- `g_fileLogger.Append(...)`
- `g_fileLogger.Flush()`
- `g_fileLogger.Close()`

維持するもの:

- UI listbox への表示

### 4.3 Version C

対象:

- `AnalogBoard_Dll/AnalogBoard_Dll.cpp`

内容:

- Version B に加えて DLL 側の EP6 `OutputDebugStringA(...)` を止める

対象ブロック:

- `USB_Lib_Info::EP6_GetData(...)` 終端付近の `perfLog` 作成と `::OutputDebugStringA(perfLog);`

## 5. 推奨実施順

1. 比較版の正常データを 1 セット確保する
2. current 無改造版の再現データを 1 セット取る
3. `Version A` を build してデータを 1 セット取得する
4. A でまだ異常なら `Version B` を build してデータを 1 セット取得する
5. B でまだ異常なら `Version C` を build してデータを 1 セット取得する

時間優先の簡略順は以下。

1. `Version B` を先に試す
2. 改善しなければ `Version C`

ただし、どの logging 層が効いたかを見たい場合は A/B/C の順を崩さない方がよい。

## 6. 保存先の分け方

各 run の成果物が混ざらないよう、保存先を分ける。

例:

- `compare/`
- `current-baseline/`
- `version-A/`
- `version-B/`
- `version-C/`

各フォルダに残すもの:

- 使用した `exe`
- 使用した `dll`
- 取得データ
- 実行日時
- sample 名
- parameter
- 判定メモ

## 7. 各 run の実施チェック

1. 対象 Version の `exe + dll` を配置する
2. `x64 Release` build であることを確認する
3. debugger と DebugView が起動していないことを確認する
4. 比較版と同じ条件で取得を 1 回実施する
5. 取得データを対象フォルダへ保存する
6. 判定メモを残す

## 8. 判定観点

以下を比較版と current 無改造版に対して比較する。

- notebook 上の waveform 見た目
- periodic corrupted-channel window が残るか
- `fl` と `fh` が比較版に近づくか

## 9. 結果の解釈

### Case A

- `Version A` で正常化、または比較版に近づく

解釈:

- app 側 EP6 ホットパス `PrintLog(...)` が最有力トリガー

### Case B

- `Version A` では改善せず、`Version B` で改善する

解釈:

- file logging が UI logging に加えて有意に効いている

### Case C

- `Version B` では改善せず、`Version C` で改善する

解釈:

- DLL 側 `OutputDebugStringA(...)` も timing に影響している

### Case D

- `Version C` でも改善しない

解釈:

- logging は主因ではない可能性が高い
- 次は raw EP6 dump を両版で取り、`fl/fh` split 前 buffer を比較する

## 10. 実務上の補足

- 「何個か version 違いの exe を作る」という理解でよい
- ただし実際には `exe` だけでなく `dll` も対で管理する
- 最初から全部まとめて作るより、A/B/C を順番に build した方が切り分け精度が高い

## 11. Branch 運用方針

今回の A/B/C 変更は本修正ではなく、切り分け専用の一時変更として扱う。

推奨方針:

- 基準ブランチから切り分け用 branch を作る
- A/B/C の検証はその branch 上で実施する
- 原因が見えたら、その branch は証跡用として残す
- 本修正は基準ブランチから別 branch を切って実施する

推奨 branch 名の例:

- `investigation/ep6-log-isolation`
- `investigation/waveform-corruption-log-trigger`

本修正 branch 名の例:

- `fix/ep6-reassembly-timing`
- `fix/async-log-decoupling`

避けるべきこと:

- 切り分け branch のまま本修正まで進めること
- 一時的な `log off` 差分を本修正に混ぜること

## 12. 推奨 commit の切り方

### 12.1 最小構成

最も管理しやすい切り方は以下。

1. 切り分け branch を作成
2. `Version A` を 1 commit
3. `Version B` を 1 commit
4. `Version C` を 1 commit
5. 検証結果は docs または作業メモに記録

この形なら、どの段階で改善したかを commit 単位で追える。

### 12.2 commit の積み方

- `Version A` commit
  - `Dialog1_Main.cpp` の EP6 hot-path log 抑止のみ
- `Version B` commit
  - `AnalogBoard_TestAppDlg.cpp` の file logger 抑止のみ
- `Version C` commit
  - `AnalogBoard_Dll.cpp` の `OutputDebugStringA(...)` 抑止のみ

重要:

- B は A の上に積む
- C は B の上に積む
- 1 commit に複数の切り分け意図を混ぜない

### 12.3 実作業の流れ

1. 基準ブランチを最新化する
2. 切り分け用 branch を作成する
3. `Version A` を実装して commit する
4. A の build と実機試験を行い、結果を保存する
5. 改善しなければ A の上に `Version B` を積んで commit する
6. B の build と実機試験を行い、結果を保存する
7. 改善しなければ B の上に `Version C` を積んで commit する
8. C の build と実機試験を行い、結果を保存する

### 12.4 原因確定後の扱い

原因が確定したら、切り分け branch の差分をそのまま本修正に使わない。

推奨手順:

1. 基準ブランチに戻る
2. 本修正用 branch を新しく切る
3. 切り分け結果を根拠に、根本原因への修正だけを実装する
4. 本修正後、必要なら通常 logging 条件で再検証する

### 12.5 補足

- `Version A/B/C` は診断パッチであり、完成形ではない
- `A で改善した` 場合でも、単に log を消すのが本修正とは限らない
- 本当に直すべき対象は、EP6 再構成処理、同期 I/O 設計、または frame boundary 処理である可能性がある

