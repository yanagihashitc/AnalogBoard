# Review checkpoint profile: P0-C4 v1

Profile ID: `P0-C4-v1`

Review the current P0-C4 batch against the Draft 4.7 corpus-indexing acceptance
criteria and the 2026-07-20 local-only owner decision. Findings come first with
severity, exact file references, and acceptance impact. A pass is incomplete
until every finding is fixed and retested or explicitly blocked.

## Scope and immutable-asset checks

- The diff is limited to tracked corpus indexes, manifests, procedures,
  focused validation scripts/tests, bounded evidence logs, and current-batch
  tracking.
- `artifacts/field-session/2026-07-17-characterization/` remains a read-only
  invariant: no create, change, move, rename, delete, or touch occurred below
  it. Any access is limited to availability and integrity verification.
- No corpus payload, raw waveform, raw pcap, packet body, payload-derived bulk
  dump, generated asset copy, binary, or absolute host locator is tracked,
  staged, attached to the PR, sent to CI, or exported externally.
- gcsa, sys_app, task_management, acquisition/product code, driver, registry,
  firmware, `goal.md`, `goal.draft.md`, fixed `prompt.md`, checkpoint routers,
  and existing versioned profiles remain unchanged.
- Corpus-gate closure, D17 golden regression, A-4b, Frozen v1, and Phase 0
  completion remain outside the batch.

## At-rest and payload-free checks

- The index states that the assets are pre-D19 plaintext, local-only evidence
  and records the 2026-07-20 no-export owner decision without claiming D19
  protection from `.gitignore` or Git exclusion.
- Tracked asset entries contain metadata only: normalized repository-relative
  path, byte size, SHA-256, kind, run/pair relationship, and required
  timestamp/clock information.
- Procedures do not upload, attach, copy, migrate, reacquire, or otherwise
  move asset payloads. Logged commands and evidence are bounded and
  payload-free.

## Integrity and relationship checks

- Expected counts are exact and cover every kind: bin 3,520, cfg 6, telemetry
  2, and capture 6. Missing and extra entries both fail closed.
- Every manifest entry was checked for normalized path confinement, presence,
  regular-file/readable status, exact byte size, and recomputed SHA-256.
  Recorded hashes alone are not accepted as verification.
- Discovery failures, unreadable files, hash mismatches, count drift, duplicate
  identities, and unexpected files remain typed failures and cannot be
  downgraded to warnings or partial success.
- FL/FH completeness is verified per run. cfg, telemetry, and capture
  correspondence is explicit, and the clock basis plus tolerance is stated and
  checked rather than inferred.
- Availability and restore have separate fields and verdicts. A local
  present/readable check is never labeled restore success; restore requires an
  identical SHA-256 recovered from distinct media or host. Reacquisition is
  documented as a new corpus version, never as restore.
- Failure-trace absence remains explicit; successful Type C protocol evidence
  is not used to assert an unobserved failure cause.

## Verification and repository checks

- Focused tests cover deterministic ordering/serialization and all positive and
  fail-closed boundaries using payload-free synthetic fixtures.
- The authorized live verification recomputes SHA-256 and records exact tool
  identity, counts, exits, and bounded results without storing payload bytes.
- `git diff --check`, complete branch-diff review, tracked/staged path and mode
  inspection, symlink rejection, payload/secret/generated-file scan, protected
  file identity checks, and sibling-status comparison pass.
- The process log records the applied profile paths, expected and actual Git
  blob identities, findings, fixes, skipped or unavailable checks,
  verification, and residual limits. An unavailable or failed check is never
  relabeled as passed.

Stop and report if any asset mutation/export is observed, any exact integrity
or relationship condition is unmet, restore evidence is unavailable but
claimed, or the batch attempts to close a later gate without its own approval.
