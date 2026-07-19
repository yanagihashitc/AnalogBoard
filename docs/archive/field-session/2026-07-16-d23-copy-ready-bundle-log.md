# D23実機copy-ready bundle Process Log

## 対象

- Package: `r7-driver-ep4-polling-20260715T1618JST-source-package`
- READY: `TELEMETRY_CSV_READY_1314`
- Canonical operation: `docs/260706-field-session-runbook.html` Draft 2.6

## Entries

| DateTime (JST) | Phase | Activity | Result | Evidence / Next |
|---|---|---|---|---|
| 2026-07-16 16:00 | Design | ユーザー承認の推奨構成を固定 | 既存READYを唯一の実験用folderとして拡張し、親直下から誘導。driver rollbackは自動化しない | `tasks/todo.md`; checklist |
| 2026-07-16 16:06 | TDD Red→Green | NoDfx exact-one/readback、required context、CSV schema/row/cycle/duration、summary/final log、active sessionをtest-first実装 | module不在Red後、19/19 Green。operator sequence追加後23/23 Green | `tools/tests/FieldSessionBundle_test.ps1` |
| 2026-07-16 16:14 | Operator flow | 00〜11の番号付きBAT／HTMLとsystem-state gateを配置 | NoDfxはadmin apply＋readback、Gate C/B inventory、leg別launcher、CSV/log verifier、evidence folderを一式化 | `TELEMETRY_CSV_READY_1314/run_*`; `tools/` |
| 2026-07-16 16:18 | Offline docs | 親入口、統合手順、rollback、結果sheet、current runbook/短縮版を同梱 | N smoke=別session 2 cycles、N/B formal=各33 cyclesを明示。汎用launcherはfail-closedで無効化 | `00_START_D23_SESSION.html`; `00_RUN_THIS_SESSION.html`; `SESSION_RESULT_SHEET.md` |
| 2026-07-16 16:21 | Package verification | READY checksumを51 immutable filesへ更新しpositive/negative gateを実施 | positive Pass。EXE破損、runtime欠落、Build ID改変はexit 2。runtime evidence追加は許容 | `manifest/checksums.sha256`; verifier outputs |
| 2026-07-16 16:23 | Final verification | PowerShell parse、23 tests、HTML/link、current runbook copy、source overlay、build/旧evidence integrityを照合 | 全Pass。EXE/DLL hash不変。実機runtime状態fileなし | `/tmp/d23-final-*`; source overlay attestation |
