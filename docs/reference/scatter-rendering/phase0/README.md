# P0-R1 scatter-rendering evidence

This directory contains tracked, bounded correctness, dependency, performance,
and rendering-path decision evidence for Phase 0 Step `P0-R1`.

Batch 1 establishes the exact .NET/WPF dependency and verification contract,
the standalone project boundary, bounded aggregate DTOs, deterministic
synthetic fixture generator, and versioned metric schema. Batch 2 adds the
accepted display-transform and density-binning correctness seam. Batch 3 adds
the fixed BGRA density raster, thin built-in `WriteableBitmap` publication,
pending-one latest-frame scheduling, and bounded development instrumentation.
GMI, combined fairness, official performance evidence, and the final renderer
decision remain pending their ordered checkpoints.

## Batch 2 correctness evidence

- [`display-transform-contract-v1.json`](display-transform-contract-v1.json)
  is the independent CPython 3.12.2 standard-library fixture for
  `AB-DISPLAY-TRANSFORM-v1`; SHA-256
  `5955f5b3ec649c490cd1bc752b1515fb33f5b7170212f0e221ca7552acb4adaf`.
- linear/log/biexponential behavior is pinned to the sys_app authorities listed
  in Draft 4.5; asinh uses cofactor `150.0` and the owner-amended formula.
- Every transform has a typed finite boundary. Density excludes a pair before
  transformation when either raw axis is non-finite and records that count.
- Reusable binner scratch and caller-owned grids retain raw ADC-linear values;
  counts use Y-major row-major order with explicit exclude/clamp accounting.
- Correctness covers 100,001 events at 512 by 512 bins. This is deterministic
  correctness evidence on the development PC, not official performance
  acceptance.

## Batch 3 raster and scheduling evidence

- [`density-raster-contract-v1.json`](density-raster-contract-v1.json) fixes
  opaque BGRA32 output, a colorblind-safe five-anchor viridis-family palette,
  integer interpolation, and the one required Y-axis inversion. The contract
  file SHA-256 is
  `c6015c919cc79e7c746f4b6c0d8b42672e4165cfc0253663bbfe89e04121cc33`.
- The WPF surface retains one owner-STA `WriteableBitmap` and copies from a
  caller-owned reusable BGRA buffer. It creates no per-event WPF objects.
- The Core scheduler uses one latest-frame slot, one queued/in-flight callback,
  explicit coalesce/release accounting, strict generation order, and bounded
  raw-tick/allocation rings. Producer calls do not await or lock the UI.
- [`batch3-development-observation.json`](batch3-development-observation.json)
  records the single 100,001-event, 512-square compatible-PC smoke emitted by
  the exact wrapper. Its scheduler submit p99 is explicitly identified as a
  single-slot test-double measurement; real Dispatcher timing remains open. It
  is non-official and does not substitute for the `AB-PERF-REF-v1` Phase
  Checkpoint.

## Boundaries

- Toolchain: .NET SDK `10.0.302`, Desktop Runtime `10.0.10`,
  `net10.0-windows`, Release x64.
- Renderer candidate: built-in WPF `WriteableBitmap` with a preallocated
  raster buffer.
- External rendering, test, and benchmark NuGet packages: zero.
- Restore uses [`NuGet.Config`](../../../../prototypes/scatter-rendering/NuGet.Config)
  with all package, fallback, and audit sources cleared.
- Generated frames, verbose benchmark traces, binaries, and raw metrics remain
  under ignored `artifacts/phase0-scatter-rendering/`.
- Tracked evidence must contain bounded synthetic summaries, identities,
  hashes, decisions, and residual limits only.

## Verification

Run the reusable Windows wrapper from the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/scatter-rendering/verify.ps1 -Mode Focused -Configuration Release -Architecture x64
```

The complete standalone solution is restored with the repository
`NuGet.Config`, built with `--no-restore`, and exercised by its self-hosted test
runner with `--no-build --no-restore`. A missing or partial scaffold fails loud.

Dependency details are recorded in
[`analogboard-p0-r1-dependencies.json`](../../../dependencies/analogboard-p0-r1-dependencies.json).
