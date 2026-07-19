# r7 EP4 Polling Portable Source Package Checklist

対象プラン: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](2026-07-15-portable-source-package-log.md)
作成日: 2026-07-15

## Package scope

- [x] Freeze the exact `r7-driver-ep4-polling-20260715T1618JST` host source state.
- [x] Include the fixed Cypress SDK 1.3 x64 import library and headers.
- [x] Include FPGA source/project assets and the Win10/Win11 Cypress driver packages.
- [x] Include the build manifest, source diff, verification evidence, and returned failure logs.
- [x] Exclude linked-worktree `.git` metadata and generated host MSVC intermediates.

## Portability and verification

- [x] Add a README with Windows build and field-debug guardrails.
- [x] Generate a complete SHA-256 manifest.
- [x] Create a ZIP archive and verify full decompression and sampled extracted payload hashes.
- [x] Confirm the portable host source diff and pinned binary hashes match the original build manifest.
- [x] Record residual limitations and archive the completed tracking files.
