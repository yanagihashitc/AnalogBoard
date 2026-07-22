# Refactor checkpoint profile: P0-R1 v1

Profile ID: `P0-R1-v1`

This checkpoint applies only to the bounded Phase 0 real-time visualization
prototype authorized by roadmap v1.42. Preserve deterministic results and the
numeric performance contract while improving clarity, boundedness, and Phase 2
portability.

## Scope guard

- Limit changes to the isolated P0-R1 prototype, its reusable verification
  wrapper, deterministic fixtures, bounded evidence, and current-batch tracking.
- Keep production WPF shell, native acquisition core, C ABI, EP2/EP4/EP6,
  CyAPI, Decoder, ZarrWriter, gate editing/canonical payload, recipes, and UI
  persistence out of scope.
- Do not modify gcsa, sys_app, task_management, driver, registry, firmware,
  `goal.md`, `goal.draft.md`, or fixed `prompt.md`.
- Use .NET SDK `10.0.302`, Desktop Runtime `10.0.10`,
  `net10.0-windows` Release x64, built-in WPF `WriteableBitmap`, and no external
  NuGet packages. Pin `global.json` against roll-forward and prerelease SDKs.
  The approved offline SDK ZIP SHA-512 is
  `7d170ed75fa9af34c00646621d92011dbd71943952e2787cd15df9be78e6452b55dadef34d7eff77b802e6af4959e071a55855ac649afeac70901c3a2a258716`.
  Do not download, install, or silently substitute tooling.
- Refactor only files touched by the current batch.

## Refactor checks

1. Keep the deterministic synthetic aggregate frame, display-only transforms,
   density binning, raster publication, render scheduling, GMI snapshot model,
   instrumentation, and WPF presentation behind explicit boundaries.
2. Preserve raw ADC-linear pulse features and gate-coordinate fixtures. Never
   overwrite them with linear/log/biexp/asinh display-space values.
3. Keep density and timing data bounded: no per-event WPF objects, unbounded
   history, per-frame LINQ pipelines, or UI ownership of producer buffers.
4. Reuse preallocated raster/snapshot buffers where ownership is unambiguous.
   Keep latest-frame coalescing explicit, pending work capped at one, and
   producer callbacks independent of UI progress.
5. Preserve deterministic seeds, event ordering, bin edges, count accounting,
   raster hashes, clock source, warm-up/measurement windows, and metric units.
6. Keep correctness fixtures separate from hard performance fixtures and from
   non-gating headroom observations. Do not optimize away correctness checks or
   convert observational results into acceptance evidence.
7. Keep GMI input fixed at one selected channel and at most 100 waveforms by
   2400 samples. Reject or explicitly trim out-of-contract input according to
   the pinned prototype contract; never accept an unbounded raw stream.
8. Route prototype UI strings through `.resx`, retain a colorblind-safe density
   palette, and do not introduce `MessageBox` or color-only status meaning.
9. Keep instrumentation outside acquisition/product hot paths and avoid
   allocations that invalidate the per-frame and event-count-delta gates.
10. Update boundary-focused tests before behavior changes, preserving explicit
    Given/When/Then intent and actionable typed failures.

## Verification

- Run the current batch's focused correctness and Release x64 performance checks
  through the repository wrapper required by the active goal.
- Recheck deterministic raster/count results and raw/display immutability after
  refactoring transform, binning, buffering, or scheduling code.
- Recheck allocation, pending-work, latency, and fairness measurements after any
  refactor that can affect ownership or timing.
- Run `git diff --check`; inspect tracked/staged paths and sizes; confirm no
  generated frame, raw benchmark trace, dependency cache, binary, secret, real
  measurement, product code, protected prompt/goal, or sibling change is in the
  diff.
- Record retained complexity, metric changes, machine/toolchain identity, and
  residual limits in the active process log.

Stop if refactoring would change an accepted display transform, raw-data
authority, hard fixture, threshold, timing window, reference-PC rule, dependency
pin, or the Phase 0/Phase 2 boundary.
