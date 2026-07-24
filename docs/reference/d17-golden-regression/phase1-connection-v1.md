# D17 golden regression — Phase 1 connection contract v1

This contract connects a future product Decoder/Writer adapter to the P0-M1
regression harness. It freezes an interface; it does not implement the adapter,
authorize product integration, reinterpret D17, or claim Phase 1 acceptance.

## Inputs and authority

The adapter consumes exactly the three ordered FL/FH pairs pinned by
`golden-inputs-v1.json`. It must independently verify each input's canonical
manifest path, SHA-256, and byte size before decode. The sole channel-order
authority is `channel-mapping-v1.json`: CH1–CH13 select the stated FL or FH
stream and zero-based source index. The adapter must not infer mapping from
fixture values, labels, or observed waveforms.

The future product Decoder supplies one decoded channel for every mapping row.
Before summary construction, the adapter must require:

- exactly three ordered input pairs and 13 ordered channels per pair;
- an unsigned 16-bit decoded representation capable of lossless conversion to
  canonical little-endian `<u2`;
- rank 2 and the exact `[event_count, 2400]` shape;
- no missing, excess, duplicated, or substituted channel.

Any failed input identity, decode, mapping, cardinality, dtype, or shape check
is a typed hard failure. The adapter must not coerce a wider or signed type,
fill a missing channel, discard an excess channel, or continue with a warning.

## Candidate construction

For each mapped channel, the adapter converts the already-validated unsigned
16-bit values to canonical little-endian `<u2`, lays them out in C row-major
order, and computes SHA-256 over exactly those canonical bytes. It also
computes the bounded integer statistics used by `golden-reference-v1.json`:
`element_count`, `min`, `max`, `sum`, and `nonzero_count`. The canonical bytes
and decoded arrays remain process-local.

The adapter then emits one strict UTF-8 JSON document with schema
`analogboard.d17.candidate-summary`, version 1, as documented in `README.md`.
It binds the exact golden-reference identity, all three pair/input identities,
and all 39 ordered channel summaries. It must not copy gcsa reader provenance,
decoded waveform arrays, asset bytes, secrets, absolute host locators, or
environment-specific paths into the document. The serialized candidate must
fit the harness's 64 KiB input bound.

## Invocation and pass rule

From the AnalogBoard repository root, send the candidate document on standard
input:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 \
  scripts/d17-golden-regression/regression_harness.py compare \
  < candidate-summary.json
```

Pass requires exit status 0 and bounded `analogboard.d17.regression-result`
evidence with `status=pass`. Every pair/input identity and every one of the 39
channels must match the golden mapping, stream/source index, dtype, shape,
digest, and bounded statistics. Any typed failure or nonzero exit fails the
Phase 1 check; warnings and partial passes are not accepted.

The candidate digest in Pass evidence identifies the submitted summary. It is
not attestation that the product adapter followed this construction contract.
Phase 1 must therefore add product-owned adapter tests and Tier 1/2 integration
that exercise the real Decoder/Writer seam. That implementation and CI wiring
remain outside P0-M1.

## Retained boundaries

The adapter integration must preserve the P0-C4 custody boundary: corpus
assets remain local-only and read-only, and no payload enters Git, PR text, CI
artifacts, or logs. gcsa remains a read-only historical authority for P0-M1;
the Phase 1 product must not mutate it through this seam. No additional
hardware run is required or authorized. D17 remains unchanged.
