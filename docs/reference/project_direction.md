# Project Direction

## 現状の認識

### システム構成
- USB 経由で FPGA 搭載アナログボードから波形データを高速取得する Windows ホストアプリケーション
- 3層構成: FPGA FW (Quartus) → FX3 USB 3.0 コントローラ → Host (MFC TestApp + DLL)
- 出力 `.bin` は下流の `sys_app`（API + Web）で消費される

### 各ブランチの到達点
- **dev** (`490371e`): Phase 2 PR-04 code-side 完了。WaveAcquisitionEngine（Reader/Writer/Publisher パイプライン）導入済み。Ep6TimeoutRecoveryPolicy / Ep6TransferTuningPolicy 追加。151 個の unit test。field gate 実施中
- **lab/0.2.2-engine-semantics** (`d873b56`): SimRunner + 12種 JSON シナリオ、DDR stale/RD_WAIT coverage。検証資産として proven
- **feature/win11-driver-compat**: Win11 新ドライバ対応。endpoint discovery hardening 済み、first gate pending
- **Initial commit** (`aebf296`): TestApp (MFC GUI) + DLL (USB通信) が動作する状態。Win10 では安定動作するが、Win11 では USB Timeout が頻発し不安定。`.tmp` → `.bin` rename 機能は未実装

### テスト・ログ資産
- unit test 151個（FPGA レジスタ、ファイル I/O、USB 転送、パス検証、エンジンなど）
- シミュレーション preset 12種（normal, timeout, disconnect, write_fail 等）
- 実機ログ: `logs/` にバージョン別で蓄積（0.1.4r1〜r17）
- トラブルシューティング集: `docs/troubleshooting/` (build, environment, fpga, usb)

## dev/lab からの知見

### うまくいったこと
- **comparison build による逆行分析**: 0.1.5/0.1.6 の EP6 timeout 増加の原因を allocator 変更と特定 → CRT malloc/free 固定で解消
- **DDR semantics の明確化**: `DDR_WR_END` = draining hint、`DDR_RD_END` = final completion と整理 → empty capture 解消
- **軽量計測への転換**: hot-path の PrintLog がwaveform 破損を引き起こす教訓 → 集計 + 取得後サマリに切り替え
- **Simulation-First**: 複雑な状態遷移（RD_WAIT, stale status）を先に model/test で検証するアプローチが有効
- **Process Log**: 実装の「なぜ」を記録することで後続判断が高速化

### 失敗・教訓
- **intrusive instrumentation**: Phase 0 の高頻度ログが計測対象を乱し、waveform corruption の原因に
- **allocator 変更の波及**: `new[]/delete[]` への変更が EP6 タイムアウト増加を招いた（0.1.5/0.1.6 regression）
- **ブランチ分散**: baseline/dev/lab/feature で多数ブランチに分散し、管理コストが増大
- **過剰な改修による袋小路**: dev/lab ではポリシークラス・エンジン・シミュレーション等を段階的に追加した結果、変更が変更を呼ぶ状態に。Win11 の安定化は未達成のまま、Win10 での安定性まで低下している。改修が問題を増やしている現状であり、これが min_* ブランチで Initial commit からやり直す最大の動機

## 課題

- **コードベースの肥大化**: dev では Phase 0〜2 の段階的改修で多数のポリシークラス・ヘルパーが追加された。必要最小限がどこまでかの見極めが必要
- **high-density timeout の安定性**: Ep6TimeoutRecoveryPolicy 導入後の field verification が未完了
- **Win11 ドライバ互換性**: Win11 用ドライバに切り替えるとアプリ自体が動かなくなる（`Get ep4 register data failed.`）。first gate 未通過
- **ブランチ間の知見統合**: dev/lab の成果物が分散しており、Initial commit からやり直す際に何を持ち込むか整理が必要
- **FPGA FW は非改変前提**: ホスト側だけで対処する制約下での安定性確保
- **Initial commit にテストがない**: dev/lab の 151 個の unit test は min_* では直接使えない。必要なものを選んで持ち込むか書き直す必要がある
- **Win11 Timeout の原因未特定**: ドライバ、CyAPI、DLL 実装、OS の USB スタックのどこに起因するか切り分けができていない
- **CyAPI が `.gitignore` 対象**: worktree ごとに手動コピーが必要で、環境構築の摩擦になっている
- **MFC ベース**: レガシー技術で拡張・保守が困難。将来のプロット機能追加や UI 改善のハードルが高い
- **仕様書が存在しない**: アプリの動作仕様がコードにしかなく属人化している。Rust 等での作り直しの前提が欠けている
- **ディスク I/O に対して極度に sensitive**: 同一ドライブで Docker コンテナを起動するだけでアプリが不安定化する。他プロセスのファイル読み書きが波形取得に干渉しており、I/O パスにバッファリングや非同期化が不足している可能性がある

### Initial commit コードの構造的問題（FPGA RTL との突き合わせで判明）

- **EP6 読み込みとファイル書き込みが同一スレッド**: `EP6_GetData` → `SaveWaveDataToFile` → `File.Close()` → `CreateWaveDataFile` が全て直列実行。ファイル I/O 中に USB 転送がブロックされ、FPGA 側 FIFO が溢れる。Docker 等でディスク I/O 負荷が増えると即座に破綻する直接原因
- **EP6 `XferData` が同期ブロッキング**: `XferData(FALSE)` で同期待ち。Win10 では許容範囲だが、Win11 の USB スタックではタイムアウトがより厳密に適用される
- **EP4 ポーリングが `Sleep(0)` スピンループ**: Win11 のスケジューラでは `Sleep(0)` が数 ms に膨れる可能性があり、その間に FPGA からのデータが FIFO に蓄積 → オーバーフロー
- **DDR 制御との非同期タイミング**: `DDR_WR_END` は FPGA 側で非同期に変化するが、ホストの EP4 ポーリングとは同期していない。ホストが早すぎるタイミングで読み始めると `LIMIT_ADDR` が最新でない可能性
- **バッファ切り替え時の memcpy**: USB 転送直後に大量の `memcpy` を実行。この間に次の USB フレームがドロップするリスク

## やりたいこと

### 最終ゴール
1. **Win11 での安定稼働** — USB Timeout を解消し、Win10 同等以上の安定性を確保
2. **エラーに対する robustness** — タイムアウト・切断等に対して graceful に復帰できる堅牢性
3. **`.tmp` → `.bin` rename 出力** — 書き込み中は `.tmp` で出力し、完了後 `.bin` にリネーム。下流 `sys_app` が準リアルタイムで安全に読める仕組み

### ドキュメント整備
4. **Initial commit 時点の仕様書作成** — Rust 等で作り直せる粒度で、アプリの全仕様を文書化
5. **処理フロー解説資料** — 実際の使用フロー（接続→設定→取得→保存）に沿ってコードの処理の流れを解説。各コードが何をやっているか徹底的に理解できるようにする

### 将来的に
- **UI 改善** — 現状のUIは非常に使いにくい。操作性・レイアウトの改善案を作成
- **アプリ内プロット表示** — 現在は外部で `.bin` を読み込んでプロットしているが、最終的にはアプリ内で波形プロットを見ながら gain 調整・データ取得ができるようにする

## 制約・前提条件

- FPGA FW (Firmware A) は変更不可。ホスト側のみで対応
- USB デバイス: VID `0x04B4`, PID `0xFFF2`(USB3.0)/`0xFFF3`(USB2.0)
- EP6 読み取りは 16KB アラインメント (`kEp6ReadAlignmentBytes = 0x4000`)
- ビルド環境: VS2022, x64 のみ（x86 廃止）
- CyAPI ライブラリ: `.gitignore` 対象、worktree では手動複製が必要
- 下流 `sys_app` との `.bin` 互換性維持

## 優先順位

1. **Win11 での USB Timeout 解消** — 現状最大のブロッカー
2. **`.tmp` → `.bin` rename 出力** — 下流 `sys_app` が準リアルタイムで消費するために必須
3. **エラー robustness** — シミュレーション・テスト資産を活用して堅牢性を確保
4. **仕様書・処理フロー解説** — 全トラックと並行して進める

## スコープ外（当面）

- UI 改善・アプリ内プロット — 安定性・機能・仕様書が先
- FPGA FW の変更 — ホスト側のみで対処
- x86 ビルド対応 — x64 のみ
- dev/lab のコードの直接マージ — 知見のみ持ち込み、コードは最小限に書き直す
- Rust 等での作り直し — まず仕様書を完成させてから

## 方針

### 並行トラック

| トラック | ブランチ | 役割 |
|---|---|---|
| 知見蓄積 | `dev`, `lab/*` | シミュレーション・テスト資産の拡充、Recovery Policy の検証、新たな仮説の実験。開発を継続して知見を貯める |
| 確定実装 | `min_change` | dev/lab で確度が高まった知見だけを Initial commit から最小差分で実装する本命ブランチ |
| 原因調査 | `min_debug` | Win11 Timeout の原因切り分け（ドライバ・CyAPI・DLL・OS USB スタックの各層を個別検証） |
| 試行検証 | `min_experiment` | min_debug の調査結果をもとに修正案を試行。確認が取れたら min_change へ |
| 仕様書 | （ブランチ不問） | 全トラックと並行して、分かったことから随時書き進める |

### フロー

```
dev/lab（知見蓄積）──→ 確度高い ──→ min_change（確定実装）
                                         ↑
min_debug（原因調査）──→ min_experiment（試行）──┘

仕様書 ← 全トラックの成果を随時反映
```

### 進め方

1. **知見の棚卸し** — dev/lab で得られた知見と未解決事項を整理
2. **仕様書作成** — Initial commit の動作仕様 + 知見を反映
3. **棚卸し結果から確度の高い知見を min_change に実装** — DDR semantics 修正、allocator 固定など、dev/lab で既に実証済みのものを最小差分で反映
4. **dev/lab で調査・実験を継続** — 目的を絞って試行。Win11 Timeout の原因特定、Recovery Policy の検証など
5. **min_debug で Win11 Timeout の原因切り分け** — 未解決の課題について、ドライバ・CyAPI・DLL の各層を個別に検証
6. **min_experiment で修正案を試行** — 5 の結果をもとに修正案を検証
7. **min_change に追加実装** — 5, 6 で確度が高まったものを最小差分で反映
7. **`.tmp` → `.bin` rename** — Win11 安定化と並行して min_experiment → min_change で実装
8. **robustness 強化** — dev/lab のシミュレーション・テスト資産を活用して堅牢性を積み上げる
9. 安定稼働後に UI 改善案 → アプリ内プロット表示へ段階的に進む
