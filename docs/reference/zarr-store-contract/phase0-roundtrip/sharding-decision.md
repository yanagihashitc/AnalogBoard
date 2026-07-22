# P0-S2 partition sharding decision

Decision date: 2026-07-22

Status: `gate_ready` pending the phase PR merge and central synchronization

Contract candidate: `gcsa-store-a4a-rc1` at gcsa commit
`20689a991697217518ec2ff15aaaa2533b169eb0`

## Decision

Adopt round-robin sharding (`global_event % n_partitions`) for the Frozen v1
input. Do not include append-sequential in Frozen v1.

The deciding criterion is compatibility with the accepted gcsa strict
validator and unchanged read-only APIs. Round-robin passed strict validation,
partition reads, full reads, cross-partition slices, and duplicate/out-of-order
GMI/FL gathers. Append-sequential preserved all three arrays within each
partition, but the accepted strict validator and every global read surface
rejected its `[2,3]` distribution because the Contract RC expects round-robin
`[3,2]`.

Performance was not used for selection. Three alternating observations showed
about 1.9 s for round-robin and 1.7 s for append-sequential when the timer
included the VsDevCmd wrapper and generator process. These five-event prototype
values are not production throughput guarantees.

## Compared behavior

| Observation | Round-robin | Append-sequential |
|---|---|---|
| Global mapping | `[0,2,4] / [1,3]` | `[0,1] / [2,3,4]` |
| Final rows | `[3,2]` | `[2,3]` |
| Final generation | 3 | 2 |
| Atomic chunk publications | 12 | 6 |
| Same-coordinate rewrites | 6 | 0 |
| Partition-local alignment | Pass | Pass |
| Strict validator | Pass | Expected rejection |
| Full / slice / gather | Pass | Expected rejection |
| Final chunk files | 6 | 6 |

The append-sequential strict rejection is
`StoreContractValidationError` containing
`D21 round-robin row distribution mismatch`. Its global full/slice/gather
surfaces raise `ValueError` containing
`events_per_partition distribution mismatch`. A successful partition-local
read is a content/alignment control, not global compatibility evidence.

## Lifecycle consequence

Round-robin keeps both partitions unsealed while the two synthetic append
cycles are in progress. The writer reaches `[1,1]` at generation 1, `[3,2]` at
generation 2, seals both partitions at generation 3, and then finalizes without
advancing the content generation. Normal gcsa product APIs reject every
`status=open` dataset, so no downstream consumer depends on an intermediate
sealed prefix. The diagnostic open reader returns data only after all
partitions are sealed.

The C++ observer tests prove chunk durable publication before metadata rename.
The Python failure-cut matrix uses a completed synthetic store with disposable
metadata overlays; it proves metadata-lag cut disposition and ordered Contract
RC transitions, not a separately captured writer crash at every cut.

## Downstream handoff

- gcsa A-4b may take round-robin as the sole Frozen v1 sharding input after
  this phase PR is merged and the central gate accepts the evidence.
- gcsa B-6 remains required even though the selected mapping matches the
  current implementation. It must make the strategy explicit and close the
  slice-length validation bypass using D21 manifests. It does not need to add
  append-sequential support for Frozen v1.
- A future append-sequential format requires a schema/version change, reader
  and validator changes across full/slice/gather/open diagnostics, and a new
  joint roundtrip before adoption.
- No gcsa, sys_app, or task_management file was modified in this scope.

## Evidence and limits

- Joint golden: `joint-roundtrip-golden.json`, SHA-256
  `7bc0e903d3b363e64d79d0ff93b58bee0d3775c5e20b64686ae741ee07860a6e`
- Sharding comparison: `sharding-comparison.json`, SHA-256
  `1cd8299455554f5484ebcb0412472fceb391525b4c11c2032ecaabb774d852c7`
- Joint matrix: 269 positive and 35 fail-loud negative checks
- Sharding matrix: 37 positive and 9 expected rejection checks

An old authenticated chunk at the same coordinate cannot be identified from a
single static snapshot because generation is not part of AAD and the manifest
does not contain a per-coordinate wire digest. Ordered comparison with a
previous trusted metadata snapshot detects generation rollback; it does not
remove that static-snapshot limitation.

This decision does not declare A-4b, Frozen v1, P0-C4, the scatter prototype,
the D17 golden regression, or Phase 0 complete.
