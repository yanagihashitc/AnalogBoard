# Refactor checkpoint: P0-S dependency preparation

This checkpoint applies only to files touched by the isolated dependency and
encrypted-Zarr preparation branch. Preserve contract bytes and fail-closed
behavior while improving clarity, determinism, and maintainability.

## Scope guard

- Keep all implementation under the isolated prototype and verification paths.
- Do not link the prototype to production `AcquisitionEngine`, EP2/EP4/EP6,
  CyAPI, WPF, the existing solution acquisition path, or real measurement data.
- Do not modify gcsa, sys_app, task_management, driver, registry, firmware, or
  the current `goal.md`.
- Refactor only files touched by the current batch.

## Refactor checks

1. Keep contract constants in one typed contract surface; do not duplicate
   hand-copied wire values across writer, tests, and scripts.
2. Preserve deterministic JSON with stable key ordering and byte-identical
   serialization across repeated runs.
3. Separate binary wire parsing/building, c-blosc adaptation, strict JSON,
   AES-GCM, metadata state, and filesystem publication helpers.
4. Keep nonce registration explicit across arrays and partitions. Test keys are
   injected only through a test key provider; never log key bytes.
5. Keep all publication temp-to-atomic-rename and make incomplete output
   unobservable as committed state.
6. Use stable typed errors with actionable messages. Do not add raw-byte,
   plaintext, codec, version, or library fallbacks.
7. Keep generated stores, dependency caches, source archives, compiled output,
   secrets, nonces from real data, and raw measurement payload outside Git.
8. Update focused tests for every behavior changed during refactoring and keep
   Given/When/Then intent visible.

## Verification

- Run the focused tests for the touched batch in Release and Debug where the CRT
  is relevant.
- Run deterministic JSON and exact KAT comparisons where applicable.
- Run `git diff --check` and inspect tracked/staged path sizes.
- Confirm no production acquisition, sibling repository, generated store,
  dependency binary, secret, or raw payload is in the diff.
- Record retained complexity and residual limits in the active process log.

Stop if a refactor would alter a pin, golden, persistent wire, D21 rule, or
P0-S2 sharding decision.
