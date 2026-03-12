# WaveAcquisitionEngine Stale DDR_WR_END Follow-up Design

## Goal

`0.2.x` 実機で確認された「取得開始直後の stale `DDR_WR_END=1` により `savedWaveCount=0` で即終了する」症状に対して、境界条件の回帰テストと実機観測用の summary 可視化を追加する。

## Scope

- stale `DDR_WR_END` guard の境界テストを追加する
- `measTrg=1` を含む実機近似 status を固定する
- cycle summary に settling 情報を載せて実機ログで判定できるようにする
- stale 判定分岐を最小限 refactor して読みやすくする

## Approach

### Option 1: Poll-count guard + observability

- 既存の `kDdrSettlingPollLimit` は維持する
- `settlingPollCount` と `sawDdrWrEndClear` を `AcquisitionSummary` に保持する
- unit test に `limit-1` / `limit` / persistent stale を追加する

Pros:

- 最小差分で現状の修正方針を維持できる
- 実機ログで stale guard 発火有無を確認できる

Cons:

- 時間ベース budget ではないため、poll 速度差の影響は残る

### Option 2: Time-based settling budget

- poll 回数ではなく経過時間で stale 判定を管理する

Pros:

- 環境差に強い

Cons:

- 今回の差分としては広がりが大きい
- 実機再観測前に最適値を決めにくい

## Recommendation

Option 1 を採用する。今回の目的は実機症状に対する回帰固定と可観測性強化であり、time-based budget への拡張は次段階で十分。

## Test Strategy

- `measTrg=1` を伴う stale `DDR_WR_END=1` が数 poll 続いても成功する
- stale poll 数が `kDdrSettlingPollLimit - 1` と `kDdrSettlingPollLimit` の両方で premature fail しない
- stale が永続する場合は `EmptyCapture` になり、summary に settling 情報が残る
- success / failure の双方で summary の settling 情報が観測できる

## Risks

- 実機 stale status が `DDR_WR_END` 以外にも複合している場合は追加観測が必要
- poll-count budget 自体の妥当性は実機ログで再確認が必要
