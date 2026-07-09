# 知見棚卸し統合レポート

> 作成日: 2026-03-21
> 対象ブランチ: dev (`490371e`), lab/0.2.2-engine-semantics, feature/win11-driver-compat
> 目的: min_change への知見移転の判断材料

---

## 0. 判定ラベル

- **code-side verified**: unit test / build / simulation で成立を確認済み
- **field verified**: 実機ログまたは実機 run で成立を確認済み
- **hypothesis**: 仮説段階。code-side または field のどちらかが未完了

---

## 1. 確定知見（min_change に持ち込むべきもの）

### 1.1 Allocator バックエンド固定 (malloc/free)
- **内容**: EP6 scratch buffer は CRT `malloc/free` で固定。`new[]/delete[]` への変更は EP6 timeout 多発を招く
- **根拠**: コミット `a0a8a97`、フィールドログ比較（`0.1.4` vs `0.1.5` vs `cmp_76b2b2a`）で逆行原因を特定
- **検証状況**: `field verified`
- **テスト**: `UsbTransferHelpers_test.cpp` で allocator backend contract を固定化
- **適用方法**: `ScopedHeapBuffer` の backend contract を明文化し、CRT `malloc/free` 以外への変更を禁止

### 1.2 DDR セマンティクスの明確化
- **内容**: `DDR_WR_END=1` は draining hint、`DDR_RD_END=1` は final completion。startup stale snapshot の誤判定を防止
- **根拠**: `AcquisitionCompletionLogic.h` で状態遷移を explicit に定義し、startup stale / RD_WAIT / final completion を分離
- **検証状況**: `code-side verified`
- **テスト**: `AcquisitionCompletionLogic_test.cpp` で TC-R-01〜07 を固定
- **適用方法**: `AcquisitionCompletionLogic.h` とテストを持ち込み、empty capture 回避ガードを必須条件にする

### 1.3 EP6 Retry + Backoff（DLL 側）
- **内容**: EP6 各 chunk transfer で 1 回 retry + 5ms backoff を許容し、transient timeout をその場で吸収する
- **根拠**: `Ep6TransferRetryPolicy.h` に retry budget と backoff contract を実装
- **検証状況**: `code-side verified`
- **テスト**: `Ep6TransferRetryPolicy_test.cpp`
- **適用方法**: DLL の `EP6_GetData()` に retry wrapper を埋め込む

### 1.4 Ep6TransferTuningPolicy（timeout 30秒固定）
- **内容**: EP6 Bulk-In endpoint の timeout を 30 秒に明示設定する
- **根拠**: `Ep6TransferTuningPolicy.h` と endpoint 初期化時の `ApplyBulkInDefaults()`
- **検証状況**: `code-side verified`, `field pending`
- **テスト**: `Ep6TransferTuningPolicy_test.cpp` (`6/6` pass)
- **適用方法**: DLL の endpoint 初期化で `ApplyBulkInDefaults()` を呼び出す

### 1.5 Lightweight メトリクス（intrusive instrumentation 排除）
- **内容**: hot-path の `PrintLog()` を排除し、取得後の summary 集計に切り替える
- **根拠**: 高頻度ログが waveform 破損を誘発した教訓。`AcquisitionPerfMetrics.h` で cycle summary 集計を実装
- **検証状況**: `code-side verified`
- **テスト**: `AcquisitionPerfMetrics_test.cpp`
- **適用方法**: `AcquisitionPerfMetrics.h` を持ち込み、per-call success log を抑止する

### 1.6 Atomic Publish（`.tmp` → `.bin` rename）
- **内容**: 書き込み中は `.tmp` で出力し、完了後 `.bin` に昇格する。2 段階 rename + rollback を使う
- **根拠**: `WaveDataFileIO.h` に `PublishWavePairAtomic()` 実装
- **検証状況**: `code-side verified`
- **テスト**: `WaveDataFileIO_test.cpp` でバイナリ互換性と rollback 系を固定
- **適用方法**: `WaveDataFileIO.h` をほぼそのまま持ち込む

### 1.7 Publish エラー分類（fatal vs non-fatal）
- **内容**: close 失敗は fatal、publish 失敗は non-fatal とし、`.tmp` を保持して次サイクルへ進む
- **根拠**: `WavePairPublishPolicy.h` に `ClassifyFinalizeOutcome()` を実装
- **検証状況**: `code-side verified`
- **テスト**: `WavePairPublishPolicy_test.cpp`
- **適用方法**: `Dialog1_Main` の finalize path に分類ポリシーを適用する

### 1.8 Startup `.tmp` Cleanup
- **内容**: 起動時に `*_fl_*.bin.tmp` / `*_fh_*.bin.tmp` / `*.bin.rollback.*` だけを安全に削除する
- **根拠**: `CleanupResidualBinTmpFiles()` + `IsCleanupTargetFileName()`
- **検証状況**: `code-side verified`
- **テスト**: `WaveDataFileIO_test.cpp` の cleanup 観点で固定
- **適用方法**: `OnInitDialog()` で呼び出す

### 1.9 ファイル I/O ログ抑制（FileIoLoggingPolicy）
- **内容**: hot path での Open/Write/Close success ログを抑止し、failure/retry のみ記録する
- **根拠**: `FileIoLoggingPolicy.h` に success suppression policy を実装
- **検証状況**: `code-side verified`
- **テスト**: `FileIoLoggingPolicy_test.cpp`
- **適用方法**: `SaveWaveDataToFile()` と publish 成功ログを policy 経由で制御する

### 1.10 AcquisitionRunMetadata（cfg ファイル追記）
- **内容**: cycle 終了後に status / error / pair count / wave count を cfg に append する
- **根拠**: `AcquisitionRunMetadata.h` と `AppendRunResultMetadata(...)`
- **検証状況**: `code-side verified`, `field verified`
- **テスト**: `AcquisitionRunMetadata_test.cpp` (`14/14` pass)
- **適用方法**: cycle 終了後に `AppendRunResultMetadata()` を呼び出す

### 1.11 AcquisitionCycleRecoveryPolicy
- **内容**: 終了ステータスに応じて runtime 継続 vs 停止、recovery stop 要否を判定する
- **根拠**: `AcquisitionCycleRecoveryPolicy.h` に runtime continuation と stop decision を分離
- **検証状況**: `code-side verified`, `field verified`
- **テスト**: `AcquisitionCycleRecoveryPolicy_test.cpp` (`8/8` pass)
- **適用方法**: namespace 関数として持ち込み、MFC dialog に call-site を追加する

---

## 2. 有望だが未検証

### 2.1 Ep6TimeoutRecoveryPolicy（App 側 throttling）
- **内容**: first timeout 後の次回 read だけを 64KB にクランプし、20ms backoff を入れる
- **現状**: 実装済み、`WaveAcquisitionEngine_test.cpp` で code-side は固定済み
- **検証状況**: `code-side verified`, `field pending`
- **残り**: `r18` 相当の high-density field run で `retryBackoffMs=20` と clamped `nextPlannedReadSize` を確認する

### 2.2 ReadRequestBurstPolicy（producer-side burst cap）
- **内容**: read request を `min(256MB, 4*OneFileSize, 16KiB align)` で制限する
- **現状**: 実装済み、field で burst 縮小までは確認済み
- **検証状況**: `code-side verified`, `field verified`, ただし持続的効果は `hypothesis`
- **残り**: queue 導入後も同じ cap が有効かを再評価する

### 2.3 WaveAcquisitionEngine + BlockingQueue（Reader/Writer 分離）
- **内容**: EP6 読取とファイル書込を独立スレッド化し、queue で逆圧制御する
- **現状**: skeleton 実装済み、`BlockingQueue` contract は固定済み、legacy loop への integration は未完了
- **検証状況**: `code-side verified` for skeleton, `field pending`
- **残り**: legacy loop の producer/consumer 置換、Reader/Writer/Publisher の 3 分離、実機検証

### 2.4 CyAPI TimeOut の Win11 固有調整
- **内容**: Win11 + new driver では CyAPI timeout 明示設定の効き方が old driver と異なる可能性がある
- **現状**: matching SDK 導入と endpoint discovery hardening の方針は整理済みだが、timeout policy の有効性は未確定
- **検証状況**: `hypothesis`
- **残り**: Win11 実機で `ApplyBulkInDefaults()` の有無を A/B 比較し、new driver 下での再現性を見る

### 2.5 Atomic Publish のパフォーマンス影響
- **内容**: `.tmp` + rename による遅延悪化を 10% 以内に抑えたい
- **現状**: 開発 PC では小ファイル 13%、大ファイル 115% の劣化で閾値未達
- **検証状況**: `hypothesis`
- **残り**: 実機 SSD での計測、必要なら flush 非同期化案の再評価

### 2.6 Downstream Polling との同時実行検証
- **内容**: `sys_app` が `.bin` を polling read しても sharing violation を起こさないことを確認したい
- **現状**: 擬似テストでは fail rate `100% -> 0%` に改善
- **検証状況**: `code-side verified`, `field pending`
- **残り**: 実機で `sys_app` 同時実行テストを行う

---

## 3. 既知の構造課題（min_change では後段フェーズ扱い）

| # | 構造課題 | 現在の判断 | 扱い |
|---|---|---|---|
| 3.1 | `EP6_GetData` と `SaveWaveDataToFile` が同一スレッドで直列実行される | high-density / disk I/O 干渉の主因候補。既知課題として強い | `WaveAcquisitionEngine + BlockingQueue` で Phase 4 扱い |
| 3.2 | `XferData(FALSE)` が同期ブロッキングで Win11 timeout を受けやすい | transport stall と OS 差分の切り分けが必要 | `min_debug` で原因調査を継続 |
| 3.3 | `Sleep(0)` 中心の EP4 polling が Win11 スケジューラ差分に敏感 | 単独変更では解決せず、副作用も大きい | polling 調整だけでは解かず、上位の completion / transport で扱う |
| 3.4 | publish / consumer / file write が acquisition loop に近すぎる | `.tmp` 契約で短期回避はできるが、根治は責務分離が必要 | `min_change` では atomic publish まで、根治は後段 |

---

## 4. 失敗・棄却（min_change で避けるべきもの）

| # | 試行内容 | 失敗理由 | 教訓 |
|---|---|---|---|
| 4.1 | `new[]/delete[]` への allocator 変更 | EP6 timeout が劇的に増加（`0.1.5` / `0.1.6` regression） | heap allocator は USB 性能に影響する。CRT `malloc/free` を固定 |
| 4.2 | Hot-path 逐次 `PrintLog` | waveform corruption、計測対象を乱す | 計測は「取得後集計」パターンで設計 |
| 4.3 | `Sleep(0) -> Sleep(1)` への変更 | EP4 polling 粗化、`DDR_WR_END` 検出遅延 | Win11 timeout は app-level polling 調整だけでは解決不可 |
| 4.4 | shared mutex -> per-endpoint mutex 分割 | 複雑さ増大、deadlock リスク上昇、効果なし | 同期化は最小限に保つ方が predictability が高い |
| 4.5 | `DDR_WR_END` を final marker として使用 | startup stale で誤完了判定が多発 | FPGA status は snapshot。stale guard + `DDR_RD_END` final の 2 層構造が必須 |
| 4.6 | legacy `waveWrCnt >= waveRdCnt` 単独完了判定 | startup stale で即時終了し `savedWaveCount=0` | active cycle observation と completion decision は分離する |
| 4.7 | 過度な buffering による file write async 化 | blocking time 増加、atomicity 保証困難 | atomic publish では simple & synchronous の方が信頼性が高い |
| 4.8 | 詳細ログ on/off フラグ | フラグ管理が複雑化し、誤設定実行が増えた | ポリシーベース出力制御へ統一する |

---

## 5. 未調査（min_debug / min_experiment での調査候補）

### 5.1 Win11 USB Timeout の根本原因【最重要】
- **仮説**: Win11 USB スタック (`xUSBHost.sys`) の strict timeout、CyAPI の BOS descriptor 処理遅延、DLL の `OVERLAPPED` 初期化不足のいずれか
- **調査**: driver log level を上げ、CyAPI step-through debug、Win10/Win11 A/B 比較、WinDbg カーネルログを取る

### 5.2 High-density failure の transport-level backpressure
- **仮説**: FPGA EP6 FIFO 満杯 -> USB flow control (`NRDY`) 発生、または host file I/O wait で blocked
- **調査**: `maxBacklogBytes` scatter plot、Wireshark USB capture、intentional board removal テスト

### 5.3 CyAPI.lib の Win11 互換性
- **仮説**: `CyLib` 内の `CyAPI.lib` が Win10 前提で、Win11 では別ビルドが必要
- **調査**: Cypress FX3 SDK リリースノート、CyAPI バージョン確認、Win11 対応 lib の入手

### 5.4 FX3 USB 3.0 コントローラの FW レベル動作
- **仮説**: FX3 FW version により bulk transfer flow control や SS detect latency が変わる
- **調査**: Cypress TRM で state machine を確認し、`CyAPI XferData()` を内部追跡する

### 5.5 ファイルシステム I/O との interference
- **仮説**: USB 転送と同一 disk への `.bin` write が disk scheduler queue や NTFS journaling と競合する
- **調査**: ramdisk テスト、Process Monitor、Reader/Writer thread 分離の A/B 比較

### 5.6 Win11 スケジューラの timer precision 低下
- **仮説**: `Sleep(5)` が 10-15ms jitter を持ち、EP6 retry backoff が効かない
- **調査**: `QueryPerformanceCounter()` で実測し、multimedia timer と power plan Performance を比較する

### 5.7 DDR memory layout と FPGA 設定の対応
- **仮説**: DDR controller の arbiter deadlock が稀に発生、または `LIMIT_ADDR` が page boundary を overshoot する
- **調査**: FPGA 設計仕様書を精読し、SimRunner に large random-sized write sequence を追加する

---

## 6. feature/win11-driver-compat 由来の既知事項

### 6.1 matching SDK 前提はほぼ確定
- **内容**: new driver track は `Win11 + new Cypress/Infineon driver + matching SDK` を前提に進める
- **根拠**: `CyAPI.h` は SDK 1.3 と一致、`CyAPI.lib` は worktree に補完済み
- **検証状況**: `code-side verified`
- **持ち帰り方**: `CyAPI.h` / `CyAPI.lib` / `cyusb3.inf` の比較結果を source of truth に固定する

### 6.2 code change の主戦場は DLL 接続層
- **内容**: new driver 対応は `Sysmex_AnalogBoard_Dll` の binding / endpoint discovery 側へ限定し、`EP2_SendData` / `EP4_GetData` / `EP6_GetData` の public API は維持する
- **根拠**: driver track の runbook と architecture note で方針固定済み
- **検証状況**: `code-side verified`
- **持ち帰り方**: `min_change` へは feature ブランチのコードを直接持ち込まず、判断基準だけを持ち帰る

### 6.3 endpoint discovery hardening は既知、first gate は未通過
- **内容**: endpoint discovery hardening 自体は実装済みだが、Win11 + new driver で `USBBoard_Connect` と idle `EP4_GetData` の安定化はまだ gate 未通過
- **根拠**: `driver_next.md` の current status と goal
- **検証状況**: `code-side verified`, `field pending`
- **持ち帰り方**: feature トラックは `min_change` の blocker ではなく parallel investigation として扱う

---

## 7. テスト資産の移植推奨

### 高優先度（必須）

| ファイル | 状態 | 対象 |
|---|---|---|
| `FpgaRegisterLogic_test.cpp` | broad coverage, focused pass | FPGA レジスタ読み書き基盤 |
| `SavePathValidation_test.cpp` | focused pass | 保存先パス検証・セキュリティ |
| `WaveDataFileIO_test.cpp` | broad coverage, focused pass | 波形ファイル I/O・バイナリ互換性 |
| `AcquisitionCompletionLogic_test.cpp` | focused pass | DDR stale / RD_WAIT / final completion |
| `Ep6TransferRetryPolicy_test.cpp` | focused pass | USB 転送 retry |
| `AcquisitionPerfMetrics_test.cpp` | focused pass | 軽量計測基盤 |
| `AcquisitionRunMetadata_test.cpp` | focused pass | failed run metadata 可視化 |

### 中優先度（推奨）

| ファイル | 状態 | 対象 |
|---|---|---|
| `BlockingQueue_test.cpp` | focused pass | queue contract |
| `ReadRequestBurstPolicy_test.cpp` | focused pass | バースト制御 |
| `WavePairPublishPolicy_test.cpp` | focused pass | publish 判定 |
| `FileIoLoggingPolicy_test.cpp` | focused pass | ログ抑制 |
| `AcquisitionCycleRecoveryPolicy_test.cpp` | focused pass | リカバリー判定 |
| `Ep6TransferTuningPolicy_test.cpp` | focused pass | timeout tuning |
| `AcquisitionLogMessageFormatter_test.cpp` | focused pass | ログフォーマット |
| その他 | branch 依存 | エンジン、ダイアログ等 |

### シミュレーション資産（全量持ち込み推奨）

| プリセット | 検証対象 | 結果 |
|---|---|---|
| `normal_complete` | 正常完走 | 実証済み |
| `ep6_timeout_once_then_recover` | 1 回 timeout 後の復帰 | 実証済み |
| `ep6_timeout_persistent` | timeout 打ち切り | 実証済み |
| `usb_disconnect_midstream` | 途中 disconnect | 実証済み |
| `writer_slow_queue_pressure` | write 遅延時の backlog | 実証済み |
| `write_fail` | write failure | 実証済み |
| `publish_fail` | publish failure | 実証済み |
| `burst_boundary_stress` | バースト境界ストレス | 実証済み |
| `slow_producer` | 非 16KB アライン中間 progress | 実証済み |
| `empty_capture` | 0 wave capture 検知 | 実証済み |
| `rd_wait_stale_ddr` | RD_WAIT と stale DDR | 実証済み |

---

## 8. 提案事項

### 8.1 EP6 タイムアウト短縮（30秒 → 3秒）
- **現状**: `kEp6BulkEndpointTimeoutMs = 30000u`（`Ep6TransferTuningPolicy.h`）
- **問題**: EP4 ポーリングで「データあり」を確認してから EP6 を呼ぶため、正常な USB 転送は数百 ms 以内に完了する。30 秒のタイムアウトは USBパイプ障害時に無駄な待ち時間を生む
- **影響**: DLL 層で最大 30 秒 × 2 回（retry 込み）= 60 秒のロスが発生し、1 サンプル 54 秒の測定サイクルをまるごと浪費する
- **提案**: タイムアウトを **3 秒**に短縮。正常転送の数十倍あれば十分で、異常時は早期に見切れる

### 8.2 DLL 側 retry 廃止（1回 → 0回）
- **現状**: `kEp6TimeoutRetryMaxRetries = 1`、`kEp6TimeoutRetryBackoffMs = 5u`
- **問題**: タイムアウト = USB パイプ障害であり、5ms 後に同じ `XferData` を繰り返しても復帰しない。アプリ層に早く戻して USB 再接続させた方が合理的
- **提案**: `kEp6TimeoutRetryMaxRetries = 0` にして即座にアプリ層へ返す

### 8.3 Ep6Timeout 時の自動 USB 再接続リカバリ
- **現状**: `AcquisitionCycleRecoveryPolicy::ShouldContinueRuntimeAfterCycle()` は `Success` 以外すべて `false` を返し、タイムアウト後はランタイムが停止する。ユーザーが手動で USB 再接続 + 再起動する必要がある
- **問題**: 測定中に USB 障害が起きるたびに手動介入が必要になり、連続測定が中断される
- **提案**: `Ep6Timeout` 時に自動で USB disconnect → reconnect → サイクル再開するリカバリパスを `AcquisitionCycleRecoveryPolicy` に追加する。`ShouldContinueRuntimeAfterCycle()` で `Ep6Timeout` を recoverable として扱い、USB セッション再接続後にループを継続する

---

## 9. min_change 実装の推奨順序

> **Note**: Section 8 の提案事項のうち 8.1 / 8.2 は Phase 1、8.3 は Phase 3 に統合する想定

```text
Phase 0: 基盤（回帰防止）
  ├── allocator contract 固定化 (1.1)
  ├── DDR completion logic (1.2)
  └── テスト基盤の移植 (Section 7)

Phase 1: DLL 側改善
  ├── EP6 retry + backoff (1.3)
  ├── EP6 timeout 30秒固定 (1.4)
  └── lightweight metrics (1.5)

Phase 2: ファイル I/O
  ├── atomic publish .tmp → .bin (1.6)
  ├── publish エラー分類 (1.7)
  ├── startup cleanup (1.8)
  ├── ログ抑制 (1.9)
  └── run metadata (1.10)

Phase 3: Recovery
  ├── cycle recovery policy (1.11)
  └── timeout recovery throttling (2.1) ← field 検証後

Phase 4: パイプライン分離（field 検証後）
  ├── BlockingQueue (2.3)
  ├── burst cap (2.2)
  └── Reader/Writer thread 分離 (2.3)
```

---

## 10. 並行トラックとの連携

| トラック | 本レポートとの関係 |
|---|---|
| **dev** | Section 1〜3 の知見源。引き続き実験・検証を継続 |
| **min_change** | Section 1 を Phase 0〜2 で段階実装し、Section 8 の順序に従う |
| **min_debug** | Section 5 の調査を実施し、結果を `min_experiment` へ渡す |
| **min_experiment** | Section 2 の未検証項目と Section 5 の調査結果を試行する |
| **feature/win11-driver-compat** | Section 6 の互換性知見を蓄積する parallel track |
| **仕様書** | 全セクションの知見を仕様書に随時反映する |
