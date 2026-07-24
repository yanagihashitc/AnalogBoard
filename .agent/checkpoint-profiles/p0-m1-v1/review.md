# Review checkpoint profile: P0-M1 v1

Profile ID: `P0-M1-v1`

Review the current P0-M1 batch against the Draft 4.9 D17 channel-mapping
golden-regression acceptance criteria. Findings come first with severity,
exact file references, and acceptance impact. A pass is incomplete until every
finding is fixed and retested or explicitly blocked.

## Scope and immutable-asset checks

- The diff is limited to the tracked mapping contract, golden fixture,
  regression harness and focused tests, procedures, bounded evidence logs, and
  current-batch tracking under the approved P0-M1 scope, including
  `docs/reference/d17-golden-regression/`.
- Everything below `artifacts/` remains a read-only invariant: no create,
  change, move, rename, delete, or touch occurred. Asset reads are limited to
  the decode and integrity verification required by the pinned golden inputs.
- gcsa remains a read-only reader authority. No file, commit, worktree,
  dependency, cache, environment, or other state below `../gcsa` is changed.
  The exact gcsa commit, source/configuration path, and channel-order symbol
  used as authority are recorded as provenance.
- sys_app, task_management, `goal.md`, `goal.draft.md`, fixed `prompt.md`,
  checkpoint routers, existing versioned profiles, acquisition/product code,
  driver, registry, firmware, and sibling repositories remain unchanged.
- D17 is not changed or reinterpreted, no additional channel-identification or
  other real-hardware run is performed, and no unapproved product Decoder,
  Writer, or Tier 1/2 integration is introduced.

## Mapping-authority and golden-input checks

- The CH1-CH13 mapping is derived mechanically from the current gcsa
  channel-order authority, not inferred, guessed, or entered by hand.
- The derived contract contains exactly 13 unique channel entries and exactly
  the D17 label set: `FSC`, `SSC`, `FL1` through `FL6`, and `fsGMI`, `ssGMI`,
  `flGMI`, `dGMI`, `bfGMI`. Count, uniqueness, order, or label-set mismatch
  fails closed as `decision_required`; no missing value is filled locally.
- Golden inputs are selected by entry identity from
  `docs/reference/initial-recording-corpus/2026-07-17/manifest.json`, with the
  manifest path and SHA-256 plus each selected entry's path, SHA-256, and size
  pinned in tracked metadata.
- Input selection is bounded rather than covering all 3,520 bin assets. The
  procedure records the representative FL/FH selection rule and exclusion
  reasons without weakening pair completeness or identity checks.
- The inherited P0-C4 custody boundary is explicit: selected assets remain
  pre-D19 plaintext, local-only, read-only, and prohibited from export.

## Payload-free reference and determinism checks

- Pinned inputs are decoded only with the provenance-recorded current gcsa
  reader. Its version/commit, invocation, environment, authoritative path, and
  symbol are recorded without modifying gcsa.
- Tracked fixtures contain only manifest identity references and, for each
  channel, a decoded-array SHA-256 digest plus a bounded statistical summary.
  Decoded waveform arrays, asset payload bytes, raw or bulk derived dumps, and
  absolute host locators are absent from tracked fixtures, evidence, process
  logs, the PR, and CI inputs or outputs.
- Fixture and contract generation have deterministic ordering and
  serialization. Two regenerations from identical pinned inputs are
  byte-identical; commands, exits, tool identity, and resulting hashes are
  recorded.
- Missing or unreadable inputs, identity mismatch, mapping mismatch, decode
  failure, non-deterministic output, or any payload-boundary violation is a
  typed failure and is never downgraded to a warning or partial pass.

## Regression-harness and Phase 1 seam checks

- The persistent harness compares candidate output with the golden reference
  for all channels and requires mapping and digest equality.
- Focused positive tests establish the accepted current-gcsa reference path.
  Payload-free synthetic negative fixtures independently exercise channel
  permutation, label mismatch, missing channel, extra channel, dtype drift,
  shape drift, and value/digest drift.
- Every required regression class produces a stable typed failure. Unknown,
  malformed, duplicate, missing, or excess structure cannot pass by warning,
  truncation, implicit reordering, coercion, defaulting, or partial comparison.
- The Phase 1 connection contract documents candidate input format, invocation,
  and the all-channel mapping/digest acceptance rule. It does not claim that
  the future product Decoder/Writer or Tier 1/2 CI integration already exists.

## Verification, evidence, and completion checks

- Focused verification remains green after every fix and covers deterministic
  regeneration, the accepted mapping/reference path, and all required typed
  negative boundaries.
- `git diff --check`, complete current-batch diff review, tracked/staged path
  and mode inspection, normalized regular-file and symlink rejection, and
  payload/secret/generated-file scans pass. No `artifacts/` or sibling change
  is present.
- The process log records the applied profile path, expected and actual Git
  blob identities, findings, fixes, skipped or unavailable checks, focused
  verification results, evidence hashes, and residual limits. An unavailable
  or failed check is never relabeled as passed.
- Existing behavior is preserved, work remains within the current batch, and
  focused verification stays green; review findings do not expand the approved
  product scope.
- P0-M1 completion requires its phase PR human merge and live evidence
  verification. Product Decoder/Writer implementation, Tier 1/2 integration,
  A-4b, Frozen v1, Phase 0 completion, and later phases remain open.

Stop and report if the gcsa-derived mapping is not exactly the D17 13-entry
contract, any asset or sibling mutation/export is observed, payload bytes or
absolute host locators cross the tracked/PR/CI boundary, determinism fails, or
the batch attempts to reinterpret D17 or close a later scope.
