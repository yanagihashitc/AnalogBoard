# P0-R1 Bounded Visualization Path Checklist

対象プラン: [AnalogBoard rebuild plan](../../plans/260710-analogboard-rebuild-plan.html)
プロセスログ: [Process Log](../../process_log/2026-07-22-p0-r1-visualization-log.md)
作成日: 2026-07-22

---

## Activation and Batch 1: contract/scaffold

- [x] Validate the amended Resume Activation Gate and accepted transform/profile identities
- [x] Fix the Risk-pin-ordered batch split before implementation
- [x] Synchronize known ambient guidance to active P0-R1 and .NET 10
- [x] Pin the exact offline toolchain/dependency contract and generated-root exclusions
- [x] Build the standalone aggregate-contract/WPF/test scaffold and reusable verifier by TDD
- [x] Complete focused verification, pinned refactor/review, final audit, one commit, push, and todo archive

## Batch 2: transform/binning correctness

依存: Batch 1

- [x] Port pinned linear/log/biexp display behavior and owner-pinned asinh behavior
- [x] Prove raw/gate immutability, typed finite failures, pre-transform pair exclusion, bin edges, count conservation, and deterministic raster
- [x] Publish the independent asinh fixture and bounded correctness evidence
- [x] Complete focused verification, pinned refactor/review, final audit, one commit, push, and todo archive

## Batch 3: WPF scatter/latest-frame scheduling

依存: Batch 2

- [x] Implement preallocated `WriteableBitmap` publication without per-event WPF objects
- [x] Implement observable producer-nonblocking latest-frame replacement with pending maximum one
- [x] Instrument generation order, allocation, publication, frame timing, and UI latency
- [x] Complete focused verification, pinned refactor/review, final audit, one commit, push, and todo archive

## Batch 4: bounded GMI/combined harness/decision

依存: Batch 3

- [x] Implement one selected channel bounded at 100 waveforms by 2400 samples
- [x] Prove scatter/GMI/input fairness in the minimal combined harness
- [x] Record candidate comparison, dependency/license, CPU/GPU/fallback, maintenance, and Phase 2 seam
- [x] Record development performance and separate headroom observations without claiming official acceptance
- [x] Complete focused verification, pinned refactor/review, final audit, one commit, push, and todo archive

## Batch 5: official runner/evidence guard correction

依存: Batches 1–4

- [x] Freeze exact official schedule and the non-substituting dry-run boundary
- [x] Add strict CLI, reference/live profile, provenance, raw metric, threshold, soak, suite, and atomic artifact contracts by TDD
- [x] Separate producer, owner STA Dispatcher, GMI, and input probe with render-completion timestamps and bounded raw buffers
- [x] Add fresh-child PowerShell orchestration and raw-to-manifest atomic suite finalization
- [x] Run one complete compatible-PC short dry-run and prove all eight distinct children plus the manifest remain non-official
- [x] Complete pinned refactor/review, final audit, one commit, push, and todo archive

## Batch 6: generated-tree-independent contract fixture copy

依存: Batches 1–5

- [x] Reproduce the Dell preflight failure with generated `bin/obj` content
- [x] Copy source-drift fixtures without traversing generated directories
- [x] Prove normal copy plus missing-source and nested-destination failures
- [x] Complete focused verification and pinned refactor/review checks
- [ ] Complete final audit, one commit, push, and todo archive

## Phase Checkpoint

依存: Batches 1–5

- [x] Pin the owner-approved Dell live observation as canonical `AB-PERF-REF-v1`
- [ ] Rebuild from a clean generated root and rerun full correctness/determinism/dependency checks
- [ ] Run hard scatter and combined scenarios on `AB-PERF-REF-v1` with 30-second warm-up plus three independent 60-second measurements
- [ ] Run the independent 10-minute soak and verify all memory/scheduling metrics
- [ ] Run the 131,072-event/1024-square/three-tile headroom observation separately
- [ ] Audit source revision, machine/toolchain/profile/fixture identity, clocks, raw metrics, summaries, exits, and artifact hashes
- [ ] Verify todo/archive, branch/remote parity, clean tracked worktree, and ignored generated payloads
- [ ] Create the single `perf/phase0-scatter-prototype` to `main` PR and stop before merge

Batches 5 and 6 are preparatory corrections discovered by checkpoint audits.
Batch 5 supplies the mandatory timed evidence transaction. Batch 6 makes its
contract preflight independent of ignored generated trees. Neither relaxes or
replaces any Phase Checkpoint threshold.

## Scope guard

- [x] No production shell, acquisition/C ABI/USB/Decoder/Zarr, gate/recipe, hardware/driver/registry/firmware, or sibling write enters the diff
- [x] No external renderer/test/benchmark NuGet package, network restore, raw payload, secret, cache, binary, or generated trace is tracked
- [x] P0-C4, D17 golden, A-4b, Frozen v1, Phase 0 completion, and Phase 2 remain open
