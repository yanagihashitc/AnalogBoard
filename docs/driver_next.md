# Driver Next

対象 branch: `feature/win11-driver-compat`

対象 worktree: `D:\ubuntu\jupyter\sys_analyzer\AnalogBoard-win11-driver`

## Purpose

このメモは Win11 new driver compatibility track の「次に何を実行するか」だけを見るための短い実行メモ。

参照:

- 全体方針: [2026-03-02-usb-acquisition-stability.md](./2026-03-02-usb-acquisition-stability.md)
- active checklist: [2026-03-02-usb-acquisition-stability-checklist.md](./2026-03-02-usb-acquisition-stability-checklist.md)
- architecture: [2026-03-02-usb-acquisition-stability-architecture.md](./2026-03-02-usb-acquisition-stability-architecture.md)
- 履歴: [process log](./process_log/2026-03-02-usb-acquisition-stability-log-02.md)

## Goal

**Win11 + new driver で `USBBoard_Connect` と idle `EP4_GetData` の first gate を通す。**

## Current Status

- dedicated worktree は作成済み
- `CyAPI.h` は SDK 1.3 と一致、`CyAPI.lib` は worktree に補完済み
- `AnalogBoard_Dll` には endpoint discovery hardening を入れた
- focused unit test は pass、`Release|x64` の DLL / TestApp rebuild は pass
- full `build_test.bat` はこの tag 既存の `WaveDataFileIO_test` failure が残っている

## Read First

1. [2026-03-02-usb-acquisition-stability.md](./2026-03-02-usb-acquisition-stability.md)
2. [2026-03-02-usb-acquisition-stability-checklist.md](./2026-03-02-usb-acquisition-stability-checklist.md)
3. [2026-03-02-usb-acquisition-stability-architecture.md](./2026-03-02-usb-acquisition-stability-architecture.md)
4. [process_log/2026-03-02-usb-acquisition-stability-log-02.md](./process_log/2026-03-02-usb-acquisition-stability-log-02.md)

## Do Next

1. `feature/win11-driver-compat` worktree の `x64/Release` 出力物を Win11 検証 PC に配布する
2. new driver + matching SDK が入った状態で `USBBoard_Connect` の成功を確認する
3. `FPGA Debug` タブを開いて `EP4 RX` ボタンを数回押し、idle 状態の `EP4_GetData` を確認する
4. `Get ep4 register data failed.` / disconnect が出ないことを確認する
5. first gate を通ったら `EP2 -> EP4 -> EP6` の順で smoke を確認する
6. その後に low-density / high-density 実機で timeout と `EP4` failure を再評価する

## Observe

- `USBBoard_Connect` の成功/失敗
- `FPGA Debug -> EP4 RX` の成否
- idle `EP4_GetData` の再現性
- `Get ep4 register data failed.`
- `USB board is removed! Disconnect usb.`
- endpoint discovery の変化有無

## Constraints

- `baseline/0.1.4-hw-recovery` の PR-04 field gate とは切り分ける
- public API (`EP2_SendData`, `EP4_GetData`, `EP6_GetData`) は維持する
- `Dialog1_Main` や acquisition semantics は new driver 対応だけを理由に広く触らない
- full suite の既存 `WaveDataFileIO_test` failure は別件として扱う

## Done When

- new driver と matching SDK の差分が docs に記録されている
- 必要な code 差分が `AnalogBoard_Dll` 側に限定されている
- Win11 + new driver で `USBBoard_Connect` と idle `EP4_GetData` が安定する
