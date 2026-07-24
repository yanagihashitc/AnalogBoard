# Refactor checkpoint profile: P0-M1 v1

Profile ID: `P0-M1-v1`

This checkpoint applies only to the bounded D17 channel-mapping golden
regression work authorized by a central goal that pins this profile's path and
Git blob identity. This profile does not independently authorize P0-M1
execution. Preserve the frozen D17 decision, mapping authority, source asset
bytes, deterministic references, and fail-closed behavior while improving
clarity and maintainability.

## Scope guard

- Limit changes to the tracked mapping contract, golden fixture, regression
  harness, focused tests, procedures, bounded evidence logs, and current-batch
  tracking, under documentation/reference paths such as
  `docs/reference/d17-golden-regression/`.
- Treat the entire `artifacts/` tree as a read-only invariant. Reading is
  allowed only when the active goal authorizes it and only for decode or
  integrity verification. Creating, changing, moving, renaming, deleting, or
  touching anything below `artifacts/` is forbidden.
- Treat the current gcsa implementation only as the read-only reader and
  channel-order authority. Do not modify or commit in `../gcsa`, alter its
  environment, or substitute an inferred authority. Record the referenced gcsa
  commit, path, and symbol as provenance.
- Do not modify gcsa, sys_app, task_management, product Decoder or Writer code,
  acquisition/product code, driver, registry, firmware, `goal.md`,
  `goal.draft.md`, fixed `prompt.md`, checkpoint routers, or existing
  versioned profiles.
- Refactor only files touched by the current batch. Do not turn this checkpoint
  into an independent P0-M1 execution or expand it into product Decoder/Writer
  implementation, Tier 1/2 integration, A-4b, Frozen v1, or Phase 0 closure.

## Mapping and payload boundary

- Derive the CH1-CH13 mapping from the actual channel-order code or
  configuration in the pinned gcsa revision. Never construct the mapping by
  guesswork, hand entry, fixture observation, or test-driven reordering.
- Require exactly 13 entries and the frozen D17 label set
  (`FSC,SSC,FL1..FL6` and `fsGMI..bfGMI`). A non-13 result or any label-set
  disagreement is a typed, fail-closed `decision_required` condition.
- Do not change or reinterpret D17 and do not run or request an additional
  hardware channel-identification acquisition.
- Pin every bounded golden input by its P0-C4 canonical-manifest identity:
  normalized repository-relative path, SHA-256, and byte size. Keep selection
  criteria and exclusions explicit in the procedure.
- Keep tracked fixtures, evidence, PR output, and CI output payload-free. Never
  emit decoded waveform arrays, waveform samples, asset payload bytes, payload
  copies, or absolute host locators. A tracked fixture may contain only
  per-channel digests, bounded statistical summaries, and canonical-manifest
  identity references.

## Refactor checks

1. Keep gcsa authority discovery, mapping-contract construction, manifest
   identity resolution, decode adaptation, bounded digest/statistics
   generation, comparison, and typed error reporting behind explicit
   boundaries.
2. Preserve gcsa provenance as reviewable data: commit, authority path and
   symbol, reader version, invocation, and relevant environment identity.
   Never silently fall back to another reader or channel ordering.
3. Keep mapping disagreement, label disagreement, missing or extra channels,
   dtype drift, shape drift, and value/digest mismatch as stable typed
   failures. Do not downgrade any required regression class to a warning,
   partial pass, or best-effort result.
4. Preserve deterministic channel ordering, normalized paths, numeric
   representation, bounded-statistic fields, key ordering, and serialization.
   Regenerating the contract and fixture from the same pinned authority and
   input must produce byte-identical tracked output.
5. Keep payload processing streaming and bounded. Decoded arrays may be used
   only transiently for authorized decode and integrity checks and must not
   escape into tracked output, logs, PRs, CI artifacts, or unbounded command
   output.
6. Preserve focused synthetic negative tests for channel permutation, label
   mismatch, missing channel, extra channel, dtype drift, shape drift, and
   digest mismatch. Each mutation must demonstrate the corresponding
   fail-closed typed error.
7. Keep the Phase 1 connection surface documentation-only in this scope:
   describe candidate input format, invocation, and the all-channel
   mapping/digest acceptance rule without implementing product Decoder/Writer
   integration.
8. Update focused tests before any behavior change, retain explicit
   Given/When/Then intent, and keep all focused verification green.

## Verification

- Run the current batch's focused contract, fixture, harness, determinism, and
  synthetic-negative tests.
- Confirm authority derivation produces exactly 13 entries with the frozen D17
  label set and recorded gcsa commit/path/symbol provenance.
- Regenerate the mapping contract and payload-free golden fixture twice from
  the same pinned authority and manifest identities and require byte identity.
- Run `git diff --check`; inspect tracked and staged paths, modes, sizes, and
  symlinks. Confirm no `artifacts/` mutation, decoded array, asset payload,
  absolute host locator, product code, protected goal/prompt/router/profile, or
  sibling-repository change is present.
- Record the applied profile path, expected and actual Git blob identities,
  gcsa provenance, pinned manifest identities, verification commands and
  results, retained complexity, and residual limits in the active process log.

Stop if refactoring would infer or hand-enter the mapping, produce other than
exactly 13 authoritative entries, reinterpret D17, mutate or export an asset,
emit payload, weaken a typed fail-closed condition, lose byte determinism,
change gcsa or its environment, require another hardware run, or broaden the
authorized P0-M1 scope.
