# USB データ取得・書き込み安定性改善 タスクチェックリスト

対象プラン: [USB データ取得・書き込み安定性改善プラン](./2026-03-02-usb-acquisition-stability.md)
プロセスログ: [Process Log](./2026-03-02-usb-acquisition-stability-log.md)
作成日: 2026-03-05
最終同期: 2026-03-11

---

## Phase 0: 現状可視化・ログ計測（PR-01）

依存: なし

注記（2026-03-11 更新）: 2026-03-09 に hot-path の高頻度 `PrintLog(...)` を Phase 0 完了条件から外し、軽量集計 + 取得終了後サマリ出力へ再定義した。その後、2026-03-09 16:47 JST run で baseline を再取得できたため、Phase 0 は完了扱いとする。

- [x] hot-path の呼び出しごと `PrintLog(...)` 計測を無効化し、逐次ログ前提の実装を完了扱いから外す
- [x] EP6 読み取り時間を hot-path で逐次出力せず、メモリ集計（合計/最大/回数）して取得終了後に要約出力する
- [x] ファイル書き込み時間を hot-path で逐次出力せず、メモリ集計（合計/最大/回数）して取得終了後に要約出力する
- [x] 転送サイズ・`USB_ERR_TRANSFER_TIMEOUT` 発生頻度を軽量カウンタで記録し、取得終了後に要約出力する
- [x] DDR ステータスポーリング回数を軽量カウンタで記録し、取得終了後に要約出力する
- [x] FPGA DDR レジスタ値（`WAVE_WR_CNT`, `WAVE_RD_CNT`, `DDR_WR_END/DDR_RD_END`）を hot-path を乱さない形で集計し、取得終了後に要約出力する
- [x] Debug ビルド成功、軽量計測サマリが1取得サイクルで出力されても waveform が壊れないことを確認（2026-03-09 16:47 JST run で waveform 正常確認）
- [x] ベースライン計測結果を process_log に記録（`2603091643.log` の 2026-03-09 16:47:08 run）

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
```

---

## Phase 1: DLL 致命バグ修正（PR-02）

依存: Phase 0 (PR-01)

- 注記（2026-03-11 更新）: versioned field logs により、`0.1.4` と `cmp_76b2b2a` は正常完走、`0.1.5` と生の `76b2b2a` は取得開始後にログが途切れて再起動相当、`0.1.6` follow-up は `new[]/delete[]` 化後に EP6 timeout で未完走と判明した。Phase 1 の完了条件は comparison build の成功条件である `per-call local scratch buffer + CRT malloc/free` の維持に置き、shared mutex 分離は後続 Phase で再評価する。

- [x] EP6 shared mutex 方針を comparison build と整合させ、現時点では shared mutex 維持・後続 Phase で再評価と明記
- [x] OVERLAPPED イベントハンドルリーク修正（EP2/EP4/EP6 各関数末尾に `CloseHandle`）
- [x] OVERLAPPED 構造体の `ZeroMemory` 初期化追加
- [x] EP6 scratch buffer 戦略を修正し、comparison build と同じ per-call local heap buffer + CRT `malloc/free` backend に固定する
- [x] Mutex 待機タイムアウト追加（`INFINITE` → 5000ms）
- [x] `SaveWaveDataToFile` の失敗検知強化
- [x] TDD: 各修正に対する failing test を先に追加し、修正後に pass を確認
- [x] `0.1.4 / 0.1.5 / 76b2b2a / cmp_76b2b2a / 0.1.6` の実機ログ比較結果を plan / process log / troubleshooting に記録する
- [ ] 既存通信シーケンスで回帰なし、timeout 率が baseline 以下を確認
- [ ] 実機確認: PR-02 完了直後に通常取得 3-5 サイクルを実施し、waveform 正常・`[PR01][CYCLE]` が baseline から大きく悪化しない・timeout 率が baseline 以下であることを確認

---

## Phase 1: Save Path バリデーション強化（PR-02b）

依存: Phase 0 (PR-01)

- [x] `ValidateSavePath()` 関数新設（空文字・ディレクトリ存在・書き込み権限・`..` 拒否）
- [x] Windows 予約名チェック実装（`^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)`）
- [x] 制御文字（0x00-0x1F）拒否実装
- [x] 呼び出しタイミング実装（起動時・UI変更時・Set Parameters 時）
- [x] フォルダ選択ダイアログ `Cancel` 時にも SavePath 再検証を実行
- [x] バリデーション失敗時のエラーメッセージ表示
- [x] TDD: T9-T13 のテストケースを先に追加
- [x] T9-T13 全件 pass
- [x] TDD: UIトリガーポリシーの T17-T19（Startup / FolderDialogCancel / TextChanged）を追加し pass

---

## Phase 1.5: preview consumer 耐性の短期安定化（PR-03a）

依存: Phase 1 (PR-02b)

- [ ] `FlushCloseAndPublishWavePair()` の失敗を acquisition fatal に直結させない方針を実装する
- [ ] publish failure 時の保持方針を実装する（`.tmp` 保持または quarantine 退避）
- [ ] consumer は `.tmp` を見ず `.bin` のみ読む公開契約をコード/ログ上で明確化する
- [ ] hot-path logging を最小限に保ち、publish failure の件数・理由・再試行結果のみ低頻度で残す
- [ ] Version A 調査結果を反映し、UI/file logging を acquisition hot-path から段階的に外す前提をコード/設計に反映する
- [ ] TDD: T5（publish 失敗でも取得継続）を先に追加し pass
- [ ] TDD: T16（`sys_app` preview 相当で `.bin` 継続読み取り中も acquisition 継続）を追加し pass
- [ ] 実機確認: PR-03a 完了直後に preview 相当 consumer 動作ありで 20-50 サイクルを実施し、publish failure が acquisition fatal にならず、`.bin` 読み取り中も取得継続することを確認

---

## Phase 2 前半: Queue 実装と I/F 導入（PR-03）

依存: Phase 1.5 (PR-03a)

- [ ] `WaveAcquisitionEngine.h` 新規作成（型定義: `EngineStatus`, `AcquisitionConfig`, `WaveChunk`）
- [ ] `BlockingQueue<WaveChunk>` 実装（`std::mutex` + `std::condition_variable`）
- [ ] `Enqueue` / `Dequeue` のタイムアウト・停止要求時挙動実装
- [ ] エラーコード追加（`USB_ERR_INVALID_STATE`, `USB_ERR_DEVICE_DISCONNECTED`, `USB_ERR_THREAD_STOP_TIMEOUT`, `USB_ERR_QUEUE_FULL_TIMEOUT`, `USB_ERR_INVALID_OUTPUT_PATH`, `USB_ERR_OUTPUT_PATH_NOT_FOUND`, `USB_ERR_OUTPUT_PATH_NOT_WRITABLE`）
- [ ] TDD: Queue 正常系・timeout ケースの UnitTest 追加
- [ ] UnitTest 全件 pass

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat x64\Debug\AnalogBoard_UnitTest.exe"
```

---

## Phase 2 後半: Reader/Writer/Publisher 分離（PR-04）

依存: Phase 2 前半 (PR-03)

- [ ] USB Reader スレッド実装（EP6 データ読み取り + EP4 ステータスチェック専念）
- [ ] File Writer スレッド実装（`.tmp` 作成・追記・close 専念）
- [ ] Publisher 責務実装（completed `.tmp` の rename / retry / quarantine / consumer 互換維持）
- [ ] `LoopTestProcessThread_EP6_GetData` の主要ロジックを Engine 経由に移行
- [ ] キュー上限時ポリシー（バックプレッシャー）の実装
- [ ] TDD: 遅延注入時も取得継続、`USB_ERR_QUEUE_FULL_TIMEOUT` 遷移確認
- [ ] TDD: T17（`last_n + 1` probe と publish の整合）を追加し pass
- [ ] I/O / publish / consumer 影響の直列実行が解消されていることを計測ログで確認
- [ ] 実機確認: PR-04 完了直後に 100 サイクル以上を実施し、停止/欠損/破損なし、I/O / publish / consumer 影響の直列実行解消を確認

---

## Phase 3: スレッド安全性と堅牢性の向上（PR-05）

依存: Phase 2 後半 (PR-04)

- [ ] スレッド制御フラグを `std::atomic<int>` に変更（`g_bEP24LoopFlag`, `g_bEP6ThreadFlag`, `g_bStartSampling`）
- [ ] USB 転送リトライ機構実装（段階的 backoff 付き、最大 N 回）
- [ ] DDR ステータスポーリング改善（`Sleep(0)` → `Sleep(1)` + adaptive delay）
- [ ] DDR 書き込み完了タイムアウトの設定値化（デフォルト 10秒）
- [ ] スレッド終了待機の適切化（`WaitForSingleObject` + timeout）
- [ ] TDD: T3（retry 内復帰）、T4（retry 上限超過 Error）テスト追加・pass
- [ ] 実機確認: PR-05 完了直後に Stop/再開/USB 再接続の短時間シナリオを実施し、retry・停止待機・復帰パスが想定どおり動作することを確認

---

## Phase 4: コード構造改善・状態遷移明確化（PR-06）

依存: Phase 3 (PR-05)

- [ ] `WaveAcquisitionEngine` クラスに `Start()` / `Stop()` / `GetStatus()` 実装
- [ ] 状態遷移の明確化（`Idle → Sampling → Draining → Publish → Completed/Error`）
- [ ] `Draining` ステートと FPGA 側 `RD_WAIT` の対応関係をコード/ログで追跡可能にする
- [ ] publish failure を acquisition から切り離す補助状態（`PublishRetry` / `Degraded` 等）の要否を確定し、必要なら実装
- [ ] `Error` → `Idle` 復帰パス実装（`Stop()` でリソースクリーンアップ）
- [ ] USB 物理切断時の検知と `USB_ERR_DEVICE_DISCONNECTED` エラー遷移
- [ ] 途中停止時のデータ整合性ポリシー実装（`.tmp` 保持、未読出量ログ、Publish 遷移防止）
- [ ] `.tmp` 失敗時の隔離ディレクトリ退避（`{SavePath}\.quarantine\{YYYYMMDD_HHmmss}\`）
- [ ] グローバル変数のクラスメンバ移動
- [ ] TDD: T6（Draining 中 Stop）テスト追加・pass
- [ ] Dialog1_Main.cpp の行数削減目標確認（各ファイル 1000行以下）

---

## Phase 5: テスト戦略・耐久テスト（PR-07）

依存: Phase 4 (PR-06)

- [ ] T1（正常系チャンク受け渡し）テスト追加・pass
- [ ] T2（Queue 容量1 + Writer 遅延）テスト追加・pass
- [ ] T7（8時間連続運転 Soak テスト）実行手順整備
- [ ] T8（既存 `wave_file_publish` 回帰テスト）全件 pass
- [ ] T14（EP6 FIFO フル状態再現）テスト追加・pass
- [ ] T15（オートモード DDR 領域枯渇再現）テスト追加・pass
- [ ] 擬似 USB タイムアウト注入（DLL モック化）テスト整備
- [ ] 擬似遅延書き込みテスト整備
- [ ] preview 実行中に acquisition 停止が再現しないことを soak / integration で再確認する
- [ ] Soak テスト結果を成果物として保存
- [ ] 実機確認: PR-07/PR-08 判定用として 3 セッション（各1000回取得）と 8 時間連続運転を実施し、KPI 判定に必要な実測値を取得

**検証コマンド:**
```bat
cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /t:AnalogBoard_UnitTest:Rebuild /p:Configuration=Debug /p:Platform=x64 /m:1"
cmd /d /c "scripts\run_with_vsdevcmd.bat x64\Debug\AnalogBoard_UnitTest.exe"
```

---

## Phase 6: KPI 判定・オプション検討（PR-08）

依存: Phase 5 (PR-07)

- [ ] 計測環境の記録（PC 型番・CPU・メモリ・ディスク種別・USB 構成・FPGA FW バージョン）
- [ ] 3セッション（各1000回取得）実施
- [ ] 8時間連続運転 1回実施
- [ ] 負荷条件テスト（通常/ディスク遅延 50ms,100ms/擬似 timeout 1%,5%）実施
- [ ] セッションごとの平均値に加えて最悪値を記録し、しきい値超過時は Phase 2-4 見直し判断に使う
- [ ] KPI 判定: タイムアウト起因失敗率 < 0.1%（3セッション平均）
- [ ] KPI 判定: low/high ペア欠損ゼロ、破損ゼロ
- [ ] KPI 判定: preview 実行中でも acquisition 停止 0 件
- [ ] KPI 判定: スループット・最大遅延の baseline 比改善確認
- [ ] 判定結果と次アクション（継続改善 or Rust 検討）を process_log に記録
- [ ] プランの「ステータス」テーブルを更新

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] `wave_file_publish` 回帰テスト pass（low/high 出力順序互換維持）
- [x] TDD 順守: failing test → 修正 → pass の順序で実施
- [x] process_log にエントリ追記

---

## 移行・ロールバック確認

- [ ] `UseAsyncWriter` フラグによる旧経路/新経路切替動作確認
- [ ] ロールバック手順のリハーサル（`UseAsyncWriter=0` 切替 → 旧経路動作確認）
- [ ] 隔離された `.tmp` の手動 Publish / `.quarantine` 保持判断フローを確認
- [ ] 段階展開（開発機 → 検証機 → 長時間連続運転）の実施
- [ ] ベースライン計測完了後、`[PR01][EP6]` の詳細ログ（呼び出しごと出力）を削除
