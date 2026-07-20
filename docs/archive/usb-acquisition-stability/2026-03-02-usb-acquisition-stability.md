# USB データ取得・書き込み安定性改善プラン

> **履歴文書（2026-07-19 にarchive）**: 実機 gate は D4 の r7 移植例外と D23 の owner decision を含めて決着済みで、次の主要 gate は Frozen v1 です。以下の `dev`／旧 feature branch／専用 worktree に関する記述は当時の履歴であり、現在の実行指示ではありません。現行の branch 運用は [Branch Strategy](../../../branch_plan/README.md) を参照してください。

## 概要

USB データ取得中の書き込み中断・タイムアウトエラーの根本原因を修正し、アーキテクチャを改善してデータパイプラインの安定性を向上させる。

2026-03-09 の追加調査により、`Version A`（EP6 hot-path の高頻度 `PrintLog(...)` を停止）で waveform が正常化した。また、`sys_app` の `preview` 系 consumer は `.tmp` ではなく完成済み `.bin` のみを読むことを確認した。このため、本プランでは「EP6 取得ループが UI/file/publish に近すぎること」と「下流 consumer による `.bin` 読み取り負荷や publish 競合で acquisition を止めないこと」を追加の重要要件として扱う。

同じ調査で、Phase 0 の「呼び出しごとの詳細ログ出力」は計測対象そのものを乱す intrusive instrumentation であることが分かった。したがって Phase 0 は未完了のままとし、hot-path ではメモリ集計のみを行い、取得終了後に要約を出力する軽量計測へ切り替える。

2026-03-11 の versioned field log 比較により、`0.1.4` と `cmp_76b2b2a` は正常完走、`0.1.5` と生の `76b2b2a` は取得開始後にログが途切れて再起動相当、`0.1.6` follow-up は `new[]/delete[]` 化後に EP6 timeout で未完走という経緯が確認された。したがって Phase 1 の直近ゴールは comparison build で成功した **per-call local scratch buffer + CRT `malloc/free` backend** を固定し、共有 mutex 分離のような未検証仮説は acquisition 安定化後に再評価することとする。

2026-03-12 の追加調査で、初期実装 `aebf296adf72a8ac5a8355f7f539dca87521f724` の acquisition flow と、仕様書 / FPGA VHDL 上の `DDR_WR_END` / `DDR_RD_END` の意味差を整理した。今後 acquisition 実装を変更する際は、まず [Host-FPGA Acquisition Reference](../2026-03-12-host-fpga-acquisition-reference.md) を参照すること。

同日の実機ログ比較により、現状は少なくとも 2 系統の不具合が重なっていると判断した。`0.1.5+` には USB / EP6 timeout・disconnect 系の途中停止があり、`0.2.0+` にはそれに加えて `DDR_WR_END` を完了信号として扱った host 側 semantics mismatch による empty capture がある。したがって以後の実装方針は、**`0.1.4` を recovery baseline として実機安定性を守りながら、`0.2.2` の test / simulation / telemetry 資産を検証用 lab として救出する** ものに切り替える。

branch / worktree を最終的に `main` + 開発用 branch 1 本へ収束させる運用は、[2-Branch Convergence Roadmap](../../../branch_plan/2026-03-12-two-branch-convergence-roadmap.md) と [Temporary Branch Closure Checklist](../../../branch_plan/2026-03-12-temporary-branch-closure-checklist.md) を正本とする。

## 2026-03-18 How To Use These Docs

この plan は「なぜその順番で進めるか」「各 branch が何を担うか」を判断するための正本とする。

- 実装の進捗と exit gate は [チェックリスト](./2026-03-02-usb-acquisition-stability-checklist.md) を見る
- 判断の根拠、実機ログ、Red/Green/field validation の履歴は [process_log](./2026-03-02-usb-acquisition-stability-log.md) を見る
- `dev` の次タスクは [baseline_next.md](./baseline_next.md) を見る
- `lab/0.2.2-engine-semantics` の次タスクは [lab_next.md](./lab_next.md) を見る
- `feature/win11-driver-compat` の次タスクは [driver_next.md](./driver_next.md) を見る
- 設計詳細は [Architecture Notes](./2026-03-02-usb-acquisition-stability-architecture.md) を見る
- 実行詳細は [Execution Notes](./2026-03-02-usb-acquisition-stability-execution.md) を見る
- 実機 signature と run bundle は [Field Reference](./2026-03-02-usb-acquisition-stability-field-reference.md) を見る

2026-03-18 時点の運用ルール:

- **Phase 2 の実装本線は `dev`**
- **`lab/0.2.2-engine-semantics` は non-blocking な verification asset**
- lab で有効性を確認した差分だけを baseline へ小さく戻す
- `WaveAcquisitionEngine` / SimRunner / simulator の維持は重要だが、release-track Phase 2 着手の前提条件にはしない

## 2026-03-19 New Driver Compatibility Track

`PR-04 field gate` を置き換えずに、Win11 上の USB host / driver 差分を別軸で潰すための **parallel track** をここで固定する。

- 対象は **`Win11 + new Cypress/Infineon driver + matching SDK`**
- 目的は `Get ep4 register data failed.` を先に消し、Win11 上で high-density acquisition / timeout を再評価できる状態へ戻すこと
- この track は release-track (`dev`) の blocker ではなく、field gate と並走する investigation / compatibility work として扱う

2026-03-19 時点の観測:

- 同じ `0.1.4r7` でも Win10 では high-density で `ep6Timeouts=0`、Win11 では `EP6 timeout` が出た
- Win11 で Cypress driver を新しい版へ更新すると、`logs/0.1.4r7/logs/2603191025.log` で `Get ep4 register data failed.` が intermittent に出て acquisition 自体が不安定になった
- driver rollback 後は再びデータ取得できたため、現時点の主仮説は **new driver と repo 内の legacy `CyAPI` / `CyAPI.lib` / `INF` 前提の mismatch** である

このため new driver track の実装方針は次で固定する。

1. まず matching SDK を導入し、repo に入っている bundled `CyLib` と新 SDK の `CyAPI.h` / `CyAPI.lib` / `cyusb3.inf` を比較する
2. 比較結果から interface GUID、device binding / service 名、endpoint 列挙前提を source of truth として docs に固定する
3. production code の変更範囲は `AnalogBoard_Dll` の USB 接続層だけに限定し、public API (`EP2_SendData`, `EP4_GetData`, `EP6_GetData`) は維持する
4. SDK 更新後の first gate は `USBBoard_Connect` と idle 状態での `EP4_GetData` 安定化とし、その後に `EP2 -> EP4 -> EP6`、low-density smoke、high-density timeout 再評価へ進む

> **2026-03-19 status note**: dedicated worktree `feature/win11-driver-compat` / `D:\ubuntu\jupyter\sys_analyzer\AnalogBoard-win11-driver` を切り、以後の Win11 driver 実行手順は [driver_next.md](./driver_next.md) を正本とする。

## 2026-03-12 Recovery Strategy

- **release track**: `dev`
  - tag `0.1.4` から開始し、実機安定性を最優先にする
  - まず legacy acquisition loop の completion semantics だけを FPGA 仕様に合わせる
  - `DDR_WR_END` は draining 開始ヒント、`DDR_RD_END` を最終完了条件として扱う
- **lab track**: `lab/0.2.2-engine-semantics`
  - `WaveAcquisitionEngine`, SimRunner, UnitTest を使って FPGA semantics を再現する
  - host-visible `DDR_WR_END=s_samp_end`, internal `DDR_WSTOP`, stale `DDR_WR_END`, stale `WAVE_WR_CNT`, `RD_WAIT` を model / test へ追加する
  - lab で妥当性が確認できた変更だけを release track に戻す
- **investigation track**: `investigate/ep6-regression`
  - `0.1.4 -> 0.1.7` の途中停止原因を二分探索する
  - empty capture 問題と混ぜない

### Recovery gates

1. `0.1.4` を high-density 実機条件で 3-5 サイクル再取得し、failure signature を baseline と比較可能な形で再確認する
2. `dev` 上で low-density semantic gate を通し、`ep6Timeouts=0` と `DDR_RD_END=1` 到達を 3-5 サイクルで確認する
3. high-density では timeout / disconnect / `DDR_RD_END=0` の failure を completion semantics と別問題として分類する
4. SimRunner / UnitTest で `RD_WAIT` と stale status を再現できる状態にする
5. release track が安定した後にだけ `WaveAcquisitionEngine` の段階再導入を検討する

> **2026-03-18 gate status update**: 1-3 は release-track Phase 1 gate として `0.1.4r7` までで閉じた。4 は lab asset として継続育成する。したがって次の主作業は **baseline で Phase 2 前半の write scope を切り、Queue / Reader / Writer / Publisher 分離へ入ること** である。

> **2026-03-19 compatibility-track update**: 上記 release/lab/recovery の主線は維持しつつ、Win11 固有の host/driver 差分をつぶすための `new driver compatibility track` を parallel に追加した。これは Phase 2 / PR-04 の release-track gate を差し替えるものではなく、old driver rollback で field continuity を保ったまま、matching SDK 導入後に `CyLib` と USB binding を整合させるための準備トラックである。

### Branch convergence policy

- `dev` を code mainline とし、`main` へは PR 経由でマージする
- `dev` は docs / plan / process log と host acquisition code の両方を保持する
- 一時 branch を閉じるときは、branch 自体を残すことより「必須 change が `dev` に移り切っていること」を優先する
- 運用形は `main` + `dev` の 2 ブランチ体制で確定した (2026-03-19)

## 前提方針（FPGA Firmware A 非改変）

- 原則として `FPGA_FW/ANA_20250129_restored`（Firmware A）には変更を加えない
- 本プランの改修対象はホスト側（`AnalogBoard_Dll` / `AnalogBoard_TestApp` / テスト / ドキュメント）に限定する
- FPGA 変更が必要な事項は本プランの実装対象から除外し、別タスクとして管理する

## スコープ

### In Scope

- Phase 0-5 で定義した DLL 修正、取得/書き込み分離、状態遷移明確化、耐久テスト追加
- `sys_app` preview のような `.bin` consumer が動作中でも acquisition を継続できる host 側安定化
- `LoopTestProcessThread_EP6_GetData` の責務分解と `WaveAcquisitionEngine` 導入
- KPI による改善判定（失敗率、欠損/破損、スループット、遅延）
- FPGA Firmware A の既存レジスタ/既存通信シーケンスを維持したままのホスト側安定化

### Out of Scope

- 波形フォーマット仕様そのものの変更（`fl/fh` の low/high 意味変更を含む）
- UI/画面設計の刷新や操作フロー変更
- FPGA ファームウェア・USB プロトコル仕様の変更
- FPGA レジスタマップの追加/再割り当て（例: 新規デバッグレジスタ公開）
- Rust 実装の即時本採用（Phase 6 判定を通過した場合のみ次イテレーションで検討）

## ステータス


| Phase | 内容                         | 状態      |
| ----- | -------------------------- | ------- |
| 0     | 現状可視化・ログ計測                 | completed |
| 1     | DLL 層 致命的バグ修正              | in_progress |
| 1.5   | preview consumer 耐性の短期安定化     | pending |
| 2     | Reader/Writer/Publisher パイプライン導入 | pending |
| 3     | スレッド安全性・堅牢性向上                | pending |
| 4     | コード構造改善・状態遷移明確化              | pending |
| 5     | テスト戦略・耐久テスト                  | pending |
| 6     | KPI 判定・オプション検討               | pending |


## 実施ログ運用（process_log）

- 本プランの進捗・判断・計測結果は、`docs/archive/usb-acquisition-stability/2026-03-02-usb-acquisition-stability-log.md` に**逐次追記**する（Phase 0 着手時に新規作成）
- 各 Phase の着手時/完了時に最低1エントリを記録する
- 記録項目は「日時、Phase/PR、実施内容、結果、課題/次アクション」を必須とする
- Phase 1 以降の判断は process_log 上の Phase 0 計測結果（baseline）を参照して実施する

---

## TDD 実施方針（全Phase共通）

- 本プランの実装は Red-Green-Refactor を必須とし、**実装前に failing test を追加**する
- 不具合修正（例: mutex 競合、Save Path 不正、timeout/retry）は、再現テストを先に追加してから修正する
- 各 PR の DoD には「新規 failing test が修正前に失敗し、修正後に成功すること」を含める
- テスト実行は Windows 環境で `scripts\run_with_vsdevcmd.bat` を経由して行う

---

## Detail References

詳細は役割ごとに分けて管理する。

- architecture / root causes / contract:
  [USB Acquisition Stability Architecture Notes](./2026-03-02-usb-acquisition-stability-architecture.md)
- phase / PR / test / rollback:
  [USB Acquisition Stability Execution Notes](./2026-03-02-usb-acquisition-stability-execution.md)
- field signature / session bundle:
  [USB Acquisition Stability Field Reference](./2026-03-02-usb-acquisition-stability-field-reference.md)

## Related Documents

- 実行中の gate と未完タスク:
  [USB acquisition stability checklist](./2026-03-02-usb-acquisition-stability-checklist.md)
- release-track 次タスク:
  [baseline_next.md](./baseline_next.md)
- lab support 次タスク:
  [lab_next.md](./lab_next.md)
- 実施履歴:
  [process log](./2026-03-02-usb-acquisition-stability-log.md)
- archive:
  [USB データ取得・書き込み安定性改善プラン（FPGA ソース不要版）](../plans/2026-03-04-usb-stability-without-fpga-source.md)
  [Wave Output Format Handover (2026-03-02)](../plans/2026-03-02-wave-output-handover.md)
