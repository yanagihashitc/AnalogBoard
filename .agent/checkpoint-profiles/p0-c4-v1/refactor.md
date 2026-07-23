# Refactor checkpoint profile: P0-C4 v1

Profile ID: `P0-C4-v1`

This checkpoint applies only to the Phase 0 corpus-indexing work authorized for
P0-C4. Preserve asset bytes, recorded identities, fail-closed validation, and
the distinction between availability, restore, and reacquisition while
improving clarity, determinism, and maintainability.

## Scope guard

- Limit changes to tracked corpus indexes, manifests, and procedures under
  documentation/reference paths such as `docs/reference/`, focused validation
  scripts and tests, bounded evidence logs, and current-batch tracking.
- Treat `artifacts/field-session/2026-07-17-characterization/` as a read-only
  invariant. Reading for availability and SHA-256 verification is allowed;
  creating, changing, moving, renaming, deleting, or touching anything below
  that path is forbidden.
- Do not modify asset payload bytes or place any asset, raw waveform, raw pcap,
  generated payload copy, or absolute host locator in Git, a PR, CI, or any
  external destination.
- Do not modify gcsa, sys_app, task_management, acquisition/product code,
  driver, registry, firmware, `goal.md`, `goal.draft.md`, fixed `prompt.md`,
  checkpoint routers, or existing versioned profiles.
- Refactor only files touched by the current batch. Do not expand P0-C4 into
  corpus-gate closure, D17 golden regression, A-4b, Frozen v1, or Phase 0
  completion.

## At-rest boundary

- Preserve the 2026-07-20 owner decision: these assets are pre-D19 plaintext,
  local-only evidence. Do not claim that Git exclusion supplies D19 at-rest
  protection and do not migrate or reacquire the assets in this scope.
- Tracked indexes may contain metadata only: normalized repository-relative
  path, byte size, SHA-256, asset kind, run/pair correspondence, and required
  timestamp/clock information. They must remain payload-free.
- Validation and evidence output must never include payload bytes, waveform
  samples, pcap packet bodies, secrets, or unbounded command output.

## Refactor checks

1. Keep expected counts in one typed contract surface: bin 3,520, cfg 6,
   telemetry 2, and capture 6. Do not duplicate drifting totals across scripts,
   tests, indexes, and procedures.
2. Keep discovery, normalized-path validation, readability checks, streaming
   SHA-256 calculation, manifest comparison, and relationship validation behind
   explicit boundaries with stable typed failures.
3. Preserve deterministic ordering and serialization so the same files and
   tool version produce byte-identical tracked metadata and bounded evidence.
4. Recompute SHA-256 from readable source files. Never trust a recorded digest
   merely because it is present, and never turn missing, unreadable, extra,
   count-mismatched, hash-mismatched, or pair-mismatched input into a warning or
   partial pass.
5. Keep FL/FH pair completeness, cfg/telemetry/capture correspondence, clock
   basis, and tolerance explicit and reviewable per run.
6. Model availability and restore as separate verdicts. Availability proves
   only that the canonical local locator is present and readable; restore
   requires recovery from distinct media or host with the identical SHA-256.
   Reacquisition always creates a separate corpus version.
7. Keep large-file processing streaming and bounded. Do not add payload caches,
   temporary copies, touching probes, or write-based readability checks.
8. Update focused tests before behavior changes and keep synthetic fixtures
   payload-free. Preserve Given/When/Then intent and exact fail-closed cases.

## Verification

- Run focused tests for the touched validation/indexing surfaces and keep them
  green after refactoring.
- Re-run the full expected-count, present/readable, streaming SHA-256, FL/FH
  pair, correspondence, and clock/tolerance checks against the canonical local
  locator when the active goal authorizes asset reads.
- Run `git diff --check`; inspect tracked/staged paths, modes, sizes, and
  symlinks; confirm no asset payload, raw waveform, raw pcap, generated copy,
  secret, protected goal/prompt/router/profile, or sibling change is present.
- Record the applied profile path, expected and actual Git blob identities,
  verification results, retained complexity, and residual limits in the active
  process log.

Stop if a refactor would mutate an asset, export payload, weaken a fail-closed
condition, conflate availability with restore, change an accepted count or
identity, or broaden the authorized P0-C4 scope.
