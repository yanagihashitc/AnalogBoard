# Project Instructions for Coding Agents

This file is automatically read before starting work.

## 現在のプロジェクト状態（最重要）

このリポジトリは **AnalogBoard 再構築プラン実行中**。AnalogBoard 固有の要件・意思決定・下流契約の正規ソースは、次の2つのローカルプラン：

- **親プラン**: [docs/plans/260710-analogboard-rebuild-plan.html](docs/plans/260710-analogboard-rebuild-plan.html)
  — 確定意思決定 D1〜D23／機能要件 FR-01〜13／UX要件 UX-01〜20／gcsa・sys_app 契約／UI仕様（現行アプリとの突き合わせ）／UIモック／ロードマップ（フェーズ0〜4）
- **下流改変プラン**: [docs/plans/260703-downstream-modification-plan.html](docs/plans/260703-downstream-modification-plan.html)
  — sys_app S1〜S11（S2/S5 廃止）・gcsa G1〜G5 の改変プロンプト集（直読版）。**原則実行保留（出力契約凍結後）だが G1/G3/G4 は凍結前必須**（フェーズ0 往復プロトタイプの前提）
- **横断ロードマップ**: [../task_management/260710-cross-repo-execution-roadmap.html](../task_management/260710-cross-repo-execution-roadmap.html)
  — AnalogBoard / gcsa / sys_app / gain_scope の実行順・現在 phase/status・同期 gate と証拠の正規インデックス。詳細設計の正本は各リポジトリのプランに置く

**フェーズ0実機確認と地固めの現在地（2026-07-23）**：r7/r18直結A/B、スイッチ区間A/B、D17確認は完了し、r18相当全4runは`queue_full_timeout`でD4 gate未成立。旧build＋driver `1.2.3.20`のGate Bはlogが34/34 Type CだがFL/FH data integrityとtelemetry CSV／summary不足で全体未判定。新driver `1.3.0.4`のSet→automatic wait EP4 failure（3/3）は、10ms polling remediation buildでも実機3/3 Fail（poll-rate仮説棄却）ののち、**2026-07-16のDFX A-B-A（session `20260716_111343`）でDFXが因果要因と確定**（A=3/3 fail→B=NoDfxでlow/high Type C全Pass→復元A=再発）。**owner decision D23でNoDfx（`WdfDirectedPowerTransitionEnable=0`）を正式採用**。判定build `r7-driver-telemetry-graceful-stop-20260716T1314JST`（`TELEMETRY_CSV_READY_1314`・hash pin済み）による**NoDfx dual-driver session（D23 session `20260716_2`）は2026-07-16〜17に実施済み**：N leg＝N0／N-smoke Pass＋30 valid high cycle非formal aggregate（EP4 failure 0・rearm中央値15ms／max 16ms）、R＝`1.2.3.20`へrollback Pass、B leg＝初回pre-trigger EP4 failure（`UsbdStatus=0xC0000011`）→Type B drain stall→承認済みUSB replug後recovery 33/33 Type Cで、**ownerがGate Bを条件付きPassとして閉鎖（2026-07-17 13:02 JST）**。legacy re-arm baseline取得済み（B-formal p50 15／p99 32／max 32ms）。旧driver側EP4 failure／Type B stall（DFX registryはdriver default状態で発生・因果未確定）はopen reliability note——D23だけで全故障解消とは言えない。session証跡は`artifacts/field-session/packages/r7-driver-ep4-polling-20260715T1618JST-source-package/TELEMETRY_CSV_READY_1314/evidence/session/20260716_2/`、2026-07-17 characterization＋USBPcap captureは`artifacts/field-session/2026-07-17-characterization/`。注意：新driver INFはインストール毎に`WdfDirectedPowerTransitionEnable=1`を上書きするため、driver入替後はNoDfx再適用＋`dfx_status.ps1`検証が必須。**2026-07-19のowner gateでD4＝「r7移植例外」承認・N leg formal soak＝phase 4の100run soakへ集約が確定**（実機gate・owner gateは全決着）。USBPcap P0-C1〜C3はcompleted。**P0-S1/P0-S2は2026-07-22 completed**（D19保護込み accepted gcsa 往復合格、round-robin採用。PR #4／#5 merged・中央独立再検証Pass）。**P0-R1は2026-07-23 completed**（phase PR #7 merged `db5eb758…`・sealed Official gate合格・中央独立検証Pass、renderer決定＝preallocated WPF `WriteableBitmap`）。次scopeは**P0-C4**（2026-07-23 owner決定でD17 golden regressionより先行）で、本 preparatory PR のmerge後に中央dispatchされるgoalを待つ。それまではRun／Resume／Advanceを実行しない。Phase 0の残作業はP0-C4／初期録画コーパス、D17対応のgolden regression、A-4b／Frozen v1。フェーズ1着手はFrozen v1後。hardwareはdriver `1.2.3.20`のまま。

**P0-C4 pre-dispatch preparation（Draft 4.7・2026-07-23）**：P0-R1はphase PR #7 merge（`db5eb758…`）、`AB-PERF-REF-v1`上のsealed Official gate合格、中央独立検証Passをもってcompleted。採用rendererはpreallocated WPF `WriteableBitmap`。次scopeのP0-C4はD17 golden regressionより先行するが、現時点はcheckpoint profileとDraft 4.7を導入するrepository-local preparatory PRだけが承認範囲である。human merge後に中央からgoalがdispatchされるまでRun／Resume／Advanceを実行しない。

## Branch Strategy

- 新規開発は最新`main`から短命task branchを作る。`main`へ直接実装せず、長期`dev` branchは新設・再利用しない
- D4のr7移植baselineはコード／挙動の移植元。Git起点は`main`のままとし、`validation/win11-driver-r7-acq`はreference-onlyで保持する
- 旧`dev`、`feature/win11-driver-compat`、`lab/0.2.2-engine-semantics`を日常開発へ再利用・一括mergeしない。必要資産だけをtest/evidence付きで選択移植する
- 通常はmain worktree 1本。並行taskで必要な場合だけ専用worktreeを作り、差分保全後に撤去する
- branch命名・資産移植・remote退役条件の正本は[branch plan](branch_plan/README.md)

運用ルール：

- 実装・設計判断の前に、親プランの該当節を読むこと
- **確定済み意思決定（D1〜D23）を再議論・変更しない**。変更が必要と考えた場合はオーナーに確認する
- コード実態とプランが食い違う場合は、勝手にどちらかへ合わせず**差異を報告**する
- プラン中の file:line 参照はスナップショット。実装時に現ソースで再検証する

横断タスク管理との同期ルール：

- 共通契約・責務境界・依存 DAG・同期 gate・owner decision は `../task_management/plans/` 直下の横断 plan を正本とし、AnalogBoard の詳細設計・Step・テスト・受け入れ基準はローカル plan を正本とする
- `../task_management/plans/` 直下の registry 登録済み AnalogBoard mirror は閲覧用。直接編集せず、ローカル plan から一方向同期する。正本と差分がある場合は勝手に逆流させず報告する
- phase/gate の進行時は、同じ作業単位でローカル plan の metadata と evidence を更新し、`task_management` workspace の同期 workflow へ引き渡す。中央側で mirror、roadmap の表・Mermaid node・caption を同期し、repository・branch・current Step/phase・検証日・PR/commit/test evidence・次 gate/blocker を残す
- AnalogBoard sandbox から `../task_management` は原則 read-only。唯一の例外として、phase完了後のAdvanceは中央 `scripts/completion-handoff.py publish` を使い、`handoffs/inbox/`へ新規completion reportを1件作成できる（2026-07-22 owner決定）。既存inbox/archive、中央mirror、roadmap、plan、NEXT、AGENTSその他は編集・上書き・削除せず、中央更新は`task_management`専用sessionが行う
- status は `planned / in_progress / blocked / gate_ready / completed / superseded` を使う。`completed` は合格した phase gate、merged PR、または owner 承認済みの同等証跡がある場合だけ付ける
- 完了・陳腐化・置換済み plan はローカル規約と中央の `plans/archive/` 直下にある registry 登録済み AnalogBoard mirror を同じ変更単位で同期する。roadmap 自体は横断作業全体が完了または superseded になった場合だけ `roadmaps/archive/` へ移す

## 確定済み意思決定の要点（正規表は親プラン「確定した意思決定」節）

| # | 決定 |
|---|---|
| D1 | UI = C#/.NET 10 WPF ＋ ネイティブ C++17 コア（C ABI 境界） |
| D4 | 移植ベースライン = **r7（移植例外・2026-07-19 owner gate通過）**。当初の「r18合格後にdev」は2026-07-14のr18 gate Fail（4/4 `queue_full_timeout`）で不成立となり、owner gateでr7移植例外を承認。dev限定回復policyはheader単位でTier2回帰つきcherry-pick可。devの修正・再gateはcritical path外の任意作業 |
| D5/D6 | 本アプリが A/H/W を算出・記録し、下流は再計算しない。gate 軸 = FL 8ch×{A,H,W} の24カラム |
| D9/D10 | 主出力 = gcsaネイティブ Zarr ストア（float64 特徴量＋生FH＋生FL）。生FL保全は既定ON |
| D12 | Zarr C++ライタ = c-blosc＋最小自作ライタが実質第一候補（D19 で昇格）。ZarrWriter 境界で隔離し、フェーズ0の保護込み gcsa 往復検証で確定 |
| D13 | gate は正準 payload＝**canonical v2** のみで出力（2026-07-12 D-GA-11 で v1→v2 改訂。boolean 構造化・schema マーカー。旧 sys_app 2.0 envelope 拡張は出さない。正本＝task_management `plans/260712-gcsa-sys-app-gate-contract-alignment-plan.html`） |
| D14 | 特徴量版管理 = 整数版 `feature_schema_version`＋`meta.versions` 記録。同一データセット内の版混在禁止 |
| D16 | インライン切替スイッチ除去を標準化（機械の固定ケーブルは交換不可。着脱可能なスイッチ区間を外した直結が標準、外した区間はラベル保管） |
| D17 | 物理ch順 = gcsa想定順（`FSC,SSC,FL1..FL6` / `fsGMI..bfGMI`）。実機CH1–CH13と現行gcsaの波形／ラベル対応を2026-07-14に目視確認済み。追加runは行わず、既存対応を新Decoder／Writerのgolden regressionへ固定 |
| D18 | 現行設定項目の完全継承。チャンネル毎設定はダイアログ集約（UX-18）、開始/停止は Manual Get ON のみ表示（UX-19） |
| D19 | ストア暗号化採用：測定配列チャンクのみ Blosc(lz4)→AES-256-GCM。AEAD は Store ラッパ層（`.zarray` は内側 Blosc を宣言、独自 codec ID は使わない）で行い、`key_id` 1バイト＋座標 AAD を使用。鍵は難読化埋込＝抑止レベル、meta.json/gate は平文。gcsa へ G4（Store ラッパ＋鍵管理） |
| D20 | AnalogBoard と sys_app は別アプリ維持（統合しない）。到達形＝一製品・複数プロセス。sys_app は ML/DR 実装後に Tauri デスクトップ化 |
| D21 | ストア可視性契約：meta.json に additive で `status: open\|finalized`・`finalized_at`・`write_generation`・パーティションマニフェスト（配列毎行数＋per-partition sealed）・特徴量毎 min/max。単一書き手規則（`datasets/<id>/` の書き手は取得コアのみ）。下流は finalized のみ対象 |
| D22 | sys_app はストア完全直読（registration-only import・射影コピー廃止）。D11 の達成手段変更。唯一の設計変更は補償列の sidecar 化 |
| D23 | 新driver構成 = cyusb3 1.3.x＋NoDfx（`WdfDirectedPowerTransitionEnable=0`）。2026-07-16 DFX A-B-Aで因果確認し採用。適用は配布スクリプト＋readback検証のみ（INF編集・NoDfxセクション・regedit禁止）。driver（再）インストール毎に再適用必須（INFが`=1`を上書き）。旧driver 1.2.3.20はフェーズ0 baseline＋rollback経路 |

## アーキテクチャ不変条件（実装時に必ず守る）

- **生バイト列は C ABI を越えない**。UI へ渡すのは集計済み（ビニング済み scatter・histogram・カウンタ・特徴量・固定上限のサンプリング済み GMI 波形スナップショット＝FR-13 既定〜100波形）のみ。GC を約200MB/s 経路に触れさせない
- **取得ホットパスにログ・計装を入れない**（過去に計装が取得タイミングを壊した実績あり）。EP6 スクラッチの CRT malloc/free 契約を守る（`docs/troubleshooting/usb.md` の負の知識）
- 読取・デコード・ファイル書込はスレッド分離し `BlockingQueue` で背圧。ZarrWriter は writer スレッド（EP6 ホットパス外）に置く
- 通信録画（EP2/4/6・タイミング・config・障害トレース）は通信層に**初日から**組み込む（エミュレータ／回帰コーパスの供給源）

## 出力契約（下流互換・違反すると gcsa/sys_app が壊れる）

- 特徴量カラム順は **ch毎に A→H→W** で固定：`FSC_A,FSC_H,FSC_W, SSC_A,…, FL6_W`（24カラム）。GMI 順 `fsGMI,ssGMI,flGMI,dGMI,bfGMI`
- 全特徴量・gate 座標は**生ADC線形**で記録（log/biexp を焼き込まない。表示変換は表示時のみ）
- gate は正準 payload＝canonical v2（D-GA-11 改訂。ellipse 不可・色 `#RRGGBB`・`gate_id` は `^[A-Za-z0-9_-]+$`・boolean は構造化 `{op, operands}`・schema マーカー必須）
- **主経路で `_cfg.txt` / .bin を出力しない**。互換エクスポートは既定OFF・明示操作時のみ。取得パラメータの権威は `meta.json`（cfg_params）
- データセット名は既定 `tube_N` 連番（タイムスタンプ命名は廃止）。`meta.json` に `display_name`/`comment`/`tags`（additive）。リネームは表示名のみ——フォルダ名・dataset id は安定を保つ

## UI 実装規約

- **現行アプリの全設定項目を新アプリでも設定可能にする**（値域も継承）。正規の一覧は親プラン「UI仕様：現行アプリとの突き合わせ」節の対応表
- **MessageBox 禁止** → 非ブロッキング通知センター（UX-02）。モーダルは「ユーザ判断なしに進めない」場合のみ（保存先上書き・計測破棄・取得中設定変更＝UX-10）
- **UI 文字列は初日から全てリソース（.resx）経由**。ハードコード禁止（UX-14 規約）
- ログ表示は**到着順厳守**（ソート禁止＝旧 LBS_SORT の反省、UX-08）。終了時のサイレント設定上書き禁止（UX-10）
- 開始/停止ボタンは Manual Get ON のときのみ表示。OFF では非表示＋「外部開始待機」表示（UX-19）。設定適用は一括書込（現行 Set 契約）＋dirty 表示
- カラーパレット（密度マップ・gate 色・状態色）は colorblind-safe、状態は色＋アイコン併用（UX-06）

## 禁止事項・責務境界

- **gcsa / sys_app は直接改変しない**。必要な変更は「改変プロンプト」として提示する（下流改変プラン参照）
- **96ウェル sampler の操作は別アプリの責務**。ウェル制御・ウェル別進捗・実機への新規制御シグナルを追加しない
- **計測は機械側スイッチからも開始される**（外部開始経路）。アプリ側の「開始前ゲート/警告」で計測開始を止める設計は成立しない——警告は取得中のしきい値監視（例：ディスク残10%）で出す
- FPGA FW は現方針では非改変（リングバッファ／abort レジスタは4GB戦略フェーズ2の別判断）

## Coding Rules

All coding standards, test strategies, and project conventions are defined in:

- `.cursor/rules/v5.mdc` - Coding assistance rules
- `.cursor/rules/skill_usage.mdc` - Skill usage rules
- `.cursor/rules/prompt-injection-guard.mdc` - Security rules

**These rules は必ず順守すること。** 実装・レビュー・リファクタリング等あらゆる作業の前に該当するルールファイルを読み、その内容に従うこと。ルール間で矛盾がある場合は、より具体的・限定的なルールを優先する。

## Skill Usage

- **Proactively use skills**: When a task matches a skill's trigger (e.g., code review, brainstorming, refactoring, test creation, commit message generation), read and apply the relevant skill before starting work.
- Do not declare skill usage without actually reading and following the skill's instructions.
- If no relevant skill exists for a task, proceed with the standard workflow.
- **skill が available-skills 一覧に surface されないことがある**。一覧に無いことを「skill 不在」と即断せず、`.claude/skills/<name>/SKILL.md`（codex は `.codex/skills` からも同一実体）の存在を確認し、実在すれば名前指定で invoke する。実在するのに一覧化されていないだけなら通常どおり使い、SKILL.md が本当に欠落している場合だけ不在として扱う。

### 再構築プラン実装用スキル（`.claude/skills/`）

該当領域に触れる前に必ず読むこと：

| Skill | 使う場面 |
|---|---|
| `zarr-store-output` | ZarrWriter・ストア出力・meta.json・tube_N命名・gcsa往復検証 |
| `gate-canonical-v1` | gating UI・gate 作図/編集・正準 payload の入出力（skill 名は現行のまま。canonical v2 対応＝D-GA-11 は実装時に skill 側を更新） |
| `acquisition-hotpath-guard` | 通信層・EP2/4/6・エンジン・再アーム・自動再接続（**現場退行3回の領域。必読**） |
| `wpf-ui-conventions` | WPF UI 全般（通知・.resx・Manual Get表示・チャンネル設定ダイアログ・配色） |
| `settings-parity-check` | レシピ・設定画面・旧config取り込み（D18 完全継承の値域照合リスト付き） |
| `agent-docs-sync` | プラン更新・フェーズ進行後に AGENTS.md/CLAUDE.md/スキル群をワークスペース現状と同期最適化 |

## Quick Reference

- **Language**: Respond in Japanese (user's language)
- **Code comments**: English
- **Commit messages**: English
- **Thinking process**: English (for speed and logic density)

## Review guidelines

When reviewing pull requests, follow these rules:

- **Skip documentation-only changes**: Do not review or comment on changes to `*.md` files, `docs/`, or `docs_ja/` directories.
- **Skip CODEMAPS**: Do not review changes in `docs/CODEMAPS/` directories.
- **Focus on source code**: Only review changes to source code files (`.py`, `.ts`, `.tsx`, `.js`, `.jsx`, `.css`, `.json` config files, and for this repo `.cs`, `.cpp`, `.h`, `.xaml`).
- If a PR contains only documentation changes with no source code changes, respond with a brief acknowledgment instead of a detailed review.

## Project Structure

- **Plans（正規仕様）**: `docs/plans/` — 上記2つの AnalogBoard ローカル plan（親・下流改変）
- **Cross-repo plan（共通契約、`plans/` 直下）**: `../task_management/plans/`
- **Cross-repo roadmap（実行・進捗）**: `../task_management/260710-cross-repo-execution-roadmap.html`
- **AnalogBoard plan mirror（閲覧専用、registry 登録済み）**: `../task_management/plans/`
- **FPGA firmware**: `FPGA_FW/`。FPGA 関連の実装・調査・デバッグ時に参照
- **Device logs**: 実機計測ログは `logs/`。調査・デバッグ・4GB再アーム設計（D2）の入力。ただしlegacy r7ログにはhost再アーム完了markerがなく、`[PR01][CYCLE]`→次の`FPGA start sampling.`からp99を算出しない
- **移植資産**: 親プラン「資産の移植・破棄」節が正。ポリシー群ヘッダ・`AnalogBoard_UnitTest/`（17スイート）・SimRunner＋FpgaDdrModel・レジスタ意味論ドキュメントは keep、`Dialog1_Main.cpp` の god class・`AnalogBoard_Dll` 実装・MFC リソース層は discard（プロトコル文書としてのみ保持）
- **現行UI仕様の根拠**: `AnalogBoard_TestApp/Dialog1_Main.cpp`・`AnalogBoardTestApp.rc`（親プラン「UI仕様」節の対応表に file:line あり）

### Downstream consumer: sys_app

Outputs produced by this app are consumed by `../sys_app`. When investigating how data is used or its specification, refer to:

- **API frontend source**: `../sys_app/apps/api/src/`
- **Web frontend source**: `../sys_app/apps/web/src/`
- **API CODEMAP**: `../sys_app/apps/api/docs/CODEMAPS/`
- **Web CODEMAP**: `../sys_app/apps/web/docs/CODEMAPS/`

## Key Policies

- Always read related files before making changes
- **Long-term over short-term**: 短期的な対症療法（その場しのぎ）ではなく、長期的視点で実装する。将来の保守性・拡張性・一貫性を優先し、症状ではなく根本原因を修正する。安易な回避策・ハードコード・技術的負債の先送りで済ませない。
- **Global over local optimization**: 局所最適ではなく、gcsa・sys_app・AnalogBoard の3アプリ全体を俯瞰した全体最適で設計・実装する。単一リポジトリ内の都合だけで判断せず、アプリ横断の一貫性・整合性、データ/契約の流れ、共有インタフェースへの影響を優先する。リポジトリ横断の計画・契約・依存関係ゲートは、sibling `../task_management` リポジトリの active roadmap を正本とする。
- Do not commit/push unless explicitly requested. Instructions recorded in the active batch of `tasks/todo.md` to run checkpoint/review `git commit` or `git push` count as explicit user authorization for those actions.
- **commit/push の前に必ず `$claude-review-fixer` を実行する**（codex 実装フロー等、対話・手動作業での commit/push が対象）。review を通し、必要な findings を修正してから commit/push する——未 review のまま commit/push しない。skill が available-skills 一覧に出ていなくても、`.claude/skills/claude-review-fixer/SKILL.md`（codex は `.codex/skills` からも同一実体）の存在を確認して名前指定で実行する（上記 Skill Usage の確認手順に従う）。SKILL.md が本当に欠落している場合は commit/push せず報告する。自律 `/goal` フロー（P0-C4 等）はこの限りでなく、`goal.md` の Batch Checkpoint review pass と外部 review 制約に従う。
- For modifications to **gcsa / sys_app**, present a modification prompt instead of making changes directly（下流改変プランに集約する）
- For questions and confirmations: present recommended options and use **AskUserQuestion** or **ask_user_input** style

## Implementation Approach (TDD ＋ フェーズゲート)

- Use a **TDD (Test-Driven Development)** approach for implementation
  1. **Red**: Write a failing test first that defines the expected behavior
  2. **Green**: Write the minimum production code to make the test pass
  3. **Refactor**: Clean up the code while keeping all tests green
- Always ensure tests exist and pass before considering a task complete
- When fixing bugs, first write a test that reproduces the bug, then fix the code
- **単体テスト緑は弱い証拠**（過去のin-place改修は3回、テスト緑のまま現場退行した）。取得・通信に触れる変更は Tier1/2 エミュレータ回帰（録画再生＋障害注入）を通し、フェーズ末の実機ゲートで受け入れる（親プラン「ロードマップ」「非機能要件」節）
- 下流互換に触れる変更は**デュアル・シームレス往復テスト**（ストア（暗号化・finalized）→gcsa 無再計算→sys_app registration-only＋直読→scatter/gate 一致）を回す

## Windows Build/Test Execution

- legacy C++ のWindows build/testは `scripts\run_with_vsdevcmd.bat` 経由で実行する。このwrapperは`VsDevCmd.bat`を初期化してx64 developer environmentでcommandを実行する
- legacy C++ のビルドには **`msvc-build` スキル**（`.claude/skills/msvc-build/scripts/build.sh`）を使う。WSL→cmd.exe→VsDevCmd のブリッジと多重クォートを吸収する
  - Solution build: `build.sh app|rebuild|clean [Debug|Release]`（`AnalogBoard_TestApp.sln` / 構成 Debug|x64, Release|x64 / プロジェクト `AnalogBoard_TestApp`, `AnalogBoard_Dll`）
  - Unit tests: `build.sh test` = `AnalogBoard_UnitTest\build_test.bat`（cl で ~10 スイートをビルド＆実行。**sln 外。msbuild ターゲットではない**）
  - Coverage: `build.sh coverage` = `scripts\run_coverage.bat`（OpenCppCoverage, 80% 閾値）
- P0-R1の.NET WPF standalone prototypeは`net10.0-windows`、SDK `10.0.302`／Desktop Runtime `10.0.10`固定。`msvc-build`スキルを使わず、`scripts/scatter-rendering/verify.ps1`を`goal.md`指定のPowerShell commandで実行する
- 素の msbuild 例（ソリューション）:
  - `cmd /d /c "scripts\run_with_vsdevcmd.bat msbuild AnalogBoard_TestApp.sln /p:Configuration=Debug /p:Platform=x64 /m:1"`
- production新アプリ（.NET 10 / C++コア）のビルド手順はフェーズ1のプロジェクト雛形確定時に本節へ追記する

## Implementation Tracking

実装作業時は [docs/guides/IMPLEMENTATION_TRACKING.md](docs/guides/IMPLEMENTATION_TRACKING.md) のルールに従い、チェックリストとプロセスログで進捗を管理すること。再構築作業ではフェーズ（0〜4）とゲート基準（親プラン「ロードマップ」節）に紐づけて記録する。

### `tasks/todo.md` 運用

- 実装・修正・レビュー対応など複数ステップの作業では、着手前に計画を `tasks/todo.md` に active batch として記録し、進捗に合わせて `[ ]` → `[x]` を更新する。短い確認・単発調査のみの場合は省略可。
- active batch に checkpoint があり、その前提タスクが完了している場合は、後続番号のタスクに進む前に checkpoint を先に実行する。ready な checkpoint を飛ばさない。
- 作業完了時は Review セクションを追加し、実施した検証・残リスク・未実施項目を記録する。
- batch 内の全項目（Review セクションを含む）が `[x]` になったら、その batch 全体（見出し・項目・メモ）を `tasks/todo.md` から `tasks/todo_archive.md` へ移動し、完了済み batch を `todo.md` に残さない。
- `tasks/todo.md` と `tasks/todo_archive.md` は短い運用ファイルとして Markdown のまま管理する。
