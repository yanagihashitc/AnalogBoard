# Host-FPGA Acquisition Reference Process Log

## 対象プラン

- [Host-FPGA Acquisition Reference](./2026-03-12-host-fpga-acquisition-reference.md)
- [チェックリスト](./2026-03-12-host-fpga-acquisition-reference-checklist.md)
- [Process Log INDEX](../process_log/INDEX.md)

## 記入ルール

- 本ログは実施都度、時系列で追記する（上書きしない）
- 各タスクの着手時/完了時に最低 1 エントリを記録する
- 「判断理由」がある変更（方針変更、スコープ調整、閾値変更）は必ず記録する
- 測定値は可能な限り実数で記録し、推測値は `estimate` と明記する
- **100行を超えたら分割**: 新ファイル (`-02`, `-03`...) を作成し、INDEX.md に追加する

## Process Log Entries

| DateTime (JST) | Phase / Task | Activity | Result | Evidence | Risks / Issues | Next Action |
|---|---|---|---|---|---|---|
| 2026-03-12 19:40 | Phase 1 / Init | checklist / reference doc / process log の作成方針を確定 | initialized | `docs/2026-03-12-host-fpga-acquisition-reference*.md`, このファイル | FPGA fix 自体は未実装 | `aebf296` flow と VHDL semantics を文書化する |
| 2026-03-12 19:45 | Phase 1 / Investigation | `aebf296` の `LoopTestProcessThread_EP6_GetData` と現行 FPGA VHDL / 仕様書を照合し、legacy host flow と `DDR_WR_END` / `DDR_RD_END` の意味差を整理 | completed | `git show aebf296adf72a8ac5a8355f7f539dca87521f724:AnalogBoard_TestApp/Dialog1_Main.cpp`, `FPGA_TOP.vhd`, `MEAS_CTRL.vhd`, `DDR3_WCTRL.vhd`, `DDR3_RCTRL.vhd`, 仕様書 Rev1.0 | 実機 fix の正当性は別途検証が必要 | reference doc に guardrail を明記する |
| 2026-03-12 19:50 | Phase 2 / Docs Wiring | active acquisition stability doc から新規 reference への導線を追加し、process log index に登録 | completed | `docs/2026-03-02-usb-acquisition-stability.md`, `docs/process_log/INDEX.md` | docs-only のため build/test は未実施 | 以後 acquisition 実装前に本 reference を参照する |
