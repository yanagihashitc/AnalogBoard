# P0-C4 Initial Recording Corpus Index Checklist

Target plan: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html#p0-corpus-steps)
Process log: [P0-C4 corpus index log](../../process_log/2026-07-23-p0-c4-corpus-index-log.md)
Created: 2026-07-23

## Frozen scope and batch sequence

- [x] Batch 1: one typed contract surface plus manifest tooling TDD
- [x] Batch 2: full 3,534-file discovery, readability, size, and streaming SHA-256 sweep
- [x] Batch 3: per-run FL/FH and cfg/telemetry/capture clock/correspondence evidence
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

## Batch 3 test perspectives

| Case ID | Input / Precondition | Perspective | Expected Result | Notes |
|---|---|---|---|---|
| R3-N-01 | One registered run with sequence 1 FL/FH, one cfg/capture/telemetry row | Equivalence — normal/min | Relationship evidence succeeds | Synthetic counts derive from its contract |
| R3-N-02 | Two runs share one declared capture | Equivalence — normal | Both mappings succeed | `low_mid` regression |
| R3-N-03 | Contiguous sequences 1..N in shuffled input | Boundary — max observed/determinism | Same byte-identical evidence | No invented finite N limit |
| R3-A-01 | FL or FH missing / one side extra | Boundary — expected -1/+1 | Typed pair failure | No partial pass |
| R3-A-02 | Interior gap, duplicate logical sequence, sequence 0/-1/leading zero | Boundary — 0/min-1/invalid | Typed sequence failure | Preserve total-count attack is included |
| R3-A-03 | Unknown run or odd authoritative bin count | Equivalence — invalid | Typed run/count failure | Production totals are not duplicated |
| R3-A-04 | cfg missing/extra/duplicate/wrong-run | Boundary — expected ±1 | Typed cfg failure | Global count alone is insufficient |
| R3-A-05 | capture missing from either index, size/SHA mismatch, orphan, duplicate identity | Equivalence — invalid | Typed capture failure | Shared declared capture remains valid |
| R3-N-04 | Two telemetry sessions cover every run exactly once with row counts 2+4 | Equivalence — normal | Ordered bijection succeeds | Evidence contains no row values |
| R3-A-06 | telemetry section NULL/empty, session missing/extra, run zero/twice/orphaned | Boundary — NULL/empty/±1 | Typed mapping failure | No filename-nearest inference |
| R3-A-07 | CSV empty/header-only, header missing/duplicate, ragged, invalid UTF-8/unreadable | Equivalence — dependency/shape | Typed telemetry failure | Error is payload-free |
| R3-A-08 | monotonic value nonnumeric/nonfinite/negative, cycle ID gap/duplicate, boundary order violation | Boundary — 0/-1/invalid | Typed clock-row failure | No raw value in evidence/error |
| R3-N-05 | Run minute bucket equals capture lower/upper containment boundary | Boundary — exact | Accepted | Half-open 60-second bucket |
| R3-A-09 | Capture begins 1µs late or ends 1µs early / inverted interval | Boundary — ±1µs | Typed containment failure | Additional skew tolerance is zero |
| R3-A-10 | Clock basis missing/unknown, quantization NULL/bool/string/0/-1/not 60, skew non-NULL, mtime use | Boundary — NULL/0/-1/type | Typed clock-policy failure | Calibrated skew bound remains unknown |
| R3-A-11 | Source path escapes/is absolute, SHA/schema/version mismatch, unknown field | Equivalence — invalid source | Typed source failure | Repository-relative metadata only |
| R3-N-06 | Pinned USB constraint is boolean false | Equivalence — normal | Failure-trace absence retained | Successful Type C is not causal evidence |
| R3-A-12 | Failure trace missing/true/string false | Equivalence — invalid authority | Typed failure | Never synthesize a failure trace |
| R3-N-07 | Reversed contract/session/source order | Equivalence — determinism | UTF-8 JSON byte-identical, terminal LF | Sorted sets and runs |
| R3-A-13 | Duplicate entry/key, raw telemetry values, payload-like key, absolute host locator | Equivalence — payload boundary | Rejected / absent | Metadata-only invariant |

Clock acceptance records a 60-second run-label quantization width and a
one-second telemetry filename quantization width. The containment rule adds
zero seconds beyond those label buckets; calibrated cross-clock skew remains
`null` because it is not established. No timezone conversion or filesystem
mtime participates in matching.

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
