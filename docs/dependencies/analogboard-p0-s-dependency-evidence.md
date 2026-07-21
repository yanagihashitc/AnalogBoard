# AnalogBoard P0-S dependency preparation evidence

Status: technical validation complete; preparatory PR open and human merge
pending. This document records repository-local dependency and checkpoint
preparation only. It does not close P0-S1/P0-S2, choose P0-S2 sharding,
declare A-4b/Frozen v1, or complete Phase 0.

## Accepted identities

- AnalogBoard base: `f4ea8ffb80e704a1064c268a13701be198766d07`
- Draft 4.1 plan: `e52a081e50b5a67463905b8b59e2b0c4f0ac53e4b46ead97f2bfeed84396282a`
- gcsa joint-validation commit: `20689a991697217518ec2ff15aaaa2533b169eb0`
- Contract RC: `gcsa-store-a4a-rc1` (not Frozen v1)
- Accepted PR #33 amendment: merge `b89556c51faa6e4b0c6a36a98eb066726df4ca16`, canonical v2 and gate golden v2
- Packaged AEAD KAT: `cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa`
- Gate golden v2: `44be68e012df5aea9e365081a33b5db12790e7fefbf380c2e36512df257b8cd0`

The accepted gcsa commit is `origin/main`. PR #33 is its ancestor. A path-level
comparison found no later store wire, AEAD, D21, or gate material delta. The
gate golden retains the historical filename `gate_boundary_v1.json`, but its
schema/version/vector content is canonical v2 and its hash matches the accepted
identity. This is the known accepted amendment, not a post-RC surprise.

## Offline supply boundary

Expected archive: `analogboard-p0-s-dependencies-20260721.tar.gz`

- Archive SHA-256: `4fbae209a47b050ee21f8d48091393a093e02ed396c108e90bf1cf59c0af019f`
- Embedded `MANIFEST.sha256`: `71886c544cc15b101eca4ae67e149a9c9d284b76096df4011af747e7336282d0`
- Local extraction root: ignored `.deps/p0-s/`
- Input override: `ANALOGBOARD_P0S_DEPENDENCY_BUNDLE`
- CI/other hosts: provision from an approved artifact cache or manual seed; no download path exists

Pre-extraction inspection found 28 outer archive entries, zero symlinks, and
zero absolute/traversal paths. The Batch 2 validator then checked the exact
archive before extraction, required the 15-file embedded manifest inventory,
ran `sha256sum -c MANIFEST.sha256`, and extracted only into ignored
`.deps/p0-s/`. It also checked the source ZIP and its extracted tree: zero
traversal entries, zero links, `.gitmodules` absent, 490 files/60 directories,
no extra files, and no ELF/PE/shared/archive payload. Exit was 0; deterministic
ignored evidence is at
`artifacts/phase0-zarr-roundtrip/dependency/bundle-validation.json`.

## Source and license audit pins

c-blosc is v1.21.6 at commit
`616f4b7343a8479f7e71dd3d7025bd92c9a6bbd0`, acquired from the official
commit ZIP at `2026-07-21 14:12:33 JST`. The ZIP hash is
`31b005197faa7ffd63983fadca9d36a3259371eb98f17ad21073987d0a112afc`;
the extracted-tree manifest hash is
`700070935e041ced3695b5314bb44a56770ee4bdab021890033a6d3723f6675e`.
The approved source audit records `.gitmodules` absent, zero traversal entries,
zero symlinks, 490 files/60 directories, and no ELF/PE/shared/archive payload.

Tracked notices retain c-blosc BSD-3-Clause, internal LZ4 BSD-2-Clause, and
nlohmann/json MIT text byte-for-byte. nlohmann/json v3.11.3's official header
and GhostCytometer commit `41a7e3b52bf195e88f1864e590cec0e3613ee2ea`
blob are pinned byte-identical. Retaining the MIT notice permits AnalogBoard to
use, copy, modify, and distribute the single header; it does not relicense
AnalogBoard itself. Windows CNG `bcrypt` is a Windows SDK/system dependency; no
third-party crypto library or license is added.

## Verified local environment

- MSVC x64: `19.37.32826.1`
- Windows SDK: `10.0.26100.0`
- CMake: `3.31.6-msvc6`
- Ninja: `1.12.1`
- gcsa container: `d141d00e5edb0bd17ee37836340a4315343019d32db4f9197322e9a3a5c9e1d8`
- image: `sha256:e65e9f8b0ffafef5b5d2b9711c9a3411649ae80fd036cc79f0febb80b4c0b06e`
- Python/package identity and pip-freeze hash match the owner pin exactly

All Windows inventory/build commands use
`.claude/skills/msvc-build/scripts/build.sh raw -- ...`, which enters
`scripts/run_with_vsdevcmd.bat`. The reproducible path is Ninja because the raw
bridge does not reliably preserve the quoted Visual Studio generator.

## c-blosc source builds

Three out-of-source builds used the verified commit ZIP without source edits:

1. Upstream regression: Release `/MD`, static and shared enabled, tests enabled,
   internal LZ4/zlib/Zstd. Configure reported c-blosc `1.21.6` and
   `Using LZ4 internal sources.` Configure/build/test exited 0 in
   2.79/5.54/39.47 seconds; `ctest --output-on-failure` passed 1626/1626.
2. Accepted Release: static-only, LZ4-only, tests/fuzzers/benchmarks off,
   external LZ4 off, `/MD`. Configure/build exited 0 in 2.68/2.63 seconds.
3. Accepted Debug: the same static-only profile with `/MDd`.
   Configure/build exited 0 in 2.49/1.64 seconds.

The static-only profiles use every option recorded in the deterministic JSON
manifest. c-blosc's Windows CMake test graph refers to `blosc_shared` when
static-only plus tests is selected, so tests remain in the separate upstream
regression build. No source patch or shared test artifact is shipped.

Approved bundle library identities remain:

- Release: `0660daf135ccd08a3060905a35ba139f332c8e398a6ddc82b308a0deae4d70ee`
- Debug: `cbeb1b05517d82adf36d787c1b0790d6d503f27749b4ff7e48edcbc3108045e6`

The fresh local COFF outputs have expected timestamp-dependent hashes:

- Release: `4db0e7317f4173da35d0ff9f2f0b0ee5a4fbe2d1e878b15e0b1a2630907d86ff`
- Debug: `523ccc44eb4ece9e7d05b4961a45d562a398aad6a8a9b46cd7fca6f5a872e191`

Both installed headers are byte-identical to their approved hashes. The local
libraries were linked into the same adapter smoke executable: Release and
Debug each passed 29 checks, including c-blosc `1.21.6`, compressor list,
internal LZ4 `1.9.4`, header/link/runtime compression/decompression, strict
JSON, and CNG authentication behavior. The approved libraries passed the same
29 checks.

`dumpbin /headers` found 12/12 library objects as `8664 machine (x64)` in both
configurations. `/directives` found Release `MSVCRT` and Debug `MSVCRTD`.
Linked approved and locally reproduced smoke executables import only
`bcrypt.dll`, `KERNEL32.dll`, and the configuration-appropriate MSVC/UCRT
system DLLs. No c-blosc, LZ4, zlib, Zstd, or unknown third-party DLL appears.
The fail-closed parser exited 0 and published ignored evidence at
`artifacts/phase0-zarr-roundtrip/dependency/windows-artifact-verification.json`.

## Isolated adapters

`prototypes/zarr-store-roundtrip/` contains the only c-blosc call site, a
strict-JSON boundary, and a Windows CNG AES-256-GCM Store-wrapper boundary.
The c-blosc adapter fixes LZ4/level 5/shuffle/blocksize 0/one internal thread,
uses context APIs, checks destination arithmetic and typesize 2/8, validates
the decoded size before allocation, and has no raw-byte fallback. Strict JSON
rejects duplicate keys, invalid UTF-8, non-finite tokens, type mismatch,
missing/unexpected fields, and trailing data; sorted serialization is stable.
CNG uses an explicit test-only key provider and `(key_id, nonce)` registry and
does not log keys.

This remains an isolated harness. Nothing is linked to production acquisition,
EP2/EP4/EP6, the existing solution, C ABI, or WPF. It does not claim
P0-S1/P0-S2 acceptance, a sharding choice, A-4b, Frozen v1, or Phase 0 closure.

## Byte-exact KAT and boundary matrix

Before reading the vector, `sha256sum` verified the read-only gcsa KAT as
`cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa`.
CMake independently requires that hash. The C++ test parses the pinned JSON
strictly rather than copying its wire constants. In approved and locally
reproduced Release `/MD` and Debug `/MDd`, it passed 88 assertions:

- 42 KAT checks prove byte-identical inner Blosc frame, AAD, AES-GCM
  ciphertext, tag, complete wire, authenticated decryption, decompression, and
  original input bits;
- the 40-byte inner frame SHA-256 is
  `f3d3b18c284608ee1500b93556ba13a66373b4923a7ac19a533e03a7ff0de7db`;
- 33 boundary rows cover sizes 0/1/15/16/17 with typesize 2/8, zero,
  repetitive and deterministic incompressible inputs, bit-exact
  NaN/+Inf/-Inf, partial chunks, and full 1,920,000/48,000,000/76,800,000-byte
  feature/GMI/FL chunks;
- 13 typed negative checks cover zero-byte decoded-result rejection,
  truncated/corrupt/mismatched Blosc frames, wrong AES key, tag/ciphertext
  mutation, structural and authenticated truncation, chunk-swap AAD mismatch,
  unknown key ID, and nonce reuse. The return-by-value API exposes no partial
  plaintext on failure.

c-blosc emits a valid frame for a zero-byte source, but its decoded result is
zero and the adapter rejects the `<= 0` return as `kDecompressionFailed`.
Consequently a zero-row partition is a writer-level metadata/manifest state and
must not publish a zero-length chunk file.

All four Windows runs used the required wrapper and reported
`kat_matrix_checks=88 kat_checks=42 boundary_cases=33 negative_cases=13
status=pass`. No golden changed. An ignored `git archive` snapshot of gcsa
commit `20689a991697217518ec2ff15aaaa2533b169eb0` was then tested in the pinned
`gcsa-dev` environment. The two accepted KAT tests passed with exit 0, proving
gcsa authentication/decryption and numcodecs 0.13.1 expansion to values
`0..11` without modifying the sibling worktree.

## Minimal encrypted Zarr publication

The isolated writer uses one typed contract surface for the three exact Zarr
v2 arrays. Each full fixed chunk is assembled in a writer-thread-only buffer,
compressed through the c-blosc adapter, encrypted through the CNG Store
wrapper, and published by same-directory temp-to-rename. No raw byte crosses a
C ABI or WPF boundary. The output root must be absent, and failures never
select a different dependency, codec, profile, key, or wire version.

The writer test ran in approved and locally reproduced Release `/MD` and Debug
`/MDd` builds. Every configuration passed all three CTest targets. The focused
writer target reports 501 checks, six publication events, six encrypted chunks,
and two deterministic-JSON runs. It proves:

- exact `.zarray` dtype/shape/chunks/fill/order/dimension separator and the
  inner `blosc`/`lz4`/5/shuffle/blocksize-0 object;
- zero-row partitions publish shape/manifest state but no chunk file;
- all three partition chunks exist before the committed manifest advances;
- `write_generation` progresses `0 -> 1 -> 2`, row counts stay aligned, both
  partitions seal, and finalization follows the last manifest;
- all six `(key_id, nonce)` pairs are unique and no temp file remains;
- marker, metadata, and all six `.zarray` files are byte-identical across two
  independent runs even though nonce-bearing chunk wires intentionally differ.

The Release generator created ignored `open` and `finalized` stores at
`artifacts/phase0-zarr-roundtrip/stores/`. Each has `tube_1`, two partitions,
and all three arrays. Each partition represents one global event, which is the
minimum append/re-arm-like cycle needed for the reader seam. This records the
accepted reader's round-robin reconstruction only; it does not choose P0-S2
sharding. No generated store, executable, key, nonce registry, or payload is
tracked.

Publication uses a single writer and atomic replacement, but this harness does
not claim crash-consistent directory fsync beyond the Windows
`FlushFileBuffers` plus `MOVEFILE_WRITE_THROUGH` boundary. AAD rejects a chunk
swapped to another array/partition/key, but an older authenticated wire at the
same exact coordinate remains a static-snapshot rollback limit. Those are
recorded residuals, not hidden acceptance claims.

## Accepted gcsa snapshot roundtrip

The existing `gcsa-dev` identity was rechecked immediately before use:
container `d141d00e5edb0bd17ee37836340a4315343019d32db4f9197322e9a3a5c9e1d8`,
image `sha256:e65e9f8b0ffafef5b5d2b9711c9a3411649ae80fd036cc79f0febb80b4c0b06e`,
Python 3.10.17, and pip-freeze stdout SHA
`1dfb6e928ff4b3c5e6bd46eb9e7cada01baeb9b99c4818c4fc463740046a954a`.
No package was installed or downloaded. All imports came from the ignored
570-file `git archive` snapshot of accepted commit `20689a99`, with bytecode and
pytest cache disabled.

The exact focused command from the owner prompt passed 328/328 in 5.72 seconds,
exit 0. The generated-store command then passed 84 positive checks plus nine
fail-loud negative/visibility checks, exit 0. The accepted strict validator
reported six encrypted chunks for each of the open and finalized stores. The
read-only `ZarrStore` proved:

- raw synthetic bits -> c-blosc -> AES wrapper -> gcsa authentication and
  decryption -> numcodecs 0.13.1 -> original bits for `pulse_features`,
  `gmi_waveform`, and `fl_waveform`;
- exact dtype, shapes, chunk contract, 24 feature columns, eight FL channels,
  five GMI channels, feature min/max, aligned rows, and global event order;
- the two one-row partitions reconstruct the same logical two-event arrays;
- open is hidden from product listing and product reads fail with
  `DatasetNotFinalizedError`, while finalized is visible;
- wrong key, tag mutation, ciphertext mutation, truncation, partition swap/AAD
  mismatch, unknown key, plaintext chunk replacement, and manifest overclaim
  all fail without returned plaintext or repair;
- tree digests before and after validation are identical, so neither generated
  store nor the accepted snapshot was mutated.

The validation script is
`prototypes/zarr-store-roundtrip/scripts/validate_gcsa_roundtrip.py`. Its
negative stores exist only in a temporary directory and are deleted on exit.
The sibling gcsa worktree is not imported or modified.

## Remaining human gate

The technical dependency path, KAT, boundary matrix, and isolated three-array
roundtrip are green; no approved golden changed. The preparatory blocker is not
declared closed until this branch's checkpoint documents and evidence are
human-merged. P0-S1/P0-S2, the P0-S2 sharding decision, A-4b, Frozen v1, and
Phase 0 remain open.

## Batch 5 clean verification

The clean closeout rerun completed on 2026-07-21 at 18:58 JST from branch
`chore/p0-s-dependency-preflight`, commit
`4fddf2cee749986f06705526af433cf966cc9a12`, against unchanged
`origin/main` `f4ea8ffb80e704a1064c268a13701be198766d07`.

- Exact offline bundle validation and Windows artifact-report validation:
  pass, exit 0; refreshed ignored deterministic evidence files.
- Upstream c-blosc regression: 1626/1626 passed, exit 0.
- Approved and reproduced Release/Debug prototype CTest: 3/3 in each of four
  configurations, every exit 0.
- Repository-local Python validators: 19/19 passed, exit 0.
- Immutable gcsa snapshot focused suite: 328/328 passed in 5.72 seconds,
  exit 0.
- Fresh ignored open/finalized store generation: both pass, exit 0; gcsa
  strict validator/reader: 84 positive checks and 9 negative/visibility
  checks, exit 0.
- Dependency manifest stable sorted serialization SHA-256:
  `5adc55be5457a0d09e838938d7e78ce749b866b0cae326c1380acc6405eb1162`.
- Zarr checkpoints: `.agent/refactor.md`
  `e3ffb288c07566282dfd9b90a9310444f3b8f03198fa6beb212669df4714257a`;
  `.agent/review.md`
  `fe5958c6d50bc971b639df4d9175eb7b991b54fa6221e424ed088630fd4e7f58`.
- Complete branch refactor/review plus read-only Claude review: Blocker 0,
  Major 0, Minor 0. The branch contains no dependency binary/archive/header,
  generated store, executable, secret, raw payload, production integration,
  or sibling-repository change.

The technical evidence commit is
`7af097da3cd47f51f1815b0f07fbd3216daabbd8`. The non-merge preparatory PR is
[PR #3](https://github.com/yanagihashitc/AnalogBoard/pull/3). A human merge
remains mandatory before the dependency blocker can be handed back as closed.
