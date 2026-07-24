# Review checkpoint profile: P1-1 v1

Profile ID: `P1-1-v1`

Review the current P1-1 batch against the Draft 4.11 Tier 1 foundation,
Decoder, and D17 harness-connection acceptance criteria. Findings come first
with severity, exact file references, and acceptance impact. A pass is
incomplete until every finding is fixed and retested or explicitly blocked.

## Scope and immutable-state checks

- The diff is limited to the in-process Tier 1 core model, new Decoder
  (A/H/W plus accumulated min/max), product-owned D17 adapter, Tier 1 suite
  wiring, focused tests, bounded evidence, and current-batch tracking.
- No real hardware operation or driver, registry, firmware, USB endpoint,
  acquisition communication, or machine configuration change occurred.
- gcsa remains read-only. No file, commit, worktree, dependency, cache,
  environment, or other state below `../gcsa` changed.
- Everything below `artifacts/` and every P0 contract, fixture, harness, and
  evidence file remains read-only. Asset reads, if authorized by the goal, are
  limited to identities pinned by the P0-M1 fixture.
- ZarrWriter, legacy exporter, communication, AcquisitionEngine, Recorder,
  production Logger, Tier 2/fault injection, power-fail, C ABI expansion, WPF
  UI, later phases, sibling repositories, central task-management files,
  `goal.md`, `goal.draft.md`, fixed `prompt.md`, checkpoint routers, and
  existing versioned profiles remain unchanged.
- Git, PR, CI, logs, and evidence are payload-free: no raw waveform bytes,
  decoded arrays, samples, bulk derived dumps, asset copies, absolute host
  locators, or secrets are present.

## D17 authority checks

- The only channel authority consumed by product code is
  `docs/reference/d17-golden-regression/channel-mapping-v1.json`, and its
  verified SHA-256 is exactly
  `8e197eade3fff0f7427c7cf0e9d77409624b803a51a782c6a429e705f15fc99b`.
- No independent table, copied label/index list, inferred mapping, fixture-based
  reordering, waveform guess, fallback mapping, or locally invented default is
  present in production code, tests, generated sources, or evidence.
- The implementation requires exactly 13 unique, ordered CH1-CH13 entries.
  Missing, excess, duplicate, relabeled, reordered, malformed, or identity
  drift fails closed with a stable typed error.
- D17 and D1-D23 are unchanged, and no additional channel-identification or
  other real-hardware run is performed or requested.

## Determinism checks

- Identical Tier 1 input produces byte-identical model output. Identical
  Decoder input produces byte-identical candidate serialization and identical
  digests across repeated runs.
- Review production and test code for wall-clock/local time, random generators,
  process identity, temporary or absolute paths, environment variables,
  locale, runtime hash ordering, unordered collection traversal, parallel
  completion order, task scheduling, and mutable global state.
- Any such value that can influence output, ordering, digest, statistics, or
  evidence is a fail-closed finding. Rerunning until green, seeding a hidden
  random source, accepting sorted-after-the-fact corruption, or documenting
  flakiness does not satisfy determinism.
- Channel, pair, feature, statistic, JSON-key, and evidence ordering is explicit
  and independent of parallel execution. Numeric width, signedness,
  endianness, non-finite handling, and serialization are explicit and
  platform-stable.
- Determinism tests compare canonical bytes and digests over at least two
  regenerations, not only semantic JSON equality or one successful run.

## Decoder and harness-connection checks

- A/H/W calculation and accumulated min/max preserve the authorized legacy and
  plan semantics without channel/feature reordering, intermediate rounding,
  implicit narrowing, missing-value invention, or payload escape.
- The integration fulfils without modifying
  `docs/reference/d17-golden-regression/phase1-connection-v1.md`. Its candidate
  schema, canonical `<u2` bytes, bounded statistics, 64 KiB input bound,
  invocation, and all-channel pass rule remain unchanged.
- Golden input is consumed only through P0-M1 pinned selection/reference
  identities. Missing, unreadable, substituted, identity-drifted, malformed,
  wrong-count, wrong-dtype, or wrong-shape input is a typed hard failure.
- The product-owned adapter exercises the real Decoder seam. It does not copy
  expected digests, construct a reference-shaped candidate without decoding,
  bypass the existing harness, add a second comparator, or accept warning,
  partial, or subset results.
- Tier 1 tests permanently invoke the existing harness and require every
  selected pair and all 13 channels per pair to match mapping, dtype, shape,
  digest, and bounded statistics.

## Test and fail-closed checks

- Tests demonstrate Red before Green for behavior changes and include explicit
  Given/When/Then intent.
- Positive coverage includes the accepted Tier 1/Decoder/harness path and
  repeated byte/digest determinism.
- Negative coverage includes mapping identity drift, permutation, label/order
  mismatch, missing/excess/duplicate channel, input identity drift, malformed
  candidate, dtype/shape drift, value/digest drift, non-finite or narrowing
  attempts, and at least one induced nondeterministic-output mismatch.
- Negative tests assert exact exception type plus stable code and bounded
  message. Unknown fields, invalid types, missing values, and order drift
  cannot pass through defaulting, coercion, truncation, warning, or partial
  comparison.
- Tests remain meaningful under mutation: removing the mapping pin, channel
  count, all-channel comparison, digest comparison, or determinism check must
  make an appropriate test fail.

## Verification, evidence, and completion checks

- Focused Tier 1 model, Decoder, adapter, determinism, and D17 harness suites
  are green, and the existing P0-M1 regression suite remains unchanged and
  green.
- Two identical generation runs are byte-identical and digest-identical; the
  commands and resulting bounded hashes are recorded.
- `git diff --check`, complete current-batch diff review, path/mode/symlink
  inspection, payload/secret/generated-file scans, and nondeterministic-API
  review pass.
- No `artifacts/`, P0 authority, gcsa, sibling, protected
  goal/prompt/router/profile, ZarrWriter, communication, Tier 2, hardware,
  driver, registry, firmware, or UI mutation is present.
- The process log records the applied profile path, expected and actual Git
  blob identities, mapping and connection-contract pins, Red/Green evidence,
  deterministic output hashes, findings and fixes, verification results, and
  residual limits. An unavailable or failed check is never relabeled passed.
- P1-1 completion requires its phase PR human merge and central live evidence
  verification. This checkpoint does not close P1-2, P1-3, P1-4, or Phase 1.

Stop and report if the mapping does not come solely from the pinned D17
contract, the Phase 1 interface changes, an independent mapping or expected
digest is copied into product code, output is nondeterministic, a typed failure
is weakened, a P0 asset or authority is mutated/exported, gcsa or system state
changes, payload crosses the evidence boundary, or the batch expands into
ZarrWriter, communication, Tier 2, hardware, or another later scope.
