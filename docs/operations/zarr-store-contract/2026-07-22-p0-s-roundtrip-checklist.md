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

- [x] Generalize the synthetic fixture to five events, two re-arm cycles, and two candidate sharding modes
- [x] Implement multi-row/multi-chunk planning, committed-prefix min/max, 14-bit waveform values, nonce-safe replacement, and correct sealing
- [x] Preserve the strict three-array Zarr v2/Blosc/AEAD/D21 contract
- [x] Complete focused verification, refactor, review, final diff, commit, push, and todo archive

## Batch 3: Pinned gcsa joint roundtrip and failure matrix

依存: Batch 2

- [x] Validate ordered D21 metadata states and finalized-store metadata-lag cut dispositions with accepted gcsa
- [x] Prove full/slice/gather value, bit, dtype, shape, order, no-recompute, and raw-zarr rejection
- [x] Cover required crypto/schema/visibility negatives and canonical two-run identity
- [x] Publish bounded payload-free golden evidence
- [x] Complete focused verification, refactor, review, final diff, commit, push, and todo archive

## Batch 4: Sharding decision and closeout

依存: Batch 3

- [x] Compare round-robin and append-sequential on one five-event/two-cycle fixture and identical observations
- [x] Adopt exactly one mode only if the accepted gcsa validator/reader passes without modification
- [x] Publish the decision record, comparison evidence, central handoff, and bounded parent-plan status/evidence update
- [x] Complete focused verification, refactor, review, final diff, commit, push, and todo archive (`8fdcd747e0d6bf760fd6e674f620f8c97b356235`; terminal publication and archive evidence recorded in the process log)

## Phase checkpoint

依存: Batches 1-4

- [x] Rebuild from clean generated roots and rerun the complete C++/Python/gcsa matrix
- [x] Audit every Product acceptance and Completion Criteria item against current evidence
- [ ] Confirm Active todo empty, branch/remote parity, clean tracked worktree, and ignored generated payloads
- [ ] Create the phase PR to `main` and stop before merge
