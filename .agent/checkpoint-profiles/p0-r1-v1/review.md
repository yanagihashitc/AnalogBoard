# Review checkpoint profile: P0-R1 v1

Profile ID: `P0-R1-v1`

Review the current batch against roadmap v1.42's bounded real-time
visualization decision. Findings come first with severity, exact file
references, and acceptance impact. A pass is incomplete until every finding is
fixed and retested or explicitly blocked.

## Scope and dependency checks

- The diff is limited to the isolated P0-R1 prototype, focused verification,
  deterministic fixtures, bounded evidence, and current-batch tracking.
- No production WPF shell, native acquisition, C ABI, EP2/EP4/EP6, CyAPI,
  Decoder, ZarrWriter, gate editor/canonical payload, recipe/persistence,
  driver, registry, firmware, or real measurement path is introduced.
- gcsa, sys_app, task_management, `goal.md`, `goal.draft.md`, and fixed
  `prompt.md` remain unchanged.
- The build is pinned to .NET SDK `10.0.302`, Desktop Runtime `10.0.10`,
  `net10.0-windows` Release x64, built-in WPF `WriteableBitmap`, and zero
  external NuGet packages. `global.json` forbids roll-forward and prerelease
  selection. The offline SDK ZIP SHA-512 is
  `7d170ed75fa9af34c00646621d92011dbd71943952e2787cd15df9be78e6452b55dadef34d7eff77b802e6af4959e071a55855ac649afeac70901c3a2a258716`.
  Network restore and undeclared rendering fallbacks fail loud.

## Correctness and bounded-data checks

- Pulse-feature identity and A/H/W channel order remain intact; raw ADC-linear
  features and gate-coordinate fixtures are byte/value unchanged by display.
- linear/log/biexp/asinh behavior, zero/negative/non-finite policy, bin edges,
  clamp/out-of-range accounting, and forward/inverse tolerances match the
  accepted fixtures rather than a new local formula.
- Density counts are conserved for in-range events; same-seed results, raster
  bytes, metric schema, and ordering are deterministic.
- The UI receives only bounded density images, axis/count metadata, timing
  summaries, and fixed GMI snapshots. It receives no raw event or waveform
  stream and creates no per-event WPF object graph.
- GMI covers 0/1/99/100 waveforms, 2399/2400/2401 samples, selected-channel
  validation, non-finite display values, and generation reversal. The contract
  remains one selected channel and at most 100 by 2400 samples.
- Producer work never waits on the UI. Latest-frame coalescing is observable,
  pending work is capped at one, generation ordering is explicit, and scatter,
  GMI, and UI-input scheduling do not starve one another.
- Prototype UI strings come from `.resx`; the density palette is colorblind
  safe; `MessageBox` and color-only status meaning are absent.

## Hard performance gate

- Final acceptance runs on the versioned Gate B reference PC
  `AB-PERF-REF-v1`. Compatible Windows machines may produce development
  observations, but cannot replace the final reference-PC result.
- Hard scatter fixture: 100,001 events at 512 by 512 bins.
- Hard combined fixture: the hard scatter plus one 512 by 512 GMI tile carrying
  100 waveforms by 2400 samples for one selected channel.
- Each hard scenario uses a 30-second warm-up followed by three independent
  60-second measured runs, and every run must pass all applicable thresholds.
- Scatter update rate is at least 30 fps; frame time p95 is at most 33.3 ms and
  maximum is at most 100 ms.
- GMI update rate is at least 5 Hz and the maximum update gap is at most 500 ms.
- UI-input latency p95 is at most 100 ms and maximum is at most 250 ms.
- Frame publication p99 is at most 1 ms; pending publication work is at most
  one.
- Managed allocation is at most 64 KiB per frame, and the incremental
  allocation caused by increasing event count is at most 8 KiB.
- A 10-minute soak limits retained managed-heap growth to 8 MiB and private-byte
  growth to the greater of 32 MiB or 10 percent of the starting value.
- A 131,072-event, 1024 by 1024, three-tile run is headroom observation only.
  Record it separately and do not use it to waive or redefine the hard gate.
- Owner-equivalent evidence alone never turns a numeric miss into a pass.
  Missing machine identity, raw metric sample, unit, clock, or per-run result is
  a failed/incomplete gate, not a presumed pass.

## Evidence and repository checks

- Evidence records source revision, profile path/blob identities, exact SDK and
  runtime, OS/display/GPU/driver/reference-PC identity, build configuration,
  fixture identity, seeds, bin/tile sizes, clock, warm-up, run durations, every
  raw metric, summary method, exits, and artifact hashes.
- Correctness evidence and bounded summaries are tracked; generated frames,
  verbose traces, build trees, caches, binaries, secrets, and real payloads are
  excluded from Git.
- Selected and rejected rendering paths, dependency/license result, CPU/GPU
  behavior, maintenance boundary, and Phase 2 portability are recorded without
  claiming a production throughput guarantee from the prototype.
- `git diff --check`, focused tests, final staged path/size inspection,
  generated/secret/raw-payload scan, and sibling status comparison pass.
- A-4b, P0-C4, the initial-corpus gate, D17 golden regression, Frozen v1,
  Phase 0, and Phase 2 UI remain open.

Record all findings, fixes, skipped checks, verification, observed headroom,
and residual limits in the active process log. Never relabel an unavailable,
incomplete, or failed check as passed.
