# Baseline Next

対象 branch: `dev`

## Purpose

このメモは baseline branch の「次に何を実行するか」だけを見るための短い実行メモ。

参照:

- 全体方針: [2026-03-02-usb-acquisition-stability.md](./2026-03-02-usb-acquisition-stability.md)
- active checklist: [2026-03-02-usb-acquisition-stability-checklist.md](./2026-03-02-usb-acquisition-stability-checklist.md)
- field signature: [2026-03-02-usb-acquisition-stability-field-reference.md](./2026-03-02-usb-acquisition-stability-field-reference.md)
- 履歴: [process log](./process_log/2026-03-02-usb-acquisition-stability-log.md)

## Goal

**PR-04 field gate を閉じる。**

## Restart Memo

- 今日の停止点: `r18` 向け code は実装・unit test・`Release|x64` rebuild まで完了
- 次回の最初の一手: **`0.1.4r18` 相当を実機で high-density `x1` だけ回す**
- 回収する証跡:
  - `logs/0.1.4r18/logs/*.log`
  - 必要なら対応する `data/0.1.4r18/*_cfg.txt`
- 最優先で見る行:
  - `[PR04][TIMEOUT_RECOVERY] phase=resume`
  - `retryBackoffMs=20`
  - `nextPlannedReadSize=...`
  - `deltaWr=... deltaRd=... deltaBacklog=...`
  - `[PR01][CYCLE] status=... error=... publishedPairs=...`
- 期待:
  - `retryBackoffMs=20` が出る
  - `nextPlannedReadSize` が `31211520` より小さい
  - `deltaBacklog` が `r17` より悪化しない
- 分岐:
  - `first timeout` 自体が消えたら、そのまま high-density `x3`
  - `first timeout` が残っても `TIMEOUT_RECOVERY` が改善したら、もう 1 回 high `x1` 比較
  - `retryBackoffMs=20` や clamped `nextPlannedReadSize` が出ないなら deploy mismatch を先に疑う

## Do Next

1. `0.1.4r16` 相当では behavior tuning を止め、`[PR04][TIMEOUT_RECOVERY]` を使って high-density `x1` の cause isolation を最優先で進める
2. `One time max size=31211520byte` を前提に、1 回目 timeout 直後の `requestedReadSize`, `unreadBytes`, `backlogBytes` と、resume 側の `nextPlannedReadSize`, `deltaWr`, `deltaRd`, `deltaBacklog` を回収する
3. `0.1.4r16` では `deltaWr=1047822336`, `deltaRd=65536`, `deltaBacklog=1047756800`, `nextPlannedReadSize=31211520`, `retryBackoffMs=0` だったため、first timeout 後の host retry/read-size 制御が第一候補とみなす
4. `A` の入口として DLL 側に `EP6 TimeOut=30000ms` と `retry backoff=5ms` を入れたが、`0.1.4r17` でも first timeout は残った
5. `r17` では `ep6MaxMs=34172` まで伸びたため DLL timeout 延長は反映されている可能性が高いが、resume 直後に `deltaWr=2975662080`, `deltaRd=49152`, `deltaBacklog=2975612928`, `retryBackoffMs=0` となり main blocker は依然として engine 側 recovery path とみなす
6. `r18` 相当では engine 側に first-timeout-only `backoff=20ms` と `retry read clamp=64KiB` を入れたので、high-density `x1` で `TIMEOUT_RECOVERY` の `retryBackoffMs=20` と `nextPlannedReadSize` の縮小を確認する
7. failed run の `_cfg.txt` と completed `.bin` pair の対応を使って、timeout 直前の publish 境界と waveform 品質を spot check する
8. low-density / high-density ともに success run の `.bin` pair が壊れていないことを spot check する
9. retry tuning 後に high-density `x1` を再実施して `TIMEOUT_RECOVERY` の変化を比較する
10. その後 high-density `x3` を再実施する
11. high-density `x3` が clean pass したら low-density `x100` に進む
12. 続けて high-density `x100` に進む
13. preview / consumer 併用 session を 1 本回す

## Observe

- `[PR04][ENGINE_CONTEXT] curObject/mainDlg/usbLibInfo`
- `[PR04][ENGINE_ENTER]`
- `[PR04][ENGINE_USB_SESSION_NULL] api=...`
- `[PR04][RECOVERY_STOP] status/stage`
- `[PR04][ENGINE_EXIT] status/error/savedWaveCount/publishedPairs`
- `[PR04][STARTUP_EP4] poll/WAVE_WR_CNT/WAVE_RD_CNT/DDR_WR_END/DDR_RD_END/savedBytes`
- `[PR04][TIMEOUT_RECOVERY] phase/ordinal/requestedReadSize/unreadBytes/backlogBytes/nextPlannedReadSize/deltaWr/deltaRd/deltaBacklog`
- `[PR01][CYCLE] status/error/publishedPairs`
- `One time max size`
- failed run の `_cfg.txt` に `Run Status / Error Code / Published Pairs / Saved Wave Count`
- completed pair だけが `*_fh_*.bin` / `*_fl_*.bin` として残り、open 中の `.tmp` は残っていないこと
- `.tmp` 残置の有無
- `DDR_WR_END=0`, `DDR_RD_END=0` のまま timeout しているか
- first timeout 後に `deltaWr >> deltaRd` になって backlog が急増していないか
- `retryBackoffMs=0` のまま次回 read が即再開していないか
- DLL 側 `EP6 TimeOut=30000ms`, `retry backoff=5ms` の build が field binary に入っている前提で比較すること
- engine 側 `retryBackoffMs=20` と clamped `nextPlannedReadSize` が `TIMEOUT_RECOVERY` に出ているか

## If Blocked

- `saved wave count 0` / immediate exit が出たら、100-cycle へ進まない
- `ENGINE_EXIT error=-21001` が残るなら、`Dialog1_Main` の parent binding fix が field binary に入っているかを先に確認する
- `ENGINE_CONTEXT` が出ず `STARTUP_EP4` と `PR01][CYCLE]` も両方出ないなら、build/deploy mismatch を先に疑う
- `ep6_timeout` の直後に自動で次 cycle が始まるなら、`AcquisitionCycleRecoveryPolicy` fix が field binary に入っているかを先に確認する
- failed run の `_cfg.txt` に result metadata が無いなら、field binary に cfg append fix が入っているかを先に確認する
- `One time max size` が従来どおり `124813312byte` なら、field binary に 1-file burst cap fix が入っているかを先に確認する
- `[PR04][TIMEOUT_RECOVERY]` が出ないなら、field binary に timeout recovery observation fix が入っているかを先に確認する
- high-density `x3` で `ep6_timeout` が続くなら、100-cycle へ進まず timeout 後 retry path と waveform quality の切り分けを先に進める
- new driver 対応は [driver_next.md](./driver_next.md) を正本にし、`feature/win11-driver-compat` / `D:\ubuntu\jupyter\sys_analyzer\AnalogBoard-win11-driver` で進める

## After Pass

- PR-05 (`std::atomic<int>`, retry/backoff, stop wait) に進む
