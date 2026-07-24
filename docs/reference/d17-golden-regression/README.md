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

## Boundaries

- `../gcsa` is a read-only authority and reader dependency.
- `artifacts/` is read-only and may be accessed only by later, pinned decode
  and integrity-verification steps.
- Tracked contracts and fixtures are payload-free: no decoded waveform array,
  asset payload byte, or absolute host locator is permitted.
- Additional hardware runs, product integration, A-4b, Frozen v1, and Phase 0
  completion are outside P0-M1.
