# Review checkpoint: P0-S dependency preparation

Review the current batch diff against the pinned dependency-preflight prompt,
accepted gcsa commit, Contract RC, and machine-readable KAT. Findings come first
with severity and exact file references. A pass is incomplete until every
finding is fixed or explicitly blocked.

## Contract and wire checks

- `.zarray` is exact Zarr v2: dtype, shape, fixed chunks, C order, JSON fill
  type, `dimension_separator="."`, no unknown fields, and exactly the inner
  Blosc lz4 profile.
- c-blosc uses lz4, clevel 5, shuffle 1, blocksize 0, one internal thread, the
  context APIs, checked sizes, and no fallback.
- The inner Blosc frame, AES-GCM ciphertext/tag, and complete wire match the
  accepted byte KAT in Release and Debug.
- AEAD wire is `GCSA|version|key_id|nonce|ciphertext|tag`, and AAD binds exact
  dataset id, array-relative path, and chunk key.
- Nonce uniqueness spans arrays and partitions. Wrong key, tag/ciphertext
  mutation, chunk swap, unknown key, and truncation fail loud without plaintext
  or partial output.

## Store and D21 checks

- Chunk and metadata publication is temp-to-atomic-rename and never overclaims
  durable chunks.
- `status=open` is not normally visible; finalized is terminal and requires all
  partitions sealed plus `finalized_at`.
- `write_generation`, manifest row counts, row alignment, global event order,
  feature min/max, and partition append match the accepted gcsa validators.
- Same-coordinate rollback remains a documented static-snapshot limitation; AAD
  alone is not claimed to prevent it.
- The strict gcsa validator/reader authenticates, decrypts, expands, and returns
  original bits for all three arrays without plaintext fallback.

## Scope checks

- No P0-S2 sharding decision is made; only the minimum append needed to validate
  the dependency path is exercised.
- No production acquisition, EP2/EP4/EP6, CyAPI, WPF, driver, firmware, real
  measurement, or current-goal integration is present.
- gcsa, sys_app, and task_management remain unchanged by this branch.
- No dependency archive/header/library, generated store, executable, build tree,
  secret, nonce registry from real data, or raw payload is tracked.
- A-4b, Frozen v1, Phase 0, P0-S1, and P0-S2 remain open.

## Repository and evidence checks

- Dependency/archive/source/license/header/approved-library/local-build hashes,
  commands, tool versions, test counts, exits, and durations are recorded.
- JSON output is strict and deterministic; duplicate keys, invalid UTF-8,
  non-finite values, type mismatch, missing fields, and unexpected structure are
  rejected where the contract is strict.
- `git diff --check`, staged scope/size review, secret/generated-artifact scan,
  and sibling status comparison pass.

Record all findings, fixes, skipped checks, and residual limits in the active
process log. Never relabel an unavailable or failed check as passed.
