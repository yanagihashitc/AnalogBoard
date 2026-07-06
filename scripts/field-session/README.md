# 実機セッション用スクリプト（実験2: DFX A/B）

`docs/260706-field-session-runbook.html` の実験2（**条件付き**——実験3が失敗した場合のみ実施）で使う。
レジストリを手で触らないための自動化スクリプト。**regedit や INF の編集は一切不要。**

## 使い方（この順で）

前提: このフォルダ（`scripts\field-session`）を実機PCにコピーしておく。

1. **管理者ターミナルを開く**: スタートボタンを**右クリック** → 「ターミナル (管理者)」→「はい」
2. このフォルダへ移動: `cd C:\<配置先>\scripts\field-session`
3. 現状確認（いつ実行しても安全）:
   `powershell -ExecutionPolicy Bypass -File .\dfx_status.ps1`
4. DFX 無効化（B条件へ）:
   `powershell -ExecutionPolicy Bypass -File .\dfx_off.ps1`
   → もう一度 `dfx_status.ps1` で「`0 = DFX DISABLED`」を確認
5. 試験後、元に戻す:
   `powershell -ExecutionPolicy Bypass -File .\dfx_on.ps1`
   → `dfx_status.ps1` で「`not set (driver default)`」を確認

## 各スクリプト

| ファイル | やること | 管理者権限 |
|---|---|---|
| `dfx_status.ps1` | 現在の設定値を表示するだけ（変更しない） | 不要 |
| `dfx_off.ps1` | `WdfDirectedPowerTransitionEnable=0` を書いてデバイス再起動 | **必要** |
| `dfx_on.ps1` | 上記設定を削除してドライバ既定に戻し、デバイス再起動 | **必要** |

対象デバイス（VID_04B4 の FX3 ボード）はスクリプトが自動検出するので、
インスタンスパスの手動確認は不要。

## 困ったとき

- 「NG: administrator rights required」→ 手順1の**管理者**ターミナルで開き直す
- 「NG: FX3 board not found」→ USB 接続・機械の電源を確認して再実行
- 「WARN: device restart failed」→ USB コネクタを抜き差しすれば同じ効果
- スクリプトが赤字で実行ブロックされる → コマンドを `powershell -ExecutionPolicy Bypass -File .\〜` の形で打っているか確認
