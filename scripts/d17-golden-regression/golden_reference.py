#!/usr/bin/env python3
"""Build the payload-free D17 golden reference through the pinned gcsa reader."""

from __future__ import annotations

import argparse
import hashlib
import inspect
import json
import os
import re
import stat
import sys
from collections.abc import Callable, Mapping, Sequence
from pathlib import Path, PurePosixPath, PureWindowsPath
from typing import Any

import numpy as np


_MAPPING_PATH = PurePosixPath(
    "docs/reference/d17-golden-regression/channel-mapping-v1.json"
)
_SELECTION_PATH = PurePosixPath(
    "docs/reference/d17-golden-regression/golden-inputs-v1.json"
)
_OUTPUT_PATH = PurePosixPath(
    "docs/reference/d17-golden-regression/golden-reference-v1.json"
)
_MAPPING_SHA256 = (
    "8e197eade3fff0f7427c7cf0e9d77409624b803a51a782c6a429e705f15fc99b"
)
_MAPPING_SIZE = 1_349
_SELECTION_SHA256 = (
    "76ef20a12ff3b0850a25501f13832690e84dd762b25f3500918c0dde6d443023"
)
_SELECTION_SIZE = 1_785
_REFERENCE_SHA256 = (
    "581fa28e05d85d4fb6ff0b5157958c1e908326505acf39a3f732b1b720d25095"
)
_REFERENCE_SIZE = 13_178
_GCSA_COMMIT = "20689a991697217518ec2ff15aaaa2533b169eb0"
_GCSA_READER_PATH = "src/gcsa/io/binary_reader.py"
_GCSA_READER_SYMBOL = "BinaryReader"
_GCSA_READER_VERSION = "v1"
_GCSA_READER_SHA256 = (
    "620ab899b0fb75f75da0a1c8b5722a2f02212726910aea5115401506f8eb4254"
)
_GCSA_PARSER_PATH = "src/gcsa/io/parsers/v1.py"
_GCSA_PARSER_SHA256 = (
    "5035b9147ec42c2381cc2fd45a1f83a9f251edece7b21c4dd099f2da315a2964"
)
_CONTAINER_IMAGE_ID = (
    "sha256:e65e9f8b0ffafef5b5d2b9711c9a341"
    "1649ae80fd036cc79f0febb80b4c0b06e"
)
_ASSET_LOCATOR = "artifacts/field-session/2026-07-17-characterization"
_CHANNELS_PER_STREAM = {"FL": 8, "FH": 5}
_CHANNEL_COUNT = sum(_CHANNELS_PER_STREAM.values())
_SAMPLES_PER_EVENT = 2_400
_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_RUN_ID_RE = re.compile(r"^[0-9]{6}_[0-9]{4}$")
_ASSET_NAME_RE = re.compile(
    r"^(?P<run_id>[0-9]{6}_[0-9]{4})_"
    r"(?P<stream>fl|fh)_(?P<ordinal>[1-9][0-9]*)[.]bin$"
)
_URI_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*://")
_PROHIBITED_KEYS = frozenset(
    {
        "array",
        "arrays",
        "byte",
        "bytes",
        "data",
        "decoded",
        "payload",
        "sample",
        "samples",
        "waveform",
        "waveforms",
    }
)


class GoldenReferenceError(Exception):
    """Base class for stable, typed golden-reference failures."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        self.message = message
        super().__init__(f"{code}: {message}")


class ReferenceSourceError(GoldenReferenceError):
    """A fixed tracked source has drifted from its identity pin."""


class UnsafeAssetError(GoldenReferenceError):
    """A selected asset cannot be opened without following filesystem links."""


class AssetIdentityError(GoldenReferenceError):
    """A selected asset does not match its fixed manifest identity."""


class ReaderProvenanceError(GoldenReferenceError):
    """The active reader does not match the fixed P0-M1 authority."""


class DecodeError(GoldenReferenceError):
    """The pinned reader failed or returned a non-array value."""


class DecodedDtypeError(GoldenReferenceError):
    """The decoded array does not use canonical little-endian uint16."""


class DecodedShapeError(GoldenReferenceError):
    """The decoded array shape violates the fixed stream contract."""


class MappingAddressError(GoldenReferenceError):
    """The mapping does not address exactly the fixed 13 decoded channels."""


class PayloadBoundaryError(GoldenReferenceError):
    """The reference document contains payload or a host locator."""


class ReferenceOutputError(GoldenReferenceError):
    """The sole authorized tracked reference output is unsafe."""


class _DuplicateKeyError(ValueError):
    pass


def _fail(error_type: type[GoldenReferenceError], code: str, message: str) -> None:
    raise error_type(code, message)


def verify_reference_source_identities(
    mapping_source: object,
    selection_source: object,
) -> None:
    """Require byte-identical mapping and selection sources."""

    valid = (
        type(mapping_source) is bytes
        and len(mapping_source) == _MAPPING_SIZE
        and hashlib.sha256(mapping_source).hexdigest() == _MAPPING_SHA256
        and type(selection_source) is bytes
        and len(selection_source) == _SELECTION_SIZE
        and hashlib.sha256(selection_source).hexdigest() == _SELECTION_SHA256
    )
    if not valid:
        _fail(
            ReferenceSourceError,
            "reference.source.identity_mismatch",
            "fixed mapping or selection source identity does not match its pin",
        )


def _validate_mapping(mapping: object) -> list[dict[str, object]]:
    if type(mapping) is not list or len(mapping) != _CHANNEL_COUNT:
        _fail(
            MappingAddressError,
            "reference.mapping.channel_count",
            "channel mapping must contain exactly 13 entries",
        )

    normalized: list[dict[str, object]] = []
    labels: set[str] = set()
    source_indices = {"FL": 0, "FH": 0}
    for position, row in enumerate(mapping, start=1):
        if type(row) is not dict:
            _fail(
                MappingAddressError,
                "reference.mapping.address",
                "channel mapping cannot address the decoded stream",
            )
        label = row.get("label")
        expected_stream = (
            "FL"
            if position <= _CHANNELS_PER_STREAM["FL"]
            else "FH"
        )
        expected_source_index = source_indices[expected_stream]
        if (
            row.get("physical_channel") != f"CH{position}"
            or type(label) is not str
            or not label
            or label in labels
            or row.get("stream") != expected_stream
            or type(row.get("source_index")) is not int
            or row.get("source_index") != expected_source_index
        ):
            _fail(
                MappingAddressError,
                "reference.mapping.address",
                "channel mapping cannot address the decoded stream",
            )
        labels.add(label)
        source_indices[expected_stream] += 1
        normalized.append(
            {
                "physical_channel": f"CH{position}",
                "label": label,
                "stream": expected_stream,
                "source_index": expected_source_index,
            }
        )
    return normalized


def _validate_selection_entry(
    entry: object,
    *,
    pair_run_id: str,
    pair_ordinal: int,
    expected_stream: str,
) -> dict[str, object]:
    if type(entry) is not dict:
        _unsafe_asset()
    stream = entry.get("stream")
    path = entry.get("path")
    digest = entry.get("sha256")
    size = entry.get("size_bytes")
    if (
        stream != expected_stream
        or type(path) is not str
        or type(digest) is not str
        or _SHA256_RE.fullmatch(digest) is None
        or type(size) is not int
        or size < 0
    ):
        _unsafe_asset()

    relative = PurePosixPath(path)
    match = _ASSET_NAME_RE.fullmatch(path)
    if (
        relative.is_absolute()
        or len(relative.parts) != 1
        or path in ("", ".", "..")
        or "\\" in path
        or match is None
        or match.group("run_id") != pair_run_id
        or match.group("stream").upper() != expected_stream
        or int(match.group("ordinal")) != pair_ordinal
    ):
        _unsafe_asset()
    return {
        "stream": expected_stream,
        "path": path,
        "sha256": digest,
        "size_bytes": size,
    }


def _validate_selection(selection: object) -> list[dict[str, object]]:
    if type(selection) is not dict:
        _unsafe_asset()
    if selection.get("asset_locator") != _ASSET_LOCATOR:
        _unsafe_asset()
    pairs = selection.get("pairs")
    pair_count = selection.get("pair_count")
    entry_count = selection.get("entry_count")
    if (
        type(pairs) is not list
        or not pairs
        or type(pair_count) is not int
        or pair_count != len(pairs)
        or type(entry_count) is not int
        or entry_count != pair_count * 2
    ):
        _unsafe_asset()

    normalized: list[dict[str, object]] = []
    pair_identities: set[tuple[str, int]] = set()
    for pair in pairs:
        if type(pair) is not dict:
            _unsafe_asset()
        density = pair.get("density")
        run_id = pair.get("run_id")
        ordinal = pair.get("ordinal")
        entries = pair.get("entries")
        if (
            type(density) is not str
            or not density
            or type(run_id) is not str
            or _RUN_ID_RE.fullmatch(run_id) is None
            or type(ordinal) is not int
            or ordinal <= 0
            or type(entries) is not list
            or len(entries) != 2
            or (run_id, ordinal) in pair_identities
        ):
            _unsafe_asset()
        pair_identities.add((run_id, ordinal))
        normalized.append(
            {
                "density": density,
                "run_id": run_id,
                "ordinal": ordinal,
                "entries": [
                    _validate_selection_entry(
                        entries[index],
                        pair_run_id=run_id,
                        pair_ordinal=ordinal,
                        expected_stream=stream,
                    )
                    for index, stream in enumerate(("FL", "FH"))
                ],
            }
        )
    return normalized


def _validate_reader_provenance(provenance: object) -> dict[str, object]:
    expected = {
        "repository": "gcsa",
        "commit": _GCSA_COMMIT,
        "path": _GCSA_READER_PATH,
        "symbol": _GCSA_READER_SYMBOL,
        "version": _GCSA_READER_VERSION,
        "invocation": "BinaryReader(version='v1')",
        "reader_source_sha256": _GCSA_READER_SHA256,
        "parser": {
            "path": _GCSA_PARSER_PATH,
            "sha256": _GCSA_PARSER_SHA256,
        },
        "environment": {
            "kind": "container-image",
            "identity": _CONTAINER_IMAGE_ID,
            "python_version": "3.10.17",
            "numpy_version": "2.2.6",
            "logical_invocation": (
                "golden_reference.py generate "
                "--asset-root <fixed-custody-root>"
            ),
        },
    }
    if type(provenance) is not dict or provenance != expected:
        _fail(
            ReaderProvenanceError,
            "reference.reader.pin_mismatch",
            "gcsa reader provenance does not match the fixed P0-M1 pin",
        )
    return expected


def _unsafe_asset() -> None:
    _fail(
        UnsafeAssetError,
        "reference.asset.unsafe",
        "selected asset must be a regular file beneath the fixed asset root",
    )


def _directory_descriptor(path: Path) -> int:
    no_follow = getattr(os, "O_NOFOLLOW", 0)
    directory = getattr(os, "O_DIRECTORY", 0)
    if (
        no_follow == 0
        or directory == 0
        or os.open not in os.supports_dir_fd
        or os.stat not in os.supports_dir_fd
        or os.stat not in os.supports_follow_symlinks
    ):
        _unsafe_asset()
    try:
        if (
            not isinstance(path, Path)
            or not path.is_absolute()
            or path.resolve(strict=True) != path
        ):
            _unsafe_asset()
        metadata = path.lstat()
        if not stat.S_ISDIR(metadata.st_mode) or stat.S_ISLNK(metadata.st_mode):
            _unsafe_asset()
        return os.open(
            path,
            os.O_RDONLY
            | directory
            | no_follow
            | getattr(os, "O_CLOEXEC", 0),
        )
    except GoldenReferenceError:
        raise
    except OSError:
        _unsafe_asset()
    raise AssertionError("unreachable")


def _decode_verified_asset(
    asset_root_descriptor: int,
    entry: Mapping[str, object],
    decode_entry: Callable[[Path, str], object],
) -> np.ndarray:
    no_follow = getattr(os, "O_NOFOLLOW", 0)
    flags = os.O_RDONLY | no_follow | getattr(os, "O_CLOEXEC", 0)
    descriptor: int | None = None
    try:
        before = os.stat(
            str(entry["path"]),
            dir_fd=asset_root_descriptor,
            follow_symlinks=False,
        )
        if not stat.S_ISREG(before.st_mode) or stat.S_ISLNK(before.st_mode):
            _unsafe_asset()
        descriptor = os.open(
            str(entry["path"]),
            flags,
            dir_fd=asset_root_descriptor,
        )
        opened = os.fstat(descriptor)
        if (
            not stat.S_ISREG(opened.st_mode)
            or (opened.st_dev, opened.st_ino)
            != (before.st_dev, before.st_ino)
        ):
            _unsafe_asset()

        digest, size = _descriptor_identity(descriptor)
        if (
            size != entry["size_bytes"]
            or digest != entry["sha256"]
        ):
            _fail(
                AssetIdentityError,
                "reference.asset.identity_mismatch",
                "selected asset identity does not match its canonical manifest pin",
            )
        os.lseek(descriptor, 0, os.SEEK_SET)
        descriptor_path = Path(f"/proc/self/fd/{descriptor}")
        decoded: object | None = None
        decode_failed = False
        try:
            decoded = decode_entry(descriptor_path, str(entry["stream"]))
        except Exception:
            decode_failed = True
        if decode_failed:
            _fail(
                DecodeError,
                "reference.decode.failed",
                "pinned gcsa reader failed for selected input",
            )
        if not isinstance(decoded, np.ndarray):
            _fail(
                DecodeError,
                "reference.decode.non_array",
                "pinned gcsa reader must return a numpy ndarray",
            )
        after = os.fstat(descriptor)
        if (
            not stat.S_ISREG(after.st_mode)
            or (after.st_dev, after.st_ino) != (opened.st_dev, opened.st_ino)
        ):
            _unsafe_asset()
        os.lseek(descriptor, 0, os.SEEK_SET)
        post_digest, post_size = _descriptor_identity(descriptor)
        if (
            post_size != entry["size_bytes"]
            or post_digest != entry["sha256"]
        ):
            _fail(
                AssetIdentityError,
                "reference.asset.identity_mismatch",
                "selected asset identity does not match its canonical manifest pin",
            )
        return decoded
    except GoldenReferenceError:
        raise
    except OSError:
        _unsafe_asset()
    finally:
        if descriptor is not None:
            os.close(descriptor)
    raise AssertionError("unreachable")


def _descriptor_identity(descriptor: int) -> tuple[str, int]:
    digest = hashlib.sha256()
    size = 0
    while True:
        chunk = os.read(descriptor, 1024 * 1024)
        if not chunk:
            break
        digest.update(chunk)
        size += len(chunk)
    return digest.hexdigest(), size


def _validate_decoded_stream(
    decoded: np.ndarray,
    stream: str,
) -> None:
    if decoded.dtype.str != "<u2":
        _fail(
            DecodedDtypeError,
            "reference.dtype.mismatch",
            "decoded stream dtype must be canonical <u2",
        )
    if decoded.ndim != 3:
        _fail(
            DecodedShapeError,
            "reference.shape.rank",
            "decoded stream must have rank 3",
        )
    if decoded.shape[1] != _CHANNELS_PER_STREAM[stream]:
        _fail(
            DecodedShapeError,
            "reference.shape.channels",
            "decoded stream channel count does not match the fixed mapping",
        )
    if decoded.shape[2] != _SAMPLES_PER_EVENT:
        _fail(
            DecodedShapeError,
            "reference.shape.samples",
            "decoded stream must contain exactly 2400 samples per event",
        )


def _channel_summary(
    mapping_row: Mapping[str, object],
    decoded: np.ndarray,
) -> dict[str, object]:
    values = np.ascontiguousarray(
        decoded[:, int(mapping_row["source_index"]), :],
        dtype="<u2",
    )
    return {
        "physical_channel": mapping_row["physical_channel"],
        "label": mapping_row["label"],
        "stream": mapping_row["stream"],
        "source_index": mapping_row["source_index"],
        "dtype": "<u2",
        "shape": [int(values.shape[0]), int(values.shape[1])],
        "sha256": hashlib.sha256(values.tobytes(order="C")).hexdigest(),
        "statistics": {
            "element_count": int(values.size),
            "min": int(values.min()),
            "max": int(values.max()),
            "sum": int(values.sum(dtype=np.uint64)),
            "nonzero_count": int(np.count_nonzero(values)),
        },
    }


def build_golden_reference(
    *,
    mapping: object,
    selection: object,
    asset_root: Path,
    reader_provenance: object,
    decode_entry: Callable[[Path, str], object],
) -> dict[str, object]:
    """Decode selected assets once and return bounded channel summaries."""

    normalized_mapping = _validate_mapping(mapping)
    normalized_selection = _validate_selection(selection)
    normalized_reader = _validate_reader_provenance(reader_provenance)
    if not callable(decode_entry):
        _fail(
            DecodeError,
            "reference.decode.failed",
            "pinned gcsa reader failed for selected input",
        )

    root_descriptor = _directory_descriptor(asset_root)
    reference_pairs: list[dict[str, object]] = []
    try:
        for pair in normalized_selection:
            decoded: dict[str, np.ndarray] = {}
            inputs: list[dict[str, object]] = []
            for entry in pair["entries"]:
                assert isinstance(entry, dict)
                stream = str(entry["stream"])
                value = _decode_verified_asset(
                    root_descriptor,
                    entry,
                    decode_entry,
                )
                _validate_decoded_stream(value, stream)
                decoded[stream] = value
                inputs.append(dict(entry))

            fl_events = int(decoded["FL"].shape[0])
            fh_events = int(decoded["FH"].shape[0])
            if fl_events <= 0 or fl_events != fh_events:
                _fail(
                    DecodedShapeError,
                    "reference.shape.events",
                    "decoded FL and FH streams must have the same positive event count",
                )
            channels = [
                _channel_summary(row, decoded[str(row["stream"])])
                for row in normalized_mapping
            ]
            reference_pairs.append(
                {
                    "density": pair["density"],
                    "run_id": pair["run_id"],
                    "ordinal": pair["ordinal"],
                    "inputs": inputs,
                    "event_count": fl_events,
                    "channels": channels,
                }
            )
    finally:
        os.close(root_descriptor)

    return {
        "schema": "analogboard.d17.golden-reference",
        "schema_version": 1,
        "sources": {
            "channel_mapping": {
                "path": str(_MAPPING_PATH),
                "sha256": _MAPPING_SHA256,
                "size_bytes": _MAPPING_SIZE,
            },
            "golden_inputs": {
                "path": str(_SELECTION_PATH),
                "sha256": _SELECTION_SHA256,
                "size_bytes": _SELECTION_SIZE,
            },
        },
        "reader": normalized_reader,
        "pair_count": len(reference_pairs),
        "channel_count_per_pair": len(normalized_mapping),
        "pairs": reference_pairs,
    }


def _is_prohibited_key(key: str) -> bool:
    normalized = key.lower().replace("-", "_")
    if normalized in _PROHIBITED_KEYS:
        return True
    return (
        normalized.endswith(("_payload", "_samples", "_waveform", "_waveforms"))
        or (
            normalized.endswith("_bytes")
            and normalized != "size_bytes"
        )
        or normalized.startswith(("decoded_", "raw_payload", "raw_bytes"))
    )


def _is_absolute_locator(value: str) -> bool:
    return (
        value.startswith("/")
        or value.startswith("\\\\")
        or PureWindowsPath(value).is_absolute()
        or _URI_RE.match(value) is not None
        or re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:/", value) is not None
    )


def _validate_payload_boundary(value: object) -> None:
    if isinstance(value, bytes):
        _fail(
            PayloadBoundaryError,
            "reference.payload.bytes",
            "golden reference must not contain bytes values",
        )
    if type(value) is dict:
        for key, child in value.items():
            if type(key) is not str:
                _fail(
                    PayloadBoundaryError,
                    "reference.payload.prohibited_key",
                    "golden reference contains a prohibited payload field",
                )
            if _is_absolute_locator(key):
                _fail(
                    PayloadBoundaryError,
                    "reference.payload.absolute_locator",
                    "golden reference must not contain an absolute host locator",
                )
            if _is_prohibited_key(key):
                _fail(
                    PayloadBoundaryError,
                    "reference.payload.prohibited_key",
                    "golden reference contains a prohibited payload field",
                )
            _validate_payload_boundary(child)
        return
    if type(value) is list:
        for child in value:
            _validate_payload_boundary(child)
        return
    if type(value) is str and _is_absolute_locator(value):
        _fail(
            PayloadBoundaryError,
            "reference.payload.absolute_locator",
            "golden reference must not contain an absolute host locator",
        )
    if value is not None and type(value) not in (str, int, bool):
        _fail(
            PayloadBoundaryError,
            "reference.payload.prohibited_key",
            "golden reference contains a prohibited payload field",
        )


def serialize_golden_reference(document: object) -> bytes:
    """Serialize a payload-free reference as canonical UTF-8 JSON."""

    if type(document) is not dict:
        _fail(
            PayloadBoundaryError,
            "reference.payload.prohibited_key",
            "golden reference contains a prohibited payload field",
        )
    _validate_payload_boundary(document)
    return (
        json.dumps(
            document,
            ensure_ascii=False,
            allow_nan=False,
            separators=(",", ":"),
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")


def _open_safe_root(root: Path, error: Callable[[], None]) -> int:
    no_follow = getattr(os, "O_NOFOLLOW", 0)
    directory = getattr(os, "O_DIRECTORY", 0)
    if no_follow == 0 or directory == 0:
        error()
    try:
        if (
            not isinstance(root, Path)
            or not root.is_absolute()
            or root.resolve(strict=True) != root
        ):
            error()
        metadata = root.lstat()
        if not stat.S_ISDIR(metadata.st_mode) or stat.S_ISLNK(metadata.st_mode):
            error()
        return os.open(
            root,
            os.O_RDONLY
            | directory
            | no_follow
            | getattr(os, "O_CLOEXEC", 0),
        )
    except GoldenReferenceError:
        raise
    except OSError:
        error()
    raise AssertionError("unreachable")


def write_golden_reference(
    repository_root: Path,
    relative_output: Path,
    content: bytes,
) -> None:
    """Write only the fixed tracked reference through no-follow descriptors."""

    if (
        not isinstance(relative_output, Path)
        or PurePosixPath(relative_output.as_posix()) != _OUTPUT_PATH
        or relative_output.is_absolute()
    ):
        _fail(
            ReferenceOutputError,
            "reference.output.path",
            f"reference output must be {_OUTPUT_PATH}",
        )
    if type(content) is not bytes:
        _fail(
            ReferenceOutputError,
            "reference.output.target",
            "reference output must be absent or a regular file",
        )
    if (
        len(content) != _REFERENCE_SIZE
        or hashlib.sha256(content).hexdigest() != _REFERENCE_SHA256
    ):
        _fail(
            ReferenceOutputError,
            "reference.output.identity_mismatch",
            "generated or existing reference identity does not match its pin",
        )

    def parent_error() -> None:
        _fail(
            ReferenceOutputError,
            "reference.output.parent",
            "reference output parent must contain only existing directories",
        )

    root_descriptor = _open_safe_root(repository_root, parent_error)
    descriptors = [root_descriptor]
    target_descriptor: int | None = None
    try:
        parent_descriptor = root_descriptor
        for part in _OUTPUT_PATH.parts[:-1]:
            try:
                metadata = os.stat(
                    part,
                    dir_fd=parent_descriptor,
                    follow_symlinks=False,
                )
                if not stat.S_ISDIR(metadata.st_mode) or stat.S_ISLNK(
                    metadata.st_mode
                ):
                    parent_error()
                opened = os.open(
                    part,
                    os.O_RDONLY
                    | getattr(os, "O_DIRECTORY", 0)
                    | getattr(os, "O_NOFOLLOW", 0)
                    | getattr(os, "O_CLOEXEC", 0),
                    dir_fd=parent_descriptor,
                )
                descriptors.append(opened)
                parent_descriptor = opened
            except GoldenReferenceError:
                raise
            except OSError:
                parent_error()

        target_name = _OUTPUT_PATH.name
        try:
            existing = os.stat(
                target_name,
                dir_fd=parent_descriptor,
                follow_symlinks=False,
            )
        except FileNotFoundError:
            existing = None
        except OSError:
            existing = None
            _fail(
                ReferenceOutputError,
                "reference.output.target",
                "reference output must be absent or a regular file",
            )
        if existing is not None and (
            not stat.S_ISREG(existing.st_mode) or stat.S_ISLNK(existing.st_mode)
        ):
            _fail(
                ReferenceOutputError,
                "reference.output.target",
                "reference output must be absent or a regular file",
            )
        if existing is not None and existing.st_nlink != 1:
            _fail(
                ReferenceOutputError,
                "reference.output.target",
                "reference output must be absent or a regular unaliased file",
            )
        try:
            open_flags = (
                os.O_WRONLY | os.O_CREAT | os.O_EXCL
                if existing is None
                else os.O_RDWR
            )
            target_descriptor = os.open(
                target_name,
                open_flags
                | getattr(os, "O_NOFOLLOW", 0)
                | getattr(os, "O_CLOEXEC", 0),
                0o644,
                dir_fd=parent_descriptor,
            )
            opened = os.fstat(target_descriptor)
            if (
                not stat.S_ISREG(opened.st_mode)
                or opened.st_nlink != 1
                or (
                    existing is not None
                    and (opened.st_dev, opened.st_ino)
                    != (existing.st_dev, existing.st_ino)
                )
            ):
                _fail(
                    ReferenceOutputError,
                    "reference.output.target",
                    (
                        "reference output must be absent or "
                        "a regular unaliased file"
                    ),
                )
            if existing is not None:
                digest = hashlib.sha256()
                existing_size = 0
                while True:
                    chunk = os.read(target_descriptor, 1024 * 1024)
                    if not chunk:
                        break
                    digest.update(chunk)
                    existing_size += len(chunk)
                if (
                    existing_size != _REFERENCE_SIZE
                    or digest.hexdigest() != _REFERENCE_SHA256
                ):
                    _fail(
                        ReferenceOutputError,
                        "reference.output.identity_mismatch",
                        (
                            "generated or existing reference identity "
                            "does not match its pin"
                        ),
                    )
                os.lseek(target_descriptor, 0, os.SEEK_SET)
            os.ftruncate(target_descriptor, 0)
            offset = 0
            while offset < len(content):
                written = os.write(target_descriptor, content[offset:])
                if written <= 0:
                    raise OSError("short write")
                offset += written
            os.fsync(target_descriptor)
        except GoldenReferenceError:
            raise
        except OSError:
            _fail(
                ReferenceOutputError,
                "reference.output.target",
                "reference output must be absent or a regular file",
            )
    finally:
        if target_descriptor is not None:
            os.close(target_descriptor)
        for descriptor in reversed(descriptors):
            os.close(descriptor)


def _decode_unique_json(source: bytes, name: str) -> dict[str, object]:
    def unique_object(pairs: Sequence[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise _DuplicateKeyError(key)
            result[key] = value
        return result

    try:
        value = json.loads(source, object_pairs_hook=unique_object)
    except (_DuplicateKeyError, UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise ReferenceSourceError(
            "reference.source.json",
            f"fixed {name} source must be an unambiguous JSON object",
        ) from exc
    if type(value) is not dict:
        _fail(
            ReferenceSourceError,
            "reference.source.json",
            f"fixed {name} source must be an unambiguous JSON object",
        )
    return value


def _read_fixed_source(
    repository_root: Path,
    relative: PurePosixPath,
    expected_size: int,
) -> bytes:
    def source_error() -> None:
        _fail(
            ReferenceSourceError,
            "reference.source.unsafe",
            "fixed reference source must be a regular tracked file",
        )

    def identity_error() -> None:
        _fail(
            ReferenceSourceError,
            "reference.source.identity_mismatch",
            "fixed mapping or selection source identity does not match its pin",
        )

    root_descriptor = _open_safe_root(repository_root, source_error)
    descriptors = [root_descriptor]
    file_descriptor: int | None = None
    try:
        parent = root_descriptor
        for part in relative.parts[:-1]:
            try:
                metadata = os.stat(part, dir_fd=parent, follow_symlinks=False)
                if not stat.S_ISDIR(metadata.st_mode) or stat.S_ISLNK(
                    metadata.st_mode
                ):
                    source_error()
                opened = os.open(
                    part,
                    os.O_RDONLY
                    | getattr(os, "O_DIRECTORY", 0)
                    | getattr(os, "O_NOFOLLOW", 0)
                    | getattr(os, "O_CLOEXEC", 0),
                    dir_fd=parent,
                )
                descriptors.append(opened)
                parent = opened
            except GoldenReferenceError:
                raise
            except OSError:
                source_error()
        try:
            file_descriptor = os.open(
                relative.name,
                os.O_RDONLY
                | getattr(os, "O_NOFOLLOW", 0)
                | getattr(os, "O_CLOEXEC", 0),
                dir_fd=parent,
            )
            metadata = os.fstat(file_descriptor)
            if not stat.S_ISREG(metadata.st_mode):
                source_error()
            if type(expected_size) is not int or metadata.st_size != expected_size:
                identity_error()
            chunks: list[bytes] = []
            remaining = expected_size + 1
            while remaining > 0:
                chunk = os.read(
                    file_descriptor,
                    min(1024 * 1024, remaining),
                )
                if not chunk:
                    break
                chunks.append(chunk)
                remaining -= len(chunk)
            source = b"".join(chunks)
            if len(source) != expected_size:
                identity_error()
            return source
        except GoldenReferenceError:
            raise
        except OSError:
            source_error()
    finally:
        if file_descriptor is not None:
            os.close(file_descriptor)
        for descriptor in reversed(descriptors):
            os.close(descriptor)
    raise AssertionError("unreachable")


def _module_source_identity(module: object, expected_sha256: str) -> str:
    source_path = inspect.getsourcefile(module)
    try:
        if source_path is None:
            raise OSError("module has no source")
        path = Path(source_path)
        descriptor = os.open(
            path,
            os.O_RDONLY
            | getattr(os, "O_NOFOLLOW", 0)
            | getattr(os, "O_CLOEXEC", 0),
        )
        try:
            metadata = os.fstat(descriptor)
            if not stat.S_ISREG(metadata.st_mode):
                raise OSError("module source is not regular")
            digest = hashlib.sha256()
            while True:
                chunk = os.read(descriptor, 1024 * 1024)
                if not chunk:
                    break
                digest.update(chunk)
        finally:
            os.close(descriptor)
    except OSError:
        _fail(
            ReaderProvenanceError,
            "reference.reader.pin_mismatch",
            "gcsa reader provenance does not match the fixed P0-M1 pin",
        )
    actual = digest.hexdigest()
    if actual != expected_sha256:
        _fail(
            ReaderProvenanceError,
            "reference.reader.pin_mismatch",
            "gcsa reader provenance does not match the fixed P0-M1 pin",
        )
    return actual


def _container_image_identity(candidate: object) -> str:
    if candidate != _CONTAINER_IMAGE_ID:
        _fail(
            ReaderProvenanceError,
            "reference.reader.pin_mismatch",
            "gcsa reader provenance does not match the fixed P0-M1 pin",
        )
    return _CONTAINER_IMAGE_ID


def _live_reader() -> tuple[dict[str, object], Callable[[Path, str], object]]:
    try:
        import gcsa.io.binary_reader as binary_reader_module
        import gcsa.io.parsers.v1 as parser_module
        from gcsa.io.binary_reader import BinaryReader
    except ImportError as exc:
        raise ReaderProvenanceError(
            "reference.reader.pin_mismatch",
            "gcsa reader provenance does not match the fixed P0-M1 pin",
        ) from exc

    reader_digest = _module_source_identity(
        binary_reader_module,
        _GCSA_READER_SHA256,
    )
    parser_digest = _module_source_identity(parser_module, _GCSA_PARSER_SHA256)
    try:
        reader = BinaryReader(version=_GCSA_READER_VERSION)
    except Exception as exc:
        raise ReaderProvenanceError(
            "reference.reader.pin_mismatch",
            "gcsa reader provenance does not match the fixed P0-M1 pin",
        ) from exc
    if reader.version != _GCSA_READER_VERSION:
        _fail(
            ReaderProvenanceError,
            "reference.reader.pin_mismatch",
            "gcsa reader provenance does not match the fixed P0-M1 pin",
        )
    image_identity = _container_image_identity(
        os.environ.get("P0_M1_CONTAINER_IMAGE_ID")
    )

    def decode_entry(path: Path, stream: str) -> object:
        if stream == "FL":
            return reader.read_fl(path)
        if stream == "FH":
            return reader.read_fh(path)
        raise ValueError("unsupported fixed stream")

    provenance: dict[str, object] = {
        "repository": "gcsa",
        "commit": _GCSA_COMMIT,
        "path": _GCSA_READER_PATH,
        "symbol": _GCSA_READER_SYMBOL,
        "version": _GCSA_READER_VERSION,
        "invocation": "BinaryReader(version='v1')",
        "reader_source_sha256": reader_digest,
        "parser": {
            "path": _GCSA_PARSER_PATH,
            "sha256": parser_digest,
        },
        "environment": {
            "kind": "container-image",
            "identity": image_identity,
            "python_version": sys.version.split()[0],
            "numpy_version": np.__version__,
            "logical_invocation": (
                "golden_reference.py generate "
                "--asset-root <fixed-custody-root>"
            ),
        },
    }
    return provenance, decode_entry


def _repository_root() -> Path:
    root = Path(__file__).resolve().parents[2]
    if not root.is_absolute():
        _fail(
            ReferenceSourceError,
            "reference.source.unsafe",
            "fixed reference source must be a regular tracked file",
        )
    return root


def _generate(asset_root: Path) -> None:
    repository_root = _repository_root()
    mapping_source = _read_fixed_source(
        repository_root,
        _MAPPING_PATH,
        _MAPPING_SIZE,
    )
    selection_source = _read_fixed_source(
        repository_root,
        _SELECTION_PATH,
        _SELECTION_SIZE,
    )
    verify_reference_source_identities(mapping_source, selection_source)
    mapping_document = _decode_unique_json(mapping_source, "mapping")
    selection = _decode_unique_json(selection_source, "selection")
    mapping = mapping_document.get("mapping")
    provenance, decode_entry = _live_reader()
    reference = build_golden_reference(
        mapping=mapping,
        selection=selection,
        asset_root=asset_root,
        reader_provenance=provenance,
        decode_entry=decode_entry,
    )
    content = serialize_golden_reference(reference)
    write_golden_reference(repository_root, Path(_OUTPUT_PATH), content)


def main(argv: Sequence[str] | None = None) -> int:
    """Run the sole authorized generation operation."""

    parser = argparse.ArgumentParser(
        description="Generate the fixed D17 payload-free golden reference.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    generate = subparsers.add_parser("generate")
    generate.add_argument("--asset-root", required=True, type=Path)
    arguments = parser.parse_args(argv)
    if arguments.command == "generate":
        try:
            _generate(arguments.asset_root)
        except GoldenReferenceError as error:
            print(str(error), file=sys.stderr)
            return 1
        else:
            return 0
    parser.error("unsupported command")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
