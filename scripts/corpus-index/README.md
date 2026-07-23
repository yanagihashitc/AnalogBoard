# P0-C4 corpus index tooling

This standard-library-only tool builds and verifies the deterministic,
payload-free manifest for the initial recording corpus. The single production
contract is
`docs/reference/initial-recording-corpus/2026-07-17/contract.json`; expected
counts, total bytes, the normalized repository-relative locator, the explicit
`analysis/` exclusion, exact filename grammars, six run/capture mappings, and
the explicit idle capture must not be duplicated as another authority. The
fixed capture set is derived from mapped captures plus `idle_captures` and must
equal the contract-authoritative capture count.

Asset files must remain directly below the canonical locator; only the
root-level `analysis/` directory is excluded. The tool discovers regular source
files without following symlinks, rejects suffix-only replacement names and
unmapped run IDs, and verifies that a source snapshot remains stable through
each read. Missing, extra, moved, unexpected, unreadable, non-normalized,
duplicate, size-mismatched, and SHA-256-mismatched input fails with a stable
typed error.
SHA-256 uses bounded streaming reads and verification always recomputes the
digest from the source. Manifest JSON contains metadata only, uses sorted keys
and paths, UTF-8, and one terminal line feed. It never serializes the absolute
host locator or payload bytes.

## Focused tests

Tests use temporary synthetic fixtures only. They do not read the live corpus.

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s scripts/corpus-index/tests -p 'test_*.py' -v
```

The cases cover the checklist C1 normal, equivalence, and boundary partitions.
Finite maximum count and file size are unspecified; boundedness is exercised
through the streaming chunk seam instead of a large fixture.

## Relationship evidence

The separate
`docs/reference/initial-recording-corpus/2026-07-17/relationship-contract.json`
pins the primary contract, frozen manifest, P0-C1–C3 USB index, and rebuild
plan by normalized repository-relative path and SHA-256. It also declares the
exact 10-column telemetry header, the explicit ordered run membership of each
telemetry session, and the clock policy:

- run labels are `GetLocalTime` `YYMMDD_HHMM` half-open 60-second buckets;
- capture bounds are Capinfos timestamps whose timezone remains undeclared;
- telemetry filenames are one-second `GetLocalTime` labels created at EP6
  thread start;
- telemetry rows are session-local `GetTickCount64` monotonic milliseconds;
- capture containment adds zero tolerance, calibrated cross-clock skew remains
  unknown (`null`), and filesystem mtime is forbidden.

`corpus_relationships.py` reads bin/cfg/capture identities only from the
tracked metadata after validating the complete primary manifest schema. The
CLI accepts only the tracked relationship-contract and relationship-evidence
paths, and rejects symlinks in those metadata paths before reading them. It
reads telemetry CSV solely for the exact header, row
width/count, continuous cycle IDs, and monotonic boundary/duration semantics.
On the supported POSIX/WSL execution path, contract, evidence, pinned metadata,
and telemetry reads traverse already-open directory descriptors and open every
component and leaf with no-follow semantics. Platforms without descriptor-
relative no-follow support fail closed as typed unreadable input.
It does not open bin or pcapng payloads. Normal evidence contains only
global/run/session aggregates and one canonical FL/FH pair-identity digest per
run; it contains no sequence arrays, telemetry row values, raw payload,
absolute host paths, mtimes, packets, or samples. Unknown contract/evidence
fields and duplicate JSON keys fail closed.

The relationship cases are synthetic temporary fixtures and do not read the
live corpus:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s scripts/corpus-index/tests -p 'test_corpus_relationships.py' -v
```

The cases cover checklist R3-N-01–07 and R3-A-01–13, including zero/minus-one
and exact/one-microsecond clock boundaries. No finite sequence maximum is
invented. The intended target is complete branch coverage of relationship
validation; OS-specific atomic publication failures remain covered by the
existing manifest publication seam.

## Build

Run only on the asset-retaining machine from the repository root. Output is
restricted to the tracked manifest path shown below; the contract and unrelated
repository files are not valid overwrite targets. Publication uses a completed,
fsynced same-directory temporary file followed by atomic replacement, and
rejects symlink parents or destinations. The command reads source bytes in
bounded chunks but neither copies nor modifies them.

```bash
PYTHONDONTWRITEBYTECODE=1 python3 scripts/corpus-index/corpus_index.py \
  build \
  --output docs/reference/initial-recording-corpus/2026-07-17/manifest.json
```

## Verify

Verification rediscovers the exact source set, checks all four counts and the
aggregate byte contract, then reopens every regular file and recomputes its
size and SHA-256. A recorded digest is never trusted on its own.

```bash
PYTHONDONTWRITEBYTECODE=1 python3 scripts/corpus-index/corpus_index.py \
  verify \
  --manifest docs/reference/initial-recording-corpus/2026-07-17/manifest.json
```

`build` and `verify` are live asset sweeps. Do not run either in CI, on a
machine without the local-only corpus, or during a contract-only test batch.

Relationship build writes only the exact tracked `relationships.json` path,
using a completed and fsynced same-directory temporary file followed by atomic
replacement. Relationship verify regenerates evidence and byte-compares it.
Capture timestamp containment accepts only fixed-width
`YYYY-MM-DD HH:MM:SS.ffffff` source metadata.
Both commands read the local telemetry CSV files and therefore are also live
asset operations:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 scripts/corpus-index/corpus_relationships.py \
  build \
  --output docs/reference/initial-recording-corpus/2026-07-17/relationships.json

PYTHONDONTWRITEBYTECODE=1 python3 scripts/corpus-index/corpus_relationships.py \
  verify \
  --evidence docs/reference/initial-recording-corpus/2026-07-17/relationships.json
```

Do not run these relationship commands in CI or a synthetic-only TDD batch.

## Custody policy

`docs/reference/initial-recording-corpus/2026-07-17/custody.json` is the
verify-only, static current-state custody index. It is not generated from the
asset tree and deliberately does not duplicate expected counts, manifest entry
count, or aggregate bytes. The exact five source roles pin the Draft 4.7 plan,
corpus contract, corpus manifest, manifest verifier, and
`restore-and-reacquisition.md` by normalized repository-relative path and
SHA-256. Contract and manifest schema/version plus their shared canonical
locator are validated through the public metadata-only corpus-index validators.

The schema accepts only the authorized present state:

- assets are pre-D19 plaintext, local-only on the asset-retaining machine;
  export is prohibited, and Git exclusion is not at-rest protection;
- availability is verified for the manifest's exact source set, regular-file
  readability, size, and SHA-256, without inferring restore;
- restore is not performed because a distinct restore source is not identified;
- reacquisition is out of scope, would create a new corpus version, and is
  neither restore nor a replacement for the current corpus;
- asset owner, retention, and restore-source identity remain three exact,
  typed open items.

The validator rejects alternate policy/source paths, symlink traversal,
duplicate JSON keys, non-canonical bytes, source identity drift, speculative
resolved states, authority duplication, and payload/host metadata. It reads
only tracked metadata and the procedure; it does not read the local corpus
assets. The procedure permits one read-only canonical availability verifier
command and contains no restore transfer or reacquisition command.

Run the synthetic custody cases:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s scripts/corpus-index/tests -p 'test_corpus_custody.py' -v
```

The cases cover checklist C4-N-01–03 and C4-A-01–14. They include NULL/empty,
zero/one/future schema boundaries, source confinement and symlinks, duplicate
keys, source/hash/schema/locator drift, every current-state contradiction,
open-item set/order/linkage, procedure lint, and bounded CLI output. No finite
maximum policy size is invented because the policy has a closed, fixed schema.
The repository has no pinned Python coverage runner, so branch coverage is not
reported by this command.

Verify the tracked static custody index without reading assets:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 scripts/corpus-index/corpus_custody.py \
  verify \
  --policy docs/reference/initial-recording-corpus/2026-07-17/custody.json
```

## Phase closeout

`docs/reference/initial-recording-corpus/2026-07-17/closeout.json` is the
closed-schema composition of the accepted plan, corpus metadata, relationship
evidence, custody policy, recovery procedure, historical P0-C1–C3 index, and
the three accepted verifier identities. It contains source pins and authority
bindings only; inventory totals, per-run values, source-location values, and
acceptance prose stay in their existing authorities.

`corpus_closeout.py` is verify-only. It first validates the exact closeout and
source snapshot with descriptor-relative no-follow reads capped at one MiB per
metadata source. It then performs one in-memory manifest regeneration and byte
comparison, one relationship regeneration and byte comparison, and custody
verification. A final read verifies that the closeout and every pinned source
remained byte-identical throughout the operation. Errors are typed, bounded,
and do not reflect untrusted source content.

Run the 24 payload-free synthetic cases:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s scripts/corpus-index/tests -p 'test_corpus_closeout.py' -v
```

Run the live phase verifier only on the asset-retaining machine. This command
performs the full streaming corpus read and must not run in CI or on a
development machine:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 scripts/corpus-index/corpus_closeout.py \
  verify \
  --index docs/reference/initial-recording-corpus/2026-07-17/closeout.json
```

The successful closeout status is gate-ready and pending human merge. It does
not declare scope completion, gate closure, central handoff publication, or a
next-scope transition. The pre-merge workflow creates the planned PR and stops.
