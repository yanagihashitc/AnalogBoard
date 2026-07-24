# P0-M1 D17 Golden Regression Checklist

Target plan: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html#p0-m1-golden)
Process log: [P0-M1 process log](../../process_log/2026-07-24-p0-m1-golden-regression-log.md)
Created: 2026-07-24

## Frozen scope and batch sequence

- [x] Batch 1: pinned-gcsa channel authority and tracked mapping contract
- [x] Batch 2: bounded golden-input selection and manifest identity pins
- [x] Batch 3: payload-free gcsa reference output and byte determinism
- [x] Batch 4: typed candidate regression harness and synthetic negatives
- [x] Batch 5: Phase 1 connection contract and closeout evidence
- [x] Phase checkpoint: all six acceptance conditions proven
- [ ] Phase PR: one `analysis/phase0-d17-golden` to `main` PR created

The non-binding recommended split is retained because it preserves the fixed
mapping-before-reference order and isolates each schema or fixture format
change. Every batch preserves gcsa read-only, asset read-only, payload-free,
fail-closed, and deterministic behavior.

## Acceptance evidence

- [x] Exactly 13 CH1–CH13 entries are derived from pinned gcsa authority
- [x] gcsa commit, path, and symbol provenance are tracked
- [x] Bounded golden inputs are pinned by canonical manifest identity
- [x] Per-channel reference digests and bounded statistics are payload-free
- [x] Identical pinned input regenerates byte-identical tracked output
- [x] Permutation, label, missing, extra, dtype, shape, and value drift fail typed
- [x] Phase 1 candidate interface and all-channel acceptance rule are documented
- [x] gcsa/assets remain read-only and no extra hardware run is performed
- [x] Focused tests, repository safety checks, and phase verification pass
- [ ] Process log and checklist are archived after the phase PR is created
