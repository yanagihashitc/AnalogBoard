#!/usr/bin/env python3
"""Select the bounded, metadata-only inputs for the D17 golden regression."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import stat
import sys
import types
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from types import MappingProxyType
from typing import Any


_MANIFEST_PATH = PurePosixPath(
    "docs/reference/initial-recording-corpus/2026-07-17/manifest.json"
)
_CONTRACT_PATH = PurePosixPath(
    "docs/reference/initial-recording-corpus/2026-07-17/contract.json"
)
_OUTPUT_PATH = PurePosixPath(
    "docs/reference/d17-golden-regression/golden-inputs-v1.json"
)
_CORPUS_INDEX_PATH = PurePosixPath("scripts/corpus-index/corpus_index.py")
_EXPECTED_LOCATOR = "artifacts/field-session/2026-07-17-characterization"
_DENSITIES = ("low", "mid", "high")
_BIN_PATH_RE = re.compile(
    r"(?P<run_id>[0-9]{6}_[0-9]{4})_"
    r"(?P<stream>fl|fh)_(?P<ordinal>[1-9][0-9]*)[.]bin"
)
_SOURCE_UNSAFE_MESSAGE = (
    "fixed tracked metadata source must be a regular file "
    "beneath the canonical repository root"
)


@dataclass(frozen=True)
class _SourceIdentityPin:
    path: PurePosixPath
    sha256: str
    size_bytes: int


_SOURCE_PINS = MappingProxyType(
    {
        "manifest": _SourceIdentityPin(
            path=_MANIFEST_PATH,
            sha256=(
                "f51fc2759598dba71bfc51ede5d9128e63ab90a5dd70dd31e3c02bb015e56002"
            ),
            size_bytes=656_570,
        ),
        "corpus_contract": _SourceIdentityPin(
            path=_CONTRACT_PATH,
            sha256=(
                "825503ad2cb84a6262a2b2a6c88e661973a0fba4a37f77be6d04dc451b7c51e6"
            ),
            size_bytes=1_489,
        ),
        "corpus_index_validator": _SourceIdentityPin(
            path=_CORPUS_INDEX_PATH,
            sha256=(
                "d6589cdf99c733f1eea8455fcb4cdfa1c3f4289aa9662375c7382769648d87f7"
            ),
            size_bytes=47_861,
        ),
    }
)
_CORPUS_INDEX_MODULE: types.ModuleType | None = None


class GoldenSelectionError(Exception):
    """Base class for stable, typed golden-selection failures."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        self.message = message
        super().__init__(f"{code}: {message}")


class SelectionSourceError(GoldenSelectionError):
    """A tracked metadata source is not an unambiguous JSON object."""


class SelectionSchemaError(GoldenSelectionError):
    """A tracked metadata source has drifted from the pinned P0-C4 schema."""


class SelectionDensityError(GoldenSelectionError):
    """The required density mapping is incomplete."""


class SelectionDecisionRequiredError(GoldenSelectionError):
    """Selection would require expanding a frozen P0-M1 decision."""


class SelectionEntryError(GoldenSelectionError):
    """A manifest entry is invalid or ambiguous."""


class SelectionPairError(GoldenSelectionError):
    """A mechanically selected run lacks a complete FL/FH pair."""


class SelectionOutputError(GoldenSelectionError):
    """The sole allowed generated output is unsafe or unauthorized."""


class _DuplicateKeyError(ValueError):
    pass


def _unique_object(pairs: Sequence[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise _DuplicateKeyError(key)
        value[key] = item
    return value


def decode_json_document(source: object, source_name: str) -> dict[str, object]:
    """Decode one tracked JSON document while rejecting duplicate keys."""

    if type(source) is not bytes:
        raise SelectionSourceError(
            "selection.source.type",
            f"{source_name} source must be bytes",
        )
    try:
        value = json.loads(source, object_pairs_hook=_unique_object)
    except _DuplicateKeyError as exc:
        raise SelectionSourceError(
            "selection.source.duplicate_key",
            f"{source_name} source must not contain duplicate JSON keys",
        ) from exc
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise SelectionSourceError(
            "selection.source.json",
            f"{source_name} source must be valid JSON",
        ) from exc
    if type(value) is not dict:
        raise SelectionSourceError(
            "selection.source.object",
            f"{source_name} source must be a JSON object",
        )
    return value


def _stat_signature(metadata: os.stat_result) -> tuple[int, ...]:
    return (
        metadata.st_dev,
        metadata.st_ino,
        metadata.st_mode,
        metadata.st_size,
        metadata.st_mtime_ns,
        metadata.st_ctime_ns,
    )


def _read_regular_beneath_root(
    repository_root: Path,
    relative_path: PurePosixPath,
    *,
    allowed_paths: frozenset[PurePosixPath],
) -> bytes:
    if (
        not isinstance(repository_root, Path)
        or not repository_root.is_absolute()
        or not isinstance(relative_path, PurePosixPath)
        or relative_path not in allowed_paths
        or relative_path.is_absolute()
        or any(part in ("", ".", "..") for part in relative_path.parts)
    ):
        raise OSError("unsafe fixed path")
    try:
        root_mode = repository_root.lstat().st_mode
        resolved_root = repository_root.resolve(strict=True)
    except OSError as exc:
        raise OSError("unsafe repository root") from exc
    if (
        repository_root != resolved_root
        or not stat.S_ISDIR(root_mode)
        or stat.S_ISLNK(root_mode)
    ):
        raise OSError("unsafe repository root")

    no_follow = getattr(os, "O_NOFOLLOW", 0)
    directory = getattr(os, "O_DIRECTORY", 0)
    if (
        no_follow == 0
        or directory == 0
        or os.open not in os.supports_dir_fd
        or os.stat not in os.supports_dir_fd
        or os.stat not in os.supports_follow_symlinks
    ):
        raise OSError("safe descriptor traversal is unavailable")

    close_on_exec = getattr(os, "O_CLOEXEC", 0)
    directory_flags = os.O_RDONLY | directory | no_follow | close_on_exec
    file_flags = os.O_RDONLY | no_follow | close_on_exec
    descriptors: list[int] = []
    file_descriptor: int | None = None
    try:
        root_descriptor = os.open(repository_root, directory_flags)
        descriptors.append(root_descriptor)
        parent_descriptor = root_descriptor
        for component in relative_path.parts[:-1]:
            before = os.stat(
                component,
                dir_fd=parent_descriptor,
                follow_symlinks=False,
            )
            if not stat.S_ISDIR(before.st_mode) or stat.S_ISLNK(before.st_mode):
                raise OSError("unsafe parent")
            opened = os.open(
                component,
                directory_flags,
                dir_fd=parent_descriptor,
            )
            after = os.fstat(opened)
            if (
                not stat.S_ISDIR(after.st_mode)
                or (before.st_dev, before.st_ino)
                != (after.st_dev, after.st_ino)
            ):
                os.close(opened)
                raise OSError("parent identity changed")
            descriptors.append(opened)
            parent_descriptor = opened

        leaf = relative_path.parts[-1]
        before = os.stat(
            leaf,
            dir_fd=parent_descriptor,
            follow_symlinks=False,
        )
        if not stat.S_ISREG(before.st_mode) or stat.S_ISLNK(before.st_mode):
            raise OSError("unsafe leaf")
        file_descriptor = os.open(
            leaf,
            file_flags,
            dir_fd=parent_descriptor,
        )
        opened = os.fstat(file_descriptor)
        if (
            not stat.S_ISREG(opened.st_mode)
            or (before.st_dev, before.st_ino)
            != (opened.st_dev, opened.st_ino)
        ):
            raise OSError("source identity changed")

        chunks: list[bytes] = []
        while True:
            chunk = os.read(file_descriptor, 64 * 1024)
            if not chunk:
                break
            chunks.append(chunk)
        final = os.fstat(file_descriptor)
        if _stat_signature(opened) != _stat_signature(final):
            raise OSError("source changed while reading")
        return b"".join(chunks)
    finally:
        if file_descriptor is not None:
            os.close(file_descriptor)
        for descriptor in reversed(descriptors):
            os.close(descriptor)


def read_fixed_metadata_source(
    repository_root: Path,
    source_path: Path,
) -> bytes:
    """Read one immutable metadata source without symlink traversal."""

    try:
        relative_path = PurePosixPath(source_path.as_posix())
        return _read_regular_beneath_root(
            repository_root,
            relative_path,
            allowed_paths=frozenset({_MANIFEST_PATH, _CONTRACT_PATH}),
        )
    except (AttributeError, OSError, ValueError) as exc:
        raise SelectionSourceError(
            "selection.source.unsafe",
            _SOURCE_UNSAFE_MESSAGE,
        ) from exc


def require_pinned_source_identities(
    manifest_bytes: bytes,
    contract_bytes: bytes,
) -> None:
    """Fail before selection if either tracked metadata identity has drifted."""

    sources = {
        "manifest": manifest_bytes,
        "corpus_contract": contract_bytes,
    }
    for source_name, content in sources.items():
        if not _matches_source_pin(source_name, content):
            raise SelectionSourceError(
                "selection.source.identity_mismatch",
                "fixed tracked P0-C4 metadata source identity does not match its pin",
            )


def _matches_source_pin(source_name: str, content: object) -> bool:
    pin = _SOURCE_PINS[source_name]
    return (
        type(content) is bytes
        and len(content) == pin.size_bytes
        and hashlib.sha256(content).hexdigest() == pin.sha256
    )


def _load_corpus_index_module() -> types.ModuleType:
    global _CORPUS_INDEX_MODULE
    if _CORPUS_INDEX_MODULE is not None:
        return _CORPUS_INDEX_MODULE
    repository_root = _repository_root()
    try:
        source = _read_regular_beneath_root(
            repository_root,
            _CORPUS_INDEX_PATH,
            allowed_paths=frozenset({_CORPUS_INDEX_PATH}),
        )
        if not _matches_source_pin("corpus_index_validator", source):
            raise TypeError("validator source identity mismatch")
        module_name = "_analogboard_p0_c4_corpus_index"
        module = types.ModuleType(module_name)
        module.__file__ = _CORPUS_INDEX_PATH.as_posix()
        sys.modules[module_name] = module
        exec(
            compile(source, _CORPUS_INDEX_PATH.as_posix(), "exec"),
            module.__dict__,
        )
        if not callable(getattr(module, "load_contract_data", None)):
            raise TypeError("missing load_contract_data")
        if not callable(getattr(module, "validate_manifest_metadata", None)):
            raise TypeError("missing validate_manifest_metadata")
        corpus_index_error = getattr(module, "CorpusIndexError", None)
        if not isinstance(corpus_index_error, type) or not issubclass(
            corpus_index_error,
            Exception,
        ):
            raise TypeError("invalid CorpusIndexError")
    except (OSError, SyntaxError, TypeError) as exc:
        raise SelectionSchemaError(
            "selection.schema.validator",
            "unable to load the canonical P0-C4 metadata validator",
        ) from exc
    _CORPUS_INDEX_MODULE = module
    return module


def _canonical_metadata(
    manifest: Mapping[str, object],
    contract: Mapping[str, object],
) -> tuple[object, dict[str, dict[str, object]]]:
    module = _load_corpus_index_module()
    try:
        canonical_contract = module.load_contract_data(contract)
        entries = module.validate_manifest_metadata(canonical_contract, manifest)
    except module.CorpusIndexError as exc:
        validator_code = getattr(exc, "code", None)
        if not isinstance(validator_code, str) or not validator_code:
            validator_code = "validator.error.untyped"
        raise SelectionSchemaError(
            "selection.schema.invalid",
            f"P0-C4 metadata validation failed ({validator_code})",
        ) from exc
    if canonical_contract.canonical_locator != _EXPECTED_LOCATOR:
        raise SelectionSchemaError(
            "selection.schema.invalid",
            "P0-C4 metadata validation failed (selection.locator.mismatch)",
        )
    return canonical_contract, entries


def select_golden_inputs(
    manifest: Mapping[str, object],
    contract: Mapping[str, object],
) -> dict[str, object]:
    """Select the first run and lowest common FL/FH ordinal per density."""

    canonical_contract, canonical_entries = _canonical_metadata(manifest, contract)

    bins: dict[str, dict[str, dict[int, dict[str, object]]]] = {}
    for path, entry in canonical_entries.items():
        if entry["kind"] != "bin":
            continue
        match = _BIN_PATH_RE.fullmatch(path)
        if match is None:
            raise SelectionSchemaError(
                "selection.schema.invalid",
                "P0-C4 metadata validation failed (selection.bin.filename)",
            )
        run_id = match.group("run_id")
        stream = match.group("stream").upper()
        ordinal = int(match.group("ordinal"))
        bins.setdefault(run_id, {}).setdefault(stream, {})[ordinal] = {
            "stream": stream,
            "path": path,
            "sha256": entry["sha256"],
            "size_bytes": entry["size_bytes"],
        }

    runs_by_density: dict[str, list[str]] = {}
    for row in canonical_contract.run_capture_mapping:
        runs_by_density.setdefault(row.density, []).append(row.run_id)
    actual = set(runs_by_density)
    expected = set(_DENSITIES)
    extra = sorted(actual - expected)
    if extra:
        raise SelectionDecisionRequiredError(
            "selection.density.decision_required",
            f"density mapping contains an unauthorized density; extra={extra}",
        )
    missing = sorted(expected - actual)
    if missing:
        raise SelectionDensityError(
            "selection.density.mismatch",
            (
                "density mapping must contain exactly low, mid, high; "
                f"missing={missing}; extra=[]"
            ),
        )

    pairs: list[dict[str, object]] = []
    for density in _DENSITIES:
        run_id = sorted(runs_by_density[density])[0]
        run_entries = bins.get(run_id, {})
        fl = run_entries.get("FL", {})
        fh = run_entries.get("FH", {})
        common_ordinals = sorted(set(fl) & set(fh))
        if not common_ordinals:
            raise SelectionPairError(
                "selection.pair.missing",
                f"selected run has no complete FL/FH pair for density {density}",
            )
        ordinal = common_ordinals[0]
        pairs.append(
            {
                "density": density,
                "run_id": run_id,
                "ordinal": ordinal,
                "entries": [fl[ordinal], fh[ordinal]],
            }
        )

    return {
        "pair_count": len(pairs),
        "entry_count": sum(len(pair["entries"]) for pair in pairs),
        "pairs": pairs,
    }


def _source_identity(path: PurePosixPath, content: bytes) -> dict[str, object]:
    return {
        "path": path.as_posix(),
        "sha256": hashlib.sha256(content).hexdigest(),
        "size_bytes": len(content),
    }


def serialize_golden_selection(
    selection: Mapping[str, object],
    *,
    manifest_bytes: bytes,
    contract_bytes: bytes,
) -> bytes:
    """Serialize the payload-free selection with tracked source identities."""

    document = {
        "schema": "analogboard.d17.golden-input-selection",
        "schema_version": 1,
        "asset_locator": _EXPECTED_LOCATOR,
        "selection_policy": {
            "densities": list(_DENSITIES),
            "run": "lexicographically-first-per-density",
            "pair": "lowest-common-fl-fh-ordinal",
            "pair_count": 3,
            "entry_count": 6,
        },
        "sources": {
            "manifest": _source_identity(_MANIFEST_PATH, manifest_bytes),
            "corpus_contract": _source_identity(_CONTRACT_PATH, contract_bytes),
            "corpus_index_validator": _expected_output_sources()[
                "corpus_index_validator"
            ],
        },
        "pair_count": selection["pair_count"],
        "entry_count": selection["entry_count"],
        "pairs": selection["pairs"],
    }
    return (
        json.dumps(
            document,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def _validated_output(repository_root: Path, output_path: Path) -> Path:
    if (
        not isinstance(output_path, Path)
        or output_path.is_absolute()
        or output_path.as_posix() != _OUTPUT_PATH.as_posix()
    ):
        raise SelectionOutputError(
            "selection.output.path",
            f"selection output must be {_OUTPUT_PATH.as_posix()}",
        )
    try:
        resolved_root = repository_root.resolve(strict=True)
        root_mode = repository_root.lstat().st_mode
    except OSError as exc:
        raise SelectionOutputError(
            "selection.output.root",
            "repository root must be an existing canonical directory",
        ) from exc
    if (
        not repository_root.is_absolute()
        or repository_root != resolved_root
        or not stat.S_ISDIR(root_mode)
        or stat.S_ISLNK(root_mode)
    ):
        raise SelectionOutputError(
            "selection.output.root",
            "repository root must be an existing canonical directory",
        )

    current = repository_root
    for component in _OUTPUT_PATH.parent.parts:
        current = current / component
        try:
            mode = current.lstat().st_mode
        except OSError as exc:
            raise SelectionOutputError(
                "selection.output.parent",
                "selection output parent must contain only existing directories",
            ) from exc
        if not stat.S_ISDIR(mode) or stat.S_ISLNK(mode):
            raise SelectionOutputError(
                "selection.output.parent",
                "selection output parent must contain only existing directories",
            )

    target = repository_root / Path(_OUTPUT_PATH.as_posix())
    try:
        target_metadata = target.lstat()
    except FileNotFoundError:
        return target
    except OSError as exc:
        raise SelectionOutputError(
            "selection.output.target",
            "selection output must be absent or a regular file",
        ) from exc
    if (
        not stat.S_ISREG(target_metadata.st_mode)
        or stat.S_ISLNK(target_metadata.st_mode)
    ):
        raise SelectionOutputError(
            "selection.output.target",
            "selection output must be absent or a regular file",
        )
    if target_metadata.st_nlink != 1:
        raise SelectionOutputError(
            "selection.output.target",
            "selection output must be absent or a regular unaliased file",
        )
    return target


def _expected_output_sources() -> dict[str, dict[str, object]]:
    return {
        source_name: {
            "path": pin.path.as_posix(),
            "sha256": pin.sha256,
            "size_bytes": pin.size_bytes,
        }
        for source_name, pin in _SOURCE_PINS.items()
    }


def _require_existing_output_source_pins(source: bytes) -> None:
    try:
        value = json.loads(source, object_pairs_hook=_unique_object)
        if type(value) is not dict or value.get("sources") != _expected_output_sources():
            raise ValueError("source identity mismatch")
    except (
        UnicodeDecodeError,
        json.JSONDecodeError,
        _DuplicateKeyError,
        ValueError,
    ) as exc:
        raise SelectionOutputError(
            "selection.output.identity_mismatch",
            (
                "existing selection source identities do not match "
                "the immutable P0-C4 pins"
            ),
        ) from exc


def _open_selection_output_parent(repository_root: Path) -> int:
    no_follow = getattr(os, "O_NOFOLLOW", 0)
    directory = getattr(os, "O_DIRECTORY", 0)
    if (
        no_follow == 0
        or directory == 0
        or os.open not in os.supports_dir_fd
        or os.stat not in os.supports_dir_fd
        or os.stat not in os.supports_follow_symlinks
    ):
        raise SelectionOutputError(
            "selection.output.parent",
            "selection output parent cannot be opened safely",
        )
    flags = (
        os.O_RDONLY
        | directory
        | no_follow
        | getattr(os, "O_CLOEXEC", 0)
    )
    opened: int | None = None
    try:
        parent = os.open(repository_root, flags)
        for component in _OUTPUT_PATH.parent.parts:
            before = os.stat(
                component,
                dir_fd=parent,
                follow_symlinks=False,
            )
            if not stat.S_ISDIR(before.st_mode) or stat.S_ISLNK(before.st_mode):
                raise OSError("unsafe selection output parent")
            opened = os.open(component, flags, dir_fd=parent)
            after = os.fstat(opened)
            if (
                not stat.S_ISDIR(after.st_mode)
                or (before.st_dev, before.st_ino)
                != (after.st_dev, after.st_ino)
            ):
                os.close(opened)
                opened = None
                raise OSError("selection output parent changed")
            os.close(parent)
            parent = opened
            opened = None
        return parent
    except (OSError, ValueError) as exc:
        if opened is not None:
            os.close(opened)
        if "parent" in locals():
            os.close(parent)
        raise SelectionOutputError(
            "selection.output.parent",
            "selection output parent cannot be opened safely",
        ) from exc


def _open_existing_selection(parent: int) -> tuple[int, bytes] | None:
    filename = _OUTPUT_PATH.name
    try:
        before = os.stat(filename, dir_fd=parent, follow_symlinks=False)
    except FileNotFoundError:
        return None
    except OSError as exc:
        raise SelectionOutputError(
            "selection.output.target",
            "selection output must be absent or a regular unaliased file",
        ) from exc
    if (
        not stat.S_ISREG(before.st_mode)
        or stat.S_ISLNK(before.st_mode)
        or before.st_nlink != 1
    ):
        raise SelectionOutputError(
            "selection.output.target",
            "selection output must be absent or a regular unaliased file",
        )
    flags = os.O_RDWR | getattr(os, "O_NOFOLLOW", 0)
    flags |= getattr(os, "O_CLOEXEC", 0)
    try:
        descriptor = os.open(filename, flags, dir_fd=parent)
        opened = os.fstat(descriptor)
        if (
            not stat.S_ISREG(opened.st_mode)
            or opened.st_nlink != 1
            or (before.st_dev, before.st_ino)
            != (opened.st_dev, opened.st_ino)
        ):
            raise OSError("selection output identity changed")
        chunks: list[bytes] = []
        while True:
            chunk = os.read(descriptor, 64 * 1024)
            if not chunk:
                break
            chunks.append(chunk)
        after = os.fstat(descriptor)
        if _stat_signature(opened) != _stat_signature(after) or after.st_nlink != 1:
            raise OSError("selection output changed during read")
        return descriptor, b"".join(chunks)
    except OSError as exc:
        if "descriptor" in locals():
            os.close(descriptor)
        raise SelectionOutputError(
            "selection.output.target",
            "selection output must be absent or a regular unaliased file",
        ) from exc


def write_golden_selection(
    repository_root: Path,
    output_path: Path,
    content: bytes,
) -> Path:
    """Write only the approved tracked selection, without following symlinks."""

    target = _validated_output(repository_root, output_path)
    if type(content) is not bytes:
        raise SelectionOutputError(
            "selection.output.content",
            "selection output content must be bytes",
        )
    parent = _open_selection_output_parent(repository_root)
    descriptor: int | None = None
    try:
        existing = _open_existing_selection(parent)
        if existing is None:
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
            flags |= getattr(os, "O_NOFOLLOW", 0)
            flags |= getattr(os, "O_CLOEXEC", 0)
            descriptor = os.open(
                _OUTPUT_PATH.name,
                flags,
                0o644,
                dir_fd=parent,
            )
        else:
            descriptor, source = existing
            _require_existing_output_source_pins(source)
            os.lseek(descriptor, 0, os.SEEK_SET)
            os.ftruncate(descriptor, 0)
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode) or metadata.st_nlink != 1:
            raise OSError("unsafe selection output")
        offset = 0
        while offset < len(content):
            written = os.write(descriptor, content[offset:])
            if written <= 0:
                raise OSError("short selection write")
            offset += written
    except SelectionOutputError:
        raise
    except OSError as exc:
        raise SelectionOutputError(
            "selection.output.write",
            "unable to write selection output",
        ) from exc
    finally:
        if descriptor is not None:
            os.close(descriptor)
        os.close(parent)
    return target


def _repository_root() -> Path:
    try:
        return Path(__file__).resolve(strict=True).parents[2]
    except (OSError, IndexError) as exc:
        raise SelectionOutputError(
            "selection.output.root",
            "unable to locate the AnalogBoard repository root",
        ) from exc


def generate_golden_selection() -> Path:
    """Generate the sole approved output from the two fixed tracked sources."""

    repository_root = _repository_root()
    manifest_bytes = read_fixed_metadata_source(
        repository_root,
        Path(_MANIFEST_PATH.as_posix()),
    )
    contract_bytes = read_fixed_metadata_source(
        repository_root,
        Path(_CONTRACT_PATH.as_posix()),
    )
    require_pinned_source_identities(manifest_bytes, contract_bytes)
    manifest = decode_json_document(manifest_bytes, "manifest")
    contract = decode_json_document(contract_bytes, "contract")
    selection = select_golden_inputs(manifest, contract)
    content = serialize_golden_selection(
        selection,
        manifest_bytes=manifest_bytes,
        contract_bytes=contract_bytes,
    )
    return write_golden_selection(
        repository_root,
        Path(_OUTPUT_PATH.as_posix()),
        content,
    )


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate the bounded D17 golden input selection."
    )
    parser.add_subparsers(dest="command", required=True).add_parser("generate")
    return parser


def main() -> int:
    parser = _build_parser()
    args = parser.parse_args()
    try:
        if args.command == "generate":
            generate_golden_selection()
    except GoldenSelectionError as exc:
        parser.error(str(exc))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
