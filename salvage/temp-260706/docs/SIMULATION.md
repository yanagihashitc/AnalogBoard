# Sysmex AnalogBoard Simulation Guide

## 1. 目的

- 実機前に host 側の取得処理を preset ベースで繰り返し確認する
- 正常系だけでなく、timeout / disconnect / writer 遅延 / publish failure を再現する
- 実機用 `Sysmex_AnalogBoard_TestApp.exe` とは分離した `Sysmex_AnalogBoard_SimRunner.exe` で実行する

## 2. 実行コマンド

simulation の正本は `scripts\run_simulation.bat <preset>`。

```bat
cmd /d /c "scripts\run_simulation.bat normal_complete"
```

この script は内部で以下を行う。

- `Sysmex_AnalogBoard_SimRunner` を `Debug|x64` で rebuild
- 指定 preset を `Sysmex_AnalogBoard_SimRunner.exe` で実行

### sim-preflight skill

全 preset を一括実行するには Claude Code の `/sim-preflight` skill を使う。
build → unit test → 全 preset 実行 → サマリレポートを一括で行う。

## 3. preset 一覧

| Preset | 用途 | 期待結果 |
|---|---|---|
| `normal_complete` | 正常完走確認 | `success` |
| `ep6_timeout_once_then_recover` | 1 回 timeout 後の復帰確認 | `success` |
| `ep6_timeout_persistent` | timeout 打ち切り確認 | `ep6_timeout` |
| `usb_disconnect_midstream` | 途中 disconnect 確認 | `usb_disconnect` |
| `writer_slow_queue_pressure` | write 遅延時の backlog 確認 | `success` |
| `write_fail` | write failure 確認 | `write_failed` |
| `publish_fail` | publish failure 確認 | `publish_failed` |
| `burst_boundary_stress` | バースト境界のストレステスト | `success` |
| `slow_producer` | 非 `16KB` アライン中間 progress の回帰確認 | `success` |
| `empty_capture` | 0 wave capture が success にならないことの確認 | `empty_capture` |

## 4. まず何を回すか

最小:

- `normal_complete`
- `write_fail`
- `publish_fail`

実機前の推奨（全 preset）:

```bat
cmd /d /c "scripts\run_simulation.bat normal_complete"
cmd /d /c "scripts\run_simulation.bat ep6_timeout_once_then_recover"
cmd /d /c "scripts\run_simulation.bat ep6_timeout_persistent"
cmd /d /c "scripts\run_simulation.bat usb_disconnect_midstream"
cmd /d /c "scripts\run_simulation.bat writer_slow_queue_pressure"
cmd /d /c "scripts\run_simulation.bat write_fail"
cmd /d /c "scripts\run_simulation.bat publish_fail"
cmd /d /c "scripts\run_simulation.bat burst_boundary_stress"
cmd /d /c "scripts\run_simulation.bat slow_producer"
cmd /d /c "scripts\run_simulation.bat empty_capture"
```

`normal_complete` だけでは正常系しか見られないため、不十分。

## 5. シナリオ JSON フィールド仕様

preset 定義は `data/sim_scenarios/<preset>.json` に配置。

### 必須フィールド

| フィールド | 型 | 説明 |
|---|---|---|
| `wave_size_low` | unsigned int | Low 波形サイズ (bytes) |
| `wave_size_high` | unsigned int | High 波形サイズ (bytes) |
| `waves_per_file` | unsigned int | 1 ファイルあたりの波形数 |
| `total_wave_count` | unsigned int | 取得する総波形数 (`0` は anomaly simulation 用) |
| `max_read_chunk_bytes` | unsigned int | EP6 読み取りチャンクサイズ (0x4000 の倍数) |
| `timeout_retry_limit` | unsigned int | timeout 時のリトライ上限 |
| `ep6_results` | string[] | EP6 呼び出しごとの結果 (`"success"`, `"timeout"`, `"disconnect"`) |

### オプショナルフィールド

| フィールド | 型 | デフォルト | 説明 |
|---|---|---|---|
| `write_delay_ms` | unsigned int | 0 | write 遅延 (ms) |
| `write_fail_at` | int | 0 | 指定回目の write で failure を注入 (`0` は無効) |
| `publish_fail_at` | int | 0 | 指定回目の publish で failure を注入 (`0` は無効) |
| `producer_step_bytes` | unsigned int | - | legacy 方式: 1 poll あたりの書き込みバイト数 (`32-byte` 単位、`16KB` 未満も可) |
| `producer_bursts_per_poll` | unsigned int | - | burst 方式: 1 poll あたりのバースト数 |
| `init_poll_count` | unsigned int | 1 | Init 状態のポーリング回数 |
| `wait_poll_count` | unsigned int | 1 | Wait 状態のポーリング回数 |

### 検証ルール

- `wave_size_low + wave_size_high > 0`
- `waves_per_file > 0`
- `total_wave_count >= 0` (`0` は anomaly simulation 用)
- `max_read_chunk_bytes > 0` かつ `0x4000` の倍数
- `producer_step_bytes` と `producer_bursts_per_poll` は **同時設定不可**（排他）
- `producer_step_bytes` 設定時は `32-byte` DDR address unit に揃えること
- unsigned フィールドに負の値を指定するとエラー

## 6. FPGA DDR モデル状態遷移

`FpgaDdrModel` は FPGA の DDR メモリ動作をシミュレーションする。

```
Idle → Init → Measuring → Wait → RdWait
```

| 状態 | 説明 | EP4 ステータス |
|---|---|---|
| `Idle` | 初期状態 | ADC_SET_END=0, MEAS_TRG=0 |
| `Init` | 初期化 (`init_poll_count` 回待機) | ADC_SET_END=1, MEAS_TRG=1 |
| `Measuring` | 波形データ書き込み進行中 | ADC_SET_END=1, MEAS_TRG=1, DDR_WR_END=0 |
| `Wait` | 書き込み完了後の待機 (`wait_poll_count` 回) | DDR_WR_END=1 |
| `RdWait` | 読み取り完了待ち | DDR_WR_END=1, DDR_RD_END=(readBytes>=writtenBytes) |

### Producer 進行メカニズム

- **Legacy 方式** (`producer_step_bytes`): 1 poll で指定バイト数ずつ進行
- **Burst 方式** (`producer_bursts_per_poll`): 1 poll で指定バースト数 (burst = EP6 アライメント単位) ずつ進行

## 7. 結果の見方

実行直後のコンソールには以下が出る。

```text
preset=<name> status=<terminal_status> exit_code=<code> output=<path>
```

主に確認する場所は `logs\sim\<preset>\<timestamp>\`。

- `runner.log`: 実行ログ
- `summary.json`: 最終結果の要約
- `*_fl_*.bin`, `*_fh_*.bin`: 生成 wave file

最初に見るのは `summary.json` でよい。

## 8. `summary.json` で見る項目

| フィールド | 型 | 説明 |
|---|---|---|
| `preset` | string | プリセット名 (JSON エスケープ済み) |
| `terminal_status` | string | 終了ステータス文字列 |
| `error_code` | int | 内部エラーコード |
| `exit_code` | int | プロセス終了コード (0-11) |
| `ep6_call_count` | int | EP6 呼び出し総数 |
| `timeout_count` | int | timeout 発生回数 |
| `WAVE_WR_CNT` | int | 波形書き込みカウンタ (FPGA レジスタ) |
| `WAVE_RD_CNT` | int | 波形読み取りカウンタ (FPGA レジスタ) |
| `DDR_WR_END` | int | DDR 書き込み完了フラグ |
| `DDR_RD_END` | int | DDR 読み取り完了フラグ |
| `saved_wave_count` | int | 保存された波形数 |
| `published_pair_count` | int | 公開されたペア数 |

## 9. exit code

| Exit Code | Meaning |
|---|---|
| `0` | success |
| `1` | unexpected error |
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
| `12` | empty capture |

`publish_fail` など異常系 preset は expected non-zero で終わる。

## 10. 関連ファイル

- 実行手順: [BUILD.md](./BUILD.md)
- 設計: [archive/simulation/2026-03-11-acquisition-preflight-simulation-design.md](./archive/simulation/2026-03-11-acquisition-preflight-simulation-design.md)
- preset 定義: `data/sim_scenarios/*.json`
- 実行 script: `scripts/run_simulation.bat`
- SimRunner ソース: `Sysmex_AnalogBoard_SimRunner/`
  - `SimulationRunnerCore.h/.cpp` - 実行フレームワーク
  - `SimulationScenario.h/.cpp` - JSON パーサ & 検証
  - `FpgaDdrModel.h` - FPGA DDR 状態遷移モデル
  - `SimulationEp4StatusHelper.h` - EP4 ステータスバッファ生成補助
- 統合テスト: `Sysmex_AnalogBoard_UnitTest/SimulationRunnerIntegration_test.cpp`
