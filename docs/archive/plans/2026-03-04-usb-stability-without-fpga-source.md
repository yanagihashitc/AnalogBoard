# USB データ取得・書き込み安定性改善プラン（FPGA ソース不要版）

## 概要

[元プラン](./2026-03-02-usb-acquisition-stability.md) から、**FPGA ファームウェアのソースコードが入手できない前提で実施可能な改修のみ** を抽出・再編成したサブセットプラン。

FPGA 側のレジスタマップ・ビット定義・パケットフォーマット・DDR 補正値（`+32`）・16K アライメント等は **現行仕様を変更せずそのまま使用** する。

### 元プランとの関係

- 本プランは元プランの Phase 0〜5 から「ホスト側のみで完結する改修」を抽出したもの
- FPGA ソースコード入手後に元プランの残項目（mutex 分離の安全性確認、FPGA プロトコル依存の最適化等）を追加実施する
- 元プランの Phase 6（KPI 判定）は本プラン完了後にも適用可能

## スコープ

### In Scope

- ホスト側（DLL + TestApp）のみで完結するバグ修正・改善
- FPGA プロトコル仕様を一切変更しない範囲での安定性向上
- 計測基盤の導入と baseline 取得

### Out of Scope

- FPGA ファームウェア・USB プロトコル仕様の変更
- EP2/EP4 パケットフォーマットの変更
- FPGA レジスタマップ・ビット定義の変更
- `DDRWaveBytes += 32` 補正値や `0x4000`（16K）アライメントの変更
- **EP6 の EP2/EP4 Mutex 分離**（後述: FPGA ソース入手後に実施）
- UI/画面設計の刷新

### 保留項目（FPGA ソース入手後に実施）

| 項目 | FPGA ソースが必要な理由 | 元プラン参照 |
| --- | --- | --- |
| EP6 を EP2/EP4 Mutex から分離 | FPGA が EP2/EP4 と EP6 を同時に扱えるか確認が必要 | Phase 1 原因1 |
| DDR 補正値 (`+32`) の見直し | FPGA の DDR 書き込み単位・パディング仕様の確認が必要 | — |
| EP6 チャンクサイズ（16K アライメント）最適化 | FPGA の EP6 転送単位・DDR 読み出し単位の確認が必要 | — |
| EP4 512 バイトフォーマットの先頭 256 バイトの活用 | FPGA のダミーデータ送信仕様の確認が必要 | — |

## ステータス

| Phase | 内容 | 状態 |
| --- | --- | --- |
| H0 | 現状可視化・ログ計測 | pending |
| H1a | DLL 層バグ修正（Mutex 分離を除く） | pending |
| H1b | Save Path バリデーション強化 | **done** |
| H2 | Producer/Consumer パイプライン導入 | pending |
| H3 | スレッド安全性・堅牢性向上 | pending |
| H4 | コード構造改善・状態遷移明確化 | pending |
| H5 | テスト戦略・耐久テスト | pending |

> Phase 番号に `H` (Host-only) プレフィックスを付与し、元プランと区別する。

## 実施ログ運用

- 元プランと同一の process_log (`docs/plans/2026-03-02-usb-acquisition-stability-process_log.md`) に追記する
- エントリには `[Host-only]` タグを付与して識別

---

## 対象となる根本原因（FPGA ソース不要で修正可能）

### 原因2: USB 読み取りとファイル書き込みが同一スレッドで直列実行（対応可能）

`LoopTestProcessThread_EP6_GetData` で EP6 データ読み取り → ファイル書き込みが単一スレッド内で直列実行されている。ファイル I/O 中に USB 読み取りが停止し、FPGA DDR バッファのオーバーフローリスクがある。

→ **H2 で Producer/Consumer パイプラインを導入して解消**

### 原因3: OVERLAPPED イベントハンドルのリーク（対応可能）

EP2/EP4/EP6 の各関数で `CreateEvent()` 後に `CloseHandle()` していない。

→ **H1a で修正**

### 原因4: EP6 で毎回 4MB malloc/free（対応可能）

`EP6_GetData()` が呼ばれるたびに 4MB の一時バッファを malloc/free している。

→ **H1a で修正**

### 原因5: スレッド制御フラグが非アトミック（対応可能）

`g_bEP24LoopFlag`, `g_bEP6ThreadFlag`, `g_bStartSampling` が `std::atomic` でない。

→ **H3 で修正**

### 原因6: Save Path の事前バリデーション不足（対応済み）

→ **H1b（PR-02b）で対応済み**

### 原因1: EP6 が EP2/EP4 Mutex を不要に占有（**保留**）

EP6 は Bulk エンドポイント (0x86)、EP2/EP4 は Interrupt エンドポイント (0x02/0x84) であり、USB 仕様上は独立動作可能。しかし FPGA 内部で EP2/EP4 と EP6 が排他制御されている可能性を排除できないため、**FPGA ソース入手後に安全性を確認してから実施する**。

> **代替緩和策（H1a で実施）**: Mutex の `INFINITE` 待機にタイムアウトを設定し、デッドロック時の検出・復帰を可能にする。

---

## 改善フェーズ

### Phase H0: 現状可視化・ログ計測（最優先・前提条件）

対象: `Dialog1_Main.cpp`, `AnalogBoard_Dll.cpp`

改善前のベースラインを記録し、ボトルネックを特定する。

- **EP6 読み取り時間の計測**: `EP6_GetData` の呼び出しごとに経過時間をログ
- **ファイル書き込み時間の計測**: `SaveWaveDataToFile` の呼び出しごとに経過時間をログ
- **転送サイズ・タイムアウト回数の記録**: `USB_ERR_TRANSFER_TIMEOUT` 発生頻度を記録
- **DDR ステータスポーリング回数の記録**: DDR 書き込み完了待ちのループ回数をログ
- **Mutex 待機時間の計測**: EP2/EP4/EP6 各関数の mutex 待機時間をログ（原因1 の影響度を定量化）

> **重要**: Mutex 待機時間の計測は、FPGA ソース入手前に原因1 の影響度を把握するための判断材料となる。待機時間が長い場合、mutex 分離の優先度が上がる。

### Phase H1a: DLL 層バグ修正（Mutex 分離を除く）

対象: `AnalogBoard_Dll.cpp`, `AnalogBoard_Dll.h`

| 修正項目 | 詳細 | FPGA 依存 |
| --- | --- | --- |
| OVERLAPPED イベントハンドルのリーク修正 | 各関数の末尾に `CloseHandle(ovLap.hEvent)` を追加 | なし |
| OVERLAPPED 構造体の適切な初期化 | `ZeroMemory(&ovLap, sizeof(ovLap))` で初期化してから `hEvent` を設定 | なし |
| EP6 一時バッファの事前確保 | 毎回 malloc/free → クラスメンバとしてライフタイム管理 | なし |
| Mutex 待機タイムアウト追加 | `INFINITE` → 適切なタイムアウト値（例: 5000ms）に変更 | なし |
| `SaveWaveDataToFile` の失敗検知強化 | 書き込み失敗時に即エラー遷移 | なし |

> **注**: EP6 の mutex 分離（原因1）は本フェーズでは実施しない。代わりに mutex タイムアウトを設定してデッドロック耐性を向上させる。

### Phase H1b: Save Path バリデーション強化（**完了済み**）

対象: `Dialog1_Main.cpp`, `Dialog1_Main.h`

PR-02b にて TDD で実装完了。`ValidateSavePath` によるディレクトリ存在・書き込み権限・パス安全性の事前検証済み。

### Phase H2: Producer/Consumer パイプライン導入

対象: `Dialog1_Main.cpp` の `LoopTestProcessThread_EP6_GetData` (line 1100-1720)

USB 読み取りとファイル書き込みを分離し、I/O 並列化によって DDR オーバーフローリスクを低減する。

```
改善後のアーキテクチャ:

[USB Reader Thread]                  [File Writer Thread]
  EP6_GetData(buf)                     dequeue(dataChunk)
       |                                    |
       v                                    v
  enqueue(dataChunk) ---[Queue]--->   SaveWaveDataToFile()
       |                                    |
       v                                    v
  EP4_GetData(status)                 PublishWaveFilePair()
  (DDR status check)
```

- **スレッドセーフキュー**: `std::mutex` + `std::condition_variable` によるブロッキングキュー（固定サイズ、バックプレッシャー付き）
- **USB Reader スレッド**: EP6 データ読み取りと EP4 ステータスチェックに専念
- **File Writer スレッド**: ファイル作成・書き込み・Publish に専念
- **FPGA プロトコルへの影響**: なし（EP6/EP4 の呼び出し順序・パラメータは変更しない）

> **注意**: Reader スレッド内の DDR ポーリングロジック（`RegGet_DDRWriteEnd`, `RegGet_DDRWaveCnt`, `DDRWaveBytes += 32`, 16K アライメント）は**現行ロジックをそのまま移植**する。これらの値は FPGA 仕様に依存するため変更しない。

### Phase H3: スレッド安全性と堅牢性の向上

対象: `Dialog1_Main.cpp`

| 修正項目 | 詳細 | FPGA 依存 |
| --- | --- | --- |
| スレッド制御フラグを `std::atomic<int>` に変更 | `g_bEP24LoopFlag`, `g_bEP6ThreadFlag`, `g_bStartSampling` | なし |
| USB 転送リトライ機構 | `USB_ERR_TRANSFER_TIMEOUT` に対する段階的リトライ（短い backoff 付き、最大 N 回） | なし |
| DDR ポーリングの改善 | `Sleep(0)` busy loop → `Sleep(1)` + 適応的 delay | なし（ポーリング間隔のみ。判定ロジックは不変） |
| DDR 完了タイムアウトの設定値化 | 200 × 10ms = 2秒 → 設定可能なタイムアウト値（デフォルト 10秒） | なし（タイムアウト値のみ。判定ロジックは不変） |
| スレッド終了待機の適切化 | `WaitForSingleObject(threadHandle, timeout)` でスレッド終了を確認 | なし |

> **注意**: DDR ポーリングの判定条件（`RegGet_DDRWriteEnd` のビット判定、`RegGet_DDRWaveCnt` の読み取り）は FPGA 仕様に依存するため変更しない。変更するのは「ポーリング間隔」と「タイムアウト上限」のみ。

### Phase H4: コード構造改善・状態遷移明確化

対象: `Dialog1_Main.cpp` (3400行)

- `LoopTestProcessThread_EP6_GetData` の分解: 620行の関数を `WaveAcquisitionEngine` クラスに抽出
  - `WaveAcquisitionEngine::Start()` / `Stop()` / `GetStatus()`
  - 状態管理、バッファ管理、ファイル管理を責務ごとに分離
- 状態遷移の明確化: `Idle → Sampling → Draining → Publish → Completed/Error`
- グローバル変数の削減: `pEp6DataBuf1/2`, `pEp2DataBuf`, `pEp4DataBuf` 等をクラスメンバに移動
- ファイルサイズの目標: 3400行 → 複数ファイルに分割して各1000行以下

> **注意**: FPGA との通信シーケンス（EP2 コマンド送信 → EP4 応答待ち → EP6 データ取得の順序）は**厳密に維持する**。リファクタリングはコード構造の改善のみで、通信プロトコルには一切手を加えない。

### Phase H5: テスト戦略・耐久テスト

#### 実施可能なテストケース

| ID | テスト種別 | 入力/注入条件 | 期待結果 | FPGA 依存 |
| --- | --- | --- | --- | --- |
| T1 | Unit | `EP6_GetData` 成功、Queue 空きあり | Reader→Writer で全チャンク受け渡し成功 | なし |
| T2 | Unit | Queue 容量 `1`、Writer を 500ms 遅延 | Enqueue が timeout し `USB_ERR_QUEUE_FULL_TIMEOUT` 遷移 | なし |
| T3 | Unit | `USB_ERR_TRANSFER_TIMEOUT` を 1-2 回注入 | backoff 後に再取得して継続 | なし |
| T4 | Unit | `USB_ERR_TRANSFER_TIMEOUT` を連続注入（N+1 回） | retry 上限超過で Error 遷移 | なし |
| T5 | Integration | `SaveWaveDataToFile` で書き込み失敗注入 | 即時停止し `.tmp` を隔離保持 | なし |
| T6 | Integration | 取得中に Stop 要求 | `Draining -> Publish/Completed` へ遷移 | なし |
| T8 | Regression | 既存 `wave_file_publish` 一式実行 | low/high 出力順序互換維持 | なし |
| T9 | Unit | `ValidateSavePath` に存在するパスを渡す | 成功を返す | なし |
| T10 | Unit | `ValidateSavePath` に存在しないパスを渡す | `USB_ERR_OUTPUT_PATH_NOT_FOUND` | なし |
| T11 | Unit | `ValidateSavePath` に読み取り専用パスを渡す | `USB_ERR_OUTPUT_PATH_NOT_WRITABLE` | なし |
| T12 | Unit | `ValidateSavePath` に `..` を含むパスを渡す | `USB_ERR_INVALID_OUTPUT_PATH` | なし |
| T13 | Integration | Save Path 不正のまま Set Parameters を押す | エラーメッセージが表示される | なし |

#### FPGA ソース入手後に追加するテスト

| ID | テスト種別 | 内容 | 必要な FPGA 知識 |
| --- | --- | --- | --- |
| T7 | Soak | 実時間 8 時間連続運転 | DDR バッファ限界値、FPGA リカバリ挙動 |
| T-M1 | Integration | EP6 mutex 分離後の並行 EP2/EP4 + EP6 通信 | FPGA の並行転送サポート有無 |

---

## インターフェース設計（元プランと同一）

H2-H4 で使用するインターフェースは元プランの設計をそのまま採用する。

### 型定義

```cpp
enum class EngineStatus {
    Idle,
    Sampling,
    Draining,
    Publish,
    Completed,
    Error
};

struct AcquisitionConfig {
    DWORD queueCapacity;         // default: 8
    DWORD queueWaitTimeoutMs;    // default: 200
    DWORD ddrPollTimeoutMs;      // default: 10000
    DWORD stopWaitTimeoutMs;     // default: 5000
    INT maxUsbRetryCount;        // default: 3
};

struct WaveChunk {
    std::vector<BYTE> payload;
    ULONG frameSizeLow;
    ULONG frameSizeHigh;
    INT waveCount;
};
```

### `WaveAcquisitionEngine` 公開 API

- `INT Start(const AcquisitionConfig& config);`
- `INT Stop(DWORD timeoutMs = 5000);`
- `EngineStatus GetStatus() const;`

### `BlockingQueue<WaveChunk>` 契約

- `bool Enqueue(WaveChunk&& chunk, DWORD timeoutMs);`
- `bool Dequeue(WaveChunk& out, DWORD timeoutMs);`

### エラーコード追加方針

- 既存: `USB_ERR_TRANSFER_TIMEOUT`, `USB_ERR_WRITE_FILE` 等を継続利用
- 追加:
  - `USB_ERR_THREAD_STOP_TIMEOUT`
  - `USB_ERR_QUEUE_FULL_TIMEOUT`
  - `USB_ERR_INVALID_OUTPUT_PATH`
  - `USB_ERR_OUTPUT_PATH_NOT_FOUND`
  - `USB_ERR_OUTPUT_PATH_NOT_WRITABLE`
  - `USB_ERR_MUTEX_TIMEOUT` — Mutex タイムアウト検出時

---

## セキュリティ

元プランと同一。ローカル処理中心だが以下を実施:

- 出力先パスは許可ディレクトリ配下のみ、`..` を含む相対遷移は拒否
- ファイル名に制御文字/予約名を含む場合は保存拒否
- デバッグログは生波形データを出力せず、サイズ・件数・ハッシュ・エラーコードのみ記録
- `.tmp` は `Publish` 成功時のみ最終確定し、失敗時は隔離ディレクトリへ退避

## 依存関係

| 区分 | 対象 | バージョン方針 | 導入理由 |
| --- | --- | --- | --- |
| H1a-H4 | C++ 標準ライブラリ (`<atomic>`, `<thread>`, `<mutex>`, `<condition_variable>`, `<chrono>`) | 既存 MSVC / C++17 準拠 | スレッド安全化、待機制御、計測 |
| H1a-H4 | Win32 API (`WaitForSingleObject`, `CreateEvent`, `CloseHandle`) | 既存 SDK 準拠 | 既存 DLL 実装と整合 |
| H5 | 既存 UnitTest 実行基盤 (`AnalogBoard_UnitTest`) | 現行ソリューションを維持 | 回帰防止 |

---

## 移行・ロールバック計画

- Step 1: H1a を先行適用（DLL バグ修正のみ、feature flag 不要）
- Step 2: H2-H4 は `UseAsyncWriter` フラグで旧経路/新経路を切替可能に実装
- Step 3: 段階展開（開発機 → 検証機 → 長時間連続運転）

**ロールバック条件・手順は元プランと同一。**

---

## 実施順序と影響範囲

| Phase | 影響ファイル | リスク | 効果 |
| --- | --- | --- | --- |
| H0 | Dialog1_Main.cpp, DLL | 低 | ベースライン取得 + Mutex 影響度定量化 |
| H1a | DLL (.cpp/.h) | 低 | ハンドルリーク解消、バッファ最適化、Mutex タイムアウト導入 |
| H1b | Dialog1_Main.cpp | 低 | Save Path 事前検証でデータロス防止（**完了済み**） |
| H2 | Dialog1_Main.cpp | 中 | I/O 並列化（最大効果） |
| H3 | Dialog1_Main.cpp | 低 | レースコンディション除去、リトライ機構 |
| H4 | 新規ファイル + Dialog1_Main | 中 | 保守性・状態管理向上 |
| H5 | テストプロジェクト | 低 | 回帰防止 |

**推奨実施順**: H0 → H1a → H2 → H3 → H4 → H5

> **注**: 元プランでは Phase 1 の「EP6 Mutex 分離」が最大効果とされていたが、本プランではこれを保留するため、**H2 の I/O 並列化が最大効果**となる。H0 の Mutex 待機時間計測で原因1 の影響度を定量化し、FPGA ソース入手の優先度判断に活用する。

---

## 実装タスク分解（PR 単位）

| PR | 主目的 | 主要変更ファイル | 依存 | DoD（完了条件） | 検証 |
| --- | --- | --- | --- | --- | --- |
| PR-H01 | 計測基盤導入（H0） | `Dialog1_Main.cpp`, `AnalogBoard_Dll.cpp` | なし | EP6 読取時間、書込時間、timeout 回数、DDR ポーリング回数、**Mutex 待機時間**がログ出力される | Debug ビルド成功、計測ログが 1 取得サイクルで出力 |
| PR-H02 | DLL バグ修正（H1a） | `AnalogBoard_Dll.cpp`, `AnalogBoard_Dll.h` | PR-H01 | OVERLAPPED ハンドルリーク解消、EP6 バッファ再利用化、Mutex タイムアウト設定 | 既存通信シーケンスで回帰なし |
| ~~PR-02b~~ | ~~Save Path バリデーション（H1b）~~ | — | — | **完了済み** | T9-T13 pass |
| PR-H03 | Queue 実装と I/F 導入（H2 前半） | `WaveAcquisitionEngine.h` (新規), `WaveAcquisitionEngine.cpp` (新規) | PR-H02 | `AcquisitionConfig`, `WaveChunk`, `BlockingQueue`, `EngineStatus` がコンパイル可能 | UnitTest に Queue 正常/timeout ケース追加し pass |
| PR-H04 | Reader/Writer 分離（H2 後半） | `Dialog1_Main.cpp`, `WaveAcquisitionEngine.cpp` | PR-H03 | `LoopTestProcessThread_EP6_GetData` の主要ロジックが Engine 経由に移行し、I/O 直列実行が解消 | 遅延注入時も取得継続 |
| PR-H05 | スレッド安全化と retry 改善（H3） | `Dialog1_Main.cpp`, `WaveAcquisitionEngine.cpp` | PR-H04 | 制御フラグ atomic 化、retry/backoff、stop 待機 timeout が実装される | timeout 注入テストで期待動作 |
| PR-H06 | 状態遷移明確化と責務分離（H4） | `Dialog1_Main.cpp`, `WaveAcquisitionEngine.*` | PR-H05 | 状態遷移が追跡可能、ファイル分割で各 1000 行以下 | stop 中断・publish 完了・error 遷移テスト pass |
| PR-H07 | テスト拡張（H5） | `tests/`, `AnalogBoard_UnitTest/*` | PR-H06 | T1-T6, T8-T13 の unit/integration テストが整備 | UnitTest 全件 pass |

### FPGA ソース入手後に追加する PR

| PR | 主目的 | 前提条件 |
| --- | --- | --- |
| PR-F01 | EP6 Mutex 分離（元プラン Phase 1 原因1） | FPGA ソースで EP2/EP4 と EP6 の独立動作を確認済み |
| PR-F02 | Mutex 分離後の統合テスト | PR-F01 完了 |
| PR-F03 | DDR パラメータ最適化（補正値、アライメント） | FPGA の DDR 仕様を確認済み |
| PR-KPI | KPI 判定（元プラン Phase 6） | PR-H07 + PR-F02 完了 |

### PR ごとの最小検証コマンド（Windows）

```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat x64\Debug\AnalogBoard_UnitTest.exe"
```

---

## KPI 判定（参考: 元プランと同一基準）

本プランの改修のみでも以下の KPI を計測し、改善度を評価する。

**受け入れ基準:**

- 連続 1000 回取得でタイムアウト起因の失敗率が **0.1% 未満**
- 中断後の low/high ペア欠損ゼロ、破損ゼロ
- 取得スループットと最大遅延の前後比較で改善を確認

> **注**: EP6 Mutex 分離なしでは KPI を完全達成できない可能性がある。H0 の計測結果で Mutex 待機が支配的なボトルネックである場合、FPGA ソース入手を急ぐ根拠となる。

---

## 関連ドキュメント

- [元プラン: USB データ取得・書き込み安定性改善プラン（全体版）](./2026-03-02-usb-acquisition-stability.md)
- [データ取得中断・タイムアウト安定化プラン](./2026-03-02-data-acquisition-timeout-stability-plan.md)
- [Wave Output Format Handover (2026-03-02)](./2026-03-02-wave-output-handover.md)
- [USB Data Acquisition Stability Process Log](./2026-03-02-usb-acquisition-stability-process_log.md)
