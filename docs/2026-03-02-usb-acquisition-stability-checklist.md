# USB データ取得・書き込み安定性改善 タスクチェックリスト

対象プラン: [USB データ取得・書き込み安定性改善プラン](./2026-03-02-usb-acquisition-stability.md)
architecture: [Architecture Notes](./2026-03-02-usb-acquisition-stability-architecture.md)
execution: [Execution Notes](./2026-03-02-usb-acquisition-stability-execution.md)
field: [Field Reference](./2026-03-02-usb-acquisition-stability-field-reference.md)
process log: [Process Log](./process_log/2026-03-02-usb-acquisition-stability-log.md)
作成日: 2026-03-05
最終同期: 2026-03-19

## Current Execution Rule

- **実装本線は `baseline/0.1.4-hw-recovery`**
- **`lab/0.2.2-engine-semantics` は non-blocking な verification asset**
- **Win11 new driver 対応は `feature/win11-driver-compat` worktree へ切り出した**
- **日々の実行は [baseline_next.md](./baseline_next.md) / [lab_next.md](./lab_next.md) / [driver_next.md](./driver_next.md) を見る**

現在の到達点:

- Phase 1 の release-track gate は `0.1.4r7` で閉じた
- Phase 2 は PR-04 code-side まで進み、field gate が次の本線
- Win11 new driver 対応は `feature/win11-driver-compat` / `AnalogBoard-win11-driver` で進行中

## Active Release Track

### PR-04 field gate

- [x] `0.1.4r13` で `ENGINE_EXIT error=-21001` が消えていることを確認する
- [x] `[PR04][ENGINE_CONTEXT]` が出て `mainDlg` / `usbLibInfo` が `0x0` でないことを確認する
- [x] `[PR04][ENGINE_USB_SESSION_NULL]` が出ていないことを確認する
- [x] `ep6_timeout` 後に auto mode が次 cycle へ自動再突入しないことを確認する
- [x] `[PR04][RECOVERY_STOP]` が出て stop command の成否を確認できることを確認する
- [x] low-density smoke 1-3 cycle で `saved wave count 0` / immediate exit が消えていることを確認する
- [x] `[PR04][ENGINE_ENTER]` / `[PR04][ENGINE_EXIT]` を 1 cycle で確認する
- [x] `[PR04][STARTUP_EP4]` を回収し、startup snapshot を確認する
- [x] `[PR01][CYCLE]` に `status` / `error` / `publishedPairs` が出ることを確認する
- [x] non-success cycle でも open 中の `.tmp` だけを捨て、completed pair は保持する方針を code に反映する
- [x] failed run の `_cfg.txt` に `Run Status / Error Code / Published Pairs / Saved Wave Count` が残ることを確認する
- [ ] `[PR04][TIMEOUT_RECOVERY]` を high-density `x1` で回収し、timeout 直後の host/device 進行差分を切り分ける
- [ ] high-density `x3` を timeout / disconnect / waveform corruption なしで通す
- [ ] 100 cycle 以上で停止 / 欠損 / 破損なしを確認する
- [ ] preview / consumer 併用 session を 1 本以上回し、`.tmp` 非公開と pair index 単調増加を確認する
- [ ] I/O / publish / consumer 影響の直列実行が解消されていることを log で確認する

field signature と session bundle は [Field Reference](./2026-03-02-usb-acquisition-stability-field-reference.md) を参照。

## Phase 1.5 Carryover

- [ ] Version A 調査結果を反映し、UI/file logging を acquisition hot-path から段階的に外す前提をコード/設計に反映する
- [ ] T16: `sys_app` preview 相当で `.bin` 継続読み取り中も acquisition 継続することを固定する
- [ ] preview 相当 consumer 動作ありで 20-50 サイクル実機確認を行う

## Parallel Track: Win11 New Driver Compatibility

implementation branch/worktree:
- `feature/win11-driver-compat`
- `D:\ubuntu\jupyter\sys_analyzer\AnalogBoard-win11-driver`

- [ ] Win11 検証 PC に new driver と matching SDK を導入し、SDK path / version / provider を記録する
- [ ] repo 内の `CyAPI.h` / `CyAPI.lib` / `cyusb3.inf` と SDK 版を比較し、GUID / binding / endpoint 前提差分を記録する
- [ ] plan / checklist / process log に new driver track の source of truth を固定する
- [ ] dedicated branch / worktree 上で `CyLib` を SDK-matched 版へ更新する
- [ ] USB 接続層に binding / endpoint diagnostics を追加する
- [ ] first gate: `USBBoard_Connect` 成功と idle 状態での `EP4_GetData` 安定化を確認する
- [ ] second gate: `EP2 -> EP4 -> EP6` の順で通信 smoke を確認する
- [ ] third gate: low-density / high-density 実機で timeout と `EP4` failure を再評価する

## Phase Summary

### Phase 0

- [x] lightweight instrumentation と baseline capture を完了した

### Phase 1

- [x] comparison build に合わせた DLL hardening を完了した
- [x] completion semantics helper を baseline へ fixed backport した
- [ ] 既存通信シーケンスで timeout-rate が baseline 以下であることを再確認する

### Phase 2

- [x] PR-03 queue / contract 導入を完了した
- [x] PR-04 code-side の Reader / Writer / Publisher 分離を完了した
- [ ] PR-04 field gate を閉じる

### Phase 3

- [ ] `std::atomic<int>` 化
- [ ] retry / backoff と stop wait の整理
- [ ] stop / reconnect short scenario 実機確認

### Phase 4

- [ ] `WaveAcquisitionEngine::Start/Stop/GetStatus` を整理する
- [ ] state machine と cleanup path を固定する
- [ ] disconnect / partial data / quarantine path を明確化する

### Phase 5

- [ ] T1 / T2 / T7 / T8 / T14 / T15 を揃える
- [ ] timeout injection / delayed write test を整備する
- [ ] soak 結果を成果物として保存する

### Phase 6

- [ ] 3 session + 8h run の KPI 判定を行う
- [ ] timeout-rate / corruption / preview tolerance の acceptance を確定する
- [ ] 結果に応じて継続改善か次の選択肢を決める

## Common Checks

- [x] UnitTest 全件 pass
- [x] Debug x64 Rebuild 成功
- [x] `wave_file_publish` 回帰テスト pass
- [x] TDD 順守
- [x] process log 更新

## Rollback Checks

- [ ] `UseAsyncWriter` フラグによる旧経路 / 新経路切替を確認する
- [ ] ロールバック手順をリハーサルする
- [ ] retained `.tmp` / `.quarantine` の扱いを確認する
- [ ] 段階展開の手順を確認する
