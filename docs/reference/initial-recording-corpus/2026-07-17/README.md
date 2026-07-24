# P0-C4 initial recording corpus closeout

This directory is the tracked, payload-free index for the initial recording
corpus. The exact P0-C4 acceptance text remains solely in the
[Draft 4.7 rebuild plan](../../../plans/260710-analogboard-rebuild-plan.html#p0-corpus-steps).
The closeout does not repeat inventory totals, per-run values, asset locations,
or the acceptance prose.

The current repository state is `gate_ready` and pending human merge. It is not
a declaration that P0-C4 is completed, that the initial-corpus gate is closed,
that a central handoff was published, or that a next-scope transition is
authorized. The permitted pre-merge action is to create the planned pull
request and stop; merge remains manual.

## Authorities

[closeout.json](closeout.json) is a typed sink that pins accepted sources but
does not replace them as authorities:

| Authority | Responsibility |
|---|---|
| [Rebuild plan](../../../plans/260710-analogboard-rebuild-plan.html#p0-corpus-steps) | Acceptance and status semantics |
| [contract.json](contract.json) | Inventory, naming, and source-location contract |
| [manifest.json](manifest.json) | Per-file identity metadata |
| [relationship-contract.json](relationship-contract.json) | Cross-index, telemetry, and clock rules |
| [relationships.json](relationships.json) | Deterministic relationship evidence |
| [custody.json](custody.json) | At-rest, availability/restore, custody, and open-item state |
| [Recovery procedure](../../../operations/initial-recording-corpus/restore-and-reacquisition.md) | Restore and reacquisition boundary |
| [P0-C1–C3 index](../../usb-recording-corpus/2026-07-17/README.md) | Historical USB protocol baseline |

The authority flow is contract to manifest, the plan and pinned metadata to the
relationship contract, the relationship contract to relationship evidence, and
the plan plus pinned metadata and procedure to custody. The closeout consumes
those branches and the accepted verifier identities as a final sink. It does
not introduce a second inventory or relationship authority.

The earlier P0-C1–C3 README retains its historical statement that P0-C4 was
planned because it records the boundary of that already accepted scope. This
closeout explains the later repository state without rewriting the historical
artifact. Capture identity overlap and failure-trace absence are checked through
the relationship contract and evidence rather than copied here.

## Acceptance bindings

The closeout binds the plan condition identifiers to existing evidence roles:

| Condition | Evidence roles |
|---|---|
| `P0-C4-1` | custody |
| `P0-C4-2` | corpus contract, custody |
| `P0-C4-3` | corpus manifest, manifest verifier, custody |
| `P0-C4-4` | corpus contract, corpus manifest, manifest verifier |
| `P0-C4-5` | custody, recovery procedure |
| `P0-C4-6` | relationship contract, relationship evidence, USB manifest, relationship verifier |

The authoritative condition wording and all underlying values remain in the
linked sources.

## Open items

The closeout references, without resolving, the custody-owned items
`P0-C4-ASSET-OWNER`, `P0-C4-RESTORE-SOURCE`, and `P0-C4-RETENTION`. Availability
does not prove restore. Their reason, status, and unresolved values remain
authoritative only in [custody.json](custody.json).

## Verification

Focused tests use temporary metadata fixtures and mocked live dependency seams.
They do not read the local corpus:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
  -s scripts/corpus-index/tests -p 'test_corpus_closeout.py' -v
```

The phase verifier is live, read-only, and restricted to the asset-retaining
machine. Do not run it in CI, on a development machine, or on a host without
the local-only corpus:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 scripts/corpus-index/corpus_closeout.py \
  verify \
  --index docs/reference/initial-recording-corpus/2026-07-17/closeout.json
```

The command checks every pinned source before live reads, regenerates the
manifest and relationship evidence in memory for byte comparison, verifies
custody, and finally rechecks the complete metadata snapshot. It has no build,
output, publication, transfer, or metadata-only CLI success mode.
