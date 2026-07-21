# AnalogBoard P0-S dependency preparation evidence

Status: in progress. This document records repository-local dependency and
checkpoint preparation only. It does not close P0-S1/P0-S2, choose P0-S2
sharding, declare A-4b/Frozen v1, or complete Phase 0.

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
zero absolute/traversal paths. Every streamed pinned header, library, license,
source ZIP, and manifest hash matched. Full extraction/source-tree validation is
deferred to the Batch 2 fail-closed validator.

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

## Pending closure evidence

The c-blosc blocker remains open until Batch 2–4 evidence covers the separate
upstream regression build, local Release/Debug static builds, object/CRT/runtime
inspection, exact Blosc/AES KAT, boundary/negative matrix, and the encrypted
three-array gcsa roundtrip with one minimal append. Approved library hashes are
verified supply identities; local rebuild hashes will be recorded separately
because COFF timestamps are build-dependent.
