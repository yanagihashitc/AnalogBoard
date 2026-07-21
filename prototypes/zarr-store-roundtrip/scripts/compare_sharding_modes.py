#!/usr/bin/env python3
"""Compare the two Phase 0 sharding candidates with accepted gcsa."""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Any, NoReturn

import validate_gcsa_roundtrip as joint


SYNTHETIC_EVENT_COUNT = 5
SYNTHETIC_PARTITION_COUNT = 2
MEASUREMENT_ARRAYS = (
    "pulse_features",
    "gmi_waveform",
    "fl_waveform",
)
ROUND_ROBIN_MODE = "round-robin"
APPEND_SEQUENTIAL_MODE = "append-sequential"
SEQUENTIAL_STRICT_ERROR = "D21 round-robin row distribution mismatch"
SEQUENTIAL_GLOBAL_ERROR = "events_per_partition distribution mismatch"
ANALOGBOARD_BASELINE_COMMIT = "da818ba245be252a3717bf9bf3d55c57fa20594e"
EXPECTED_MANIFEST_RELATIVE_PATH = Path(
    "docs/reference/zarr-store-contract/phase0-roundtrip/"
    "phase0-roundtrip-manifest.json"
)
EXPECTED_COMPARISON_RELATIVE_PATH = Path(
    "docs/reference/zarr-store-contract/phase0-roundtrip/"
    "sharding-comparison.json"
)
EXPECTED_MANIFEST_SOURCE_PATHS = (
    "docs/reference/zarr-store-contract/phase0-roundtrip/joint-roundtrip-golden.json",
    "docs/reference/zarr-store-contract/phase0-roundtrip/sharding-comparison.json",
    "docs/reference/zarr-store-contract/phase0-roundtrip/sharding-decision.md",
    "prototypes/zarr-store-roundtrip/README.md",
    "prototypes/zarr-store-roundtrip/include/p0s/minimal_zarr_writer.h",
    "prototypes/zarr-store-roundtrip/include/p0s/store_contract.h",
    "prototypes/zarr-store-roundtrip/scripts/compare_sharding_modes.py",
    "prototypes/zarr-store-roundtrip/src/minimal_zarr_writer.cpp",
    "prototypes/zarr-store-roundtrip/tests/store_writer_tests.cpp",
    "prototypes/zarr-store-roundtrip/tests/test_compare_sharding_modes.py",
    "prototypes/zarr-store-roundtrip/tools/store_generator.cpp",
    "scripts/zarr-roundtrip/run-focused-verification.sh",
)


def global_events_by_partition(mode: str) -> tuple[tuple[int, ...], ...]:
    """Return the fixed five-event mapping for one candidate mode."""
    if mode == ROUND_ROBIN_MODE:
        return tuple(
            tuple(
                range(
                    partition,
                    SYNTHETIC_EVENT_COUNT,
                    SYNTHETIC_PARTITION_COUNT,
                )
            )
            for partition in range(SYNTHETIC_PARTITION_COUNT)
        )
    if mode == APPEND_SEQUENTIAL_MODE:
        return ((0, 1), (2, 3, 4))
    raise ValueError(f"unsupported sharding mode: {mode}")


def summarize_wall_times(samples: Sequence[int]) -> dict[str, Any]:
    """Return bounded observations for exactly three generator runs."""
    if len(samples) != 3 or any(
        type(sample) is not int or sample < 0 for sample in samples
    ):
        raise ValueError("wall time observations must be three non-negative integers")
    ordered = sorted(samples)
    return {
        "samples_ms": list(samples),
        "min_ms": ordered[0],
        "median_ms": ordered[1],
        "max_ms": ordered[2],
    }


def is_measurement_chunk(path: Path) -> bool:
    """Return whether a regular file is a fixed measurement chunk."""
    return (
        not path.name.startswith(".")
        and path.parent.suffix == ".zarr"
        and path.parent.name.startswith("partition_")
        and path.parent.parent.name in MEASUREMENT_ARRAYS
    )


def collect_directory_metrics(root: Path) -> dict[str, int]:
    """Count bounded store files and bytes without retaining any payload."""
    if not root.is_dir() or root.is_symlink():
        raise joint.CheckFailure(f"comparison store root is invalid: {root}")
    regular_files = 0
    total_bytes = 0
    measurement_files = 0
    measurement_bytes = 0
    partition_directories = 0
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise joint.CheckFailure(f"comparison store contains a symlink: {path}")
        if path.is_dir():
            if (
                path.suffix == ".zarr"
                and path.name.startswith("partition_")
                and path.parent.name in MEASUREMENT_ARRAYS
            ):
                partition_directories += 1
            continue
        if not path.is_file():
            raise joint.CheckFailure(f"comparison store entry is unsupported: {path}")
        size = path.stat().st_size
        regular_files += 1
        total_bytes += size
        if is_measurement_chunk(path):
            measurement_files += 1
            measurement_bytes += size
    return {
        "regular_files": regular_files,
        "total_regular_file_bytes": total_bytes,
        "measurement_chunk_files": measurement_files,
        "measurement_wire_bytes": measurement_bytes,
        "array_partition_directories": partition_directories,
    }


def verify_evidence_manifest(path: Path) -> Path:
    """Require exact P0-S source/evidence identities before comparison."""
    document = joint.load_strict_json_object(path)
    if set(document) != {
        "schema",
        "generated_at",
        "command",
        "identities",
        "files_sha256",
        "exclusions",
    }:
        raise joint.CheckFailure("roundtrip evidence manifest structure drift")
    if (
        document["schema"] != "analogboard-p0-s-evidence-manifest-v1"
        or document["generated_at"] != "2026-07-22"
        or document["command"]
        != "scripts/zarr-roundtrip/run-focused-verification.sh sharding"
    ):
        raise joint.CheckFailure("roundtrip evidence manifest identity drift")
    expected_identities = {
        "analogboard_baseline_commit": ANALOGBOARD_BASELINE_COMMIT,
        "gcsa_commit": joint.EXPECTED_GCSA_SNAPSHOT_COMMIT,
        "gcsa_package_tree_sha256": joint.EXPECTED_GCSA_PACKAGE_TREE_SHA256,
        "gcsa_container_id": joint.EXPECTED_GCSA_CONTAINER_ID,
        "gcsa_image_id": joint.EXPECTED_GCSA_IMAGE_ID,
        "contract_id": joint.CONTRACT_ID,
        "public_kat_sha256": joint.EXPECTED_PUBLIC_KAT_SHA256,
    }
    if document["identities"] != expected_identities:
        raise joint.CheckFailure("roundtrip evidence manifest provenance drift")
    expected_exclusions = {
        "manifest_self_hash": True,
        "parent_plan_and_central_handoff": True,
        "generated_measurement_payload": True,
    }
    if document["exclusions"] != expected_exclusions:
        raise joint.CheckFailure("roundtrip evidence manifest exclusions drift")
    source_hashes = document["files_sha256"]
    if not isinstance(source_hashes, dict) or set(source_hashes) != set(
        EXPECTED_MANIFEST_SOURCE_PATHS
    ):
        raise joint.CheckFailure("roundtrip evidence source inventory drift")

    resolved_path = path.resolve()
    try:
        repository_root = resolved_path.parents[4]
    except IndexError as exc:
        raise joint.CheckFailure("roundtrip evidence manifest path is invalid") from exc
    if resolved_path != repository_root / EXPECTED_MANIFEST_RELATIVE_PATH:
        raise joint.CheckFailure("roundtrip evidence manifest is not tracked path")
    for relative in EXPECTED_MANIFEST_SOURCE_PATHS:
        source = repository_root / relative
        if not source.is_file() or source.is_symlink():
            raise joint.CheckFailure(f"roundtrip evidence source is absent: {relative}")
        actual = hashlib.sha256(source.read_bytes()).hexdigest()
        if source_hashes[relative] != actual:
            raise joint.CheckFailure(
                f"roundtrip evidence source SHA-256 drift: {relative}"
            )
    return repository_root


def evidence_without_wall_time(document: dict[str, Any]) -> dict[str, Any]:
    """Validate observations and remove only variable wall-time evidence."""
    try:
        metrics = document["observational_metrics"]
        wall_time = metrics["wall_time"]
    except (KeyError, TypeError) as exc:
        raise joint.CheckFailure(
            "sharding comparison wall-time structure drift"
        ) from exc
    if not isinstance(metrics, dict) or not isinstance(wall_time, dict):
        raise joint.CheckFailure("sharding comparison wall-time type drift")
    if set(wall_time) != {ROUND_ROBIN_MODE, APPEND_SEQUENTIAL_MODE}:
        raise joint.CheckFailure("sharding comparison wall-time mode drift")
    for mode in (ROUND_ROBIN_MODE, APPEND_SEQUENTIAL_MODE):
        observation = wall_time[mode]
        if not isinstance(observation, dict) or set(observation) != {
            "samples_ms",
            "min_ms",
            "median_ms",
            "max_ms",
        }:
            raise joint.CheckFailure(
                f"sharding comparison {mode} wall-time structure drift"
            )
        try:
            expected = summarize_wall_times(observation["samples_ms"])
        except (TypeError, ValueError) as exc:
            raise joint.CheckFailure(
                f"sharding comparison {mode} wall-time values drift"
            ) from exc
        if observation != expected:
            raise joint.CheckFailure(
                f"sharding comparison {mode} wall-time summary drift"
            )
    normalized = copy.deepcopy(document)
    del normalized["observational_metrics"]["wall_time"]
    return normalized


def verify_expected_evidence(
    path: Path,
    runtime_summary: dict[str, Any],
    repository_root: Path,
) -> None:
    """Require tracked and runtime semantics, excluding variable timing, to match."""
    resolved_path = path.resolve()
    expected_path = (repository_root / EXPECTED_COMPARISON_RELATIVE_PATH).resolve()
    if resolved_path != expected_path or not path.is_file() or path.is_symlink():
        raise joint.CheckFailure("sharding comparison evidence is not tracked path")
    expected = joint.load_strict_json_object(path)
    if evidence_without_wall_time(expected) != evidence_without_wall_time(
        runtime_summary
    ):
        raise joint.CheckFailure("tracked sharding comparison semantic drift")


def select_sharding_mode(
    *,
    round_robin_compatible: bool,
    append_sequential_compatible: bool,
) -> str:
    """Adopt only the sole mode accepted by the immutable gcsa snapshot."""
    if round_robin_compatible is True and append_sequential_compatible is False:
        return ROUND_ROBIN_MODE
    raise joint.CheckFailure(
        "cannot adopt round-robin without sole accepted-gcsa compatibility"
    )


def require_expected_failure(
    label: str,
    action: Callable[[], object],
    expected: tuple[type[Exception], ...],
    message_fragment: str,
    checks: Any,
) -> dict[str, str]:
    """Require an exact failure category without returning partial output."""
    try:
        action()
    except expected as exc:
        if message_fragment not in str(exc):
            raise joint.CheckFailure(
                f"{label}: expected message fragment is absent: {exc}"
            ) from exc
        checks.negative += 1
        return {
            "exception": type(exc).__name__,
            "required_message_fragment": message_fragment,
        }
    except Exception as exc:
        raise joint.CheckFailure(
            f"{label}: unexpected failure type {type(exc).__name__}: {exc}"
        ) from exc
    raise joint.CheckFailure(f"{label}: accepted invalid input")


def validate_round_robin(root: Path, checks: Any) -> None:
    """Require strict and global reader compatibility for round-robin."""
    before = joint.tree_digest(root)
    report = joint.strict_validate(root)
    joint.require_report(report, checks, "round-robin")
    store = joint.ZarrStore(root, config=joint.store_config(), readonly=True)
    expected = joint.expected_arrays()
    mapping = global_events_by_partition(ROUND_ROBIN_MODE)

    checks.require(store.readonly, "round-robin reader is not read-only")
    checks.require(
        store.list_datasets() == [joint.DATASET_ID],
        "round-robin dataset is not visible",
    )
    meta = store.get_meta(joint.DATASET_ID)
    checks.require(
        meta.events_per_partition == [3, 2],
        "round-robin partition distribution drift",
    )
    checks.require(
        meta.extra.get("partition_sharding") == ROUND_ROBIN_MODE,
        "round-robin metadata marker drift",
    )
    for array_name in MEASUREMENT_ARRAYS:
        full = joint.read_measurement(store, array_name)
        checks.require(
            joint.arrays_equal(array_name, full, expected[array_name]),
            f"round-robin {array_name} full/global drift",
        )
        sliced = joint.read_measurement(
            store,
            array_name,
            event_slice=slice(1, 5),
        )
        checks.require(
            joint.arrays_equal(array_name, sliced, expected[array_name][1:5]),
            f"round-robin {array_name} slice drift",
        )
        for partition, global_events in enumerate(mapping):
            partition_view = joint.read_measurement(
                store,
                array_name,
                partition=partition,
            )
            checks.require(
                joint.arrays_equal(
                    array_name,
                    partition_view,
                    expected[array_name][list(global_events)],
                ),
                f"round-robin {array_name} partition {partition} drift",
            )
    gather = [4, 0, 3, 3, 1]
    for array_name in ("gmi_waveform", "fl_waveform"):
        gathered = joint.read_measurement(
            store,
            array_name,
            event_indices=gather,
        )
        checks.require(
            joint.arrays_equal(array_name, gathered, expected[array_name][gather]),
            f"round-robin {array_name} gather drift",
        )
    checks.require(
        joint.tree_digest(root) == before,
        "round-robin comparison mutated the store",
    )


def validate_append_sequential(
    root: Path,
    checks: Any,
) -> tuple[dict[str, str], dict[str, str]]:
    """Require local alignment and global rejection for append-sequential."""
    before = joint.tree_digest(root)
    strict_rejection = require_expected_failure(
        "append-sequential strict",
        lambda: joint.strict_validate(root),
        (joint.StoreContractValidationError,),
        SEQUENTIAL_STRICT_ERROR,
        checks,
    )
    store = joint.ZarrStore(root, config=joint.store_config(), readonly=True)
    expected = joint.expected_arrays()
    mapping = global_events_by_partition(APPEND_SEQUENTIAL_MODE)
    meta = store.get_meta(joint.DATASET_ID)

    checks.require(store.readonly, "append-sequential reader is not read-only")
    checks.require(
        meta.events_per_partition == [2, 3],
        "append-sequential partition distribution drift",
    )
    checks.require(
        meta.write_generation == 2,
        "append-sequential generation drift",
    )
    checks.require(
        meta.extra.get("partition_sharding") == APPEND_SEQUENTIAL_MODE,
        "append-sequential metadata marker drift",
    )
    for array_name in MEASUREMENT_ARRAYS:
        manifests = meta.partition_manifests[array_name]
        checks.require(
            [(entry.row_count, entry.sealed) for entry in manifests]
            == [(2, True), (3, True)],
            f"append-sequential {array_name} manifest drift",
        )
        for partition, global_events in enumerate(mapping):
            partition_view = joint.read_measurement(
                store,
                array_name,
                partition=partition,
            )
            checks.require(
                joint.arrays_equal(
                    array_name,
                    partition_view,
                    expected[array_name][list(global_events)],
                ),
                f"append-sequential {array_name} partition {partition} drift",
            )

    global_rejection: dict[str, str] | None = None
    for array_name in MEASUREMENT_ARRAYS:
        for label, event_slice in (("full", None), ("slice", slice(1, 5))):
            rejection = require_expected_failure(
                f"append-sequential {array_name} {label}",
                lambda array_name=array_name, event_slice=event_slice: (
                    joint.read_measurement(
                        store,
                        array_name,
                        event_slice=event_slice,
                    )
                ),
                (ValueError,),
                SEQUENTIAL_GLOBAL_ERROR,
                checks,
            )
            global_rejection = rejection
    for array_name in ("gmi_waveform", "fl_waveform"):
        rejection = require_expected_failure(
            f"append-sequential {array_name} gather",
            lambda array_name=array_name: joint.read_measurement(
                store,
                array_name,
                event_indices=[4, 0, 3, 3, 1],
            ),
            (ValueError,),
            SEQUENTIAL_GLOBAL_ERROR,
            checks,
        )
        global_rejection = rejection
    if global_rejection is None:
        raise joint.CheckFailure("append-sequential global matrix did not run")
    checks.require(
        joint.tree_digest(root) == before,
        "append-sequential comparison mutated the store",
    )
    return strict_rejection, global_rejection


def lifecycle_rows(mode: str) -> list[dict[str, Any]]:
    """Return the writer-observer lifecycle proven by the C++ matrix."""
    if mode == ROUND_ROBIN_MODE:
        return [
            {
                "state": "open-gen0",
                "generation": 0,
                "rows": [0, 0],
                "sealed": [False, False],
            },
            {
                "state": "open-gen1",
                "generation": 1,
                "rows": [1, 1],
                "sealed": [False, False],
            },
            {
                "state": "open-gen2",
                "generation": 2,
                "rows": [3, 2],
                "sealed": [False, False],
            },
            {
                "state": "open-gen3",
                "generation": 3,
                "rows": [3, 2],
                "sealed": [True, True],
            },
            {
                "state": "finalized",
                "generation": 3,
                "rows": [3, 2],
                "sealed": [True, True],
            },
        ]
    if mode == APPEND_SEQUENTIAL_MODE:
        return [
            {
                "state": "open-gen0",
                "generation": 0,
                "rows": [0, 0],
                "sealed": [False, False],
            },
            {
                "state": "open-gen1",
                "generation": 1,
                "rows": [2, 0],
                "sealed": [True, False],
            },
            {
                "state": "open-gen2",
                "generation": 2,
                "rows": [2, 3],
                "sealed": [True, True],
            },
            {
                "state": "finalized",
                "generation": 2,
                "rows": [2, 3],
                "sealed": [True, True],
            },
        ]
    raise ValueError(f"unsupported sharding mode: {mode}")


def build_summary(
    round_robin_root: Path,
    append_sequential_root: Path,
    round_robin_wall_ms: Sequence[int],
    append_sequential_wall_ms: Sequence[int],
    checks: Any,
) -> dict[str, Any]:
    """Build bounded compatibility and observational evidence."""
    validate_round_robin(round_robin_root, checks)
    strict_rejection, global_rejection = validate_append_sequential(
        append_sequential_root,
        checks,
    )
    selected = select_sharding_mode(
        round_robin_compatible=True,
        append_sequential_compatible=False,
    )
    return {
        "schema": "analogboard-p0-s-sharding-comparison-v1",
        "step_id": "P0-S2",
        "identities": {
            "analogboard_baseline_commit": ANALOGBOARD_BASELINE_COMMIT,
            "gcsa_commit": joint.EXPECTED_GCSA_SNAPSHOT_COMMIT,
            "gcsa_package_tree_sha256": joint.EXPECTED_GCSA_PACKAGE_TREE_SHA256,
            "contract_id": joint.CONTRACT_ID,
            "public_kat_sha256": joint.EXPECTED_PUBLIC_KAT_SHA256,
        },
        "fixture": {
            "dataset_id": joint.DATASET_ID,
            "events": SYNTHETIC_EVENT_COUNT,
            "cycles": [2, 3],
            "arrays": len(MEASUREMENT_ARRAYS),
            "logical_partitions": SYNTHETIC_PARTITION_COUNT,
        },
        "modes": {
            ROUND_ROBIN_MODE: {
                "mapping": [
                    list(events)
                    for events in global_events_by_partition(ROUND_ROBIN_MODE)
                ],
                "lifecycle": lifecycle_rows(ROUND_ROBIN_MODE),
                "atomic_chunk_publications": 12,
                "same_coordinate_rewrites": 6,
                "compatibility": {
                    "strict": "pass",
                    "partition_local": "pass",
                    "full": "pass",
                    "slice": "pass",
                    "gather": "pass",
                },
            },
            APPEND_SEQUENTIAL_MODE: {
                "mapping": [
                    list(events)
                    for events in global_events_by_partition(
                        APPEND_SEQUENTIAL_MODE
                    )
                ],
                "lifecycle": lifecycle_rows(APPEND_SEQUENTIAL_MODE),
                "atomic_chunk_publications": 6,
                "same_coordinate_rewrites": 0,
                "compatibility": {
                    "strict": "expected-rejection",
                    "partition_local": "pass",
                    "full": "expected-rejection",
                    "slice": "expected-rejection",
                    "gather": "expected-rejection",
                    "strict_rejection": strict_rejection,
                    "global_rejection": global_rejection,
                },
            },
        },
        "observational_metrics": {
            "selection_basis": False,
            "timer_scope": "VsDevCmd wrapper plus generator process",
            "alternating_order": [
                ROUND_ROBIN_MODE,
                APPEND_SEQUENTIAL_MODE,
                APPEND_SEQUENTIAL_MODE,
                ROUND_ROBIN_MODE,
                ROUND_ROBIN_MODE,
                APPEND_SEQUENTIAL_MODE,
            ],
            "wall_time": {
                ROUND_ROBIN_MODE: summarize_wall_times(round_robin_wall_ms),
                APPEND_SEQUENTIAL_MODE: summarize_wall_times(
                    append_sequential_wall_ms
                ),
            },
            "store_metrics": {
                ROUND_ROBIN_MODE: collect_directory_metrics(round_robin_root),
                APPEND_SEQUENTIAL_MODE: collect_directory_metrics(
                    append_sequential_root
                ),
            },
        },
        "verification": {
            "positive_checks": checks.positive,
            "expected_rejections": checks.negative,
        },
        "decision": {
            "selected": selected,
            "basis": "sole compatibility with accepted strict/full/slice/gather reader",
            "performance_used_for_selection": False,
            "append_sequential_in_frozen_v1": False,
        },
        "content_policy": {
            "measurement_payload_included": False,
            "encrypted_wire_bytes_included": False,
            "nonce_bytes_included": False,
            "observational_metrics_are_production_guarantees": False,
        },
    }


def parse_args() -> argparse.Namespace:
    """Parse explicit ignored roots and three observations per mode."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--round-robin-store", type=Path, required=True)
    parser.add_argument("--append-sequential-store", type=Path, required=True)
    parser.add_argument(
        "--round-robin-wall-ms",
        type=int,
        action="append",
        required=True,
    )
    parser.add_argument(
        "--append-sequential-wall-ms",
        type=int,
        action="append",
        required=True,
    )
    parser.add_argument("--evidence-manifest", type=Path, required=True)
    parser.add_argument("--expected-evidence", type=Path, required=True)
    return parser.parse_args()


def fail(message: str) -> NoReturn:
    """Exit with one stable comparison failure surface."""
    raise SystemExit(f"sharding_comparison failed: {message}")


def main() -> int:
    """Run accepted-gcsa compatibility and bounded observations."""
    args = parse_args()
    checks = joint.Checks()
    try:
        before_snapshot = joint.require_accepted_gcsa_snapshot(
            joint.gcsa_snapshot_root()
        )
        repository_root = verify_evidence_manifest(args.evidence_manifest)
        summary = build_summary(
            args.round_robin_store,
            args.append_sequential_store,
            args.round_robin_wall_ms,
            args.append_sequential_wall_ms,
            checks,
        )
        verify_expected_evidence(
            args.expected_evidence,
            summary,
            repository_root,
        )
        if joint.gcsa_snapshot_digest(joint.gcsa_snapshot_root()) != before_snapshot:
            raise joint.CheckFailure("accepted gcsa snapshot was mutated")
    except (OSError, RuntimeError, TypeError, ValueError) as exc:
        fail(str(exc))
    print(
        "sharding_evidence="
        + json.dumps(
            summary,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        )
    )
    print(
        f"sharding_positive_checks={checks.positive} "
        f"expected_rejections={checks.negative} selected={ROUND_ROBIN_MODE} "
        "status=pass"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
