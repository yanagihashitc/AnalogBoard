# 実機確認runbook改訂概要

更新日: 2026-07-14  
状態: 完了済み履歴（2026-07-19 archive）  
対象: [実機確認runbook](./260706-field-session-runbook.html) Draft 2.3

## 結論

R7取得系、Win11 endpoint discovery hardening、境界telemetryを一つにした固定buildを作成した。これを旧driver `1.2.3.20`でlow×3＋high×30まで通し、PC qualificationとlegacy再アームbaselineを同時に取得する。その後はEXE/DLLを変えず、driverだけを`1.3.0.4`へ交換してfirst gateを行う。

既存の新PC high×3はデータ完全性Passの参考証跡として保持するが、新buildとはhashが異なるため30runへ合算しない。再アーム値は今回baselineとして取得し、製品の最終p99合否は新AcquisitionEngineで正式Tier1/2後にPhase 4で再実施する。

## 変更対応表

| 項目 | Draft 2.3の扱い | 根拠／証跡 | 残件 |
|---|---|---|---|
| 既存新PC high×3 | Data Passの参考証跡。新build countへ不算入 | 全12GB、1,997 pair監査Pass | provenanceは履歴品質の残件 |
| Phase 0 PC qualification | 新build＋旧driverでlow×3、high 10→30 | binaryを固定して実機非退行を確認 | hardware run pending |
| 再アームp99 | Gate Bでlegacy baselineを取得 | trigger＋RD_END／host drain／publish／host-ready＋次triggerをcycle ID／monotonic timeで固定長記録 | 最終合否は新engine Phase 4 |
| 外部trigger待ち | `next_trigger - host_ready`として別集計 | 外部待ちをre-armへ混ぜない | hardware baseline pending |
| driver A/B | Gate Bを旧driver側とし、同一buildで新driver low×1＋high×1 | driverだけを変更変数にする | hardware first gate pending |
| 旧endpoint-only build | 不変のrollback／比較基準 | 既存package checksum全件OK | 主試験には使わない |
| gcsa物理ch照合 | Closed、追加runなし | 実機CH1–CH13波形と現行gcsaラベルを確認済み | 新Decoder／Writer golden regression |
| DFX A/B | 新driver first gate失敗時のみ | 通常経路の交絡を避ける | 条件成立時のみ |

## 新固定build

- Build ID: `r7-driver-rearm-telemetry-20260714T1940JST`
- Package: `artifacts/field-session/validation-builds/bin_r7_driver_rearm_telemetry_validation/`
- EXE: `1afcccc78e4d2c56b9599b7168a3ebb4a8ac07e4baa14bc9165de878e88a1d34`
- DLL: `434fbfba36c5f257932411cefb385f403e17a8a862aea387ab3d7a52c9669713`
- Telemetry unit: 601/601
- EP4 completion replay／fault injection: 35/35
- Endpoint discovery／connect diagnostic: 49/49
- AcquisitionCompletionLogic: 21/21
- Full unit batch: 10,698/10,698
- Claude review: unresolved Blocker/Major 0
- FpgaRegisterLogic: 417/417
- Release x64 clean rebuild: pass、warning 0

legacy R7にはCyAPI/IOCTL transportを置換する完全なTier2 seamがない。今回は実completion helperへの決定論的replay／RD_END欠損・read failure相当のfault injectionまで確認し、USB transport統合は旧driver low×3＋high×30の実機gateで受け入れる。unit testで実機gateを代替した扱いにはしない。

## Telemetry契約

- 最大128 cycle／threadの固定長メモリ。
- 取得中のCSV、逐次telemetry log、動的確保なし。
- marker: trigger、active cycleの初回`DDR_RD_END=1`、host drain (unread 0)、publish/cleanup、host-ready、次のexternal trigger。
- `rearm_ms = host_ready - ddr_rd_end_confirmed`。
- `external_trigger_wait_ms = next_external_trigger_detected - host_ready`。
- 出力はthread停止後のCSVと1行の`[REARM][SUMMARY]`のみ。
- 1 threadは128 cycle以下、`dropped>0`はrun無効。

## 中央同期

AnalogBoard workspaceから`../task_management`はread-only。親プランDraft 2.8更新後、中央workflowでmirrorとcross-repo roadmapのAnalogBoard Phase 0 status／evidence／next gateを同期する必要がある。
