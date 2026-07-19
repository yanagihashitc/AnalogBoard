# 次の実機確認タスク

更新日: 2026-07-14  
正本: [実機確認runbook](../../260706-field-session-runbook.html) Draft 2.3

## 現在地

- 完了: r7/r18 A/B、スイッチ区間A/B、直結標準化、初期USBPcap診断、D17物理ch順確認。
- 参考Pass: 既存の新PC high×3はログ／全12GBデータ完全性Pass。ただし新buildとはhashが異なるため30runへ合算しない。
- Build Ready: `r7-driver-rearm-telemetry-20260714T1940JST`。旧endpoint-only packageは不変のrollback用。
- 待機: 旧driver low×3＋high×30、telemetry baseline回収、同一build新driver first gate、D4 owner gate。
- Skip: preview／consumer併用、追加の物理ch特定run。
- 後日: 新AcquisitionEngineで正式Tier1/2後に100run soak＋再アームp99最終判定。

## 次に行う最初の1段

**新packageのchecksumを確認し、旧driver `1.2.3.20`でlow-density×3を行う。**

Package:

`/mnt/d/ubuntu/jupyter/sys_analyzer/AnalogBoard/bin/.worktrees/r7-win11-validation/bin_r7_driver_rearm_telemetry_validation`

固定hash:

- EXE: `1afcccc78e4d2c56b9599b7168a3ebb4a8ac07e4baa14bc9165de878e88a1d34`
- DLL: `434fbfba36c5f257932411cefb385f403e17a8a862aea387ab3d7a52c9669713`

手順:

1. package直下で`sha256sum -c manifest/checksums.sha256`を実行する。
2. driver `1.2.3.20`、直結、Manual Get OFF、固定USB port/controller、sample、設定、保存先、NVMe、電源モードを記録する。
3. connect、idle EP4×10、EP2→EP4を確認する。
4. low×3を実行し、各runのType CとFL/FH完全性を確認する。
5. 3/3 Pass時だけhighへ進む。

## その後の順序

1. 旧driver＋同じbuildでhigh×10。全件Passを中間gateとする。
2. 同じ条件でhigh×30まで進める。
3. 外部trigger待ち状態で正常停止し、CSVと`[REARM][SUMMARY]`を回収する。
4. `cycles<=128`、`csvWrite=1`、`dropped=0`、完了cycle数と`rearmSamples`の整合を確認する。
5. アプリを終了し、EXE/DLLを変えずdriverだけを`1.3.0.4`へ交換する。
6. 新driverでconnect、idle EP4×10、EP2→EP4、low×1、high×1を行う。
7. 新driver first gateだけがFailした場合に限りDFX A/Bへ進む。
8. 実機証跡、legacy re-arm baseline、D4 baseline方針をowner gateへ提出する。

## 最重要停止条件

- checksum／driver／binary hash／port／sample等の固定条件が不明または不一致。
- Type A/B、USB disconnect、5〜10分級遅延。
- missing pair、index欠落、波形破損、成功runの`.tmp`残置。
- 旧driverでlow 3/3またはhigh 30/30を満たさない。
- telemetryが`csvWrite=0`、`dropped>0`、またはcycle整合不明。
- 旧driver段がFailしているのに新driverへ進もうとしている。
