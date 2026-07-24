# Refactor checkpoint profile: P1-1 v1

Profile ID: `P1-1-v1`

This checkpoint applies only to the P1-1 Tier 1 foundation, Decoder, and D17
harness connection authorized by a central goal that pins this profile's path
and Git blob identity. This profile does not independently authorize P1-1
execution. Preserve deterministic model and Decoder behavior, the frozen D17
mapping authority, the existing Phase 1 harness interface, and every
fail-closed boundary while improving clarity and maintainability.

## Scope guard

- Limit production changes to the in-process Tier 1 core model, the new Decoder
  that computes A/H/W and accumulated min/max, the product-owned D17 adapter,
  the Tier 1 suite connection, and their focused tests and bounded evidence.
- Do not operate real hardware or change driver, registry, firmware, USB
  endpoint, acquisition communication, or machine configuration state.
- Treat gcsa as read-only. Do not modify or commit in `../gcsa`, change its
  environment, or derive a replacement channel authority from its current
  implementation.
- Treat all P0 outputs and the entire `artifacts/` tree as read-only. Golden
  inputs may be consumed only through the identities pinned by the P0-M1
  fixture and connection contract; do not scan, substitute, rewrite, copy,
  rename, delete, or export corpus assets.
- Keep Git, PR, CI, checkpoint, and process evidence payload-free. Never include
  raw waveform bytes, decoded arrays, waveform samples, bulk derived values,
  asset copies, absolute host locators, or secrets.
- ZarrWriter, legacy exporter, acquisition communication, AcquisitionEngine,
  Recorder, production Logger, Tier 2, fault injection, power-fail behavior,
  C ABI expansion, WPF UI, and real-hardware validation are outside P1-1.
- Do not modify D1-D23, the D17 mapping contract, P0-M1 fixtures or harness,
  the Phase 1 connection contract, existing versioned profiles, checkpoint
  routers, `goal.md`, fixed `prompt.md`, sibling repositories, or central
  task-management files.

## Determinism

- For identical model inputs, require byte-identical Tier 1 model output and
  byte-identical Decoder candidate serialization and digest output across
  repeated runs.
- Keep clock time, local time zone, random values, process identity, temporary
  paths, runtime hash ordering, unordered collection traversal, task completion
  order, and thread scheduling out of model and Decoder results.
- Normalize every externally visible order explicitly: input pairs, physical
  channels, features, statistics, JSON keys, and evidence records. Parallel
  work may not define output order.
- Keep numeric representation explicit. Do not permit locale-sensitive
  formatting, implicit narrowing, non-finite coercion, platform-dependent
  endianness, or serialization defaults to alter bytes or digests.
- Add a fail-closed review check for nondeterministic APIs and ordering. A
  detected clock, random, unordered, or scheduling dependency is a checkpoint
  failure, not a warning or an accepted flaky rerun.

## D17 authority and harness seam

- The sole CH1-CH13 authority is
  `docs/reference/d17-golden-regression/channel-mapping-v1.json`, whose
  required SHA-256 is
  `8e197eade3fff0f7427c7cf0e9d77409624b803a51a782c6a429e705f15fc99b`.
- Consume the tracked mapping contract as data. Do not add a second mapping
  table, duplicate its labels or indexes in production code, infer mapping
  from fixture values, inspect waveforms to guess mapping, or silently default
  a missing entry.
- Require exactly the contract's 13 ordered channels. Missing, excess,
  duplicated, relabeled, reordered, malformed, or identity-drifted mapping is
  a stable typed hard failure.
- Fulfil, but do not change, the interface in
  `docs/reference/d17-golden-regression/phase1-connection-v1.md`. Candidate
  construction, invocation, input bounds, all-channel comparison, and pass
  rules remain exactly as documented.
- Use only the P0-M1 pinned selection and reference identities named by that
  interface. Do not replace a missing asset, accept an unpinned input, weaken
  an identity check, or make the harness compare a partial channel set.
- A P1-1 pass requires all mapped channels to match mapping and digest through
  the existing harness. Do not bypass the CLI seam, copy expected digests into
  product output, or implement a second comparator.

## Refactor and test checks

1. Preserve explicit boundaries between Tier 1 model state, decode,
   feature/min-max calculation, canonical candidate construction, and harness
   invocation.
2. Keep the model in-process, hardware-independent, bounded, and fully
   deterministic. Do not introduce sleeps, polling of wall time, external
   services, mutable global state, or environment-dependent inputs.
3. Keep raw bytes and decoded arrays process-local and bounded. No raw byte
   sequence crosses a C ABI or enters logs, evidence, exception messages, or
   tracked fixtures.
4. Preserve exact A/H/W and min/max semantics from the authorized legacy
   behavior and plan. Refactoring must not reorder channels or features,
   change numeric width, round intermediate values, or invent missing data.
5. Write or update a failing focused test before each behavior change. Include
   explicit Given/When/Then intent and assert stable error type, code, and
   bounded message for rejected inputs.
6. Retain positive tests for the accepted mapping and harness path plus
   negative tests for mapping identity, count, label/order, dtype, shape,
   value/digest, malformed candidate, and nondeterministic-output drift.
7. Re-run identical model and Decoder inputs at least twice and compare
   canonical bytes and digests, not only parsed values.
8. Refactor only current-batch files. Do not broaden P1-1 to prepare later
   phases or opportunistically clean unrelated legacy, P0, documentation, or
   sibling-repository code.

## Verification

- Run the focused Tier 1 model, Decoder, D17 adapter, determinism, and harness
  suites selected by the central goal.
- Verify the tracked mapping SHA-256 before using it and verify that no second
  mapping definition exists in the P1-1 diff.
- Run the existing P0-M1 regression suite unchanged and require the Phase 1
  candidate to pass all-channel mapping and digest comparison.
- Repeat the same Tier 1/Decoder generation and require byte-identical output
  and identical digests.
- Run `git diff --check`; inspect tracked and staged paths, modes, sizes, and
  symlinks. Confirm no `artifacts/`, P0 authority, gcsa, sibling, protected
  goal/prompt/router/profile, ZarrWriter, communication, Tier 2, driver,
  registry, firmware, or UI change is present.
- Scan changed production and test code for clock, random, unordered, locale,
  process, path, and scheduling dependencies and fail closed on any
  output-affecting use.
- Record the applied profile path, expected and actual Git blob identities,
  mapping and connection-contract identities, verification commands, results,
  deterministic output hashes, retained complexity, and residual limits in
  the active process log.

Stop if refactoring would change the D17 authority or Phase 1 interface, add an
independent or inferred mapping, consume an unpinned P0 input, mutate or export
an asset, emit payload, introduce output nondeterminism, weaken a typed failure,
touch real hardware or system state, change gcsa, or expand into ZarrWriter,
communication, Tier 2, or another later scope.
