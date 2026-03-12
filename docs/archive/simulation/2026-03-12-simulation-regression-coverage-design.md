# Simulation Regression Coverage Design

## Goal

今回の `0.2.0` 回帰を simulator でも再現し、unit test と preset 実行で将来の取りこぼしを防ぐ。

## Scope

- `SimulationScenario` の検証を anomaly simulation に必要な範囲で緩和する
- `slow_producer` を非 `16KB` アラインな中間 progress を出す preset に更新する
- `empty_capture` preset と integration test を追加する
- simulator 向けの exit code / guide を更新する

## Approach Options

### Option 1: Existing preset update + targeted anomaly preset

- `slow_producer` を今回の本命回帰条件に寄せる
- `empty_capture` は専用 preset を追加する
- validation は `producer_step_bytes` の alignment 制約と `total_wave_count > 0` 制約だけを見直す

Pros:

- 既存の `slow_producer` が名前どおり slow progress を検証する
- 変更範囲が小さく、既存 SimRunner 実装を再利用できる

Cons:

- preset の意味が少し変わるため、guide 更新が必要

### Option 2: New preset only, existing preset unchanged

- 既存 preset はそのままにして `slow_producer_unaligned` のような新規 preset を追加する

Pros:

- 既存 preset の意味を変えない

Cons:

- 似た preset が増え、回すべきものが分散する
- `slow_producer` の名前と実態のずれが残る

## Recommendation

Option 1 を採用する。`slow_producer` は既存参照が少なく、今回拾いたい regression に最も近い名前なので、そのまま主回帰 preset にする方が運用しやすい。

## Test Strategy

- Scenario unit test:
  - 非 `16KB` アラインな `producer_step_bytes` を許可する
  - `total_wave_count = 0` を anomaly simulation 用に許可する
  - `producer_step_bytes` と `producer_bursts_per_poll` の排他制約は維持する
- Integration test:
  - `slow_producer` preset が `success` で `.bin` を生成する
  - `empty_capture` preset が `empty_capture` で終わり `.bin` を生成しない

## Risks

- `total_wave_count = 0` を一般的な正常系として誤用しないよう、guide に anomaly 用であることを明記する
- simulator exit code に `empty_capture` を追加しないと integration test の判定が曖昧になる
