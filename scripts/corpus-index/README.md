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
