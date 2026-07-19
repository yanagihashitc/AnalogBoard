# 実機セッション用スクリプト

`docs/260706-field-session-runbook.html` Draft 2.6 のD23 NoDfx dual-driver sessionで使用したスクリプト群。sessionは2026-07-16〜17に完了し、Gate Bは条件付きPass、D4はr7移植例外でowner gate通過済み。新driverを再導入する場合のD23 provisioning／readbackと、履歴証跡の再現用に保持する。判定buildは`r7-driver-telemetry-graceful-stop-20260716T1314JST`。

## Gate B/C inventory collector

`collect_gate_inventory.ps1`は読み取り専用。固定build ID／EXE・DLL hash、必須runtime configの存在、driver、USB topology、PC/OS、保存先disk、電源プランを取得し、sample ID・物理USB port・直結確認と一緒に時刻付きevidenceへ保存する。`default_config.csv`は終了時に更新される可変設定なのでhashを検証・保存しない。driver、device、電源設定、アプリ、測定データは変更しない。管理者権限は不要。

履歴上、現在の新driverでfail-fastを始める前に使用した例:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\collect_gate_inventory.ps1 `
  -Stage GateC `
  -PackageRoot C:\field-session\TELEMETRY_CSV_READY_1314 `
  -SampleId "<sample-or-batch-id>" `
  -UsbPortNote "<physical-port-note>" `
  -CableConfiguration Direct `
  -DataDriveLetter D
```

sessionではdriver `1.3.0.4`へNoDfxを適用・readbackした後に`-Stage GateC`を実行し、N leg終了後にdriverを`1.2.3.20`へrollbackして`-Stage GateB`を使用した。既定出力先は`<PackageRoot>\evidence\inventory\<timestamp>-<Stage>\`。固定build ID、EXE・DLL hash、runtime configの存在、driver、直結条件、必須入力の不一致はexit code 2で停止する。

| ファイル | やること | 管理者権限 |
|---|---|---|
| `collect_gate_inventory.ps1` | Gate B/Cのsystem provenance取得と固定条件検証 | 不要 |
| `gate_inventory_core.psm1` | collectorのEXE・DLL hash／runtime file存在／driver／manual context検証module | 不要 |

## D23 NoDfx provisioning

レジストリを手で触らないための自動化スクリプト。**regedit や INF の編集は一切不要。**

## D23 provisioning手順

前提: このフォルダ（`scripts\field-session`）を実機PCにコピーしておく。

1. **管理者ターミナルを開く**: スタートボタンを**右クリック** → 「ターミナル (管理者)」→「はい」
2. このフォルダへ移動: `cd C:\<配置先>\scripts\field-session`
3. 現状確認（いつ実行しても安全）:
   `powershell -ExecutionPolicy Bypass -File .\dfx_status.ps1`
4. DFX 無効化（D23採用構成へ）:
   `powershell -ExecutionPolicy Bypass -File .\dfx_off.ps1`
   → もう一度 `dfx_status.ps1` で「`0 = DFX DISABLED`」を確認
5. N leg後は`dfx_on.ps1`を実行せず、runbookのR節どおりdriverを`1.2.3.20`へrollbackする。`=0`が残っていても旧driverでは不活性なので削除しない。

### 各DFXスクリプト

| ファイル | やること | 管理者権限 |
|---|---|---|
| `dfx_status.ps1` | 現在の設定値を表示するだけ（変更しない） | 不要 |
| `dfx_off.ps1` | `WdfDirectedPowerTransitionEnable=0` を書いてデバイス再起動 | **必要** |
| `dfx_on.ps1` | 過去のA-B-A復元用。Draft 2.6のN→R→B手順では使用しない | **必要** |

対象デバイス（VID_04B4 の FX3 ボード）はスクリプトが自動検出するので、
インスタンスパスの手動確認は不要。

### 困ったとき

- 「NG: administrator rights required」→ 手順1の**管理者**ターミナルで開き直す
- 「NG: FX3 board not found」→ USB 接続・機械の電源を確認して再実行
- 「NG: device restart failed」→ USBを抜かず、出力を保存してowner判断まで停止
- スクリプトが赤字で実行ブロックされる → コマンドを `powershell -ExecutionPolicy Bypass -File .\〜` の形で打っているか確認
