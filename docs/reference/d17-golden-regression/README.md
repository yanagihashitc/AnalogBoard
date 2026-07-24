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

## Boundaries

- `../gcsa` is a read-only authority and reader dependency.
- `artifacts/` is read-only and may be accessed only by later, pinned decode
  and integrity-verification steps.
- Selected assets inherit the P0-C4 pre-D19 plaintext, local-only, no-export
  custody boundary.
- Tracked contracts and fixtures are payload-free: no decoded waveform array,
  asset payload byte, or absolute host locator is permitted.
- Additional hardware runs, product integration, A-4b, Frozen v1, and Phase 0
  completion are outside P0-M1.
