# Phase 1 Field Session Runbook

対象 branch / build: `baseline/0.1.4-hw-recovery`, `Release|x64`

関連:
- [USB acquisition stability checklist](./2026-03-02-usb-acquisition-stability-checklist.md)
- [Host-FPGA Acquisition Reference](./2026-03-12-host-fpga-acquisition-reference.md)

## Goal

接続機会 1 回で、Phase 1 Gate 1 の判定と manual stop spot check を同じ binary で回収する。

## Fixed Conditions

開始前に次を固定して書き残す。

- PC / USB cable / hub
- FPGA FW version
- save path
- log root
- output root
- 実行 binary path

変えてよいのは 1 run あたり 1 変数だけにする。最初は mode 以外を変えない。

## Preflight

1. `baseline/0.1.4-hw-recovery` の `Release|x64` build を使用する。
2. `logs` と `[PR01][CYCLE]` が回収できる状態で起動する。
3. preview / consumer は停止した状態から始める。
4. 直前の不要ファイルや見分けづらい output が残っていないことを確認する。

## Run A: Gate 1

設定:
- auto mode
- preview / consumer なし
- 3-5 cycle

観測:
- waveform 正常
- empty capture 0 件
- 各 cycle で `DDR_RD_END=1`
- timeout / disconnect / 再起動相当なし

保存:
- log file 名
- output root
- cycle 数
- 代表 waveform
- 異常があれば cycle index と時刻

abort:
- empty capture 1 件でも発生
- `DDR_RD_END=0` のまま cycle 終了
- USB timeout / disconnect / 再起動相当が発生

## Run B: Manual Stop Spot Check

前提:
- Run A が pass、または少なくとも致命異常なしで完了している
- 同じ binary / 同じ配線 / 同じ save path を使う

設定:
- manual mode
- 1-2 cycle

手順:
1. manual mode で acquisition を開始する
2. waveform が出始めた後、stop を 1 回だけ操作する
3. stop 後の UI 応答、log、出力ファイルを確認する

判定:
- stop 後に hang しない
- drain 完了まで進む
- `DDR_RD_END=1` に到達する
- partial / empty capture を作らない

保存:
- stop 操作時刻
- stop 後に出た `[PR01][CYCLE]`
- output root
- waveform の見え方

## Run C: Optional Exploratory Load Check

前提:
- Run A が pass

設定:
- 同じ binary のまま preview 相当 consumer を有効化
- 2-3 cycle

注意:
- これは Phase 1.5 の先行観測であり、Phase 1 Gate 1 判定とは分離して記録する

## Record Template

```text
Session:
- DateTime:
- PC / Cable / Hub:
- FPGA FW:
- Branch:
- Binary:
- SavePath:
- LogRoot:
- OutputRoot:

Run A:
- Cycles:
- Waveform:
- EmptyCapture:
- DDR_RD_END:
- Timeout/Disconnect:
- Notes:

Run B:
- Manual stop tested:
- Stop timing:
- Drain completed:
- DDR_RD_END:
- Output pair valid:
- Notes:

Run C:
- Consumer enabled:
- Cycles:
- Acquisition stable:
- Notes:
```

## Notes

- `DDR_WR_END` は draining hint であって final completion ではない。判定は `DDR_RD_END` を優先する。
- stale status の並び (`DDR_WR_END`, `DDR_RD_END`, `WAVE_WR_CNT`, `WAVE_RD_CNT`) が見えたら、そのまま process log に転記する。
- Gate 1 が崩れた状態で Run C のような exploratory 項目を増やさない。
