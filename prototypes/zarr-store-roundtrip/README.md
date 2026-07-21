# P0-S Zarr dependency prototype

This directory is an isolated Phase 0 harness. It prepares dependency and wire
building blocks only. It is not linked to the production `AcquisitionEngine`,
EP2/EP4/EP6, the existing solution, the C ABI, or WPF. It does not accept
P0-S1/P0-S2, decide partition sharding, or declare Frozen v1.

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

The test target includes adapter checks plus the byte-exact accepted gcsa KAT
and the full boundary/negative matrix. Generated-store/gcsa roundtrip remains
Batch 4 scope.

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
Release/Debug libraries. A passing run reports 88 checks, including 42 KAT
checks, 33 boundary cases, and 13 negative cases. The matrix includes all three
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
