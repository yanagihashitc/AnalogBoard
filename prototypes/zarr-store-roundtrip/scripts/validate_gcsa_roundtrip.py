#!/usr/bin/env python3
"""Validate the ignored synthetic store with an immutable gcsa snapshot."""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import tempfile
from collections.abc import Callable
from pathlib import Path
from typing import NoReturn

import numpy as np
import gcsa

from gcsa.data_model.visibility import DatasetNotFinalizedError
from gcsa.store._zarr_aead import AEAD_HEADER_SIZE, AeadChunkContext, decrypt_chunk
from gcsa.store.schema import (
    ARRAY_WIRE_CANDIDATES,
    FL_CHANNEL_ORDER,
    GMI_CHANNEL_ORDER,
    PULSE_FEATURE_COLUMNS,
    StoreContractValidationError,
    validate_analogboard_store,
)
from gcsa.store.zarr_store import ZarrStore, ZarrStoreConfig

DATASET_ID = "tube_1"
ARRAYS = tuple(ARRAY_WIRE_CANDIDATES)
CHUNK_KEYS = {
    name: ".".join("0" for _ in range(candidate.rank))
    for name, candidate in ARRAY_WIRE_CANDIDATES.items()
}
EXPECTED_SHAPES = {
    name: (2, *candidate.trailing_shape)
    for name, candidate in ARRAY_WIRE_CANDIDATES.items()
}
GCSA_SNAPSHOT_EXCLUDED_DIRECTORIES = frozenset(
    {"__pycache__", ".git", ".hg", ".svn"}
)
GCSA_SNAPSHOT_EXCLUDED_SUFFIXES = frozenset({".pyc", ".pyo"})
EXPECTED_GCSA_SNAPSHOT_COMMIT = "20689a991697217518ec2ff15aaaa2533b169eb0"
EXPECTED_GCSA_PACKAGE_TREE_SHA256 = (
    "c63c79c4add3a8034cd1486921470818ad71d024ace1e8e356ae4f8dbf396d14"
)


class CheckFailure(RuntimeError):
    """Raised when the isolated roundtrip does not match its fixed contract."""


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


def feature_bits() -> np.ndarray:
    """Return the exact two-event float64 fixture as uint64 bit patterns."""
    bits = np.empty((2, 24), dtype=np.uint64)
    bits[0, 0] = 0x8000000000000000
    bits[1, 0] = 0
    bits[0, 1] = 1
    bits[1, 1] = 0x8000000000000001
    bits[0, 2] = 0x7FEFFFFFFFFFFFFF
    bits[1, 2] = 0xFFEFFFFFFFFFFFFF
    for feature in range(3, 24):
        magnitude = float(feature) + 0.25
        bits[0, feature] = struct.unpack("<Q", struct.pack("<d", magnitude))[0]
        bits[1, feature] = struct.unpack("<Q", struct.pack("<d", -magnitude))[0]
    return bits


def waveform(array_name: str, channels: int) -> np.ndarray:
    """Return the exact two-event uint16 fixture in global event order."""
    offset = 1000 if array_name == "gmi_waveform" else 30000
    events = np.arange(2, dtype=np.uint32)[:, None, None] * 10000
    channel_offsets = np.arange(channels, dtype=np.uint32)[None, :, None] * 2400
    samples = np.arange(2400, dtype=np.uint32)[None, None, :]
    return ((offset + events + channel_offsets + samples) & 0xFFFF).astype(
        np.uint16
    )


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


def validate_positive(open_root: Path, finalized_root: Path, checks: Checks) -> None:
    """Validate strict schema, product visibility, and original-bit reads."""
    snapshot_root = gcsa_snapshot_root()
    before_snapshot = require_accepted_gcsa_snapshot(snapshot_root)
    before_open = tree_digest(open_root)
    before_finalized = tree_digest(finalized_root)

    open_report = validate_analogboard_store(open_root, mode="strict")
    final_report = validate_analogboard_store(finalized_root, mode="strict")
    for report in (open_report, final_report):
        checks.require(report.contract_id == "gcsa-store-a4a-rc1", "contract drift")
        checks.require(report.profile == "strict", "profile downgrade")
        checks.require(report.dataset_ids == (DATASET_ID,), "dataset identity drift")
        checks.require(report.encrypted_chunk_count == 6, "chunk count drift")

    open_store = ZarrStore(open_root, config=store_config(), readonly=True)
    checks.require(open_store.readonly, "open reader is not read-only")
    checks.require(open_store.list_datasets() == [], "open dataset became visible")
    checks.require_failure(
        "open product read",
        lambda: open_store.get_pulse_features(DATASET_ID),
        (DatasetNotFinalizedError,),
    )

    store = ZarrStore(finalized_root, config=store_config(), readonly=True)
    checks.require(store.readonly, "finalized reader is not read-only")
    checks.require(store.list_datasets() == [DATASET_ID], "finalized visibility drift")
    meta = store.get_meta(DATASET_ID)
    pulse = store.get_pulse_features(DATASET_ID)
    gmi = store.get_gmi_waveform(DATASET_ID)
    fl = store.get_fl_waveform(DATASET_ID)

    checks.require(pulse.shape == EXPECTED_SHAPES["pulse_features"], "pulse shape")
    checks.require(gmi.shape == EXPECTED_SHAPES["gmi_waveform"], "GMI shape")
    checks.require(fl.shape == EXPECTED_SHAPES["fl_waveform"], "FL shape")
    checks.require(pulse.dtype == np.dtype("<f8"), "pulse dtype")
    checks.require(gmi.dtype == np.dtype("<u2"), "GMI dtype")
    checks.require(fl.dtype == np.dtype("<u2"), "FL dtype")
    checks.require(
        np.array_equal(pulse.view(np.uint64), feature_bits()),
        "pulse features changed bits or global event order",
    )
    checks.require(
        np.array_equal(gmi, waveform("gmi_waveform", len(GMI_CHANNEL_ORDER))),
        "GMI changed bits or global event order",
    )
    checks.require(
        np.array_equal(fl, waveform("fl_waveform", len(FL_CHANNEL_ORDER))),
        "FL changed bits or global event order",
    )
    checks.require(
        np.array_equal(
            pulse,
            np.concatenate(
                [
                    store.get_pulse_features(DATASET_ID, partition=0),
                    store.get_pulse_features(DATASET_ID, partition=1),
                ]
            ),
            equal_nan=True,
        ),
        "partition append differs from the logical round-robin view",
    )
    checks.require(
        tuple(feature.name for feature in meta.features) == PULSE_FEATURE_COLUMNS,
        "feature column order drift",
    )
    checks.require(
        tuple(channel.name for channel in meta.channels[:8]) == FL_CHANNEL_ORDER,
        "FL channel order drift",
    )
    checks.require(
        tuple(channel.name for channel in meta.channels[8:]) == GMI_CHANNEL_ORDER,
        "GMI channel order drift",
    )
    checks.require(meta.status == "finalized", "finalized status drift")
    checks.require(meta.finalized_at is not None, "finalized_at is absent")
    checks.require(meta.write_generation == 2, "write_generation drift")
    checks.require(meta.n_events == 2, "n_events drift")
    checks.require(meta.events_per_partition == [1, 1], "partition rows drift")

    pulse_values = pulse.view(np.float64)
    for index, feature in enumerate(meta.features):
        checks.require(
            feature.range_min == float(np.min(pulse_values[:, index])),
            f"feature minimum drift: {feature.name}",
        )
        checks.require(
            feature.range_max == float(np.max(pulse_values[:, index])),
            f"feature maximum drift: {feature.name}",
        )
    for array_name in ARRAYS:
        entries = meta.partition_manifests[array_name]
        checks.require(
            [(entry.partition, entry.row_count, entry.sealed) for entry in entries]
            == [(0, 1, True), (1, 1, True)],
            f"manifest alignment drift: {array_name}",
        )

    checks.require(tree_digest(open_root) == before_open, "open read mutated store")
    checks.require(
        tree_digest(finalized_root) == before_finalized,
        "finalized read mutated store",
    )
    checks.require(
        gcsa_snapshot_digest(snapshot_root) == before_snapshot,
        "accepted gcsa snapshot was mutated",
    )


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
            "tag mutation", lambda: strict_validate(tag), (StoreContractValidationError,)
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
        unknown_wire[5] = 250
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

        overclaim = copied_store(finalized_root, parent, "manifest-overclaim")
        meta_path = overclaim / "datasets" / DATASET_ID / "meta.json"
        meta = json.loads(meta_path.read_text(encoding="utf-8"))
        meta["n_events"] = 3
        meta["events_per_partition"] = [2, 1]
        for array_name in ARRAYS:
            meta["partition_manifests"][array_name][0]["row_count"] = 2
        meta_path.write_text(
            json.dumps(meta, ensure_ascii=False, sort_keys=True, indent=2) + "\n",
            encoding="utf-8",
        )
        checks.require_failure(
            "manifest overclaim",
            lambda: strict_validate(overclaim),
            (StoreContractValidationError,),
        )


def parse_args() -> argparse.Namespace:
    """Parse explicit ignored-store paths."""
    parser = argparse.ArgumentParser()
    parser.add_argument("--open-store", type=Path, required=True)
    parser.add_argument("--finalized-store", type=Path, required=True)
    return parser.parse_args()


def fail(message: str) -> NoReturn:
    """Exit with one stable validation failure."""
    raise SystemExit(f"gcsa_roundtrip_validation failed: {message}")


def main() -> int:
    """Run the strict positive and negative matrix."""
    args = parse_args()
    checks = Checks()
    try:
        validate_positive(args.open_store, args.finalized_store, checks)
        validate_negatives(args.finalized_store, checks)
    except (CheckFailure, OSError, TypeError, ValueError) as exc:
        fail(str(exc))
    print(
        f"gcsa_roundtrip_positive_checks={checks.positive} "
        f"negative_checks={checks.negative} "
        "arrays=3 partitions=2 status=pass"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
