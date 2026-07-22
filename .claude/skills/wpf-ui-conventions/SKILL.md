---
name: wpf-ui-conventions
description: 新アプリ（C#/.NET 10 WPF）UI実装の規約——通知・リソース化・モード依存表示・チャンネル設定ダイアログ・ログ表示・配色。Use when implementing or reviewing any WPF UI feature of the rebuilt AnalogBoard app - windows, dialogs, notifications, run controls, gate panel, health display, settings screens, XAML. Triggers on WPF, XAML, UI実装, ダイアログ, 通知, トースト, MessageBox, チャンネル設定, Manual Get, ラン管理, マルチプロット, GMI波形表示, UX-01〜20. Do NOT use for acquisition core changes (use acquisition-hotpath-guard) or the plan HTML documents themselves.
---

# WPF UI 実装規約

正規仕様は `docs/plans/260710-analogboard-rebuild-plan.html` の「UX／オペレータ体験要件」（UX-01〜20）「UI仕様：現行アプリとの突き合わせ」「UI モック」節。設定項目の完全継承チェックは settings-parity-check スキルを使う。

現行P0-R1 standalone prototypeは`net10.0-windows`、.NET SDK `10.0.302`／Desktop Runtime `10.0.10`固定。production WPF shellはP0-R1のscope外とする。

## 絶対規則

- **MessageBox 禁止**（UX-02）。3段階通知: トースト（数秒・取得を止めない）→ 通知センター（ベル・未読・履歴・該当ログへジャンプ）→ モーダルは「ユーザ判断なしに進めない」場合のみ（同名保存先上書き／取得中stop／取得中設定変更＝UX-10 の3系統）
- **UI 文字列は初日から全て .resx 経由**。ハードコード禁止（UX-14 規約）
- ログビューアは**到着順厳守**（ソート禁止＝旧 LBS_SORT の反省、UX-08）。フィルタ・検索・レベル・エクスポート付き
- 終了時のサイレント設定上書き禁止（UX-10）。設定保存は明示操作（レシピ FR-12）のみ
- UI スレッドの都合で取得コアを待たせない。コアへの参照は C ABI 越しの集計データのみ（acquisition-hotpath-guard 参照）

## ラン制御（FR-11 / UX-19）

- **Manual Get ON: 開始/停止ボタンを表示。OFF: 非表示**にし「外部開始待機」インジケータを表示（グレーアウトではなく非表示）
- OFF 時は「適用」で外部開始待機に入る（現行 Set 挙動踏襲）
- ラン状態（待機/取得中/ドレイン/再アーム/保存/完了/エラー）とライブ統計（経過・イベント数・書込量・再アーム回数）をランバーに常時表示。ボタンキャプションを状態機械に流用しない
- 機械側スイッチ起点の計測（外部開始）も同一のラン管理で扱う

## チャンネル設定ダイアログ（UX-18 / D18）

- CH毎設定（有効・ゲイン・Offset・Ext Ctrl）は「チャンネル設定…」ボタンで開く**モーダルダイアログに集約**。メイン画面は要約（有効CH数・代表ゲイン）のみ
- ゲイン: CH1-8 は1スライダー自動求解（リレー4段×DAC へスナップ、D7）＋詳細で段/DAC 表示。CH9-13 はリレーのみのスナップ選択。合計 ×N 表示は必須（現行 UpdateTotalGain 踏襲）
- 範囲外は即時バリデーション表示（現行 CColorEdit の色警告踏襲）。値域は settings-parity-check の一覧が正
- 「適用」で一括レジスタ書込（現行 Set 契約: 個別変更では書き込まない・読み戻し無し）。未適用変更は dirty 表示
- 取得中・外部開始待機中はダイアログと適用を無効化

## プロット（FR-02/FR-13 / UX-20）

- 中央は**マルチプロットのタイルグリッド**（追加・削除・並べ替え可）。各タイルは種別（scatter/histogram/GMI波形）・軸・表示スケール・**表示母集団gate**（ヘッダのドロップダウン、既定 All Events）を独立に保持
- GMI波形タイルは直近〜100波形のオーバーレイ（ゲイン調整の即時フィードバック用・表示専用）。ch選択（fsGMI〜bfGMI）。データはコアからの固定上限スナップショットのみ（生ストリームを UI に引き込まない）
- GMI波形タイルの gate 絞り込みは pulse特徴量ベース gate のイベントマスク適用（sys_app `visualization_waveform_execution.py` と同方式）
- タイル構成はレシピの軸/gate構成（FR-12）と UI状態永続化（UX-09）に含める
- 先行例: sys_app `CompareSmallMultiplesGrid.tsx`（タイル毎 selectedGateId）・`AxisSelector.tsx`・`WaveformPlot.tsx`（NaN区切り単一トレースで N 波形重ね描き）

## 表示・配色

- scatter/histogram の表示スケール（linear/log/biexp/asinh）は**表示のみ変換**。保存値・gate 座標は生ADC線形（UX-04）
- 密度カラーマップ・gate 色・状態色は colorblind-safe（viridis 系等）。状態は色＋アイコン形状の併用（UX-06）
- ヘルス表示は数値だけでなく是正ナビ（トリアージカード）を伴う（UX-01）: USB2 フォールバック→ケーブル/スイッチ確認、Type A 多発→物理層チェックリスト
- UI 状態（パネル配置・選択軸・スケール・直近レシピ）は終了時保存・次回復元（UX-09）

## 完了条件

- 対応する UX-ID の要件文を満たすこと（プランの UX 表と突き合わせ）
- フェーズ2ゲートのユーザビリティ基準を意識: 「新規オペレータが手順書なしで1計測（レシピ選択→取得→gate→保存）を完走できる」
