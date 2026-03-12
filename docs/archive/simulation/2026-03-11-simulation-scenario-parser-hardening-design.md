# Simulation Scenario Parser Hardening Design

作成日: 2026-03-11

## Background

`SimulationScenario.cpp` の JSON 風パーサは `std::stoul` / `std::stoi` と正規表現に依存している。
現状は数値の範囲超過を missing 扱いにしてしまう箇所があり、`ep6_results` の配列は改行を含む整形 JSON を許容できない。

## Scope

- 数値フィールドで missing / negative / out-of-range を区別する
- required / optional の整数フィールドで範囲超過を明示的な validation error にする
- `ep6_results` で複数行配列を読み込めるようにする
- 既存の成功ケースと負値チェックは維持する

## Approach

1. 数値トークン抽出は既存の `FindNumberToken` を使い続ける
2. 数値変換は共通 helper で `unsigned long long` / `long long` に変換し、対象型の範囲を明示判定する
3. 変換結果を status enum で返し、呼び出し側が `outError` を field-specific に設定する
4. 配列抽出は `[\s\S]*?` 相当のパターンで改行を跨いで最短一致させる

## Risks

- 既存の error message 文言が一部変わる
- regex を広げることで過剰一致しないよう、最短一致を維持する必要がある

## Test Strategy

- `ULONG` max + 1 の required field が validation error になること
- `INT` max + 1 の required/optional field が validation error になること
- pretty-printed `ep6_results` が読み込めること
- 既存正常系が維持されること
