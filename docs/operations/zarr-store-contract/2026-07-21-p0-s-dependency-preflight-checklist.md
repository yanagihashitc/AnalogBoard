# P0-S dependency preflight checklist

対象プラン: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process log](../../process_log/2026-07-21-p0-s-dependency-preflight-log.md)
作成日: 2026-07-21

This checklist prepares dependencies and checkpoints only. It does not accept
P0-S1/P0-S2, decide sharding, declare A-4b/Frozen v1, or complete Phase 0.

## Batch 1: Bootstrap and supply surface

- [x] Preserve the pinned Draft 4.1 plan bootstrap
- [x] Record accepted dependency/contract identities and licenses
- [x] Add ignored offline-cache/generated-artifact boundaries
- [x] Replace stale checkpoint documents with Zarr scope
- [x] Verify deterministic manifest, hashes, links, and repository scope
- [x] Complete checkpoint review, commit, push, and archive the active batch

## Batch 2: Dependency adapters and Windows verification

- [x] Validate and extract the exact offline bundle without download
- [x] Audit the c-blosc source ZIP and reproduce upstream/static builds
- [x] Implement thin c-blosc, strict JSON, and Windows CNG adapters by TDD
- [x] Verify x64 objects, CRT directives, runtime DLLs, hashes, and behavior
- [ ] Complete checkpoint review, commit, push, and archive the active batch

## Batch 3: KAT and boundary/negative matrix

- [ ] Prove byte-exact Blosc/AES wire in Release and Debug
- [ ] Cover the required size/typesize/data-pattern/full-chunk matrix
- [ ] Cover all codec and AEAD negative cases with stable typed errors
- [ ] Complete checkpoint review, commit, push, and archive the active batch

## Batch 4: Minimal encrypted Zarr and gcsa roundtrip

- [ ] Generate the ignored deterministic three-array Zarr v2 store
- [ ] Exercise one append/re-arm-like partition without deciding sharding
- [ ] Re-run accepted gcsa focused tests from a git-archive snapshot
- [ ] Validate strict positive/negative reads and original-bit roundtrip
- [ ] Complete checkpoint review, commit, push, and archive the active batch

## Batch 5: Evidence closeout and preparatory PR

- [ ] Complete deterministic manifest/evidence with exact commands and results
- [ ] Run clean full verification, refactor, review, and Claude review
- [ ] Scan diff/staging for secrets, generated artifacts, and sibling changes
- [ ] Commit/push final preparation evidence and archive the active batch
- [ ] Open one non-merge preparatory PR to `main`

## Scope-wide gate

- [ ] `.agent/refactor.md` and `.agent/review.md` match Zarr preparation scope
- [ ] No sibling repository, production hot path, WPF, real data, or current goal changed
- [ ] No archive/header/library/executable/store/secret/raw payload is tracked
- [ ] A-4b, Frozen v1, Phase 0, P0-S1, and P0-S2 remain open
