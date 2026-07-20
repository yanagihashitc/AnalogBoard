# PR #2 reviewer and Cursor Bugbot fixes checklist

Target plan: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)
Process log: [Process log](2026-07-21-pr2-bugbot-review-log.md)
Created: 2026-07-21

## Phase 1: TDD fixes

- [x] Add failing regression tests for the five actionable findings
- [x] Refresh completion recency and exclude superseded OUT request bytes
- [x] Keep unknown tail EP4 status out of non-success lifecycle counts
- [x] Reject unsupported source manifest versions
- [x] Validate TShark paths and convert launch `OSError` to `AnalyzerError`

## Phase 2: Verification and closeout

- [x] Run focused tests and Python compilation checks
- [x] Verify existing six-capture generated output remains stable where expected
- [x] Run diff/static checks and inspect the complete change set
- [x] Record review results, residual risks, and archive tracking
