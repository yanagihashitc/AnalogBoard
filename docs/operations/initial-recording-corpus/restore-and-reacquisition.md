# P0-C4 Restore and Reacquisition Procedure

## Scope and authority

This document records the current P0-C4 custody boundary and separates a future
restore from a future reacquisition. It does not authorize either material
operation. The tracked custody index and its pinned plan, contract, manifest,
verifier, and this procedure are the metadata authorities for this scope.

Asset owner decision required and retention decision required remain open.
Neither repository ownership nor authorship identifies the physical asset
owner. No retention duration, expiry, or deletion disposition may be guessed.

## Current custody verdicts

The canonical local locator is
`artifacts/field-session/2026-07-17-characterization`. Availability is verified
against every entry in the pinned corpus manifest. Restore not performed:
availability is not restore verification, and no distinct restore source has
been identified.

The current open decisions are the asset owner, retention policy, and identity
of a distinct restore source. Resolving any of them requires a separately
reviewed policy update.

## At-rest boundary

The assets remain pre-D19 plaintext local-only on the asset-retaining machine.
D19 protection is not applied to this corpus. Git exclusion is not at-rest
protection. Export is prohibited.

This scope does not authorize relocation, reprotection, copying, upload, or
network transfer. It does not alter, rename, move, delete, touch, or otherwise
write beneath the canonical locator.

The following is the sole command in this procedure. It is a read-only
availability and integrity verifier for the current canonical corpus; it is not
a restore test:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 scripts/corpus-index/corpus_index.py \
  verify \
  --manifest docs/reference/initial-recording-corpus/2026-07-17/manifest.json
```

## Restore path

A future restore requires a separately authorized restore source and a distinct
candidate location. The source identity and candidate location must be reviewed
before any material operation. The candidate must never overwrite the
canonical corpus.

Restore verification requires the same SHA-256 for every manifest entry and an
exact source set before a restore can be called verified. A missing,
unreadable, size-mismatched, hash-mismatched, or unexpected entry is a failed
restore candidate, not a warning. Current-corpus availability cannot be reused
as restore evidence.

This document deliberately provides no restore transfer command. A future
authorized restore must supply separately reviewed tooling, source identity,
candidate locator, evidence destination, and rollback/cleanup policy.

## Reacquisition path

Future reacquisition requires a separate corpus version and separately
authorized acquisition scope. Reacquisition is not restore and is never a
fallback used to satisfy a failed or unavailable restore.

Newly acquired bytes are not required to have the same SHA-256 as the current
corpus. They must not replace, overwrite, or silently assume the identity of the
current corpus. A future reacquisition needs its own contract, manifest,
relationships, custody policy, acceptance evidence, and versioned locator.

This document deliberately provides no acquisition or device-control command.

## Stop conditions

Stop and request owner direction if any of the following occurs:

- the pinned source identities, canonical locator, source set, size, or SHA-256
  differs;
- a distinct restore source or candidate cannot be identified before a restore;
- a request would export, transfer, relocate, re-protect, overwrite, modify, or
  delete the corpus;
- a request attempts to treat current availability as restore verification;
- a request attempts to treat reacquisition as restore or to replace the
  current corpus;
- owner or retention authority is needed beyond recording the existing open
  items.

Retention remains open, so no deletion date or disposition is inferred.
