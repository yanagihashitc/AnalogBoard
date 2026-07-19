# High-Density Stress Investigation Runbook

対象 branch / build: `baseline/0.1.4-hw-recovery`, `Release|x64`

関連:
- [USB acquisition stability checklist](./usb-acquisition-stability/2026-03-02-usb-acquisition-stability-checklist.md)
- [Phase 1 Field Session Runbook](./2026-03-12-phase1-field-session-runbook.md)
- [Host-FPGA Acquisition Reference](./2026-03-12-host-fpga-acquisition-reference.md)

## Goal

high-density 条件で出る failure を、completion semantics の pass/fail とは分けて `Type A` / `Type B` / `Type C` に分類し、次の investigation に必要な最小ログを 1 セッションで回収する。

## Non-Goal

- この runbook 単体で原因を断定しない
- `DDR_WR_END` を final completion とみなす legacy 判断へ戻さない
- 1 セッション中に binary / cable / FW / save path を何度も変えない

## Failure Types

- `Type A`: active acquisition 中に `EP6 timeout`。終端時点で `DDR_WR_END=0`, `DDR_RD_END=0`
- `Type B`: drain hint 後に `EP6 timeout`。終端時点で `DDR_WR_END=1`, `DDR_RD_END=0`
- `Type C`: 正常完了。`ep6Timeouts=0`, `DDR_RD_END=1`

## Fixed Conditions

開始前に次を固定して書き残す。

- PC / USB cable / hub
- FPGA FW version
- save path
- log root
- output root
- 実行 binary path
- density 条件の説明

1 run あたり 1 変数だけ変える。最初の high-density 再現までは binary / preview / consumer / save path を変えない。

## Preflight

1. `baseline/0.1.4-hw-recovery` の `Release|x64` build を使う。
2. `logs` と `[PR01][CYCLE]` が回収できる状態で起動する。
3. preview / consumer は無効から始める。
4. 直前 session の output と見分けがつく保存先を用意する。
5. 可能なら low-density control を 1 cycle だけ先に回し、`DDR_RD_END=1` の setup sanity を確認する。

## Run Order

### Run 0: Optional Low-Density Control

目的:
- 配線や保存先の問題と high-density failure を分ける

設定:
- low-density
- auto mode
- 1 cycle

期待:
- `ep6Timeouts=0`
- `DDR_RD_END=1`
- waveform 正常

abort:
- ここで失敗したら high-density investigation に進まず setup issue として止める

### Run 1: High-Density Baseline Reproduction

目的:
- 現行 baseline build で failure signature を再現し、Type A / B / C をまず 1 回確定する

設定:
- high-density
- auto mode
- preview / consumer なし
- 3 cycle を上限に回す

各 cycle で記録:
- log file 名
- cycle index
- waveform 正常 / empty / partial
- terminal signal (`USB Timeout`, `Wait fpga write completed. Timeout`, `Wait acquisition completed. Timeout`, EP4 read fail, disconnect)
- `ep6Timeouts`
- `WAVE_WR_CNT`
- `WAVE_RD_CNT`
- `DDR_WR_END`
- `DDR_RD_END`
- `maxBacklogBytes`
- `ddrWaitPolls`（出ていれば）

分類:
- timeout 時に `DDR_WR_END=0`, `DDR_RD_END=0` なら `Type A`
- timeout 時に `DDR_WR_END=1`, `DDR_RD_END=0` なら `Type B`
- `ep6Timeouts=0`, `DDR_RD_END=1` なら `Type C`

### Run 2: Same-Condition Confirmation

目的:
- 最初の classification が偶発でないことを確認する

設定:
- Run 1 と同一条件
- 1-2 cycle

判定:
- 同じ type が再現したら、その type を current dominant signature として採用する
- 別 type が出ても異常ではない。混在したら `Type A/B mixed` と記録する

### Run 3: Single-Variable Follow-Up

前提:
- Run 1-2 の結果が取れている

候補:
- density を 1 段だけ下げる
- manual stop を 1 回だけ試す
- preview / consumer を有効にする
- control binary `0.1.4r` に切り替える

ルール:
- 変えるのは 1 つだけ
- `0.1.4r` に切り替えるのは、現行 baseline build で少なくとも 1 回 signature を採取してから
- control binary に切り替えたら cable / hub / save path は固定したままにする

## Live Decision Guide

- `Type A`:
  - active acquisition 中の transport stall として扱う
  - 次回 focus は backlog の積み上がり方と timeout 直前の read size
- `Type B`:
  - drain 中 (`DDR_WR_END=1 && DDR_RD_END=0`) の stall として扱う
  - 次回 focus は `RD_WAIT` 区間の継続時間、`ddrWaitPolls`, EP4 read fail / disconnect の並び
- `Type C`:
  - high-density でも完走した run
  - その session では条件差を 1 つだけ増やして再現境界を見る
- summary が出る前に再起動 / hard drop:
  - `Unclassified hard drop` として別扱いにする

## Abort Conditions

- low-density control が失敗する
- 同じ session で USB reconnect が 2 回必要になる
- log が欠落して classification に必要な summary が取れない
- output が上書きされて run の対応が追えなくなる

## Record Template

```text
Session:
- DateTime:
- Branch:
- Binary:
- PC / Cable / Hub:
- FPGA FW:
- SavePath:
- LogRoot:
- OutputRoot:
- Density note:

Run 0:
- Performed:
- Result:
- ep6Timeouts:
- DDR_WR_END / DDR_RD_END:
- Notes:

Run 1:
- Cycles attempted:
- Observed type:
- Terminal signal:
- ep6Timeouts:
- WAVE_WR_CNT / WAVE_RD_CNT:
- DDR_WR_END / DDR_RD_END:
- maxBacklogBytes:
- ddrWaitPolls:
- Waveform:
- Disconnect:
- Notes:

Run 2:
- Same type reproduced:
- Notes:

Run 3:
- Single variable changed:
- Result:
- Notes:
```

## Notes

- `DDR_WR_END` は draining hint であり final completion ではない。`DDR_RD_END` を優先して読む。
- high-density failure は `completion semantics` の pass/fail と混ぜず、transport / backlog / disconnect の問題として別枠で記録する。
- 実機機会が限られるため、再現した run の log file 名と cycle summary をその場で控える。
