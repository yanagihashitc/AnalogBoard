# 0.2.0 WaveAcquisitionEngine Fix Design

## Goal

`0.2.0` で `WaveAcquisitionEngine` が中間読み取り時の 16KB alignment error や空取得成功扱いで早期終了し、実機でデータが記録されない問題を防ぐ。

## Scope

- `WaveAcquisitionEngine` の EP6 読み取りサイズ決定ロジックを修正する
- `savedWaveCount == 0` のまま成功扱いされる経路を防ぐ
- unit test で再現ケースを固定する

## Approach Options

### Option 1: Intermediate read truncation + empty-capture guard

- DDR 書き込み中 (`ddrWriteCompleted == false`) の非アライン読み取りは 16KB 境界まで切り捨てる
- 最終読み取り (`ddrWriteCompleted == true`) のみ切り上げでパディングを読む
- 切り捨て後に 0 byte の場合は次回 EP4 poll を待つ
- `savedWaveCount == 0` で終了した場合は success にしない

Pros:

- 最小差分で既存設計を維持できる
- 実機ログの症状と最も整合する

Cons:

- reader / writer 分離の根本対策ではない

### Option 2: Always round up to 16KB

- 中間読み取りも常に切り上げる

Pros:

- 実装が単純

Cons:

- 未書き込み領域を先読みするリスクがある
- `logicalBytesFromRead` との差分管理が複雑になる

## Recommendation

Option 1 を採用する。中間読み取りで未書き込み領域を読まないことを優先し、既存の最終読み取りパディング設計と整合させる。

## Test Strategy

- Normal: 中間 `waveWrCnt` が 16KB 非アラインでも `AlignmentError` にならず完走する
- Boundary: 切り捨て結果が 0 byte の場合は poll 継続して後続データを読む
- Abnormal: `savedWaveCount == 0` のまま cycle 完了した場合は success にしない

## Risks

- 空取得 guard のエラー種別は新規追加せず、既存 `AlignmentError` や `Ep4ReadFailed` と混同しないよう専用 status が必要
- 実機側の stale status がある場合、空取得 guard のみでは不十分な可能性がある
