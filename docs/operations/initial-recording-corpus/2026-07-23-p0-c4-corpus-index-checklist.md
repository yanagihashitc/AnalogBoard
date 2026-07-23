# P0-C4 Initial Recording Corpus Index Checklist

Target plan: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html#p0-corpus-steps)
Process log: [P0-C4 corpus index log](../../process_log/2026-07-23-p0-c4-corpus-index-log.md)
Created: 2026-07-23

## Frozen scope and batch sequence

- [ ] Batch 1: one typed contract surface plus manifest tooling TDD
- [ ] Batch 2: full 3,534-file discovery, readability, size, and streaming SHA-256 sweep
- [ ] Batch 3: per-run FL/FH and cfg/telemetry/capture clock/correspondence evidence
- [ ] Batch 4: availability/restore, locator/owner/retention, at-rest, and recovery procedure
- [ ] Batch 5: tracked index closeout and phase-level deterministic verification
- [ ] Phase checkpoint: acceptance conditions 1–6 proven and one PR created

The non-binding recommended split is retained because it already isolates the
schema/manifest-format risk pin from the live sweep. Every batch preserves
asset read-only, payload-free output, the single expected-count contract, and
separate availability/restore verdicts.

## Batch 1 test perspectives

| Case ID | Input / Precondition | Perspective | Expected Result | Notes |
|---|---|---|---|---|
| C1-N-01 | Valid small contract and normalized repo-relative paths | Equivalence — normal | Deterministic manifest | Synthetic files only |
| C1-A-01 | Contract is `NULL` | Boundary — NULL | Typed contract failure with exact message | No implicit default |
| C1-A-02 | Contract is an empty object | Boundary — empty | Missing-schema failure | Empty string is covered by path cases |
| C1-A-03 | Wrong/future schema or invalid schema version type | Equivalence — invalid | Typed schema failure | Reject bool as integer |
| C1-B-01 | Expected count is 1 | Boundary — min | Accepted | Production values come only from the tracked contract |
| C1-B-02 | Expected count is 0 | Boundary — min - 1 / zero | Typed count failure | Zero is not a valid required kind |
| C1-B-03 | Expected count is -1 or bool | Boundary — negative / invalid type | Typed count failure | No meaningful finite max in the specification |
| C1-A-04 | Locator is empty, absolute, contains `..`, or uses a symlink | Boundary — empty / confinement | Typed path failure | Backslash and non-normalized forms are rejected |
| C1-N-02 | Every kind has its exact expected count | Equivalence — normal | Discovery succeeds | Deterministic order |
| C1-A-05 | One required file is missing | Boundary — expected - 1 | Typed count failure | Fail closed |
| C1-A-06 | One extra or unexpected file exists | Boundary — expected + 1 | Typed unexpected/count failure | Fail closed |
| C1-N-03 | `analysis/` is a regular excluded directory | Equivalence — allowed exclusion | Its files are ignored | Existing P0-C1–C3 output |
| C1-A-07 | Excluded path is a symlink | Equivalence — unsafe type | Typed symlink failure | Do not follow it |
| C1-B-04 | SHA chunk size is 1 byte | Boundary — min | Correct digest | Streaming seam |
| C1-B-05 | SHA chunk size is 0 or -1 | Boundary — below min | Typed chunk-size failure | Exact type/message assertion |
| C1-B-06 | Source file is empty | Boundary — zero bytes | Standard empty SHA-256 | Total-byte contract catches live drift |
| C1-A-08 | Source open/read fails | Equivalence — dependency failure | Typed unreadable failure | Mocked; root privileges do not mask it |
| C1-A-09 | Recorded size or SHA-256 differs | Equivalence — integrity failure | Typed mismatch failure | Recompute; do not trust recorded digest |
| C1-N-04 | Input enumeration order differs | Equivalence — determinism | Byte-identical JSON | UTF-8, sorted keys, terminal LF |
| C1-A-10 | Manifest path is missing, duplicated, or escapes root | Equivalence — invalid manifest | Typed manifest failure | No partial pass |

Finite maximum count and maximum file size are not specified, so no invented
upper bound is tested. Large-file boundedness is verified through the streaming
read seam rather than by allocating a large fixture.

## Phase acceptance evidence

- [ ] At-rest boundary distinguishes Git exclusion from D19 protection
- [ ] Canonical locator, owner, and retention are explicit
- [ ] Every manifest entry is present, readable, size-matched, and SHA-256-matched
- [ ] Exact expected counts: bin 3,520; cfg 6; telemetry 2; capture 6
- [ ] Restore and reacquisition procedures are distinct
- [ ] Per-run FL/FH, cfg/telemetry/capture, clock basis, and tolerance are verified
- [ ] Failure-trace absence remains explicit
- [ ] Same input generates byte-identical tracked metadata
- [ ] Focused tests and repository safety checks pass
- [ ] Process log and checklist are archived after completion
