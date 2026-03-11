# AnalogBoard Simulation Guide

## 1. 目的

- 実機前に host 側の取得処理を preset ベースで繰り返し確認する
- 正常系だけでなく、timeout / disconnect / writer 遅延 / publish failure を再現する
- 実機用 `AnalogBoard_TestApp.exe` とは分離した `AnalogBoard_SimRunner.exe` で実行する

## 2. 実行コマンド

simulation の正本は `scripts\run_simulation.bat <preset>`。

```bat
cmd /d /c "scripts\run_simulation.bat normal_complete"
```

この script は内部で以下を行う。

- `AnalogBoard_SimRunner` を `Debug|x64` で rebuild
- 指定 preset を `AnalogBoard_SimRunner.exe` で実行

## 3. preset 一覧

| Preset | 用途 | 期待結果 |
|---|---|---|
| `normal_complete` | 正常完走確認 | `success` |
| `ep6_timeout_once_then_recover` | 1 回 timeout 後の復帰確認 | `success` |
| `ep6_timeout_persistent` | timeout 打ち切り確認 | `ep6_timeout` |
| `usb_disconnect_midstream` | 途中 disconnect 確認 | `usb_disconnect` |
| `writer_slow_queue_pressure` | write 遅延時の backlog 確認 | `success` |
| `publish_fail` | publish failure 確認 | `publish_failed` |

## 4. まず何を回すか

最小:

- `normal_complete`
- `publish_fail`

実機前の推奨:

```bat
cmd /d /c "scripts\run_simulation.bat normal_complete"
cmd /d /c "scripts\run_simulation.bat ep6_timeout_once_then_recover"
cmd /d /c "scripts\run_simulation.bat ep6_timeout_persistent"
cmd /d /c "scripts\run_simulation.bat usb_disconnect_midstream"
cmd /d /c "scripts\run_simulation.bat writer_slow_queue_pressure"
cmd /d /c "scripts\run_simulation.bat publish_fail"
```

`normal_complete` だけでは正常系しか見られないため、不十分。

## 5. 結果の見方

実行直後のコンソールには以下が出る。

```text
preset=<name> status=<terminal_status> exit_code=<code> output=<path>
```

主に確認する場所は `logs\sim\<preset>\<timestamp>\`。

- `runner.log`: 実行ログ
- `summary.json`: 最終結果の要約
- `*_fl_*.bin`, `*_fh_*.bin`: 生成 wave file

最初に見るのは `summary.json` でよい。

## 6. `summary.json` で見る項目

- `terminal_status`
- `error_code`
- `exit_code`
- `ep6_call_count`
- `timeout_count`
- `WAVE_WR_CNT`
- `WAVE_RD_CNT`
- `DDR_WR_END`
- `DDR_RD_END`
- `saved_wave_count`
- `published_pair_count`

## 7. exit code

| Exit Code | Meaning |
|---|---|
| `0` | success |
| `2` | EP6 timeout |
| `3` | USB disconnect |
| `4` | publish failure |
| `5` | write failure |
| `6` | open pair failure |
| `7` | invalid config |
| `8` | stopped |
| `9` | EP4 read failure |
| `10` | EP6 read failure |
| `11` | alignment error |

`publish_fail` など異常系 preset は expected non-zero で終わる。

## 8. 関連ファイル

- 実行手順: [BUILD.md](./BUILD.md)
- 設計: [2026-03-11-acquisition-preflight-simulation-design.md](./2026-03-11-acquisition-preflight-simulation-design.md)
- preset 定義: `data/sim_scenarios/*.json`
- 実行 script: `scripts/run_simulation.bat`
