# 2026-07-16 D23（NoDfx採用）とNoDfx dual-driverセッション設計

## 目的

Win11新driver `1.3.0.4` のSet後EP4 failureに対する恒久方針を確定し、次の実機セッションの設計を正規ドキュメントへ反映する。

## 経緯と判断

1. **poll-rate仮説の棄却（2026-07-15夕・実機）**：10ms cadence remediation build `r7-driver-ep4-polling-20260715T1618JST` 自体が実機でSet後EP4 failureを3/3再現（+0.82/5.11/2.22s、`field_package/bin/logs/2607151650.log`／`2607151817.log`）。tight busy-poll（~400Hz）→10ms cadence（~80Hz）の変更は無効。返却package `RESULT_SHEET.md` の「Hardware 未試験」は梱包時点の記載で、実態はこのログの通り試験済み。
2. **DFX因果確定（2026-07-16午前・実機）**：単変数A-B-A（session `20260716_111343`、driver 1.3.0.4・同一build/port/cable/config固定）。A（DFX有効）＝failure 3/3（historical A、Gate C inventoryでhash一致確認）→ B（`WdfDirectedPowerTransitionEnable=0` のみ変更）＝idle EP4 10/10・automatic wait>68s・low Type C（FH/FL 60/60）・high Type C（FH/FL 678/678、4,227,600,000B、ep6Timeouts=0）→ 復元A＝+25.448sで再発。証跡：`artifacts/field-session/packages/r7-driver-ep4-polling-20260715T1618JST-source-package/host_source/diagnostic_runs/20260716_111343/SESSION_REPORT.md`。実機はDFX有効既定へ復元済み。
3. **owner decision D23（2026-07-16・本セッション）**：NoDfxを正式採用。runbook判定表「実験5失敗＋5b成功→DFXが主因。NoDfxを運用要件化。Plan A＋NoDfxで確定」（要旨。原文はFX3 FW LPM非対応時の恒久要件可能性の括弧書きを含む）分岐（コミット履歴でA-B-A前の事前合意と検証済み）の執行。
4. **セッション設計（owner指示）**：telemetry graceful-stop build（telemetry CSVの確実な出力を入れた新build、package確定待ち）を判定buildとし、実機が現在新driverであることを利用して **N leg（1.3.0.4＋NoDfx：smoke→low×3→high×10/30＋telemetry）→ driver原状回復（rollback）→ B leg（1.2.3.20：Gate B閉鎖＝low×3＋high×30＋FL/FH integrity＋telemetry、必達）** の順で実施。driver入替は1回で済み、セッション終了時に実機は現行運用状態（旧driver）へ戻る。

## 技術的注意（調査で確定した運用ピットフォール）

- 新driver INF（`FX3/Win11/x64/cyusb3.inf:222`）はインストール時に `HKR,"WDF","WdfDirectedPowerTransitionEnable",0x00010001,1` を書き込む（NOCLOBBERなし＝上書き）。**driver（再）インストール毎にNoDfxはリセットされる**ため、再適用＋`dfx_status.ps1`検証をSOP化。A-B-Aで発見された「出所不明の`=1` override」の正体はこのINF AddReg。
- 旧driver INF（`FX3/Win10 x64/cyusb3.inf`）にはDFXエントリが存在せず、rollback後に`=0`が残置されても不活性（削除不要）。
- WU Update Catalogの cyusb3 1.3.0.5/1.3.0.6 はUSB-Serialブリッジ（`VID_04B4&PID_0004`）向け配信で、本機 `PID_FFF2` は現時点で対象外（.cab実査で確認）。WU黙示上書きリスクは現状なし。将来のtargeting追加は定期確認。
- 旧driver 1.2.3.20の.catは**WHQL Microsoft署名**（2017-11-20、timestamp有効、.sysハッシュ一致）と検証済み——2026年4月のcross-signed信任除去policyの影響なし。旧driver退出の論拠は「保守されない2017年driverへの依存増」に置く。
- 現DLLは全EP4 XferData失敗を `-10` へ畳み込み `UsbdStatus`/`NtStatus` を喪失。NoDfx legでfailure再発時のみ詳細status build（`EP4_DIAGNOSTIC_READY_0105`）で1件取得（owner承認必須）。

## 反映したドキュメント

| ドキュメント | 変更 |
|---|---|
| `docs/archive/field-session/260706-field-session-runbook.html` | Draft 2.5→2.6：N leg（NoDfx provisioning＋formal soak）→R（原状回復）→B leg（Gate B閉鎖）の実行順、判定build placeholder（hash pin待ち）、DFX A-B-A完了の記録、判定・退出チェック表、停止条件（DFX状態不一致） |
| `docs/plans/260710-analogboard-rebuild-plan.html` | Draft 3.0→3.1：D23追加（確定意思決定23件）、driver戦略節を「DFX A-B-A確定とD23」へ改訂、通信層方針（Plan A具体化・Plan Bヘッジ・transport seam・環境検証）、リスク節・フェーズ0現在地更新 |
| `AGENTS.md`（=CLAUDE.md） | フェーズ0現在地（2026-07-16）、意思決定表へD23追加、D1〜D23表記 |
| `docs/archive/field-session/2026-07-16-nodfx-dual-driver-session.html` | 完了済み次実機タスク文書（07-15版をsupersede） |
| `tasks/todo.md` | 本作業のbatch追加 |

## 完了結果（2026-07-19追記）

- 判定build `r7-driver-telemetry-graceful-stop-20260716T1314JST`はhash pinとpackage検証を完了した。
- D23 session `20260716_2`を2026-07-16〜17に実施。N0／N-smoke Pass、30 valid high cycleの非formal aggregateを取得し、driver `1.2.3.20`へのrollbackを完了した。
- B legは初回pre-trigger EP4 failureとType B stallを保全後、承認済みUSB replugによるrecovery 33/33 Type Cを確認し、ownerが2026-07-17 13:02 JSTにGate Bを条件付きPassとして閉鎖した。
- 2026-07-19 owner gateでD4のr7移植例外を承認し、N formal soakはPhase 4の100run soakへ集約した。実機は旧driver `1.2.3.20`へ復元済み。
- session証跡は`artifacts/field-session/packages/r7-driver-ep4-polling-20260715T1618JST-source-package/TELEMETRY_CSV_READY_1314/evidence/session/20260716_2/`、characterization／USBPcapは`artifacts/field-session/2026-07-17-characterization/`に保全した。

## 当時の未完・引き渡し（履歴）

- **判定buildのID/EXE/DLL hashはpackage確定時にpin**（runbook・次実機タスク文書のplaceholderを更新）。pin前の実機測定は禁止と明記済み。telemetry graceful-stop buildのreview→Release→package収録は別batch（`tasks/todo.md` 2026-07-16 telemetry graceful-stop build）。
- **task_management同期**（AnalogBoardからはread-only）：中央roadmap/mirrorへD23・フェーズ0現在地・次gate（NoDfx dual-driver session）を`task_management` workspaceから反映すること。
- vendor照会（FX3 FWのLPM/suspend対応可否・改変可能性）とInfineon support case起票はowner判断待ち。NoDfxが恒久出荷要件か暫定かのラベルのみに影響し、採用構成は不変。
