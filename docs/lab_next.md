# Lab Next

対象 branch: `lab/0.2.2-engine-semantics`

## Purpose

このメモは lab branch の「baseline をどう支えるか」を確認するための短い実行メモ。

参照:

- 全体方針: [2026-03-02-usb-acquisition-stability.md](./2026-03-02-usb-acquisition-stability.md)
- active checklist: [2026-03-02-usb-acquisition-stability-checklist.md](./2026-03-02-usb-acquisition-stability-checklist.md)
- field signature: [2026-03-02-usb-acquisition-stability-field-reference.md](./2026-03-02-usb-acquisition-stability-field-reference.md)
- 履歴: [process log](./process_log/2026-03-02-usb-acquisition-stability-log.md)

## Goal

**baseline の Phase 2 / PR-04, PR-05 を支える simulation / test / safe diff を維持する。**

## Do Next

1. baseline の PR-04 field gate で必要な simulation 観点を棚卸しする
2. queue 導入後の completion semantics regression 観点を SimRunner / UnitTest に追記する
3. safe diff inventory を Phase 2 向けに更新する

## Watch Points

- `DDR_WR_END` は draining hint、`DDR_RD_END` は final completion
- `RD_WAIT`, stale `DDR_WR_END`, stale `WAVE_WR_CNT`, internal `DDR_WSTOP`
- timeout 直前 backlog と drain 境界
- baseline に戻せる helper / telemetry / test only diff

## Out Of Scope

- 実機 release 判定
- baseline への直接実装
- new driver compatibility の本番対応
