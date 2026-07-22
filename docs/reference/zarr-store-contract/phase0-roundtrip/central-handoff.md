# P0-S1/P0-S2 central handoff

Date: 2026-07-22

Local status: `gate_ready` pending phase PR merge and task_management sync

Branch: `feat/phase0-store-roundtrip`

Remote branch: `origin/feat/phase0-store-roundtrip`

## Publication identity

- Batch 1: `5833aab886cfe312a17f238773df441b09642281`
- Batch 2: `01103017c55aed109b450bc81b0af171726a6c91`
- Batch 3: `da818ba245be252a3717bf9bf3d55c57fa20594e`
- Batch 4: `8fdcd747e0d6bf760fd6e674f620f8c97b356235`
- PR #4 review source/provenance repair:
  `a3aa73383ca092ee682b46d966ff2b20c0360b81`
- Phase PR: [#4](https://github.com/yanagihashitc/AnalogBoard/pull/4)
- Terminal publication: local `HEAD`, the same-name remote-tracking ref, and
  `git ls-remote --heads origin feat/phase0-store-roundtrip` all resolved to
  the Batch 4 SHA; the tracked worktree was clean at verification. The active
  todo was empty and the completed Batch 4 was present in the ignored local
  todo archive.
- Base: `origin/main` at
  `807b44106dce35fe1f6b8f91b37e130ea69b3cb9`

No merge is authorized by this handoff. P0-S1/P0-S2 become `completed` only
after the phase PR is merged and task_management accepts the evidence.

## Accepted downstream and toolchain identity

- gcsa commit: `20689a991697217518ec2ff15aaaa2533b169eb0`
- gcsa package tree SHA-256:
  `c63c79c4add3a8034cd1486921470818ad71d024ace1e8e356ae4f8dbf396d14`
- Contract: `gcsa-store-a4a-rc1`
- gcsa container:
  `d141d00e5edb0bd17ee37836340a4315343019d32db4f9197322e9a3a5c9e1d8`
- image:
  `sha256:e65e9f8b0ffafef5b5d2b9711c9a3411649ae80fd036cc79f0febb80b4c0b06e`
- public KAT SHA-256:
  `cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa`
- dependency: c-blosc 1.21.6 with internal LZ4 1.9.4; AES backend is Windows
  CNG `bcrypt`; compiler is MSVC 19.37.32826.1, x64 `/MD` and `/MDd`

## Acceptance evidence

- C++ approved/reproduced Release/Debug: CTest 3/3 in each configuration;
  writer 314710 checks and byte-exact KAT matrix 95 in each
- Host Python: 62/62 after the required-manifest and semantic-evidence
  negatives were added
- Accepted gcsa public KAT: 2/2
- P0-S1 joint matrix: 269 positive / 35 fail-loud negative; decoded/meta
  canonical SHA-256
  `b90427471da1d18618d4add37273903ce0376dc7eccb1eb682d4e1c42fa6b62d`
- P0-S2 comparison: 37 positive / 9 expected rejection checks
- Joint golden SHA-256:
  `adddf4a035b343ff5db372fd1699a6f8f20802c59f109a6d6894657b630d5e37`
- Sharding comparison SHA-256:
  `a22eca0fca592654834bbe22919e26f1e75bc0257e4babfb870efb956a032381`
- Sharding decision SHA-256:
  `ced20553b54b27d57028c6d8474b9949c522bd51e64c7e43329e6d61f080ebc9`
- Evidence/source manifest SHA-256:
  `8c572b318efa8d45255fc56a646838e7fef9ba4b2126cfd49a2b05a61ebd37b8`

Tracked evidence contains only bounded synthetic metadata, hashes, counts, and
decisions. Generated stores remain under ignored roots. No real measurement,
encrypted chunk, nonce, secret, dependency binary, or raw payload is tracked.

## Decision and downstream action

Adopt round-robin sharding (`global_event % n_partitions`) as the sole Frozen
v1 input. It passes the accepted gcsa strict validator plus partition, full,
cross-partition slice, and duplicate/out-of-order gather reads without a gcsa
change. Append-sequential preserves partition-local content but the accepted
strict validator and all global surfaces fail loud on its `[2,3]`
distribution, so it is excluded from Frozen v1. Prototype timing was recorded
but was not used for selection.

- gcsa A-4b may consume the round-robin decision after this PR is merged and
  centrally accepted.
- gcsa B-6 remains required to make the round-robin strategy explicit and
  close the manifest/slice-length validation bypass. It need not add
  append-sequential support for Frozen v1.
- A future append-sequential format requires a versioned cross-repository
  contract change and a new joint roundtrip.

## Source-plan and central synchronization

The local parent plan is Draft 4.2. Only P0-S1 and P0-S2 advanced to
`gate_ready`; D1-D23 and their acceptance criteria did not change. The local
Zarr skill and AGENTS/CLAUDE guidance now record round-robin selection and the
same-coordinate rollback limit accurately.

The task_management mirror intentionally reports AnalogBoard drift until the
central workspace performs one-way synchronization after merge. The existing
sys_app mirror drift and task_management working-tree edits predate this scope
and were not modified. gcsa, sys_app, and task_management remained read-only.

## Explicitly open

A-4b, B-6, P0-C4, the initial recording-corpus gate, scatter prototype,
D17 golden regression, Frozen v1, Phase 0, and Phase 1 entry remain open. A
single static snapshot cannot distinguish an older authenticated chunk at the
same coordinate; only comparison with previously trusted metadata can detect a
generation rollback.
