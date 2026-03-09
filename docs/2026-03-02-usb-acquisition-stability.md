# USB データ取得・書き込み安定性改善プラン

## 概要

USB データ取得中の書き込み中断・タイムアウトエラーの根本原因を修正し、アーキテクチャを改善してデータパイプラインの安定性を向上させる。

2026-03-09 の追加調査により、`Version A`（EP6 hot-path の高頻度 `PrintLog(...)` を停止）で waveform が正常化した。また、`sys_app` の `preview` 系 consumer は `.tmp` ではなく完成済み `.bin` のみを読むことを確認した。このため、本プランでは「EP6 取得ループが UI/file/publish に近すぎること」と「下流 consumer による `.bin` 読み取り負荷や publish 競合で acquisition を止めないこと」を追加の重要要件として扱う。

同じ調査で、Phase 0 の「呼び出しごとの詳細ログ出力」は計測対象そのものを乱す intrusive instrumentation であることが分かった。したがって Phase 0 は未完了のままとし、hot-path ではメモリ集計のみを行い、取得終了後に要約を出力する軽量計測へ切り替える。

## 前提方針（FPGA Firmware A 非改変）

- 原則として `FPGA_FW/ANA_20250129_restored`（Firmware A）には変更を加えない
- 本プランの改修対象はホスト側（`AnalogBoard_Dll` / `AnalogBoard_TestApp` / テスト / ドキュメント）に限定する
- FPGA 変更が必要な事項は本プランの実装対象から除外し、別タスクとして管理する

## スコープ

### In Scope

- Phase 0-5 で定義した DLL 修正、取得/書き込み分離、状態遷移明確化、耐久テスト追加
- `sys_app` preview のような `.bin` consumer が動作中でも acquisition を継続できる host 側安定化
- `LoopTestProcessThread_EP6_GetData` の責務分解と `WaveAcquisitionEngine` 導入
- KPI による改善判定（失敗率、欠損/破損、スループット、遅延）
- FPGA Firmware A の既存レジスタ/既存通信シーケンスを維持したままのホスト側安定化

### Out of Scope

- 波形フォーマット仕様そのものの変更（`fl/fh` の low/high 意味変更を含む）
- UI/画面設計の刷新や操作フロー変更
- FPGA ファームウェア・USB プロトコル仕様の変更
- FPGA レジスタマップの追加/再割り当て（例: 新規デバッグレジスタ公開）
- Rust 実装の即時本採用（Phase 6 判定を通過した場合のみ次イテレーションで検討）

## ステータス


| Phase | 内容                         | 状態      |
| ----- | -------------------------- | ------- |
| 0     | 現状可視化・ログ計測                 | completed |
| 1     | DLL 層 致命的バグ修正              | pending |
| 1.5   | preview consumer 耐性の短期安定化     | pending |
| 2     | Reader/Writer/Publisher パイプライン導入 | pending |
| 3     | スレッド安全性・堅牢性向上                | pending |
| 4     | コード構造改善・状態遷移明確化              | pending |
| 5     | テスト戦略・耐久テスト                  | pending |
| 6     | KPI 判定・オプション検討               | pending |


## 実施ログ運用（process_log）

- 本プランの進捗・判断・計測結果は、`docs/2026-03-02-usb-acquisition-stability-log.md` に**逐次追記**する（Phase 0 着手時に新規作成）
- 各 Phase の着手時/完了時に最低1エントリを記録する
- 記録項目は「日時、Phase/PR、実施内容、結果、課題/次アクション」を必須とする
- Phase 1 以降の判断は process_log 上の Phase 0 計測結果（baseline）を参照して実施する

---

## TDD 実施方針（全Phase共通）

- 本プランの実装は Red-Green-Refactor を必須とし、**実装前に failing test を追加**する
- 不具合修正（例: mutex 競合、Save Path 不正、timeout/retry）は、再現テストを先に追加してから修正する
- 各 PR の DoD には「新規 failing test が修正前に失敗し、修正後に成功すること」を含める
- テスト実行は Windows 環境で `scripts\run_with_vsdevcmd.bat` を経由して行う

---

## 根本原因分析

コード調査により、以下の **7つの主要な根本原因** を特定した。

### 追加観測（2026-03-09 investigation）

- `Version A`（EP6 hot-path の高頻度 `PrintLog(...)` を停止）で waveform が比較版に近い状態まで改善した
- `sys_app` の `preview` は `.tmp` を触らず、完成済み `.bin` のみを `LatestFileReader` 経由で読む
- それでも preview 実行中に acquisition が停止したため、取得ループが file I/O / publish / consumer 側負荷に過度に影響されている

この観測により、「logging 負荷がトリガー」「publish/write-side の失敗や遅延を acquisition fatal にしている」の2点を、実測で優先度の高い問題として扱う。

### 原因1: EP6 が EP2/EP4 Mutex を不要に占有（最重大）

[AnalogBoard_Dll.cpp](../../AnalogBoard_Dll/AnalogBoard_Dll.cpp) の `EP6_GetData()` が `m_hEP2EP4Mutex` を取得し、**数百MBのバルク転送が完了するまで保持し続ける**。

```cpp
// EP6_GetData: line 434 - mutex取得
if (WAIT_OBJECT_0 != WaitForSingleObject(m_hEP2EP4Mutex, INFINITE))

// line 449-472 - 4MBチャンク×数十回のループ全体でmutex保持
while (ulRecvDataSize < DataSizeCount) {
    m_pInEndpt6->XferData(pOneTimeBuffer, lOneTimeSize, FALSE);
    // ...
}

// line 477 - ループ完了後にようやく解放
ReleaseMutex(m_hEP2EP4Mutex);
```

EP6 は Bulk エンドポイント (addr: 0x86)、EP2/EP4 は Interrupt エンドポイント (addr: 0x02/0x84) であり、USB 仕様上 **別エンドポイントは独立して動作可能**。EP6 が mutex を占有している間、EP4 によるDDRステータスチェックや EP2 による制御コマンド送信が全てブロックされる。

### 原因2: USB読み取りとファイル書き込みが同一スレッドで直列実行

[Dialog1_Main.cpp](../../AnalogBoard_TestApp/Dialog1_Main.cpp) の `LoopTestProcessThread_EP6_GetData` (line 1406-2120, 約715行) で、EP6 データ読み取り → ファイル書き込み → EP6 データ読み取り... のサイクルが **単一スレッド内で直列実行** されている。ファイル I/O 中は USB 読み取りが停止する。

```
[USB Read] → [File Write (blocking)] → [USB Read] → ...
             ^^^^^^^^^^^^^^^^^^^^^^^^
             この間 USB は完全停止
```

**FPGA 側のバックプレッシャーチェーン（FW ソース調査により確認）:**

FPGA ファームウェアはバックプレッシャー方式でデータフローを制御しており、即座にデータロスが起きるわけではない。PC 側が EP6 読み取りを停止した場合、以下の段階的な影響が発生する:

```
PC 側 EP6 読み取り停止
  → EP6 FIFO 使用量が増加（16KW FIFO） [BUFFER_CTRL.vhd]
    → `DDR_RD_EP6_REQ_DDR` が deassert（`wrusedw` しきい値超過）
      → DDR3_RCTRL の `RD_REQ_DDR` が止まり DDR 読み出し進行が停止
        → DDR3_WCTRL 側の書き込みは継続
          → `DDR_FULL` 到達で MEAS_CTRL が終了シーケンス（MEAS→WAIT→RD_WAIT）へ遷移
```

1回の計測データが DDR 容量に収まる場合、ファイル I/O 遅延で即座にデータロスは起きない。しかし **オートモードで連続的にトリガーが発生する場合**、前回の DDR データが読み出し切れないまま次の計測が開始されると DDR 領域が枯渇し、最終的に `DDR_FULL` 契機で計測終了シーケンスへ遷移する（**連続計測時の DDR 領域枯渇リスク**）。Producer/Consumer パイプラインにより EP6 読み取りを常時継続させることで、このリスクを大幅に低減できる。

### 原因3: OVERLAPPED イベントハンドルのリーク

EP2/EP4/EP6 の各関数で `CreateEvent()` を呼んでいるが、`CloseHandle()` していない。長時間運用でハンドルリークが蓄積し、OS リソース枯渇を招く。

```cpp
// EP2_SendData line 318
outOvLap.hEvent = CreateEvent(NULL, false, false, _T("CYUSB_OUT"));
// EP4_GetData line 372
inOvLap.hEvent = CreateEvent(NULL, false, false, _T("CYUSB_IN"));
// EP6_GetData line 439
inOvLap.hEvent = CreateEvent(NULL, false, false, _T("CYUSB_IN"));
// --> いずれも CloseHandle なし
```

### 原因4: EP6 で毎回 4MB malloc/free

`EP6_GetData()` が呼ばれるたびに 4MB の一時バッファを `malloc` → `free` している。高頻度呼び出しでメモリフラグメンテーションを引き起こす。

### 原因5: スレッド制御フラグが非アトミック

```cpp
// Dialog1_Main.cpp line 26-28
INT g_bEP24LoopFlag = 0;  // plain INT, no synchronization
INT g_bEP6ThreadFlag = 0;
INT g_bStartSampling = 0;
```

複数スレッドから読み書きされる制御フラグが `std::atomic` でもなく `volatile` でもないため、コンパイラ最適化によるレースコンディションが起こりうる。

### 原因6: Save Path の事前バリデーション不足

Set Parameters 時に Save Path の **空文字チェックしか行っていない**。ディレクトリ存在確認・書き込み権限チェックが無いため、不正なパスのまま取得が開始されうる。

```cpp
// Dialog1_Main.cpp line 2511 付近 - 空文字チェックのみ
if (packetConfig->SavePath.GetLength() == 0)
{
    strTmp.Format(_T("SavePath cannot be NULL"));
    // ...
    return -1;
}
```

特にオートモードでは、機械側は FPGA 外部トリガーでアプリとは独立して動作するため、アプリ側でパス不正に気づいた時点では既に計測が完了し細胞もロスしている。ファイル作成は FPGA 計測開始後に行われる（line 1604: `CreateWaveDataFile`）ため、**「機械は動いたがデータが保存されていない」という深刻なデータロスが無警告で発生する**。事前バリデーションでユーザーが機械を動かす前に気づけるようにすることが重要。

### 原因7: publish / consumer 側の失敗を acquisition fatal にしている

`.tmp` への書き込みと `.bin` への publish が `LoopTestProcessThread_EP6_GetData` と強く結合しており、publish 失敗や consumer 側の `.bin` 読み取り負荷が acquisition 停止に直結している。

- `FlushCloseAndPublishWavePair()` の失敗が `ErrExit = TRUE` に直結する
- `sys_app` preview は `.tmp` ではなく `.bin` のみを読むが、それでも acquisition が停止する実測がある
- 取得スレッドが write / close / rename / rollback の遅延や失敗を直接抱え込んでいる

必要なのは「publish failure non-fatal」と「consumer が `.bin` を読んでいても acquisition 継続」という契約の明文化である。

---

## インターフェース設計（Phase 2-4 で固定）

実装着手前に、取得エンジンとキューの契約を以下に固定する。

### 型定義（案）

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
    DWORD queueCapacity;         // default: 8,   valid: 1-64
    DWORD queueWaitTimeoutMs;    // default: 200,  valid: 50-10000
    DWORD ddrPollTimeoutMs;      // default: 10000, valid: 1000-60000
    DWORD stopWaitTimeoutMs;     // default: 5000, valid: 1000-30000
    INT maxUsbRetryCount;        // default: 3,    valid: 0-10 (0=retry disabled)
};

struct WaveChunk {
    std::vector<BYTE> payload;   // max: EP6_ONETIME_MAX_SIZE (4MB) * chunk count
    ULONG frameSizeLow;
    ULONG frameSizeHigh;         // frameSizeLow/High は FPGA WAVE_WR_CNT から算出
    INT waveCount;
};
```

### `WaveAcquisitionEngine` 公開 API

- `INT Start(const AcquisitionConfig& config);`
  - Returns: `USB_SUCCESS` / `USB_ERR_*`
  - 前提: `Idle` 状態でのみ呼び出し可。非 `Idle` 状態で呼び出した場合は `USB_ERR_INVALID_STATE` を返却
  - `config` のパラメータが有効範囲外の場合は clamp して警告ログを出力（呼び出し自体は成功）
- `INT Stop(DWORD timeoutMs = 5000);`
  - Returns: `USB_SUCCESS` / `USB_ERR_THREAD_STOP_TIMEOUT`
  - 振る舞い: `Sampling/Draining` から停止要求し、Writer スレッド終端まで待機
  - `Idle` / `Completed` / `Error` 状態で呼んだ場合は即座に `USB_SUCCESS` を返却（冪等）
- `EngineStatus GetStatus() const;`
  - 監視 UI/ログに対して状態を返却

### 状態遷移と復帰パス

```
Idle ──Start()──> Sampling ──DDR_WR_END──> Draining ──DDR_RD_END──> Publish ──成功──> Completed
  ^                  |            |            |                        |
  |                  |            |            |                        |
  +──── Stop() ─────+── Error <──+── Stop() ──+                  Error <── 失敗
  |                                                                    |
  +──────────────────────── Stop() ────────────────────────────────────+
```

- **`Error` → `Idle` 復帰**: `Stop()` 呼び出しで内部リソース（スレッド・キュー・バッファ）をクリーンアップし `Idle` に遷移
- **USB 物理切断時**: `XferData` の戻り値で検知し `USB_ERR_DEVICE_DISCONNECTED` で `Error` 遷移。Reader/Writer 両スレッドを停止

### `BlockingQueue<WaveChunk>` 契約

- `bool Enqueue(WaveChunk&& chunk, DWORD timeoutMs);`
  - `timeoutMs` 超過時は `false` を返し、呼び出し側は `USB_ERR_QUEUE_FULL_TIMEOUT` へ変換
- `bool Dequeue(WaveChunk& out, DWORD timeoutMs);`
  - 停止要求時は待機解除し `false` を返却
- キュー上限時ポリシー: 既定は待機（backpressure）。連続 timeout はエラー遷移

### エラーコード追加方針

- 既存: `USB_ERR_TRANSFER_TIMEOUT`, `USB_ERR_WRITE_FILE` などを継続利用
- 追加候補:
  - `USB_ERR_INVALID_STATE` — 非 Idle 状態での `Start()` 呼び出し
  - `USB_ERR_DEVICE_DISCONNECTED` — USB デバイスの物理的切断を検知
  - `USB_ERR_THREAD_STOP_TIMEOUT`
  - `USB_ERR_QUEUE_FULL_TIMEOUT`
  - `USB_ERR_INVALID_OUTPUT_PATH` — Save Path バリデーション失敗時に使用
  - `USB_ERR_OUTPUT_PATH_NOT_FOUND` — ディレクトリ不存在
  - `USB_ERR_OUTPUT_PATH_NOT_WRITABLE` — 書き込み権限なし

## 改善フェーズ

### Phase 0: 現状可視化・ログ計測（最優先・前提条件）

対象: [Dialog1_Main.cpp](../../AnalogBoard_TestApp/Dialog1_Main.cpp), [AnalogBoard_Dll.cpp](../../AnalogBoard_Dll/AnalogBoard_Dll.cpp)

改善前のベースラインを記録し、書き込み遅延起因か USB 起因かを切り分ける。

- **重要な制約（2026-03-09 更新）**: `PrintLog(...)` のような hot-path の高頻度逐次ログは waveform を壊しうるため、Phase 0 の完了条件から除外する
- **計測方式の変更**: hot-path では `PrintLog(...)` を行わず、メモリ上のカウンタ・経過時間集計のみを更新し、取得終了後に要約を1回だけ出力する
- **EP6 読み取り時間の計測**: `EP6_GetData` ごとの経過時間をメモリ集計し、合計/最大/回数を取得終了後に出力
- **ファイル書き込み時間の計測**: `SaveWaveDataToFile` ごとの経過時間をメモリ集計し、合計/最大/回数を取得終了後に出力
- **転送サイズ・タイムアウト回数の記録**: `USB_ERR_TRANSFER_TIMEOUT` 発生頻度と転送サイズ要約を取得終了後に出力
- **DDR ステータスポーリング回数の記録**: DDR 書き込み完了待ちのループ回数をメモリ集計し、取得終了後に出力
- **FPGA DDR レジスタ値の記録**: EP4 経由で取得可能な以下のレジスタ値を各取得サイクルで集計し、取得終了後に要約を出力して DDR 使用状況を可視化する（[REG_CTRL.vhd](../../FPGA_FW/ANA_20250129_restored/RTL/REG_CTRL/REG_CTRL.vhd) で定義）:
  - `WAVE_WR_CNT` (0x000018-0x00001A): DDR 書き込みカウンタ — 書き込み済みデータ量の追跡
  - `WAVE_RD_CNT` (0x00001C-0x00001E): DDR 読み出しカウンタ — 読み出し進捗の追跡、`WR_CNT` との差分で DDR 内滞留量を算出
  - `FPGA_ST` の `DDR_WR_END(bit2)` / `DDR_RD_END(bit3)`: 書き込み終了/読み出し終了タイミングの追跡
- **注記（Firmware A 非改変制約）**: `DDR_LIM_ADDR` は現行の EP4 read パスでは直接取得できないため、本プランでは計測指標に含めない
- **Phase 完了条件の補足**: 軽量計測を有効にしても waveform が壊れないことを確認したうえで baseline を process_log に記録するまでは、Phase 0 を完了扱いにしない
- **2026-03-09 baseline 記録**: `2603091643.log` の 16:47:08 run で `ep6Timeouts=0`, `ep6AvgMs=1`, `ep6MaxMs=32`, `saveMaxMs=16`, `ddrWaitPolls=1`, `maxBacklogBytes=8241120`, `DDR_WR_END=1`, `DDR_RD_END=1` を確認し、対応する waveform が正常取得であることを確認済み

### Phase 1: DLL 層の致命的バグ修正 + Save Path バリデーション（高インパクト・低リスク）

対象: [AnalogBoard_Dll.cpp](../../AnalogBoard_Dll/AnalogBoard_Dll.cpp), [AnalogBoard_Dll.h](../../AnalogBoard_Dll/AnalogBoard_Dll.h), [Dialog1_Main.cpp](../../AnalogBoard_TestApp/Dialog1_Main.cpp)

- **EP6 を EP2/EP4 Mutex から分離（ホスト側のみ）**: EP6 は独立した Bulk エンドポイントなので共有 mutex は不要。EP6_GetData から `WaitForSingleObject(m_hEP2EP4Mutex)` / `ReleaseMutex(m_hEP2EP4Mutex)` を除去（FPGA Firmware A は変更しない）
- **OVERLAPPED イベントハンドルのリーク修正**: 各関数の末尾に `CloseHandle(ovLap.hEvent)` を追加
- **OVERLAPPED 構造体の適切な初期化**: `ZeroMemory(&ovLap, sizeof(ovLap))` で初期化してから `hEvent` を設定
- **EP6 一時バッファの事前確保**: `EP6_GetData` 内の毎回 malloc/free をやめ、クラスメンバとしてライフタイム管理（Connect 時に確保、Disconnect 時に解放）
- **Mutex 待機タイムアウト追加**: `INFINITE` → 適切なタイムアウト値（例: 5000ms）に変更
- **`SaveWaveDataToFile` の失敗検知強化**: 書き込み失敗時に即エラー遷移させる
- **Save Path の事前バリデーション強化**（原因6 対応）:
  - **バリデーション関数 `ValidateSavePath()` を新設**し、以下を検証:
    1. パスが空でないこと（既存チェック）
    2. ディレクトリが存在すること（`PathFileExists` / `GetFileAttributes`）
    3. 書き込み権限があること（テストファイル作成→即削除で確認）
    4. パスに `..` 相対遷移を含まないこと（既存セキュリティ要件と統合）
  - **呼び出しタイミング**:
    - アプリ起動時（初期設定値のロード後）
    - Save Path の UI 変更時（`IDC_EDIT_SAVEPATH` のフォーカスアウト or 変更通知）
    - Set Parameters 実行時
  - **バリデーション失敗時の挙動**:
    - **目立つエラーメッセージを表示**（どの条件に引っかかったか明示）
    - 注意: オートモードでは機械側は FPGA 外部トリガーで独立して動作するため、アプリ側で EP6 スレッドをブロックしても機械の計測は止まらず、細胞がロスする。バリデーションの目的は**ユーザーが機械を動かす前にパス不正に気づけるようにする**こと

#### PR-02 テスト観点表

PR-02 着手時点で、DLL 致命バグ修正の Red を以下の観点で固定する。`SaveWaveDataToFile` の失敗検知は既存処理の回帰防止も兼ねて同時に確認する。

| Case ID | Input / Precondition | Perspective (Equivalence / Boundary) | Expected Result | Notes |
| --- | --- | --- | --- | --- |
| TC-N-01 | `EP2` command transfer | Equivalence – normal | shared `m_hEP2EP4Mutex` is required | `EP2` / `EP4` のみ共有 mutex 対象 |
| TC-N-02 | `EP4` register read transfer | Equivalence – normal | shared `m_hEP2EP4Mutex` is required | `EP2` / `EP4` の相互排他維持 |
| TC-N-03 | `EP6` bulk read transfer | Equivalence – normal | shared `m_hEP2EP4Mutex` is not required | PR-02 の最重要修正 |
| TC-B-01 | mutex wait timeout constant | Boundary – fixed config | timeout is `5000ms` | `INFINITE` を廃止 |
| TC-N-04 | fresh `OVERLAPPED` + event handle | Equivalence – normal | all fields except `hEvent` are zeroed | `ZeroMemory` 相当 |
| TC-N-05 | scoped event handle cleanup | Equivalence – normal | event handle is closed exactly once | EP2 / EP4 / EP6 の handle leak 対策 |
| TC-N-06 | `EP6` reusable buffer, same size requested twice | Equivalence – normal | buffer pointer is reused | 毎回 `malloc/free` を禁止 |
| TC-B-02 | `EP6` reusable buffer grows from small to `EP6_ONETIME_MAX_SIZE` | Boundary – max transition | capacity grows to requested size | shrink は不要 |
| TC-A-01 | low writer failure in `SaveWaveDataToFile` path | Boundary – failure | low write failure is surfaced as error | 既存失敗検知の固定化 |
| TC-A-02 | high writer failure in `SaveWaveDataToFile` path | Boundary – failure | high write failure is surfaced as error | 同上 |

### Phase 1.5: preview consumer 耐性の短期安定化（高インパクト・低-中リスク）

対象: [Dialog1_Main.cpp](../../AnalogBoard_TestApp/Dialog1_Main.cpp), [WaveDataFileIO.h](../../AnalogBoard_TestApp/WaveDataFileIO.h)

`sys_app` preview が完成済み `.bin` を読む程度で acquisition が止まらないよう、publish と acquisition の結合を一段弱める。

- **publish failure を即 fatal にしない**: `FlushCloseAndPublishWavePair()` の失敗を直ちに `ErrExit = TRUE` にせず、取得継続可能なエラーとして扱う
- **`.tmp` / `.bin` 契約の明文化**: consumer は `.tmp` を見ず、完成済み `.bin` のみを読む前提を host 側の公開契約として固定する
- **失敗時の保持方針を明確化**: publish 失敗時は `.tmp` を保持または quarantine に退避し、次の取得を継続する
- **短期ログ整理**: hot-path logging を最小限に保ちつつ、publish failure の件数・理由・再試行結果だけを低頻度で残す
- **Version A 調査結果の反映**: UI/file logging を acquisition hot-path から段階的に外す方向を Phase 2-4 の前提にする

この Phase は最終形ではなく、Phase 2 の本命アーキテクチャ改修へ安全に繋ぐための短期安定化と位置づける。

### Phase 2: Reader/Writer/Publisher パイプライン導入（高インパクト・中リスク）

対象: [Dialog1_Main.cpp](../../AnalogBoard_TestApp/Dialog1_Main.cpp) の `LoopTestProcessThread_EP6_GetData` (line 1406-2120)

現在の620行の巨大関数を、3つの専用責務に分離する:

```
改善後のアーキテクチャ:

[USB Reader Thread]                  [File Writer Thread]                  [Publisher]
  EP6_GetData(buf)                     dequeue(dataChunk)                    dequeue(completedFile)
       |                                    |                                      |
       v                                    v                                      v
  enqueue(dataChunk) ---[Queue]--->   append to *.tmp                 rename *.tmp -> *.bin
       |                                    |                                      |
       v                                    v                                      v
  EP4_GetData(status)                 close current tmp                publish result / retry / quarantine
  (DDR status check)
```

- **スレッドセーフキュー**: `std::mutex` + `std::condition_variable` によるブロッキングキュー（固定サイズ、バックプレッシャー付き）を新規実装
- **USB Reader スレッド**: EP6 データ読み取りと EP4 ステータスチェックに専念
- **File Writer スレッド**: `.tmp` の作成・追記・close に専念
- **Publisher 責務**: completed `.tmp` の `.bin` 公開、retry、quarantine、consumer 互換維持に専念
- **キュー上限時ポリシー**: 待機（バックプレッシャー）/ 中断 の動作を明示的に定義
- **利点**: USB 読み取りがファイル I/O / publish / consumer 側の影響でブロックされなくなり、DDR オーバーフローのリスクを大幅に低減

### Phase 3: スレッド安全性と堅牢性の向上（中インパクト・低リスク）

対象: [Dialog1_Main.cpp](../../AnalogBoard_TestApp/Dialog1_Main.cpp)

- **スレッド制御フラグを `std::atomic<int>` に変更**: `g_bEP24LoopFlag`, `g_bEP6ThreadFlag`, `g_bStartSampling`
- **USB 転送リトライ機構**: `USB_ERR_TRANSFER_TIMEOUT` に対する段階的リトライ（短い backoff 付き、最大 N 回）
- **DDR ステータスポーリングの改善**: `Sleep(0)` busy loop → `Sleep(1)` + adaptive delay（DDR 書き込み進捗に応じて待機時間を調整）
- **DDR 書き込み完了タイムアウトの設定値化**: 200 * 10ms = 2秒 → 設定可能なタイムアウト値に変更（デフォルト10秒程度）
- **適切なスレッド終了待機**: `WaitForSingleObject(threadHandle, timeout)` でスレッド終了を確認

### Phase 4: コード構造改善・状態遷移明確化（中インパクト・中リスク）

対象: [Dialog1_Main.cpp](../../AnalogBoard_TestApp/Dialog1_Main.cpp) (3400行)

- **`LoopTestProcessThread_EP6_GetData` の分解**: 約715行の関数を `WaveAcquisitionEngine` クラスに抽出
  - `WaveAcquisitionEngine::Start()` / `Stop()` / `GetStatus()`
  - 状態管理、バッファ管理、ファイル管理を責務ごとに分離
- **状態遷移の明確化**: `Idle → Sampling → Draining → Publish → Completed/Error` として状態マシンを定義
  - 中断時の `.tmp` は再開/調査可能な形で保持し、公開条件を厳格化
  - publish failure は acquisition と切り離し、必要に応じて `PublishRetry` / `Degraded` のような補助状態を導入してもよい
  - **`Draining` ステートと FPGA 側 `RD_WAIT` 状態の整合**: FPGA の計測状態マシン（[MEAS_CTRL.vhd](../../FPGA_FW/ANA_20250129_restored/RTL/MEAS_CTRL/MEAS_CTRL.vhd)）は `MEAS → WAIT(500μs, DDR書込完了待ち) → RD_WAIT(DDR読出完了待ち) → IDLE` と遷移する。PC 側の `Draining` は FPGA の `RD_WAIT` に対応し、DDR に残っている全データを EP6 経由で読み出し切るフェーズである
  - **途中停止時のデータ整合性ポリシー**: `Draining` 中に `Stop()` が呼ばれた場合、FPGA 側の計測は既に完了しているが DDR データの読み出しが途中の状態となる。この場合:
    1. EP6 から読み出し済みのデータは `.tmp` として保持する
    2. `WAVE_WR_CNT` と `WAVE_RD_CNT` の差分をログに記録し、未読み出しデータ量を明示する
    3. `Publish` には遷移せず `Error` へ遷移し、部分データの公開を防止する
- **グローバル変数の削減**: `pEp6DataBuf1/2`, `pEp2DataBuf`, `pEp4DataBuf` などのグローバルバッファをクラスメンバに移動
- **ファイルサイズの目標**: 3400行 → 複数ファイルに分割して各1000行以下に

### Phase 5: テスト戦略・耐久テスト

- 既存の `wave_file_publish` テストに加えて、取得中断・書き込み遅延・タイムアウトを再現する耐久テストを追加
- **擬似USBタイムアウト注入**: DLL 関数をモック化し、ランダムに `USB_ERR_TRANSFER_TIMEOUT` を返す
- **擬似遅延書き込み**: ファイル I/O に人工的な遅延を挿入して DDR オーバーフローを再現
- Windows ビルド/テストは `scripts\run_with_vsdevcmd.bat` 経由で実施

#### 具体テストケース


| ID  | テスト種別       | 入力/注入条件                                 | 期待結果                                                | 合否基準                        |
| --- | ----------- | --------------------------------------- | --------------------------------------------------- | --------------------------- |
| T1  | Unit        | `EP6_GetData` 成功、Queue 空きあり             | Reader->Writer で全チャンク受け渡し成功                         | 欠損 0、戻り値 `USB_SUCCESS`      |
| T2  | Unit        | Queue 容量 `1`、Writer を 500ms 遅延          | Enqueue が timeout し `USB_ERR_QUEUE_FULL_TIMEOUT` 遷移 | 3回連続 timeout で Error 状態     |
| T3  | Unit        | `USB_ERR_TRANSFER_TIMEOUT` を 1-2 回注入    | backoff 後に再取得して継続                                   | 最大 retry 以内で Completed      |
| T4  | Unit        | `USB_ERR_TRANSFER_TIMEOUT` を連続注入（N+1 回） | retry 上限超過で Error 遷移                                | 想定エラーコード一致                  |
| T5  | Integration | publish 失敗（rename/close/consumer 競合）を注入         | acquisition は継続し `.tmp` を保持または隔離し、失敗理由を記録             | publish failure 後も次チャンク取得継続 |
| T6  | Integration | `Draining` 中に Stop 要求                      | `Publish` へ遷移せず `Error -> Idle` へ復帰            | `stopWaitTimeoutMs` 以内に停止、部分データ未公開 |
| T7  | Soak        | 実時間 8 時間連続運転                            | timeout 率と欠損率が KPI 内                                | timeout 起因失敗率 < 0.1%        |
| T8  | Regression  | 既存 `wave_file_publish` 一式実行             | low/high 出力順序互換維持                                   | 既存テスト全件 pass                |
| T9  | Unit        | `ValidateSavePath` に存在するパスを渡す           | 成功を返す                                               | 戻り値 OK                      |
| T10 | Unit        | `ValidateSavePath` に存在しないパスを渡す          | `USB_ERR_OUTPUT_PATH_NOT_FOUND`                     | エラーメッセージにパスを含む              |
| T11 | Unit        | `ValidateSavePath` に読み取り専用パスを渡す         | `USB_ERR_OUTPUT_PATH_NOT_WRITABLE`                  | エラーメッセージに権限情報を含む            |
| T12 | Unit        | `ValidateSavePath` に `..` を含むパスを渡す      | `USB_ERR_INVALID_OUTPUT_PATH`                       | パス正規化前に拒否                   |
| T13 | Integration | Save Path 不正のまま Set Parameters を押す      | エラーメッセージが表示される                                   | ユーザーが機械を動かす前に気づける            |
| T14 | Integration | EP6 読み取りに 200ms 人工遅延を注入し EP6 FIFO フル状態を再現 | FPGA バックプレッシャーが作動し DDR 読出停止→読取再開で復帰          | データ欠損 0、`WAVE_WR_CNT == WAVE_RD_CNT` |
| T15 | Soak        | オートモード連続トリガー（高頻度）で DDR 領域枯渇を再現       | DDR 枯渇時に `SAMP_END` で計測停止、エラー遷移が正しく処理される   | エラーログに DDR 領域枯渇を記録、未読出量を明示  |
| T16 | Integration | `sys_app` preview 相当で最新 `.bin` を継続的に読み取り     | acquisition が停止せず、preview は完成済み `.bin` のみ参照する         | preview 実行中も取得継続、`.tmp` 非参照 |
| T17 | Integration | `last_n + 1` probe 中に次の pair が publish される       | preview が incomplete data を読まず、次の completed pair を正しく検出 | pair index が単調増加し破損 0 |


### Phase 6: KPI 判定・オプション検討

**受け入れ基準:**

- 連続 1000 回取得でタイムアウト起因の失敗率が **0.1% 未満**
- 中断後の low/high ペア欠損ゼロ、破損ゼロ
- 取得スループットと最大遅延の前後比較で改善を確認

**計測条件（再現性固定）:**

- 測定期間: 最低 3 セッション（各 1000 回取得） + 8 時間連続運転 1 回
- 負荷条件: 通常負荷 / ディスク遅延注入（50ms, 100ms）/ 擬似 timeout 注入（1%, 5%）
- KPI 判定: セッションごとの平均値に加えて最悪値を記録し、しきい値超過時は Phase 2-4 を見直し
- **計測環境固定**: PC（型番・CPU・メモリ・ディスク種別）、USB ケーブル/ハブ構成、FPGA FW バージョンを process_log（`docs/2026-03-02-usb-acquisition-stability-log.md`）に記録し、全セッションで同一条件を使用
- **統計的注記**: 1000回中1件の失敗は境界値（0.1%）のため、3セッションの**平均値**で合否判定する。単一セッションの突発的な1件失敗では不合格としない

**閾値超過時アクション:**

- timeout 起因失敗率 >= 0.1%: retry/backpressure 設定を見直し、Phase 3 再調整
- 欠損/破損が 1 件以上: publish 条件と `.tmp` 保持ロジックを再監査
- preview 実行中に acquisition 停止が 1 件でも再現: Phase 1.5/2 の publish 契約と consumer 耐性を再監査
- 最大遅延が baseline 比 20% 以上悪化: キュー容量と writer flush 間隔を再設計

**Phase 1-5 で不十分な場合のオプション:**

- ファイル書き込みパイプライン（Phase 2 の Writer 側）を **Rust** で実装
- C FFI DLL として MFC アプリから呼び出し
- メモリ安全性保証、lock-free データ構造、`tokio` による非同期ファイル I/O

---

## セキュリティ

本件はローカル処理中心だが、ファイル破損・情報露出を防ぐために以下を実施する。

- 出力先パスは許可ディレクトリ配下のみ許可し、`..` を含む相対遷移は拒否
- Windows 予約名チェック: `^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)` に一致するファイル名は保存拒否（大文字小文字不問）
- ファイル名に制御文字（0x00-0x1F）を含む場合は保存拒否
- デバッグログは生波形データを出力せず、サイズ・件数・ハッシュ・エラーコードのみ記録
- `.tmp` は `Publish` 成功時のみ最終確定し、失敗時は隔離ディレクトリ `{SavePath}\.quarantine\{YYYYMMDD_HHmmss}\` へ退避
- エラー時ダンプの保持期間: **30日**。調査完了後に手動削除する（自動削除は実装しない）

## 依存関係


| 区分             | 対象                                                                                  | バージョン方針              | ライセンス確認           | 導入理由            |
| -------------- | ----------------------------------------------------------------------------------- | -------------------- | ----------------- | --------------- |
| Phase 1-5      | C++ 標準ライブラリ (`<atomic>`, `<thread>`, `<mutex>`, `<condition_variable>`, `<chrono>`) | 既存 MSVC / C++17 準拠   | 追加確認不要（標準ライブラリ）   | スレッド安全化、待機制御、計測 |
| Phase 1-5      | Win32 API (`WaitForSingleObject`, `CreateEvent`, `CloseHandle`)                     | 既存 SDK 準拠            | 追加確認不要            | 既存 DLL 実装と整合    |
| Phase 5        | 既存 UnitTest 実行基盤 (`AnalogBoard_UnitTest`)                                    | 現行ソリューションを維持         | 追加確認不要            | 回帰防止            |
| Phase 6 Option | Rust (`tokio`, `crossbeam` 候補)                                                      | 採用時に固定（例: tokio 1.x） | 採用前に第三者ライセンス一覧へ追記 | Writer の遅延耐性改善  |


## 技術選定基準（C++継続 vs Rustオプション）


| 観点    | C++継続（既定）           | Rustオプション（Phase 6 条件付き）   |
| ----- | ------------------- | ------------------------- |
| 性能    | まず現行コード最適化で評価       | C++で KPI 未達時に検討           |
| 導入コスト | 低（現行資産を再利用）         | 中-高（FFI、ビルド、保守体制）         |
| 保守性   | 既存メンバー習熟済み          | メモリ安全性向上の余地あり             |
| 採用条件  | Phase 1-5 後に KPI 達成 | KPI 未達 + C++ 側の追加改善余地が限定的 |
| 見送り条件 | KPI 達成済み            | 運用/保守コストが便益を上回る           |


## 移行・ロールバック計画

- Step 1: Phase 1 を先行適用（DLL バグ修正のみ、feature flag 不要）
- Step 2: Phase 2-4 は `UseAsyncWriter` フラグで旧経路/新経路を切替可能に実装
  - **格納場所**: アプリ実行ディレクトリの `config.ini` ファイル、`[Engine]` セクション
  - **デフォルト値**: `UseAsyncWriter=1`（新経路有効）。`0` で旧経路にフォールバック
  - **読み込みタイミング**: アプリ起動時に1回読み込み。動的切替は不要（再起動で反映）
- Step 3: 段階展開（開発機 -> 検証機 -> 長時間連続運転）

**ロールバック条件:**

- timeout 起因失敗率が baseline +0.05% 以上悪化
- low/high 欠損または破損が 1 件以上発生
- 停止処理が `stopWaitTimeoutMs` を超過

**ロールバック手順:**

1. 設定で `UseAsyncWriter=0` に切り替え、旧経路を再有効化
2. 問題ビルドを差し戻し、直近安定ビルドへ戻す
3. 隔離された `.tmp` を確認し、データ整合性がある場合は手動で Publish（リネーム）を実施。整合性が不明な場合は `.quarantine` に保持して調査
4. 失敗ログと計測ログを保全して原因解析へ引き渡す

## 関連ドキュメント

- [USB データ取得・書き込み安定性改善プラン（FPGA ソース不要版）](./archive/2026-03-04-usb-stability-without-fpga-source.md)
- [Wave Output Format Handover (2026-03-02)](./archive/2026-03-02-wave-output-handover.md)
- [前回レビュー (2026-03-02, 87/150 D判定)](./archive/review_2026-03-02-usb-acquisition-stability_20260302.md)

---

## 実施順序と影響範囲


| Phase | 影響ファイル                                 | リスク | 効果                                                  |
| ----- | -------------------------------------- | --- | --------------------------------------------------- |
| 0     | Dialog1_Main.cpp, DLL                  | 低   | ベースライン取得                                            |
| 1     | DLL (.cpp/.h), Dialog1_Main.cpp        | 低   | Mutex競合解消 = 最大効果 + Save Path 事前検証でデータロス防止           |
| 1.5   | Dialog1_Main.cpp, WaveDataFileIO.h     | 低-中 | preview consumer が `.bin` を読んでも acquisition を止めない短期安定化 |
| 2     | Dialog1_Main.cpp, 新規 Engine/Queue/Publisher | 中   | I/O / publish / consumer 影響の分離                      |
| 3     | Dialog1_Main.cpp, Engine               | 低   | レースコンディション除去                                        |
| 4     | 新規ファイル + Dialog1_Main                 | 中   | 保守性・状態管理向上                                          |
| 5     | テストプロジェクト                              | 低   | 回帰防止                                                |
| 6     | 判定のみ（オプションでRust）                       | -   | 最終評価                                                |


**推奨**: Phase 0 → Phase 1 → Phase 1.5 を最優先（計測 + 最小変更で最大効果 + preview consumer 耐性の早期確保）。その後 Phase 2-3 を続けて実施。Phase 4 はリファクタリングとして段階的に。Phase 5-6 で品質を確認。

---

## 実装タスク分解（PR単位）

以下は「1 PR = 1つの明確な責務」で分割した実装順。`PR-01` から順にマージする。


| PR     | 主目的                         | 主要変更ファイル                                                                                                                       | 依存        | DoD（完了条件）                                                                                  | 検証                                           |
| ------ | --------------------------- | ------------------------------------------------------------------------------------------------------------------------------ | --------- | ------------------------------------------------------------------------------------------ | -------------------------------------------- |
| PR-01  | 計測基盤導入（Phase 0）             | `AnalogBoard_TestApp/Dialog1_Main.cpp`, `AnalogBoard_Dll/AnalogBoard_Dll.cpp`                             | なし        | EP6読取時間、書込時間、timeout回数、DDRポーリング回数が hot-path を乱さない軽量集計として取得でき、取得終了後に要約出力される                                                  | Debugビルド成功、軽量計測サマリが1取得サイクルで出力されても waveform が壊れない                   |
| PR-02  | DLL致命バグ修正（Phase 1）          | `AnalogBoard_Dll/AnalogBoard_Dll.cpp`, `AnalogBoard_Dll/AnalogBoard_Dll.h`                         | PR-01     | EP6の不要mutex排除、OVERLAPPEDハンドルリーク解消、EP6バッファ再利用化が反映される                                        | 既存通信シーケンスで回帰なし、timeout率がbaseline以下           |
| PR-02b | Save Pathバリデーション強化（Phase 1） | `AnalogBoard_TestApp/Dialog1_Main.cpp`, `AnalogBoard_TestApp/Dialog1_Main.h`                                     | PR-01     | `ValidateSavePath` でディレクトリ存在・書き込み権限・パス安全性を検証し、不正時は目立つエラーメッセージを表示        | T9-T13 pass              |
| PR-03a | preview consumer 耐性の短期安定化（Phase 1.5） | `AnalogBoard_TestApp/Dialog1_Main.cpp`, `AnalogBoard_TestApp/WaveDataFileIO.h`                               | PR-02b    | publish failure が acquisition fatal にならず、`.bin` consumer 実行中でも取得継続                      | T5, T16 pass |
| PR-03  | Queue実装とI/F導入（Phase 2 前半）   | `AnalogBoard_TestApp/WaveAcquisitionEngine.h` (新規), `AnalogBoard_TestApp/WaveAcquisitionEngine.cpp` (新規) | PR-03a | `AcquisitionConfig`, `WaveChunk`, `BlockingQueue`, `EngineStatus` がコンパイル可能 | UnitTestにQueue正常/timeoutケース追加しpass           |
| PR-04  | Reader/Writer/Publisher分離（Phase 2 後半） | `AnalogBoard_TestApp/Dialog1_Main.cpp`, `AnalogBoard_TestApp/WaveAcquisitionEngine.cpp`                          | PR-03     | `LoopTestProcessThread_EP6_GetData` の主要ロジックが Engine 経由に移行し、I/O直列実行と publish 直結が解消              | 遅延注入時も取得継続、`USB_ERR_QUEUE_FULL_TIMEOUT` 遷移確認、T17 pass |
| PR-05  | スレッド安全化とretry改善（Phase 3）    | `AnalogBoard_TestApp/Dialog1_Main.cpp`, `AnalogBoard_TestApp/WaveAcquisitionEngine.cpp`                          | PR-04     | 制御フラグ atomic 化、retry/backoff、stop待機timeoutが実装される                                           | timeout注入テストで retry上限内復帰/上限超過Error が期待通り     |
| PR-06  | 状態遷移明確化と責務分離（Phase 4）       | `AnalogBoard_TestApp/Dialog1_Main.cpp`, `AnalogBoard_TestApp/WaveAcquisitionEngine.`*                            | PR-05     | 状態遷移 `Idle->Sampling->Draining->Publish->Completed/Error` がコード上で追跡可能                       | stop中断・publish完了・error遷移の3系統テストpass          |
| PR-07  | テスト拡張（Phase 5）              | `tests/wave_file_publish_test.cpp`, `AnalogBoard_UnitTest/*`                                                            | PR-06     | T1-T8 相当の unit/integration/soak 実行手順が整備され、回帰項目が固定化                                         | UnitTest全件pass、soak結果を成果物として保存               |
| PR-08  | KPI判定と運用切替（Phase 6）         | `docs/plans/2026-03-02-usb-acquisition-stability.md`, 必要に応じて設定ファイル                                                             | PR-07     | KPI判定結果と次アクション（継続改善 or Rust検討）を記録                                                          | 3セッション+8h結果で閾値判定が再現可能                        |

### PRごとのレビュー観点（必須チェック）

- TDD順守: 対象仕様/不具合に対する failing test を先に追加し、修正後に pass することを確認
- API契約: `WaveAcquisitionEngine` と `BlockingQueue` のシグネチャ変更時は同PR内で呼び出し側も更新
- エラー整合: `USB_ERR_*` の追加時は定義、ログ、分岐、テストを同時更新
- 互換性: `wave_file_publish` の low/high 出力順序回帰テストを毎PRで実行
- 安全性: `.tmp` の保持/公開条件を壊す変更は単独PRに分離し、ロールバック手順を追記

### 各PRの最小検証コマンド（Windows）

```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat x64\Debug\AnalogBoard_UnitTest.exe"
```
