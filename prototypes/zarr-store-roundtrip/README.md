# P0-S Zarr dependency prototype

This directory is an isolated Phase 0 harness. It provides the joint evidence
for P0-S1 and the round-robin decision for P0-S2; both remain `gate_ready` until
the phase PR is merged and centrally synchronized. It is not linked to the
production `AcquisitionEngine`, EP2/EP4/EP6, the existing solution, the C ABI,
or WPF, and it does not declare A-4b, Frozen v1, or Phase 0 complete.

## Offline dependency boundary

Provision the exact owner-approved archive outside every Git repository and set
`ANALOGBOARD_P0S_DEPENDENCY_BUNDLE` to it. The validator checks the archive,
embedded manifest, inventory, source ZIP, and extracted source tree before any
build. It never downloads or installs anything.

```bash
export ANALOGBOARD_P0S_DEPENDENCY_BUNDLE=../approved_dependencies/analogboard-p0-s-preflight/release/analogboard-p0-s-dependencies-20260721.tar.gz
python3 prototypes/zarr-store-roundtrip/scripts/validate_offline_bundle.py
```

The required archive name is
`analogboard-p0-s-dependencies-20260721.tar.gz`; its SHA-256 is
`4fbae209a47b050ee21f8d48091393a093e02ed396c108e90bf1cf59c0af019f`.
Missing or different input fails with that identity. `.deps/p0-s/` is ignored
and must never be committed.

## Adapter boundary

- `blosc_adapter` is the only c-blosc call site. It uses
  `blosc_compress_ctx`/`blosc_decompress_ctx`, internal LZ4, level 5, shuffle,
  blocksize 0, one internal thread, and checked allocation sizes.
- `strict_json` rejects duplicate keys, malformed UTF-8, non-finite tokens,
  missing/type-mismatched/unexpected fields, and trailing input. Serialization
  uses sorted keys and one stable trailing newline.
- `aead_store` uses Windows CNG (`bcrypt`) for AES-256-GCM, binds the accepted
  dataset/array/chunk context as AAD, requires an explicit key provider and
  nonce registry, and never logs key bytes.

The test targets include adapter checks, the byte-exact accepted gcsa KAT, the
full boundary/negative matrix, and the isolated store-publication harness.

## Minimal writer boundary

`minimal_zarr_writer` is a Phase 0 synthetic harness, not a production writer.
It uses one typed contract surface for the three fixed Zarr v2 arrays, pads each
physical chunk to its fixed decoded size, compresses only through the thin
c-blosc adapter, encrypts only through the CNG Store wrapper, and publishes
files through same-directory temp-to-rename. The CLI invokes it on a dedicated
writer thread; no acquisition or UI boundary is involved.

The discriminating fixture has five global events, two append/re-arm-like
cycles `[2,3]`, and two partitions. Round-robin maps events to
`[0,2,4] / [1,3]`; append-sequential maps them to `[0,1] / [2,3,4]`. Every
publication keeps the three arrays aligned, publishes durable chunk data before
the manifest/generation, uses a fresh nonce for coordinate rewrites, and seals
before `status=finalized`. One nonce registry spans every array and partition.
The public gcsa KAT key is test material and is never logged or tracked
separately.

P0-S2 selects round-robin because the accepted gcsa strict validator and
full/slice/gather readers accept it unchanged. Append-sequential is a valid
partition-local control but is rejected by all global Contract RC surfaces.
Performance observations are recorded but are not a selection basis. AAD
prevents cross-coordinate substitution but does not identify an older valid
wire at the same coordinate from one static snapshot; that residual remains
explicit.

## KAT and matrix

Before configuration, verify the read-only accepted vector:

```bash
sha256sum ../gcsa/src/gcsa/store/data/aead_v1_kat.json
```

The required digest is
`cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa`;
CMake rejects any other bytes and copies the verified vector into the build
tree so the test cannot race a later sibling-worktree change. `ctest -V` runs
the same strict KAT and matrix against approved and locally reproduced
Release/Debug libraries. A passing run reports 95 checks, including 42 KAT
checks, 33 boundary cases, and 20 negative cases. The matrix includes all three
full chunk sizes. A zero-byte source may form a Blosc frame, but the zero
decoded return fails loud; a zero-row partition therefore publishes
metadata/manifest state and no chunk file.

## Windows build

All Windows commands must enter the x64 VS 2022 environment through:

```bash
.claude/skills/msvc-build/scripts/build.sh raw -- <command>
```

Use Ninja. The WSL-to-`cmd.exe` raw forwarding does not reliably preserve the
quoted `Visual Studio 17 2022` generator, so it is intentionally not the
reproducible path. Configure Release with `/MD`:

```text
cmake -S D:/ubuntu/jupyter/sys_analyzer/AnalogBoard/prototypes/zarr-store-roundtrip -B D:/ubuntu/jupyter/sys_analyzer/AnalogBoard/.deps/p0-s/prototype/release-approved -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL -DANALOGBOARD_P0S_DEPENDENCY_BUNDLE=D:/ubuntu/jupyter/sys_analyzer/approved_dependencies/analogboard-p0-s-preflight/release/analogboard-p0-s-dependencies-20260721.tar.gz
```

Configure Debug the same way with the `debug-approved` build path,
`-DCMAKE_BUILD_TYPE=Debug`, and
`-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebugDLL`. Then build and test:

```text
cmake --build <build-path> --verbose
ctest --test-dir <build-path> --output-on-failure
```

The CMake configure checks the archive, both c-blosc headers, json header,
accepted gcsa KAT, selected library, configuration-specific CRT, and approved
artifact hash. A local source rebuild may be selected with `P0S_BLOSC_ROOT` and
`P0S_BLOSC_LIBRARY`; set `P0S_EXPECT_APPROVED_BLOSC_HASHES=OFF` only for that
explicitly reproduced artifact because COFF timestamps change its byte hash.
All other identities remain pinned.

## Artifact inspection

Create `dumpbin /headers`, `/directives`, and linked-executable `/dependents`
reports through the required wrapper. Validate them without network access:

```bash
python3 prototypes/zarr-store-roundtrip/scripts/verify_windows_artifacts.py \
  --release-headers <release-headers.txt> \
  --release-directives <release-directives.txt> \
  --debug-headers <debug-headers.txt> \
  --debug-directives <debug-directives.txt> \
  --release-smoke <release-smoke.txt> \
  --debug-smoke <debug-smoke.txt>
```

The verifier requires 12/12 x64 library objects, Release `MSVCRT`, Debug
`MSVCRTD`, and only the pinned Windows/MSVC/UCRT runtime dependencies. Any
c-blosc, LZ4, zlib, Zstd, or unknown third-party DLL fails loud.

## Synthetic store and read-only gcsa validation

Generate only into the ignored evidence tree. The output root must not already
exist:

```text
p0s_store_generator.exe <verified-kat> <ignored-output-root>
p0s_store_generator.exe <verified-kat> <ignored-open-output-root> --open
p0s_store_generator.exe <verified-kat> <ignored-output-root> \
  --sharding round-robin|append-sequential
```

The generator writes no key file, nonce registry, raw measurement, or real
payload. Run the validator with `PYTHONPATH` pointing to the ignored
`git archive` snapshot of accepted gcsa commit `20689a99`; do not use the
sibling worktree:

```bash
PYTHONPATH=<gcsa-snapshot>/src PYTHONDONTWRITEBYTECODE=1 \
python prototypes/zarr-store-roundtrip/scripts/validate_gcsa_roundtrip.py \
  --open-store <ignored-open-store> \
  --finalized-store-a <ignored-finalized-store-a> \
  --finalized-store-b <ignored-finalized-store-b> \
  --expected-evidence \
    docs/reference/zarr-store-contract/phase0-roundtrip/joint-roundtrip-golden.json
```

The script runs the accepted strict validator and encrypted read-only reader,
checks all three arrays to original bits, exact orders/shapes/dtypes/min-max,
partition/global/slice/gather alignment, no feature recomputation, ordered D21
transitions, and source-tree immutability. Its test-owned temporary copies cover
open-product rejection, wrong key, mutated tag/ciphertext, truncation,
partition swap/AAD mismatch, nonce reuse, unknown key ID, plaintext fallback,
schema drift, row misalignment, and manifest overclaim. It never repairs a
failed store. The tracked golden uses strict JSON and verifies its outer
provenance plus every listed source SHA before accepting the runtime summary.

Run the complete pinned entries from WSL:

```bash
scripts/zarr-roundtrip/run-focused-verification.sh joint
scripts/zarr-roundtrip/run-focused-verification.sh sharding
```

The sharding entry generates three alternating observations per mode, verifies
round-robin strict/full/slice/gather success and append-sequential global
fail-loud behavior in the accepted gcsa snapshot, then removes the exact
ignored temporary root. The bounded evidence and decision are under
`docs/reference/zarr-store-contract/phase0-roundtrip/`; no generated store,
measurement payload, encrypted chunk, nonce, or secret is tracked.
`phase0-roundtrip-manifest.json` binds the exact source/evidence inventory and
accepted gcsa identity before the comparison runs. Runtime comparison semantics
must equal `sharding-comparison.json`; only valid wall-time observations may
vary. `central-handoff.md` records the post-merge synchronization input without
participating in that hash graph.
