# P0-R1 scatter-rendering evidence

This directory contains tracked, bounded correctness, dependency, performance,
and rendering-path decision evidence for Phase 0 Step `P0-R1`.

Batch 1 establishes the exact .NET/WPF dependency and verification contract,
the standalone project boundary, bounded aggregate DTOs, deterministic
synthetic fixture generator, and versioned metric schema. The display-transform
fixture, rendering implementation, scheduler, GMI harness, performance
evidence, and final decision remain pending their ordered batches.

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
