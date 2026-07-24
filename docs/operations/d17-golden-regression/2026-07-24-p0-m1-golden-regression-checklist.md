# P0-M1 D17 Golden Regression Checklist

Target plan: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html#p0-m1-golden)
Process log: [P0-M1 process log](../../process_log/2026-07-24-p0-m1-golden-regression-log.md)
Created: 2026-07-24

## Frozen scope and batch sequence

- [x] Batch 1: pinned-gcsa channel authority and tracked mapping contract
- [x] Batch 2: bounded golden-input selection and manifest identity pins
- [x] Batch 3: payload-free gcsa reference output and byte determinism
- [x] Batch 4: typed candidate regression harness and synthetic negatives
- [ ] Batch 5: Phase 1 connection contract and closeout evidence
- [ ] Phase checkpoint: all six acceptance conditions proven and one PR created

The non-binding recommended split is retained because it preserves the fixed
mapping-before-reference order and isolates each schema or fixture format
change. Every batch preserves gcsa read-only, asset read-only, payload-free,
fail-closed, and deterministic behavior.

## Acceptance evidence

- [ ] Exactly 13 CH1–CH13 entries are derived from pinned gcsa authority
- [ ] gcsa commit, path, and symbol provenance are tracked
- [ ] Bounded golden inputs are pinned by canonical manifest identity
- [ ] Per-channel reference digests and bounded statistics are payload-free
- [ ] Identical pinned input regenerates byte-identical tracked output
- [ ] Permutation, label, missing, extra, dtype, shape, and value drift fail typed
- [ ] Phase 1 candidate interface and all-channel acceptance rule are documented
- [ ] gcsa/assets remain read-only and no extra hardware run is performed
- [ ] Focused tests, repository safety checks, and phase verification pass
- [ ] Process log and checklist are archived after the phase PR is created
