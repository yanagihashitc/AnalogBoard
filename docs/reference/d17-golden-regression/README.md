# D17 channel-mapping golden regression

This directory freezes the P0-M1 regression boundary defined by
[Draft 4.9](../../plans/260710-analogboard-rebuild-plan.html#p0-m1-golden).
It does not change or reinterpret D17 and does not implement the future
product Decoder or Writer.

## Mapping authority

The tracked [channel mapping](channel-mapping-v1.json) is generated
mechanically from the exact gcsa authority blob recorded in its provenance.
The extractor accepts only the limited syntax needed for the channel maps and
their ordered name tuples; it does not import, execute, or evaluate the gcsa
module. It requires exactly 13 entries and the frozen D17 label set.

From the AnalogBoard repository root:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 \
  scripts/d17-golden-regression/mapping_contract.py generate \
  --gcsa-repo ../gcsa \
  --commit 20689a991697217518ec2ff15aaaa2533b169eb0 \
  --source-path src/gcsa/constants.py \
  --output docs/reference/d17-golden-regression/channel-mapping-v1.json
```

The command reads only the exact `commit:path` blob and has no current-HEAD
fallback. Regeneration from the same blob must be byte-identical. Any missing
authority, invalid index, non-13 result, label disagreement, or order mismatch
is a typed failure; no local mapping value is inferred.

## Bounded golden inputs

The tracked [golden input selection](golden-inputs-v1.json) is generated only
from the canonical P0-C4 manifest and corpus contract:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 \
  scripts/d17-golden-regression/golden_selection.py generate
```

The selector validates both tracked schemas and every manifest entry identity,
then applies one fixed rule in density order `low, mid, high`:

1. choose the lexicographically first mapped run for the density;
2. choose the lowest positive ordinal for which both FL and FH entries exist;
3. pin both entries by path, SHA-256, and byte size.

The result is three complete pairs (six files, 18,720,000 bytes):
`260717_1529_{fl,fh}_1.bin`, `260717_1532_{fl,fh}_1.bin`, and
`260717_1542_{fl,fh}_1.bin`. The exact identities remain in the JSON rather
than being repeated by hand here. Each entry `path` is the exact
canonical-manifest identity and is resolved only relative to the fixture's
pinned repository-relative `asset_locator`; per-entry paths are not rewritten
or hand-entered.

The other mapped run for each density, every higher pair ordinal, the remaining
3,514 bin entries, and all cfg, telemetry, and capture entries are excluded.
They add volume but are not needed to prove the frozen 13-channel ordering.
The selector reads metadata only; it does not open any asset. A selected run
without a complete pair is a typed failure and is not replaced with another
run.

## Pinned golden reference

The tracked [golden reference](golden-reference-v1.json) is produced by
`scripts/d17-golden-regression/golden_reference.py`. Generation is permitted
only with all of these identities:

- gcsa commit `20689a991697217518ec2ff15aaaa2533b169eb0`;
- `BinaryReader(version='v1')` source SHA-256
  `620ab899b0fb75f75da0a1c8b5722a2f02212726910aea5115401506f8eb4254`;
- parser v1 source SHA-256
  `5035b9147ec42c2381cc2fd45a1f83a9f251edece7b21c4dd099f2da315a2964`;
- container image
  `sha256:e65e9f8b0ffafef5b5d2b9711c9a3411649ae80fd036cc79f0febb80b4c0b06e`;
- the exact mapping and input-selection identities recorded under `sources`.

The gcsa commit is exported to an immutable archive and mounted read-only into
an ephemeral container. The repository, gcsa archive, and corpus root are
mounted read-only; only this directory is overlaid for the single fenced
output. Before decoding, each of the six selected inputs is opened without
following symlinks and checked against its pinned path, byte size, and SHA-256.
The same open descriptor is checked and re-hashed after decoding so a
decode-time identity drift is a typed failure. No unselected corpus entry is
decoded.

The reader yields one FL array and one FH array per pair. The mapping contract
then projects their 8 + 5 source columns into the 13 authoritative channel
records. A record contains only mapping fields, canonical dtype `<u2`, shape,
SHA-256 of canonical C-order bytes, and bounded integer statistics
(`element_count`, `min`, `max`, `sum`, and `nonzero_count`). Decoded arrays
remain process-local and are never serialized.

The live result contains three pairs and 39 records, with every channel shaped
`[100, 2400]`. Two complete regenerations were byte-identical at SHA-256
`3f531bd624ad3ea8b763b7ec82da42f313fbd4976945c6cd1f636fab9636f53f`.
Reader environment versions are stored in the fixture as non-host-specific
provenance. The generator also pins this output identity and refuses to
overwrite a drifted or hard-linked fixture. Any source/input/reader identity
change, unsafe path, decode failure, dtype/shape drift, or payload/locator leak
is a typed hard failure.

## Candidate regression harness

`scripts/d17-golden-regression/regression_harness.py` compares a future
Decoder/Writer adapter summary with the exact tracked golden reference. The
candidate is a product-neutral `analogboard.d17.candidate-summary` v1 object
with these exact root fields:

- `reference`: this golden fixture's repository-relative path, SHA-256, and
  byte size;
- `pair_count`, `channel_count_per_pair`, and ordered `pairs`;
- for each pair, `density`, `run_id`, `ordinal`, `event_count`, the ordered FL
  and FH manifest input identities, and 13 ordered channel records;
- for each channel, physical channel, label, stream/source index, canonical
  dtype, shape, digest, and the same bounded statistics as the golden.

The candidate does not contain `reader`, `sources`, gcsa environment
provenance, decoded arrays, or waveform bytes. The stable command accepts one
candidate document on standard input, reads the fixed tracked golden itself,
and emits bounded Pass evidence on standard output:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 \
  scripts/d17-golden-regression/regression_harness.py compare \
  < candidate-summary.json
```

The strict Python document seam used underneath is:

```python
result = compare_candidate_bytes(candidate_source, golden_reference_bytes)
evidence = serialize_regression_result(result)
```

Candidate JSON is capped at 64 KiB, rejects duplicate keys and non-finite
tokens, and is bounded by depth, node count, and string length before schema
comparison. The CLI opens the repository's logical golden path without
following links and verifies its fixed content SHA-256 and size. The Python
seam likewise accepts only that fixed content identity, so neither path can
substitute or self-repin a reference. Pass requires all three pair/input
identities and all 39 channel records to match. Only a sealed result returned
by that comparison can be serialized. The bounded deterministic evidence
contains `status=pass`, fixed reference identity, canonical candidate
digest/size, and the compared counts.

Failures are typed and ordered: schema/payload and fixed identities first;
then channel cardinality/multiplicity, physical-channel permutation, label,
stream/source index, dtype, shape, digest, and statistics. Synthetic tests
permanently exercise swaps, label drift, missing and excess channels,
duplicate-plus-missing membership, dtype/shape drift, digest/statistics drift,
identity substitution, and payload/locator attempts. They do not read corpus
assets or invoke gcsa.

The harness proves that a submitted candidate summary matches this golden. It
does not by itself prove how the candidate digest was calculated; the Phase 1
adapter connection and canonical-byte rule are documented in the Phase 1
connection contract rather than attributed to gcsa provenance.

## Phase 1 connection

The tracked [Phase 1 connection contract](phase1-connection-v1.md) fixes the
future product-adapter handoff without implementing it. The adapter verifies
the same three manifest-pinned pairs, uses the tracked mapping rows to select
all 13 channels, rejects representation drift, and hashes canonical
little-endian `<u2>` C-order bytes. It then supplies the strict candidate
summary to the stdin command above. Exit 0 and a Pass result are accepted only
when all 39 channel mapping, dtype, shape, digest, and statistics records
match.

The submitted candidate digest is summary identity rather than producer
attestation. Product-owned adapter tests and Tier 1/2 integration are therefore
explicit Phase 1 work and are not claimed by P0-M1.

## Bounded closeout

The tracked [closeout evidence](closeout-v1.json) is built and verified from
the exact plan, P0-C4 corpus contract/manifest/closeout, D17 contracts and
fixtures, harness/tests, Phase 1 contract, and checkpoint profiles:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 \
  scripts/d17-golden-regression/closeout.py generate
PYTHONDONTWRITEBYTECODE=1 python3 \
  scripts/d17-golden-regression/closeout.py verify
```

The generator reads only repository-local metadata and source files; it does
not open corpus assets or gcsa. Every source has a fixed path, SHA-256, and
byte size, and the two profiles also have their goal-pinned Git blob identity.
It semantically joins the three selected pairs back to the P0-C4 manifest
identity, requires 13 mapping rows and 39 `<u2>` reference records, records all
seven mandatory mutation cases, and binds the six frozen P0-M1 acceptance
conditions to bounded evidence roles. An existing drifted, linked, or unsafe
output is not overwritten.

The closeout remains `gate_ready`: it does not declare completion. Human merge
and central live verification are still required.

## Boundaries

- `../gcsa` is a read-only authority and reader dependency.
- `artifacts/` is read-only; P0-M1 access was limited to the six pinned Batch 3
  decode and integrity-verification inputs.
- Selected assets inherit the P0-C4 pre-D19 plaintext, local-only, no-export
  custody boundary.
- Tracked contracts and fixtures are payload-free: no decoded waveform array,
  asset payload byte, or absolute host locator is permitted.
- Additional hardware runs, product integration, A-4b, Frozen v1, and Phase 0
  completion are outside P0-M1.
