# Host-FPGA Acquisition Reference タスクチェックリスト

対象プラン: [Host-FPGA Acquisition Reference](./2026-03-12-host-fpga-acquisition-reference.md)
プロセスログ: [Process Log](./process_log/2026-03-12-host-fpga-acquisition-reference-log.md)
作成日: 2026-03-12

---

## Phase 1: 調査内容の整理

依存: なし

- [x] `aebf296adf72a8ac5a8355f7f539dca87521f724` の acquisition フローを確認して要点を抽出する
- [x] 仕様書と FPGA VHDL の対応から `DDR_WR_END` / `DDR_RD_END` の意味を整理する
- [x] 今後の host 実装での guardrail を短い参照ドキュメントにまとめる

**検証コマンド:**
```powershell
rg -n "aebf296|DDR_WR_END|DDR_RD_END|RD_WAIT" docs/2026-03-12-host-fpga-acquisition-reference.md
```

---

## Phase 2: 既存ドキュメントへの導線追加

依存: Phase 1

- [x] active acquisition stability doc から新規参照ドキュメントへリンクする
- [x] process log と index に今回の docs 追加を記録する

**検証コマンド:**
```powershell
rg -n "Host-FPGA Acquisition Reference" docs/2026-03-02-usb-acquisition-stability.md docs/process_log/INDEX.md
```

---

## 全 Phase 共通チェック

各 Phase 完了前に以下を確認:

- [x] docs 内リンクとファイル名の整合が取れている
- [x] process_log にエントリ追記済み
- [x] acquisition 実装者が参照すべき source of truth を明記した
