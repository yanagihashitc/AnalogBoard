# P0-S1/P0-S2 Product Execution Checklist

対象プラン: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](../../process_log/2026-07-22-p0-s-roundtrip-log.md)
作成日: 2026-07-22

---

## Batch 1: Contract identity and atomic failure cuts

依存: Activation Gate 9/9 Pass

- [x] Reconfirm accepted Contract RC, KAT, dependency, and toolchain identities
- [x] Add atomic publication boundary/failure tests first
- [x] Add deterministic post-flush/pre-rename observability
- [x] Complete focused verification, refactor, review, final diff, commit, push, and todo archive

## Batch 2: Strict five-event writer

依存: Batch 1

- [ ] Generalize the synthetic fixture to five events, two re-arm cycles, and two candidate sharding modes
- [ ] Implement multi-row/multi-chunk planning, committed-prefix min/max, 14-bit waveform values, nonce-safe replacement, and correct sealing
- [ ] Preserve the strict three-array Zarr v2/Blosc/AEAD/D21 contract
- [ ] Complete focused verification, refactor, review, final diff, commit, push, and todo archive

## Batch 3: Pinned gcsa joint roundtrip and failure matrix

依存: Batch 2

- [ ] Validate ordered D21 snapshots and representative power-failure dispositions with accepted gcsa
- [ ] Prove full/slice/gather value, bit, dtype, shape, order, no-recompute, and raw-zarr rejection
- [ ] Cover required crypto/schema/visibility negatives and canonical two-run identity
- [ ] Publish bounded payload-free golden evidence
- [ ] Complete focused verification, refactor, review, final diff, commit, push, and todo archive

## Batch 4: Sharding decision and closeout

依存: Batch 3

- [ ] Compare round-robin and append-sequential on one five-event/two-cycle fixture and identical observations
- [ ] Adopt exactly one mode only if the accepted gcsa validator/reader passes without modification
- [ ] Publish the decision record, comparison evidence, central handoff, and bounded parent-plan status/evidence update
- [ ] Complete focused verification, refactor, review, final diff, commit, push, and todo archive

## Phase checkpoint

依存: Batches 1-4

- [ ] Rebuild from clean generated roots and rerun the complete C++/Python/gcsa matrix
- [ ] Audit every Product acceptance and Completion Criteria item against current evidence
- [ ] Confirm Active todo empty, branch/remote parity, clean tracked worktree, and ignored generated payloads
- [ ] Create the phase PR to `main` and stop before merge
