# SimRunner Review Follow-ups Design

作成日: 2026-03-11

## Background

PR review で以下の follow-up が残った。

- `timeout_retry_limit` が負値でも load できてしまう
- 波形総量が `ULONG` を超える scenario を validation で止められない
- `AnalogBoard_SimRunner.exe` が current working directory を repo root とみなしてしまう
- `write_delay_ms` / `write_fail_at` / `publish_fail_at` の struct default が parser で到達不能
- simulation integration test が `logs/sim` を汚し続ける

## Scope

- `SimulationScenario` の validation を non-negative / overflow-safe にする
- optional field omission 時に struct default を維持する
- SimRunner の repo root 解決を executable path ベースにする
- 上記を固定する unit / integration test を追加する
- integration test の生成物を teardown で削除する

## Approach

1. `SimulationScenario` に field-specific な lower-bound check を追加し、`timeout_retry_limit` の負値を reject する
2. `write_delay_ms` / `write_fail_at` / `publish_fail_at` は optional loader に切り替え、struct default をそのまま使う
3. `wave_size_low + wave_size_high` と `total_wave_count` の積が `ULONG` に収まるか validation で確認する
4. `wmain()` は `GetModuleFileNameW()` から executable path を取得し、親 directory を遡って repo root を見つける
5. integration test は `RunPreset()` 実行後に output directory を再帰削除して、test side effect を残さない

## Risks

- optional field omission を許可すると parser error message が一部変わる
- repo root 推定ロジックが build 配置に依存するため、想定外の配置では fallback を残す必要がある

## Test Strategy

- `timeout_retry_limit=-1` が validation error になること
- `write_delay_ms` / `write_fail_at` / `publish_fail_at` を省略しても load 成功になること
- 総バイト数 overflow scenario が validation error になること
- `x64\Debug` から SimRunner を直接起動しても preset を読めること
- simulation integration test 実行後に output directory を削除すること
