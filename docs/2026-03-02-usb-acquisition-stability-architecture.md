# USB Acquisition Stability Architecture Notes

対象プラン: [USB データ取得・書き込み安定性改善プラン](./2026-03-02-usb-acquisition-stability.md)

この文書は architecture / contract / design rationale の置き場とする。実行順や現行 gate は checklist / next docs を参照する。

## Root Causes

### 1. EP6 shared mutex hold

- `EP6_GetData()` が `m_hEP2EP4Mutex` を bulk transfer 全体で保持する
- `EP4` status read と `EP2` command send が待たされる
- comparison build では shared mutex 維持でも完走実績があるため、直近の release-track では「要再評価」に格下げした

### 2. USB read and file write are serialized

- `LoopTestProcessThread_EP6_GetData` が read -> write -> read を単一 thread で直列実行していた
- write / close / publish の遅延で EP6 read が止まり、DDR backlog が伸びる
- Phase 2 の主眼は、この直列構造を Reader / Writer / Publisher に分離すること

### 3. OVERLAPPED event handle leak

- `EP2` / `EP4` / `EP6` で `CreateEvent()` した handle が `CloseHandle()` されていなかった
- 長時間運用で handle leak を起こしうる

### 4. Per-call 4MB scratch buffer allocation

- `EP6_GetData()` が毎回 `malloc/free` 相当を繰り返す
- allocator backend の違いで regression を起こした履歴があるため、comparison build と同じ戦略を source of truth とする

### 5. Non-atomic thread flags

- `g_bEP24LoopFlag`, `g_bEP6ThreadFlag`, `g_bStartSampling` が plain `INT`
- Phase 3 で `std::atomic<int>` へ移行する

### 6. Save path validation is too late

- Set Parameters 時点で空文字しか見ていなかった
- path existence / writability / traversal を事前検証しないと、機械は動いたのに保存されない事故が起こる

### 7. Publish / consumer failures are acquisition-fatal

- publish failure が acquisition loop を直接落としていた
- `.tmp` と `.bin` の公開契約を整理し、publish failure non-fatal を Phase 1.5 の前提にした

## Design Contracts

### Completion semantics

- `DDR_WR_END` は draining hint
- `DDR_RD_END` は final completion
- `WAVE_WR_CNT` は readable upper bound の目安だが、`DDR_WR_END==1` だけで final size を固定しない
- startup stale snapshot は active cycle として latch しない

### Engine state model

```text
Idle -> Sampling -> Draining -> Publish -> Completed
   \-> Error
```

- `Sampling`: EP6 read と active acquisition の期間
- `Draining`: FPGA 側 `RD_WAIT` に対応し、DDR に残ったデータを読み切る期間
- `Publish`: completed pair を `.bin` へ昇格させる期間
- `Error`: transport failure, queue full timeout, publish fatal, disconnect など

### Queue contract

- `BlockingQueue<WaveChunk>` を Reader / Writer 間の境界とする
- 既定ポリシーは wait-based backpressure
- queue full timeout は `USB_ERR_QUEUE_FULL_TIMEOUT`
- stop requested 時は wait を解除して graceful stop する

### Public API stability

- `USB_Lib_Info` の public surface (`EP2_SendData`, `EP4_GetData`, `EP6_GetData`) は維持する
- new driver compatibility track でも、まずは `AnalogBoard_Dll` の binding / endpoint discovery 側だけで吸収する

## Phase Design Notes

### Phase 1

- comparison build の成功条件である `per-call local scratch buffer + CRT malloc/free` を維持する
- completion semantics helper を FPGA reference に合わせる

### Phase 1.5

- publish failure non-fatal
- `.tmp` non-public / `.bin` public の契約固定
- preview consumer 実行中でも acquisition を止めない

### Phase 2

- Reader / Writer / Publisher を分離する
- release-track は baseline 実機挙動を壊さない最小差分で進める
- lab track は semantics / scenario / helper diff の検証資産として使う

### Phase 3-4

- thread-safe stop / retry / state transitions を固定する
- `WaveAcquisitionEngine` の責務を明確化する

## New Driver Compatibility Notes

- 対象は `Win11 + new Cypress/Infineon driver + matching SDK`
- 現時点の first hypothesis は `new driver + old bundled CyLib` mismatch
- SDK 導入後に比較する正本:
  - `CyAPI.h`
  - `CyAPI.lib`
  - `cyusb3.inf`
- 確認項目:
  - interface GUID
  - device binding / service 名
  - endpoint discovery 前提 (`0x02`, `0x84`, `0x86`)

## Related References

- 実行順と gate: [USB acquisition stability checklist](./2026-03-02-usb-acquisition-stability-checklist.md)
- field signature と run bundle: [USB acquisition stability field reference](./2026-03-02-usb-acquisition-stability-field-reference.md)
- PR / test / verification details: [USB acquisition stability execution notes](./2026-03-02-usb-acquisition-stability-execution.md)
