# P0-C4 Initial Recording Corpus Index Checklist

Target plan: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html#p0-corpus-steps)
Process log: [P0-C4 corpus index log](../../process_log/2026-07-23-p0-c4-corpus-index-log.md)
Created: 2026-07-23

## Frozen scope and batch sequence

- [x] Batch 1: one typed contract surface plus manifest tooling TDD
- [x] Batch 2: full 3,534-file discovery, readability, size, and streaming SHA-256 sweep
- [x] Batch 3: per-run FL/FH and cfg/telemetry/capture clock/correspondence evidence
- [x] Batch 4: availability/restore, locator/owner/retention, at-rest, and recovery procedure
- [x] Batch 5: tracked index closeout and phase-level deterministic verification
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

## Batch 4 test perspectives

| Case ID | Input / Precondition | Perspective | Expected Result | Notes |
|---|---|---|---|---|
| C4-N-01 | Current custody policy: availability verified; owner/retention decision required; restore not performed | Equivalence — normal | Typed policy validates | Missing authority stays open |
| C4-N-02 | Same semantic policy keys arrive in different order | Equivalence — determinism | Canonical UTF-8 JSON is byte-identical | Sorted keys, one terminal LF |
| C4-N-03 | Procedure has all required verdict/restore/reacquisition/stop sections and only the read-only verifier command | Equivalence — normal | Procedure pin and semantic lint pass | No transfer or reacquisition command |
| C4-A-01 | Policy is NULL/empty, schema is wrong/future, or version is bool/string/0/2 | Boundary — NULL/empty/type/min-1/+1 | Typed schema failure | No implicit default |
| C4-A-02 | Required field missing, unknown field present, duplicate JSON key, or empty identifier/reason | Equivalence — invalid | Typed closed-schema failure | No partial policy |
| C4-A-03 | Source path is absolute/backslash/escaping/alternate, a component or leaf is a symlink, or SHA drifts | Equivalence — confinement/identity | Typed source failure before target read | Metadata only |
| C4-A-04 | Contract/manifest schema/version drifts or policy locator differs from either source | Equivalence — authority mismatch | Typed source/locator failure | Counts remain solely in contract |
| C4-A-05 | Policy adds expected counts, entry count, total bytes, payload, raw rows, packet body, or host locator | Equivalence — duplication/payload | Unknown/prohibited metadata rejection | No authority duplication |
| C4-A-06 | Owner is named/resolved while authority is absent, or decision-required has non-NULL identity | Equivalence — unsupported claim | Typed owner failure | Repository/decision owner is not asset owner |
| C4-A-07 | Retention policy/duration/disposition is supplied, or decision-required lacks its open item | Boundary — unsupported/non-NULL | Typed retention/open-item failure | No inferred expiry/deletion |
| C4-A-08 | Availability is not verified, omits/reorders a required check, or infers restore | Boundary — missing/order/contradiction | Typed availability failure | Live mismatch is a stop condition |
| C4-A-09 | Restore is verified/failed, source is identified, verification is non-NULL, or availability is reused | Equivalence — unsupported claim | Typed restore failure | Current verdict is not performed |
| C4-A-10 | Reacquisition is in scope, replaces the current corpus, or requires the same SHA | Equivalence — contradiction | Typed reacquisition failure | Always a separately authorized new version |
| C4-A-11 | D19 is marked applied, Git exclusion is protection, export is allowed, or relocation/reprotection is in scope | Equivalence — at-rest contradiction | Typed at-rest failure | Preserve 2026-07-20 boundary |
| C4-A-12 | Open items are missing/extra/duplicate/unsorted/resolved or do not match owner/retention/restore state | Boundary — set/order/state | Typed open-item failure | Exact three-item set |
| C4-A-13 | Procedure is missing, hash-mismatched, missing a required section/token, or has fenced copy/move/upload/network/reacquisition command | Equivalence — procedure failure | Typed procedure failure | This scope performs no material operation |
| C4-A-14 | Policy path is not the exact tracked path or output contains absolute locator/secret-like content | Equivalence — scope/security | Typed path/content failure | No alternate authority |

The custody schema intentionally accepts only the currently authorized state.
Resolving owner, retention, or restore source requires a separately reviewed
schema/policy update; the validator does not accept a speculative future state.
Availability validation pins the contract/manifest and its read-only verification
method but does not duplicate counts or total bytes.

## Batch 5 test perspectives

| Case ID | Input / Precondition | Perspective | Expected Result | Notes |
|---|---|---|---|---|
| C5-N-01 | Exact closeout plus all 13 pinned metadata/tool sources | Equivalence — normal | Typed closeout validates | No asset read in metadata seam |
| C5-N-02 | Valid sources with live phase operations injected once each | Equivalence — normal | Manifest, relationships, and custody all compose | Verify-only; no tracked write |
| C5-N-03 | Same semantic closeout keys arrive in different order | Equivalence — determinism | Canonical UTF-8 JSON is byte-identical | Sorted keys, one terminal LF |
| C5-N-04 | Frozen P0-C1–C3 README/manifest/scenarios plus P0-C4 capture binding | Equivalence — historical overlap | Prior index remains distinct and linked | Historical `P0-C4 planned` text is not current status |
| C5-N-05 | Two live compositions use unchanged generated metadata | Equivalence — repeatability | Both byte comparisons pass | Asset payload is never cached |
| C5-B-01 | Schema version 1 vs NULL/bool/string/0/2 | Boundary — exact/min-1/+1/type/NULL | Only integer 1 passes | No implicit default |
| C5-B-02 | Six ordered acceptance mappings vs five/seven/duplicate/reordered | Boundary — exact count ±1/order | Exact six-condition map required | Condition prose stays in plan |
| C5-B-03 | Thirteen source roles vs twelve/fourteen/duplicate | Boundary — exact count ±1 | Exact role set required | No closeout self-hash cycle |
| C5-B-04 | Three ordered open-item references vs two/four/duplicate/reordered | Boundary — exact count ±1/order | Exact custody open set required | Values are not resolved here |
| C5-A-01 | NULL/empty/non-object, missing/unknown field, or duplicate JSON key | Equivalence — invalid | Typed closed-schema failure | No partial closeout |
| C5-A-02 | Alternate/absolute/backslash/escaping index or source path, symlink, non-regular, unreadable | Equivalence — confinement | Typed path/source failure | Descriptor no-follow reads |
| C5-A-03 | Source SHA/schema/version/revision/tool identity drifts | Equivalence — identity | Stop before live asset verification | Untrusted content is not printed |
| C5-A-04 | Authority DAG has unknown/missing/extra/cyclic/reordered edge | Equivalence — authority graph | Typed DAG failure | Closeout is a sink, not a new value authority |
| C5-A-05 | Closeout adds count/bytes/entry/run/pair/locator/payload/raw/host metadata | Equivalence — duplication/security | Prohibited metadata failure | Contract remains sole expected-count authority |
| C5-A-06 | Status says completed/merged, gate closed, handoff published, or next transition authorized | Equivalence — manual gate | Typed status failure | Human merge remains required |
| C5-A-07 | Acceptance condition is missing/extra/duplicated/reordered, non-verified, or references the wrong role | Equivalence — acceptance map | Typed acceptance failure | No warning or partial pass |
| C5-A-08 | Custody open IDs differ, are resolved, or owner/retention/restore authority is invented | Equivalence — cross-authority | Typed open-item failure | Exact three unresolved items |
| C5-A-09 | USB source/schema/hash drifts, capture binding differs, or failure-trace is not boolean false | Equivalence — P0-C1–C3 seam | Typed source/relationship failure | Success baseline is not failure-path proof |
| C5-A-10 | Regenerated manifest differs by path/size/SHA/kind/order/newline | Equivalence — integrity/determinism | Byte-compare failure | One streaming full sweep |
| C5-A-11 | Regenerated relationships differ by pair/mapping/clock/failure/order/newline | Equivalence — relationship/determinism | Byte-compare failure | Live telemetry only |
| C5-A-12 | Manifest/relationship/custody dependency raises a typed failure | Equivalence — dependency | Bounded closeout failure, no success text | No partial gate |
| C5-A-13 | Closeout/source leaf or component is swapped during check-to-read | Equivalence — TOCTOU | No-follow failure or stable opened inode | Never read the replacement target |
| C5-A-14 | Duplicate key/path/error contains a 4 KiB secret-like value | Boundary — large untrusted input | Bounded stderr without echo/traceback | Existing error code retained |
| C5-A-15 | CLI requests build/output/metadata-only success, assets are unavailable, or a source is missing | Equivalence — operation boundary | Exit 2; fixed bounded error | Live `verify` is the sole success path |

No finite maximum asset count or file size is introduced here; those remain in
the existing contract and streaming seams. The closed closeout lists have exact
boundaries of six acceptance mappings, thirteen source roles, and three open-item
references.

## Phase acceptance evidence

- [x] At-rest boundary distinguishes Git exclusion from D19 protection
- [x] Canonical locator, owner, and retention are explicit
- [x] Every manifest entry is present, readable, size-matched, and SHA-256-matched
- [x] Exact expected counts: bin 3,520; cfg 6; telemetry 2; capture 6
- [x] Restore and reacquisition procedures are distinct
- [x] Per-run FL/FH, cfg/telemetry/capture, clock basis, and tolerance are verified
- [x] Failure-trace absence remains explicit
- [x] Same input generates byte-identical tracked metadata
- [x] Focused tests and repository safety checks pass
- [ ] Process log and checklist are archived after completion
