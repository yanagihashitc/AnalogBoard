# AnalogBoard Docs Index

このディレクトリは、再構築プラン実行中の正規仕様・運用手順・過去資料を分けて管理する。

## Current Source of Truth

- [AnalogBoard rebuild plan](plans/260710-analogboard-rebuild-plan.html)
- [Downstream modification plan](plans/260703-downstream-modification-plan.html)
- [Cross-repo execution roadmap](../../task_management/260710-cross-repo-execution-roadmap.html) — AnalogBoard / gcsa / sys_app / gain_scope の中央実行・進捗ロードマップ

## Current Operations

- Phase 0のcurrent workと次gateは[AnalogBoard rebuild plan](plans/260710-analogboard-rebuild-plan.html)および[Cross-repo execution roadmap](../../task_management/260710-cross-repo-execution-roadmap.html)を参照。
- [Repository closeout prompt](operations/repository-maintenance/2026-07-20-repository-closeout-prompt.md)
- [task_management synchronization prompt](operations/repository-maintenance/2026-07-20-task-management-sync-prompt.md)

## Guides

- [Build guide](BUILD.md)
- [Simulation guide](guides/SIMULATION.md)
- [Implementation tracking](guides/IMPLEMENTATION_TRACKING.md)
- [Branch strategy](../branch_plan/README.md)
- [USBPcap analyzer](../scripts/pcap-analysis/README.md)
- [Troubleshooting](troubleshooting/INDEX.md)
- [Test perspectives](test-perspectives/INDEX.md)

## Reference

古い仕様書、移行手順、ドライバ資料、棚卸しメモは [reference/](reference/) にまとめる。

- [Initial application specification](reference/application_specification_initial.md)
- [Initial commit architecture](reference/initial-commit-architecture.md)
- [CyUSB driver documentation](reference/CyUSB.md)
- [Gain adjustment manual](reference/gain-adjustment-manual.html)
- [Git migration notes](reference/git-migrate-analogboard-linux.md)
- [Knowledge inventory](reference/knowledge_inventory.md)
- [Project direction](reference/project_direction.md)
- [2026-07-17 USBPcap protocol baseline](reference/usb-recording-corpus/2026-07-17/README.md)

## Archive

完了済みチェックリスト、古い process log、過去プランは [archive/](archive/) に置く。進行中の判断根拠は [process_log/INDEX.md](process_log/INDEX.md) から辿る。

- [Completed field session runbook](archive/field-session/260706-field-session-runbook.html)
- [NoDfx dual-driver session (2026-07-16〜17)](archive/field-session/2026-07-16-nodfx-dual-driver-session.html)
- [New-driver fail-fast task — superseded](archive/field-session/2026-07-15-new-driver-fail-fast-next-task.html)
- [New-PC high3 data audit](archive/field-session/2026-07-14-new-pc-high3-audit.md)
- [Field session runbook revision summary](archive/field-session/2026-07-14-runbook-revision-summary.md)
- [Field artifact and worktree cleanup](archive/repository-maintenance/2026-07-19-field-artifact-worktree-cleanup-checklist.md)
- [Branch plan refresh](archive/repository-maintenance/2026-07-19-branch-plan-refresh-checklist.md)
- [Repository-maintenance closeout](archive/repository-maintenance/2026-07-20-repository-maintenance-closeout-checklist.md)
- [Historical USB acquisition stability plan](archive/usb-acquisition-stability/2026-03-02-usb-acquisition-stability.md)
- [Historical USB acquisition stability checklist](archive/usb-acquisition-stability/2026-03-02-usb-acquisition-stability-checklist.md)
- [Historical baseline next](archive/usb-acquisition-stability/baseline_next.md)
- [Historical driver next](archive/usb-acquisition-stability/driver_next.md)
- [Historical lab next](archive/usb-acquisition-stability/lab_next.md)
