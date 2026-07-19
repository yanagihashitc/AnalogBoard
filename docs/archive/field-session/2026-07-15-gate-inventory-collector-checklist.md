# Gate B/C inventory collector checklist

対象プラン: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](2026-07-15-gate-inventory-collector-log.md)
作成日: 2026-07-15
完了日: 2026-07-15

## Phase 1: Contract tests

依存: 2026-07-15 owner decision（既存 high×3 は参考証跡として保持し、Gate B へ進む）

- [x] Package artifact hash and fixed build ID mismatch are fail-loud.
- [x] Gate B/C expected driver mismatch is fail-loud.
- [x] Sample ID, physical USB port note, and direct-cable confirmation are required.

## Phase 2: Collector and field procedure

依存: Phase 1

- [x] Add a read-only collector under `scripts/field-session/`.
- [x] Collect package, driver, USB topology, PC/OS, data disk, and active power plan evidence.
- [x] Save timestamped JSON, text summary, and detailed CSV/text evidence.
- [x] Copy the collector into the fixed Gate B/C field package and refresh its checksum manifest.

## Phase 3: Gate and documentation sync

依存: Phase 2

- [x] Record Gate A as reference evidence with owner-approved Gate B entry.
- [x] Require inventory collection before Gate B and after the Gate C driver swap.
- [x] Update the canonical AnalogBoard plan and synchronize the central mirror/roadmap.

## Validation

- [x] PowerShell parser and contract tests pass.
- [x] Fixed package checksum verification passes.
- [x] HTML parse, duplicate ID, TOC anchor, local link, and Mermaid checks pass.
- [x] `scripts/sync-active-plans.sh --check` reports no registry drift.
- [x] `git diff --check` and complete scoped diff review pass.

## Remaining field gate

The live FX3 inventory is intentionally not fabricated in this implementation batch. Run the packaged collector on the measurement PC before Gate B, and repeat it after the Gate C driver-only swap.
