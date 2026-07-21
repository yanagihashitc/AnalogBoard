#!/usr/bin/env python3
"""Validate the ignored synthetic store with an immutable gcsa snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import tempfile
from collections.abc import Callable, Mapping, Sequence
from copy import deepcopy
from pathlib import Path
from typing import Any, NamedTuple, NoReturn

import numpy as np
import gcsa

from gcsa.data_model.visibility import DatasetNotFinalizedError
from gcsa.store._zarr_aead import (
    AEAD_HEADER_SIZE,
    AEAD_MAGIC,
    AEAD_NONCE_SIZE,
    AeadChunkContext,
    decrypt_chunk,
    encrypt_chunk,
)
from gcsa.store.schema import (
    ARRAY_WIRE_CANDIDATES,
    FL_CHANNEL_ORDER,
    GMI_CHANNEL_ORDER,
    PULSE_FEATURE_COLUMNS,
    STORE_FORMAT_MARKER,
    StoreContractValidationError,
    validate_analogboard_store,
)
from gcsa.store.zarr_store import ZarrStore, ZarrStoreConfig

DATASET_ID = "tube_1"
CONTRACT_ID = "gcsa-store-a4a-rc1"
SYNTHETIC_EVENT_COUNT = 5
SYNTHETIC_PARTITION_COUNT = 2
PULSE_FEATURE_COUNT = len(PULSE_FEATURE_COLUMNS)
WAVEFORM_SAMPLE_COUNT = 2400
ADC_MAXIMUM = 16383
ARRAYS = tuple(ARRAY_WIRE_CANDIDATES)
ARRAY_DTYPES = {
    "pulse_features": "<f8",
    "gmi_waveform": "<u2",
    "fl_waveform": "<u2",
}
CHUNK_KEYS = {
    name: ".".join("0" for _ in range(candidate.rank))
    for name, candidate in ARRAY_WIRE_CANDIDATES.items()
}
EXPECTED_SHAPES = {
    name: (SYNTHETIC_EVENT_COUNT, *candidate.trailing_shape)
    for name, candidate in ARRAY_WIRE_CANDIDATES.items()
}
GLOBAL_EVENTS_BY_PARTITION = tuple(
    tuple(range(partition, SYNTHETIC_EVENT_COUNT, SYNTHETIC_PARTITION_COUNT))
    for partition in range(SYNTHETIC_PARTITION_COUNT)
)
FINAL_PARTITION_ROWS = tuple(map(len, GLOBAL_EVENTS_BY_PARTITION))
ENCRYPTED_CHUNK_COUNT = len(ARRAYS) * SYNTHETIC_PARTITION_COUNT
LIFECYCLE_ROWS = {
    0: (0, 0),
    1: (1, 1),
    2: FINAL_PARTITION_ROWS,
    3: FINAL_PARTITION_ROWS,
}
GCSA_SNAPSHOT_EXCLUDED_DIRECTORIES = frozenset(
    {"__pycache__", ".git", ".hg", ".svn"}
)
GCSA_SNAPSHOT_EXCLUDED_SUFFIXES = frozenset({".pyc", ".pyo"})
EXPECTED_GCSA_SNAPSHOT_COMMIT = "20689a991697217518ec2ff15aaaa2533b169eb0"
EXPECTED_GCSA_PACKAGE_TREE_SHA256 = (
    "c63c79c4add3a8034cd1486921470818ad71d024ace1e8e356ae4f8dbf396d14"
)
EXPECTED_GOLDEN_SCHEMA = "analogboard-p0-s-joint-golden-v1"
EXPECTED_GOLDEN_DATE = "2026-07-22"
EXPECTED_GOLDEN_BASE_COMMIT = "01103017c55aed109b450bc81b0af171726a6c91"
EXPECTED_GOLDEN_COMMAND = "scripts/zarr-roundtrip/run-focused-verification.sh joint"
EXPECTED_GOLDEN_RELATIVE_PATH = Path(
    "docs/reference/zarr-store-contract/phase0-roundtrip/"
    "joint-roundtrip-golden.json"
)
EXPECTED_GOLDEN_SOURCE_PATHS = (
    "prototypes/zarr-store-roundtrip/include/p0s/minimal_zarr_writer.h",
    "prototypes/zarr-store-roundtrip/include/p0s/store_contract.h",
    "prototypes/zarr-store-roundtrip/scripts/validate_gcsa_roundtrip.py",
    "prototypes/zarr-store-roundtrip/src/minimal_zarr_writer.cpp",
    "prototypes/zarr-store-roundtrip/tests/test_validate_gcsa_roundtrip.py",
    "scripts/zarr-roundtrip/run-focused-verification.sh",
)
EXPECTED_GCSA_CONTAINER_ID = (
    "d141d00e5edb0bd17ee37836340a4315343019d32db4f9197322e9a3a5c9e1d8"
)
EXPECTED_GCSA_IMAGE_ID = (
    "sha256:e65e9f8b0ffafef5b5d2b9711c9a3411649ae80fd036cc79f0febb80b4c0b06e"
)
EXPECTED_PUBLIC_KAT_SHA256 = (
    "cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa"
)


class CheckFailure(RuntimeError):
    """Raised when the isolated roundtrip does not match its fixed contract."""


class StoreEvidence(NamedTuple):
    """Nonce-aware and nonce-excluded evidence for one finalized store."""

    canonical_digest: str
    wire_digest: str
    nonce_keys: frozenset[tuple[int, bytes]]


class Checks:
    """Count positive and fail-loud negative assertions."""

    def __init__(self) -> None:
        self.positive = 0
        self.negative = 0

    def require(self, condition: bool, message: str) -> None:
        """Require one positive invariant."""
        self.positive += 1
        if not condition:
            raise CheckFailure(message)

    def require_failure(
        self,
        label: str,
        action: Callable[[], object],
        expected: tuple[type[Exception], ...],
    ) -> None:
        """Require one negative case to fail without returning output."""
        sentinel = object()
        output: object = sentinel
        try:
            output = action()
        except expected:
            if output is not sentinel:
                raise CheckFailure(f"{label}: exposed partial output")
            self.negative += 1
            return
        except Exception as exc:
            raise CheckFailure(
                f"{label}: unexpected failure type {type(exc).__name__}: {exc}"
            ) from exc
        raise CheckFailure(f"{label}: accepted invalid input")


def tree_digest(
    root: Path,
    *,
    exclude: Callable[[Path], bool] | None = None,
) -> str:
    """Return a deterministic digest without writing cache state."""
    digest = hashlib.sha256()
    for path in sorted(root.rglob("*")):
        relative_path = path.relative_to(root)
        if exclude is not None and exclude(relative_path):
            continue
        relative = relative_path.as_posix().encode("utf-8")
        digest.update(len(relative).to_bytes(4, "big"))
        digest.update(relative)
        if path.is_symlink():
            raise CheckFailure(f"store contains a symlink: {path}")
        if path.is_file():
            digest.update(b"F")
            with path.open("rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(block)
        elif path.is_dir():
            digest.update(b"D")
        else:
            raise CheckFailure(f"store contains an unsupported entry: {path}")
    return digest.hexdigest()


def gcsa_snapshot_root() -> Path:
    """Return the installed gcsa package directory and nothing above it."""
    module_file = getattr(gcsa, "__file__", None)
    if module_file is None:
        raise CheckFailure("gcsa package has no filesystem location")
    return Path(module_file).resolve().parent


def gcsa_snapshot_digest(root: Path) -> str:
    """Digest immutable gcsa package content without transient metadata."""

    def excluded(relative: Path) -> bool:
        return (
            any(
                part in GCSA_SNAPSHOT_EXCLUDED_DIRECTORIES
                for part in relative.parts
            )
            or relative.suffix.lower() in GCSA_SNAPSHOT_EXCLUDED_SUFFIXES
        )

    return tree_digest(root, exclude=excluded)


def require_accepted_gcsa_snapshot(root: Path) -> str:
    """Require the imported package to match the accepted gcsa source tree."""
    actual = gcsa_snapshot_digest(root)
    if actual != EXPECTED_GCSA_PACKAGE_TREE_SHA256:
        raise CheckFailure(
            "gcsa snapshot identity mismatch: expected commit "
            f"{EXPECTED_GCSA_SNAPSHOT_COMMIT} package digest "
            f"{EXPECTED_GCSA_PACKAGE_TREE_SHA256}, got {actual}"
        )
    return actual


def partition_global_events(partition: int) -> tuple[int, ...]:
    """Return global event indices for one fixed round-robin partition."""
    if type(partition) is not int or not 0 <= partition < SYNTHETIC_PARTITION_COUNT:
        raise ValueError(f"partition must be 0 or 1, got {partition!r}")
    return GLOBAL_EVENTS_BY_PARTITION[partition]


def synthetic_feature_bits(global_event: int, feature_index: int) -> int:
    """Mirror the C++ five-event fixture as exact float64 bit patterns."""
    if not 0 <= global_event < SYNTHETIC_EVENT_COUNT:
        raise ValueError(f"global event is outside the fixture: {global_event}")
    if not 0 <= feature_index < PULSE_FEATURE_COUNT:
        raise ValueError(f"feature index is outside the fixture: {feature_index}")
    if feature_index == 0 and global_event < 2:
        return 0x8000000000000000 if global_event == 0 else 0
    if feature_index == 1 and global_event < 2:
        return 1 if global_event == 0 else 0x8000000000000001
    if feature_index == 2 and global_event < 2:
        return 0x7FEFFFFFFFFFFFFF if global_event == 0 else 0xFFEFFFFFFFFFFFFF
    event_scale = 1.0 if global_event < 2 else float(global_event)
    magnitude = (float(feature_index) + 0.25) * event_scale
    value = magnitude if global_event % 2 == 0 else -magnitude
    return struct.unpack("<Q", struct.pack("<d", value))[0]


def synthetic_feature_value(global_event: int, feature_index: int) -> float:
    """Return one fixture feature value without losing its signed-zero bits."""
    return struct.unpack(
        "<d",
        struct.pack("<Q", synthetic_feature_bits(global_event, feature_index)),
    )[0]


def synthetic_waveform_value(
    array_name: str,
    global_event: int,
    channel: int,
    sample: int,
) -> int:
    """Mirror the C++ 14-bit synthetic waveform coordinate function."""
    channels = {
        "gmi_waveform": len(GMI_CHANNEL_ORDER),
        "fl_waveform": len(FL_CHANNEL_ORDER),
    }.get(array_name)
    if channels is None:
        raise ValueError(f"unsupported waveform array: {array_name}")
    if not 0 <= global_event < SYNTHETIC_EVENT_COUNT:
        raise ValueError(f"global event is outside the fixture: {global_event}")
    if not 0 <= channel < channels:
        raise ValueError(f"channel is outside the fixture: {channel}")
    if not 0 <= sample < WAVEFORM_SAMPLE_COUNT:
        raise ValueError(f"sample is outside the fixture: {sample}")
    offset = 1000 if array_name == "gmi_waveform" else 30000
    return (
        offset
        + global_event * 10000
        + channel * WAVEFORM_SAMPLE_COUNT
        + sample
    ) & ADC_MAXIMUM


def feature_bits() -> np.ndarray:
    """Return the exact five-event float64 fixture as uint64 bit patterns."""
    bits = np.empty(
        (SYNTHETIC_EVENT_COUNT, PULSE_FEATURE_COUNT),
        dtype=np.uint64,
    )
    for event in range(SYNTHETIC_EVENT_COUNT):
        for feature in range(PULSE_FEATURE_COUNT):
            bits[event, feature] = synthetic_feature_bits(event, feature)
    return bits


def waveform(array_name: str, channels: int) -> np.ndarray:
    """Return one five-event uint16 waveform fixture in global event order."""
    offset = 1000 if array_name == "gmi_waveform" else 30000
    events = (
        np.arange(SYNTHETIC_EVENT_COUNT, dtype=np.uint32)[:, None, None] * 10000
    )
    channel_offsets = (
        np.arange(channels, dtype=np.uint32)[None, :, None]
        * WAVEFORM_SAMPLE_COUNT
    )
    samples = np.arange(WAVEFORM_SAMPLE_COUNT, dtype=np.uint32)[None, None, :]
    return ((offset + events + channel_offsets + samples) & ADC_MAXIMUM).astype(
        np.uint16
    )


def lifecycle_metadata(
    finalized_meta: Mapping[str, Any],
    generation: int,
) -> dict[str, Any]:
    """Create one disposable round-robin open-state metadata snapshot."""
    try:
        rows = LIFECYCLE_ROWS[generation]
    except KeyError as exc:
        raise ValueError(f"unsupported lifecycle generation: {generation}") from exc
    payload = deepcopy(dict(finalized_meta))
    sealed = generation == 3
    committed_events = sum(rows)
    payload["n_events"] = committed_events
    payload["events_per_partition"] = list(rows)
    payload["status"] = "open"
    payload["finalized_at"] = None
    payload["write_generation"] = generation
    for array_name in ARRAYS:
        entries = payload["partition_manifests"][array_name]
        for partition in range(SYNTHETIC_PARTITION_COUNT):
            entries[partition]["row_count"] = rows[partition]
            entries[partition]["sealed"] = sealed
    for feature_index, feature in enumerate(payload["features"]):
        if committed_events == 0:
            feature["range_min"] = None
            feature["range_max"] = None
            continue
        values = [
            synthetic_feature_value(event, feature_index)
            for event in range(committed_events)
        ]
        feature["range_min"] = min(values)
        feature["range_max"] = max(values)
    return payload


def validate_ordered_lifecycle_transitions(
    finalized_meta: Mapping[str, Any],
    checks: Checks,
    transition_validator: Callable[[Mapping[str, Any] | None, Mapping[str, Any]], Any],
) -> None:
    """Require every ordered gen0-to-finalized Contract RC transition."""
    open_snapshots = [
        lifecycle_metadata(finalized_meta, generation) for generation in range(4)
    ]
    states: list[Mapping[str, Any]] = [*open_snapshots, finalized_meta]
    expected_kinds = (
        "create",
        "content",
        "content",
        "content",
        "open_to_finalized",
    )
    previous: Mapping[str, Any] | None = None
    for current, expected_kind in zip(states, expected_kinds, strict=True):
        transition = transition_validator(previous, current)
        checks.require(
            transition.kind == expected_kind,
            f"ordered lifecycle transition drift: expected {expected_kind}",
        )
        previous = current


def canonical_evidence_digest(
    metadata: Mapping[str, Any],
    zarrays: Mapping[str, Any],
    decoded_digests: Mapping[str, Any],
    *,
    nonce_keys: Sequence[tuple[int, bytes]] = (),
    wire_digests: Sequence[str] = (),
) -> str:
    """Hash semantic evidence while intentionally excluding nonce/wire bytes."""
    del nonce_keys, wire_digests
    payload = {
        "metadata": metadata,
        "zarrays": zarrays,
        "decoded": decoded_digests,
    }
    serialized = json.dumps(
        payload,
        ensure_ascii=False,
        allow_nan=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(serialized).hexdigest()


def build_joint_evidence_summary(
    evidence: StoreEvidence,
    checks: Checks,
) -> dict[str, Any]:
    """Return bounded evidence with no measurement, nonce, or wire bytes."""
    return {
        "schema": "analogboard-p0-s-joint-evidence-v1",
        "contract_id": CONTRACT_ID,
        "gcsa_snapshot_commit": EXPECTED_GCSA_SNAPSHOT_COMMIT,
        "fixture": {
            "dataset_id": DATASET_ID,
            "event_count": SYNTHETIC_EVENT_COUNT,
            "n_partitions": SYNTHETIC_PARTITION_COUNT,
            "sharding": "round-robin",
            "events_per_partition": list(FINAL_PARTITION_ROWS),
            "global_events_by_partition": [
                list(partition_global_events(partition))
                for partition in range(SYNTHETIC_PARTITION_COUNT)
            ],
            "arrays": {
                name: {
                    "dtype": ARRAY_DTYPES[name],
                    "shape": list(EXPECTED_SHAPES[name]),
                }
                for name in ARRAYS
            },
        },
        "verification": {
            "canonical_evidence_sha256": evidence.canonical_digest,
            "positive_checks": checks.positive,
            "negative_checks": checks.negative,
            "encrypted_chunk_count": ENCRYPTED_CHUNK_COUNT,
            "raw_zarr_reads_rejected": ENCRYPTED_CHUNK_COUNT,
            "nonce_sets_disjoint": True,
            "encrypted_wires_differ": True,
            "gcsa_recompute_mismatch": True,
        },
        "content_policy": {
            "measurement_payload_included": False,
            "nonce_bytes_included": False,
            "encrypted_wire_bytes_included": False,
            "public_kat_only": True,
        },
    }


def require_expected_joint_evidence(
    path: Path,
    actual: Mapping[str, Any],
) -> None:
    """Require tracked provenance, source hashes, and runtime evidence."""
    document = load_strict_json_object(path)
    expected_keys = {
        "schema",
        "generated_at",
        "result",
        "generator",
        "gcsa",
        "public_kat",
        "joint_evidence",
    }
    if set(document) != expected_keys:
        raise CheckFailure("expected evidence top-level structure drift")
    if (
        document["schema"] != EXPECTED_GOLDEN_SCHEMA
        or document["generated_at"] != EXPECTED_GOLDEN_DATE
        or document["result"] != "pass"
    ):
        raise CheckFailure("expected evidence identity drift")

    generator = document["generator"]
    if not isinstance(generator, dict) or set(generator) != {
        "base_commit",
        "command",
        "source_sha256",
    }:
        raise CheckFailure("expected evidence generator provenance drift")
    if (
        generator["base_commit"] != EXPECTED_GOLDEN_BASE_COMMIT
        or generator["command"] != EXPECTED_GOLDEN_COMMAND
    ):
        raise CheckFailure("expected evidence generator identity drift")
    source_hashes = generator["source_sha256"]
    if not isinstance(source_hashes, dict) or set(source_hashes) != set(
        EXPECTED_GOLDEN_SOURCE_PATHS
    ):
        raise CheckFailure("expected evidence source manifest drift")

    resolved_path = path.resolve()
    try:
        repository_root = resolved_path.parents[4]
    except IndexError as exc:
        raise CheckFailure("expected evidence path is outside a repository") from exc
    if resolved_path != repository_root / EXPECTED_GOLDEN_RELATIVE_PATH:
        raise CheckFailure("expected evidence path is not the tracked golden")
    for relative in EXPECTED_GOLDEN_SOURCE_PATHS:
        source = repository_root / relative
        if not source.is_file() or source.is_symlink():
            raise CheckFailure(f"expected evidence source is absent: {relative}")
        actual_sha256 = hashlib.sha256(source.read_bytes()).hexdigest()
        if source_hashes[relative] != actual_sha256:
            raise CheckFailure(f"expected evidence source SHA-256 drift: {relative}")

    expected_gcsa = {
        "commit": EXPECTED_GCSA_SNAPSHOT_COMMIT,
        "contract_id": CONTRACT_ID,
        "package_tree_sha256": EXPECTED_GCSA_PACKAGE_TREE_SHA256,
        "container_id": EXPECTED_GCSA_CONTAINER_ID,
        "image_id": EXPECTED_GCSA_IMAGE_ID,
    }
    if document["gcsa"] != expected_gcsa:
        raise CheckFailure("expected evidence gcsa provenance drift")
    expected_public_kat = {
        "classification": "public-packaged-kat",
        "sha256": EXPECTED_PUBLIC_KAT_SHA256,
    }
    if document["public_kat"] != expected_public_kat:
        raise CheckFailure("expected evidence public KAT provenance drift")

    expected = document["joint_evidence"]
    if not isinstance(expected, dict):
        raise CheckFailure("expected evidence has no joint_evidence object")
    if expected != actual:
        raise CheckFailure("joint evidence differs from tracked golden")


def reject_json_constant(value: str) -> NoReturn:
    """Reject Python's non-standard JSON constants."""
    raise CheckFailure(f"expected evidence contains non-finite constant: {value}")


def object_without_duplicates(
    pairs: Sequence[tuple[str, Any]],
) -> dict[str, Any]:
    """Build one JSON object while rejecting ambiguous duplicate keys."""
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise CheckFailure(f"expected evidence contains duplicate key: {key}")
        result[key] = value
    return result


def load_strict_json_object(path: Path) -> dict[str, Any]:
    """Load one UTF-8 JSON object without duplicate or non-finite values."""
    try:
        document = json.loads(
            path.read_text(encoding="utf-8"),
            parse_constant=reject_json_constant,
            object_pairs_hook=object_without_duplicates,
        )
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise CheckFailure(f"invalid expected evidence JSON: {path}") from exc
    if not isinstance(document, dict):
        raise CheckFailure("expected evidence JSON must contain an object")
    return document


def store_config() -> ZarrStoreConfig:
    """Return the immutable encrypted-reader configuration."""
    return ZarrStoreConfig(encrypted_measurement_chunks=True)


def array_path(root: Path, array_name: str, partition: int) -> Path:
    """Return one strict partitioned measurement array path."""
    return (
        root
        / "datasets"
        / DATASET_ID
        / array_name
        / f"partition_{partition}.zarr"
    )


def chunk_path(root: Path, array_name: str, partition: int) -> Path:
    """Return one strict encrypted chunk path."""
    return array_path(root, array_name, partition) / CHUNK_KEYS[array_name]


def context(array_name: str, partition: int) -> AeadChunkContext:
    """Return the exact accepted AAD context for one synthetic chunk."""
    return AeadChunkContext(
        dataset_id=DATASET_ID,
        array_rel_path=f"{array_name}/partition_{partition}.zarr",
        chunk_key=CHUNK_KEYS[array_name],
        expected_chunk_rank=ARRAY_WIRE_CANDIDATES[array_name].rank,
    )


def expected_arrays() -> dict[str, np.ndarray]:
    """Return all three arrays in authoritative global event order."""
    return {
        "pulse_features": feature_bits().view(np.float64),
        "gmi_waveform": waveform("gmi_waveform", len(GMI_CHANNEL_ORDER)),
        "fl_waveform": waveform("fl_waveform", len(FL_CHANNEL_ORDER)),
    }


def read_measurement(
    store: ZarrStore,
    array_name: str,
    *,
    event_slice: slice | None = None,
    event_indices: list[int] | None = None,
    partition: int | None = None,
) -> np.ndarray:
    """Read one measurement array through its supported product API."""
    if array_name == "pulse_features":
        if event_indices is not None:
            raise ValueError("pulse feature reader has no gather API")
        return store.get_pulse_features(
            DATASET_ID,
            event_slice=event_slice,
            partition=partition,
        )
    reader = (
        store.get_gmi_waveform
        if array_name == "gmi_waveform"
        else store.get_fl_waveform
    )
    return reader(
        DATASET_ID,
        event_slice=event_slice,
        event_indices=event_indices,
        partition=partition,
    )


def arrays_equal(array_name: str, actual: np.ndarray, expected: np.ndarray) -> bool:
    """Compare pulse bits exactly and integer waveforms by value."""
    if actual.shape != expected.shape or actual.dtype != expected.dtype:
        return False
    if array_name == "pulse_features":
        return bool(np.array_equal(actual.view(np.uint64), expected.view(np.uint64)))
    return bool(np.array_equal(actual, expected))


def require_report(report: Any, checks: Checks, label: str) -> None:
    """Require the immutable strict Contract RC identity."""
    checks.require(report.contract_id == CONTRACT_ID, f"{label} contract")
    checks.require(report.profile == "strict", f"{label} profile")
    checks.require(report.dataset_ids == (DATASET_ID,), f"{label} dataset")
    checks.require(
        report.encrypted_chunk_count == ENCRYPTED_CHUNK_COUNT,
        f"{label} chunk count",
    )


def require_open_product_rejected(
    store: ZarrStore,
    checks: Checks,
    label: str,
) -> None:
    """Require every normal product read to reject one open dataset."""
    checks.require(store.readonly, f"{label}: reader is not read-only")
    checks.require(store.list_datasets() == [], f"{label}: open dataset is visible")
    readers = {
        "pulse": store.get_pulse_features,
        "gmi": store.get_gmi_waveform,
        "fl": store.get_fl_waveform,
    }
    for reader_name, reader in readers.items():
        checks.require_failure(
            f"{label} {reader_name} product read",
            lambda reader=reader: reader(DATASET_ID),
            (DatasetNotFinalizedError,),
        )


def array_descriptor(array: np.ndarray) -> dict[str, Any]:
    """Describe decoded content without serializing large arrays into evidence."""
    contiguous = np.ascontiguousarray(array)
    return {
        "shape": list(contiguous.shape),
        "dtype": contiguous.dtype.str,
        "sha256": hashlib.sha256(contiguous.tobytes(order="C")).hexdigest(),
    }


def collect_store_evidence(
    root: Path,
    report: Any,
    decoded_views: Mapping[str, np.ndarray],
) -> StoreEvidence:
    """Collect semantic identity plus separate encrypted-wire observations."""
    dataset_root = root / "datasets" / DATASET_ID
    raw_meta = json.loads((dataset_root / "meta.json").read_text(encoding="utf-8"))
    marker = json.loads((root / STORE_FORMAT_MARKER).read_text(encoding="utf-8"))
    zarrays: dict[str, Any] = {}
    wire_hasher = hashlib.sha256()
    nonce_keys: set[tuple[int, bytes]] = set()
    wire_digests: list[str] = []
    for array_name in ARRAYS:
        for partition in range(SYNTHETIC_PARTITION_COUNT):
            path = array_path(root, array_name, partition)
            relative = path.relative_to(dataset_root).as_posix()
            zarrays[relative] = json.loads(
                (path / ".zarray").read_text(encoding="utf-8")
            )
            wire_path = chunk_path(root, array_name, partition)
            wire = wire_path.read_bytes()
            wire_relative = wire_path.relative_to(root).as_posix().encode("utf-8")
            wire_hasher.update(len(wire_relative).to_bytes(4, "big"))
            wire_hasher.update(wire_relative)
            wire_hasher.update(wire)
            wire_digests.append(hashlib.sha256(wire).hexdigest())
            nonce_start = len(AEAD_MAGIC) + 2
            nonce_keys.add(
                (
                    wire[len(AEAD_MAGIC) + 1],
                    wire[nonce_start : nonce_start + AEAD_NONCE_SIZE],
                )
            )
    metadata = {
        "marker": marker,
        "meta": raw_meta,
        "report": {
            "contract_id": report.contract_id,
            "profile": report.profile,
            "producer": report.producer,
            "capabilities": sorted(report.capabilities),
            "dataset_ids": list(report.dataset_ids),
            "encrypted_chunk_count": report.encrypted_chunk_count,
        },
    }
    decoded = {
        name: array_descriptor(array) for name, array in sorted(decoded_views.items())
    }
    return StoreEvidence(
        canonical_evidence_digest(
            metadata,
            zarrays,
            decoded,
            nonce_keys=tuple(nonce_keys),
            wire_digests=tuple(wire_digests),
        ),
        wire_hasher.hexdigest(),
        frozenset(nonce_keys),
    )


def validate_finalized_reader(
    root: Path,
    report: Any,
    checks: Checks,
    label: str,
) -> StoreEvidence:
    """Validate full, partition, slice, gather, and no-recompute behavior."""
    store = ZarrStore(root, config=store_config(), readonly=True)
    checks.require(store.readonly, f"{label}: finalized reader is not read-only")
    checks.require(
        store.list_datasets() == [DATASET_ID],
        f"{label}: finalized visibility drift",
    )
    meta = store.get_meta(DATASET_ID)
    expected = expected_arrays()
    decoded_views: dict[str, np.ndarray] = {}

    for array_name in ARRAYS:
        full = read_measurement(store, array_name)
        decoded_views[f"{array_name}/global"] = full
        checks.require(
            arrays_equal(array_name, full, expected[array_name]),
            f"{label}: {array_name} full/global order drift",
        )
        for partition in range(SYNTHETIC_PARTITION_COUNT):
            partition_view = read_measurement(
                store,
                array_name,
                partition=partition,
            )
            decoded_views[f"{array_name}/partition_{partition}"] = partition_view
            partition_expected = expected[array_name][
                list(partition_global_events(partition))
            ]
            checks.require(
                arrays_equal(array_name, partition_view, partition_expected),
                f"{label}: {array_name} partition {partition} mapping drift",
            )
        sliced = read_measurement(store, array_name, event_slice=slice(1, 5))
        decoded_views[f"{array_name}/slice_1_5"] = sliced
        checks.require(
            arrays_equal(array_name, sliced, expected[array_name][1:5]),
            f"{label}: {array_name} cross-partition slice drift",
        )

    gather_indices = [4, 0, 3, 3, 1]
    for array_name in ("gmi_waveform", "fl_waveform"):
        gathered = read_measurement(
            store,
            array_name,
            event_indices=gather_indices,
        )
        decoded_views[f"{array_name}/gather_4_0_3_3_1"] = gathered
        checks.require(
            arrays_equal(array_name, gathered, expected[array_name][gather_indices]),
            f"{label}: {array_name} gather order/duplicate drift",
        )

    checks.require(
        tuple(feature.name for feature in meta.features) == PULSE_FEATURE_COLUMNS,
        f"{label}: feature column order drift",
    )
    checks.require(
        tuple(channel.name for channel in meta.channels[:8]) == FL_CHANNEL_ORDER,
        f"{label}: FL channel order drift",
    )
    checks.require(
        tuple(channel.name for channel in meta.channels[8:]) == GMI_CHANNEL_ORDER,
        f"{label}: GMI channel order drift",
    )
    checks.require(meta.status == "finalized", f"{label}: finalized status drift")
    checks.require(meta.finalized_at is not None, f"{label}: finalized_at absent")
    checks.require(meta.write_generation == 3, f"{label}: generation drift")
    checks.require(
        meta.n_events == SYNTHETIC_EVENT_COUNT,
        f"{label}: n_events drift",
    )
    checks.require(
        meta.events_per_partition == list(FINAL_PARTITION_ROWS),
        f"{label}: partition rows drift",
    )
    checks.require(
        meta.extra.get("partition_sharding") == "round-robin",
        f"{label}: sharding marker drift",
    )
    pulse = decoded_views["pulse_features/global"]
    for feature_index, feature in enumerate(meta.features):
        values = pulse[:, feature_index]
        checks.require(
            feature.range_min == float(np.min(values)),
            f"{label}: feature minimum drift: {feature.name}",
        )
        checks.require(
            feature.range_max == float(np.max(values)),
            f"{label}: feature maximum drift: {feature.name}",
        )
    for channel in meta.channels:
        checks.require(
            channel.range_min == 0 and channel.range_max == ADC_MAXIMUM,
            f"{label}: ADC range drift: {channel.name}",
        )
    for array_name in ARRAYS:
        entries = meta.partition_manifests[array_name]
        checks.require(
            [(entry.partition, entry.row_count, entry.sealed) for entry in entries]
            == [
                (partition, row_count, True)
                for partition, row_count in enumerate(FINAL_PARTITION_ROWS)
            ],
            f"{label}: manifest alignment drift: {array_name}",
        )

    from gcsa.transform.pulse_feature_extractor import PulseFeatureExtractor

    extracted = PulseFeatureExtractor().extract_all(
        decoded_views["fl_waveform/global"]
    )
    checks.require(
        all(column in extracted for column in PULSE_FEATURE_COLUMNS),
        f"{label}: recompute omitted a pulse feature",
    )
    recomputed = np.column_stack(
        [extracted[column] for column in PULSE_FEATURE_COLUMNS]
    ).astype(np.float64, copy=False)
    checks.require(
        recomputed.shape == EXPECTED_SHAPES["pulse_features"],
        f"{label}: recompute shape drift",
    )
    checks.require(
        not np.array_equal(recomputed.view(np.uint64), pulse.view(np.uint64)),
        f"{label}: stored authoritative features equal gcsa recomputation",
    )
    return collect_store_evidence(root, report, decoded_views)


def validate_raw_zarr_unreadable(root: Path, checks: Checks) -> None:
    """Require metadata-only raw Zarr opens and six fail-loud chunk reads."""
    import zarr

    for array_name in ARRAYS:
        for partition in range(SYNTHETIC_PARTITION_COUNT):
            raw = zarr.open_array(
                str(array_path(root, array_name, partition)),
                mode="r",
            )
            expected_shape = (
                FINAL_PARTITION_ROWS[partition],
                *ARRAY_WIRE_CANDIDATES[array_name].trailing_shape,
            )
            checks.require(
                raw.shape == expected_shape,
                f"raw zarr metadata shape drift: {array_name} p{partition}",
            )
            checks.require_failure(
                f"raw zarr encrypted read: {array_name} p{partition}",
                lambda raw=raw: raw[:],
                (RuntimeError, ValueError),
            )


def validate_positive(
    open_root: Path,
    finalized_a_root: Path,
    finalized_b_root: Path,
    checks: Checks,
) -> StoreEvidence:
    """Validate strict schema, product visibility, ordering, and identity."""
    snapshot_root = gcsa_snapshot_root()
    before_snapshot = require_accepted_gcsa_snapshot(snapshot_root)
    roots = (open_root, finalized_a_root, finalized_b_root)
    before_trees = {root: tree_digest(root) for root in roots}

    open_report = validate_analogboard_store(open_root, mode="strict")
    final_a_report = validate_analogboard_store(finalized_a_root, mode="strict")
    final_b_report = validate_analogboard_store(finalized_b_root, mode="strict")
    require_report(open_report, checks, "open")
    require_report(final_a_report, checks, "finalized-a")
    require_report(final_b_report, checks, "finalized-b")

    open_store = ZarrStore(open_root, config=store_config(), readonly=True)
    require_open_product_rejected(open_store, checks, "terminal open")
    open_meta = open_store.get_meta(DATASET_ID)
    checks.require(open_meta.status == "open", "terminal open status drift")
    checks.require(open_meta.finalized_at is None, "terminal open finalized_at set")
    checks.require(open_meta.write_generation == 3, "terminal open generation drift")
    checks.require(
        open_meta.events_per_partition == list(FINAL_PARTITION_ROWS),
        "terminal open partition rows drift",
    )

    from gcsa.store._zarr_visibility import _read_open_dataset

    open_snapshot = _read_open_dataset(open_store, DATASET_ID)
    expected = expected_arrays()
    checks.require(
        arrays_equal(
            "pulse_features",
            open_snapshot.pulse_features,
            expected["pulse_features"],
        ),
        "terminal open diagnostic pulse drift",
    )
    checks.require(
        arrays_equal(
            "gmi_waveform",
            open_snapshot.gmi_waveform,
            expected["gmi_waveform"],
        ),
        "terminal open diagnostic GMI drift",
    )
    checks.require(
        arrays_equal(
            "fl_waveform",
            open_snapshot.fl_waveform,
            expected["fl_waveform"],
        ),
        "terminal open diagnostic FL drift",
    )
    checks.require(
        open_snapshot.write_generation == 3,
        "terminal open diagnostic generation drift",
    )

    evidence_a = validate_finalized_reader(
        finalized_a_root,
        final_a_report,
        checks,
        "finalized-a",
    )
    evidence_b = validate_finalized_reader(
        finalized_b_root,
        final_b_report,
        checks,
        "finalized-b",
    )
    validate_raw_zarr_unreadable(finalized_a_root, checks)
    checks.require(
        evidence_a.canonical_digest == evidence_b.canonical_digest,
        "two-run canonical decoded/meta identity drift",
    )
    checks.require(
        len(evidence_a.nonce_keys) == 6 and len(evidence_b.nonce_keys) == 6,
        "two-run evidence contains a repeated nonce within one store",
    )
    checks.require(
        evidence_a.nonce_keys.isdisjoint(evidence_b.nonce_keys),
        "two-run evidence reuses a key_id/nonce pair",
    )
    checks.require(
        evidence_a.wire_digest != evidence_b.wire_digest,
        "two-run encrypted wire unexpectedly remained byte-identical",
    )

    for root in roots:
        checks.require(tree_digest(root) == before_trees[root], f"read mutated {root}")
    checks.require(
        gcsa_snapshot_digest(snapshot_root) == before_snapshot,
        "accepted gcsa snapshot was mutated",
    )
    return evidence_a


def copied_store(source: Path, parent: Path, name: str) -> Path:
    """Copy one ignored generated store into a test-owned negative fixture."""
    target = parent / name
    shutil.copytree(source, target)
    return target


def mutate_byte(path: Path, index: int) -> None:
    """Flip one byte in a test-owned chunk fixture."""
    payload = bytearray(path.read_bytes())
    payload[index] ^= 0x01
    path.write_bytes(payload)


def strict_validate(root: Path) -> object:
    """Invoke the accepted strict validator for a negative fixture."""
    return validate_analogboard_store(root, mode="strict")


def write_json(path: Path, payload: Mapping[str, Any]) -> None:
    """Write deterministic JSON only inside a disposable fixture copy."""
    path.write_text(
        json.dumps(
            payload,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            indent=2,
        )
        + "\n",
        encoding="utf-8",
    )


def validate_lifecycle_dispositions(finalized_root: Path, checks: Checks) -> None:
    """Validate ordered states and metadata-lag cuts with physical rows ahead."""
    from gcsa.store._zarr_visibility import OpenDatasetReadError, _read_open_dataset
    from gcsa.store.state_contract import validate_candidate_d21_transition

    source_meta_path = finalized_root / "datasets" / DATASET_ID / "meta.json"
    source_meta = json.loads(source_meta_path.read_text(encoding="utf-8"))
    validate_ordered_lifecycle_transitions(
        source_meta,
        checks,
        validate_candidate_d21_transition,
    )
    expected = expected_arrays()
    with tempfile.TemporaryDirectory(prefix="analogboard-p0s-lifecycle-") as temp:
        parent = Path(temp)
        for generation in range(4):
            cut = copied_store(finalized_root, parent, f"generation-{generation}")
            cut_meta_path = cut / "datasets" / DATASET_ID / "meta.json"
            write_json(cut_meta_path, lifecycle_metadata(source_meta, generation))
            report = strict_validate(cut)
            require_report(report, checks, f"generation-{generation} open cut")
            store = ZarrStore(cut, config=store_config(), readonly=True)
            require_open_product_rejected(store, checks, f"generation-{generation}")
            meta = store.get_meta(DATASET_ID)
            checks.require(
                meta.write_generation == generation,
                f"generation-{generation}: typed generation drift",
            )
            checks.require(
                meta.events_per_partition == list(LIFECYCLE_ROWS[generation]),
                f"generation-{generation}: typed row drift",
            )
            if generation < 3:
                checks.require_failure(
                    f"generation-{generation} has no sealed prefix",
                    lambda store=store: _read_open_dataset(store, DATASET_ID),
                    (OpenDatasetReadError,),
                )
                continue
            snapshot = _read_open_dataset(store, DATASET_ID)
            checks.require(
                arrays_equal(
                    "pulse_features",
                    snapshot.pulse_features,
                    expected["pulse_features"],
                ),
                "generation-3 diagnostic pulse drift",
            )
            checks.require(
                arrays_equal(
                    "gmi_waveform",
                    snapshot.gmi_waveform,
                    expected["gmi_waveform"],
                ),
                "generation-3 diagnostic GMI drift",
            )
            checks.require(
                arrays_equal(
                    "fl_waveform",
                    snapshot.fl_waveform,
                    expected["fl_waveform"],
                ),
                "generation-3 diagnostic FL drift",
            )
            checks.require(
                snapshot.write_generation == 3,
                "generation-3 diagnostic generation drift",
            )


def validate_unknown_metadata_roundtrip(
    finalized_root: Path,
    checks: Checks,
) -> None:
    """Require unknown top-level fields to survive typed load/save unchanged."""
    with tempfile.TemporaryDirectory(prefix="analogboard-p0s-meta-") as temp:
        copied = copied_store(finalized_root, Path(temp), "unknown-additive")
        meta_path = copied / "datasets" / DATASET_ID / "meta.json"
        before = json.loads(meta_path.read_text(encoding="utf-8"))
        unknown_fields = {
            "future_scalar": 17,
            "future_list": ["a", "b"],
            "future_nested": {"writer": {"capabilities": ["x", "y"]}},
        }
        before.update(deepcopy(unknown_fields))
        write_json(meta_path, before)

        store = ZarrStore(copied, config=store_config(), readonly=False)
        typed = store.get_meta(DATASET_ID)
        checks.require(
            typed.extra_fields == unknown_fields,
            "typed metadata did not capture unknown additive fields",
        )
        store.save_meta(typed)
        after = json.loads(meta_path.read_text(encoding="utf-8"))
        reloaded = store.get_meta(DATASET_ID)
        checks.require(
            after == before,
            "meta load/save changed known or unknown fields",
        )
        checks.require(
            reloaded.extra_fields == unknown_fields,
            "meta reload lost unknown additive fields",
        )
        checks.require(
            "extra_fields" not in after,
            "typed extra_fields leaked as a non-contract wire field",
        )
        require_report(strict_validate(copied), checks, "unknown metadata roundtrip")


def validate_negatives(finalized_root: Path, checks: Checks) -> None:
    """Prove authentication, AAD, key, truncation, and visibility failures."""
    pulse_chunk = chunk_path(finalized_root, "pulse_features", 0)
    pulse_wire = pulse_chunk.read_bytes()
    checks.require_failure(
        "wrong key",
        lambda: decrypt_chunk(
            pulse_wire,
            context("pulse_features", 0),
            keys={1: bytes([0xA5]) * 32},
        ),
        (ValueError,),
    )

    with tempfile.TemporaryDirectory(prefix="analogboard-p0s-negative-") as temp:
        parent = Path(temp)

        tag = copied_store(finalized_root, parent, "tag")
        mutate_byte(chunk_path(tag, "pulse_features", 0), -1)
        checks.require_failure(
            "tag mutation",
            lambda: strict_validate(tag),
            (StoreContractValidationError,),
        )

        ciphertext = copied_store(finalized_root, parent, "ciphertext")
        mutate_byte(chunk_path(ciphertext, "pulse_features", 0), AEAD_HEADER_SIZE)
        checks.require_failure(
            "ciphertext mutation",
            lambda: strict_validate(ciphertext),
            (StoreContractValidationError,),
        )

        truncated = copied_store(finalized_root, parent, "truncated")
        chunk_path(truncated, "pulse_features", 0).write_bytes(pulse_wire[:17])
        checks.require_failure(
            "truncation",
            lambda: strict_validate(truncated),
            (StoreContractValidationError,),
        )

        swapped = copied_store(finalized_root, parent, "swapped")
        first = chunk_path(swapped, "gmi_waveform", 0)
        second = chunk_path(swapped, "gmi_waveform", 1)
        first_wire, second_wire = first.read_bytes(), second.read_bytes()
        first.write_bytes(second_wire)
        second.write_bytes(first_wire)
        checks.require_failure(
            "partition chunk swap",
            lambda: strict_validate(swapped),
            (StoreContractValidationError,),
        )

        unknown = copied_store(finalized_root, parent, "unknown-key")
        unknown_path = chunk_path(unknown, "pulse_features", 0)
        unknown_wire = bytearray(unknown_path.read_bytes())
        unknown_wire[len(AEAD_MAGIC) + 1] = 250
        unknown_path.write_bytes(unknown_wire)
        checks.require_failure(
            "unknown key_id",
            lambda: strict_validate(unknown),
            (StoreContractValidationError,),
        )

        plaintext = copied_store(finalized_root, parent, "plaintext")
        inner = decrypt_chunk(pulse_wire, context("pulse_features", 0))
        chunk_path(plaintext, "pulse_features", 0).write_bytes(inner)
        checks.require_failure(
            "plaintext fail-open fallback",
            lambda: strict_validate(plaintext),
            (StoreContractValidationError,),
        )

        nonce_reuse = copied_store(finalized_root, parent, "nonce-reuse")
        target_path = chunk_path(nonce_reuse, "pulse_features", 1)
        target_wire = target_path.read_bytes()
        nonce_start = len(AEAD_MAGIC) + 2
        reused_nonce = pulse_wire[
            nonce_start : nonce_start + AEAD_NONCE_SIZE
        ]
        target_inner = decrypt_chunk(target_wire, context("pulse_features", 1))
        target_key_id = target_wire[len(AEAD_MAGIC) + 1]
        target_path.write_bytes(
            encrypt_chunk(
                target_inner,
                context("pulse_features", 1),
                key_id=target_key_id,
                nonce=reused_nonce,
            )
        )
        checks.require_failure(
            "authenticated nonce reuse",
            lambda: strict_validate(nonce_reuse),
            (StoreContractValidationError,),
        )

        zarray_drift = copied_store(finalized_root, parent, "zarray-drift")
        zarray_path = array_path(zarray_drift, "pulse_features", 0) / ".zarray"
        zarray = json.loads(zarray_path.read_text(encoding="utf-8"))
        zarray["chunks"][0] += 1
        write_json(zarray_path, zarray)
        checks.require_failure(
            ".zarray chunk drift",
            lambda: strict_validate(zarray_drift),
            (StoreContractValidationError,),
        )

        row_misalignment = copied_store(
            finalized_root,
            parent,
            "row-misalignment",
        )
        misaligned_meta_path = (
            row_misalignment / "datasets" / DATASET_ID / "meta.json"
        )
        misaligned_meta = json.loads(
            misaligned_meta_path.read_text(encoding="utf-8")
        )
        misaligned_meta["partition_manifests"]["gmi_waveform"][0][
            "row_count"
        ] = 2
        write_json(misaligned_meta_path, misaligned_meta)
        checks.require_failure(
            "three-array row misalignment",
            lambda: strict_validate(row_misalignment),
            (StoreContractValidationError,),
        )

        overclaim = copied_store(finalized_root, parent, "manifest-overclaim")
        meta_path = overclaim / "datasets" / DATASET_ID / "meta.json"
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        meta["n_events"] = 6
        meta["events_per_partition"] = [3, 3]
        for array_name in ARRAYS:
            meta["partition_manifests"][array_name][1]["row_count"] = 3
        write_json(meta_path, meta)
        checks.require_failure(
            "manifest overclaim",
            lambda: strict_validate(overclaim),
            (StoreContractValidationError,),
        )


def parse_args() -> argparse.Namespace:
    """Parse explicit ignored-store paths."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--open-store", type=Path, required=True)
    parser.add_argument("--finalized-store-a", type=Path, required=True)
    parser.add_argument("--finalized-store-b", type=Path, required=True)
    parser.add_argument("--expected-evidence", type=Path)
    return parser.parse_args()


def fail(message: str) -> NoReturn:
    """Exit with one stable validation failure."""
    raise SystemExit(f"gcsa_roundtrip_validation failed: {message}")


def main() -> int:
    """Run the strict positive and negative matrix."""
    args = parse_args()
    checks = Checks()
    try:
        evidence = validate_positive(
            args.open_store,
            args.finalized_store_a,
            args.finalized_store_b,
            checks,
        )
        validate_lifecycle_dispositions(args.finalized_store_a, checks)
        validate_unknown_metadata_roundtrip(args.finalized_store_a, checks)
        validate_negatives(args.finalized_store_a, checks)
    except (CheckFailure, OSError, RuntimeError, TypeError, ValueError) as exc:
        fail(str(exc))
    summary = build_joint_evidence_summary(evidence, checks)
    if args.expected_evidence is not None:
        try:
            require_expected_joint_evidence(args.expected_evidence, summary)
        except (CheckFailure, OSError, TypeError, ValueError) as exc:
            fail(str(exc))
    print(
        "joint_evidence="
        + json.dumps(
            summary,
            ensure_ascii=False,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        )
    )
    print(
        f"gcsa_roundtrip_positive_checks={checks.positive} "
        f"negative_checks={checks.negative} "
        "arrays=3 partitions=2 status=pass"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
