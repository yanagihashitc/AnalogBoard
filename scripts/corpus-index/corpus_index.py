from __future__ import annotations

import argparse
import errno
import hashlib
import json
import os
import re
import stat
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, BinaryIO, Callable, Mapping, Sequence

CONTRACT_SCHEMA = "analogboard.phase0.initial-recording-corpus-contract"
CONTRACT_SCHEMA_VERSION = 1
MANIFEST_SCHEMA = "analogboard.phase0.initial-recording-corpus-manifest"
MANIFEST_SCHEMA_VERSION = 1
REQUIRED_KINDS = ("bin", "cfg", "telemetry", "capture")
DEFAULT_CHUNK_SIZE = 1024 * 1024
METADATA_READ_CHUNK_SIZE = 64 * 1024
DEFAULT_CONTRACT_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/contract.json"
)
DEFAULT_MANIFEST_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/manifest.json"
)
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
RUN_ID_PATTERN = re.compile(r"^[0-9]{6}_[0-9]{4}$")
CONTRACT_FIELDS = frozenset(
    {
        "schema",
        "schema_version",
        "canonical_locator",
        "asset_kinds",
        "expected_total_bytes",
        "excluded_paths",
        "run_capture_mapping",
        "idle_captures",
    }
)
ASSET_KIND_FIELDS = frozenset({"kind", "expected_count", "filename_pattern"})
RUN_CAPTURE_MAPPING_FIELDS = frozenset({"run_id", "density", "capture"})
MANIFEST_FIELDS = frozenset(
    {
        "schema",
        "schema_version",
        "source_locator",
        "excluded_paths",
        "expected_counts",
        "expected_total_bytes",
        "actual_total_bytes",
        "entries",
    }
)
MANIFEST_ENTRY_FIELDS = frozenset({"kind", "path", "sha256", "size_bytes"})
OPEN_SUPPORTS_DIR_FD = os.open in os.supports_dir_fd
STAT_SUPPORTS_DIR_FD = os.stat in os.supports_dir_fd
STAT_SUPPORTS_NO_FOLLOW = os.stat in os.supports_follow_symlinks


class CorpusIndexError(Exception):
    def __init__(self, code: str, message: str) -> None:
        self.code = code
        self.message = message
        super().__init__(f"{code}: {message}")


class ContractValidationError(CorpusIndexError): pass
class PathValidationError(CorpusIndexError): pass
class CountMismatchError(CorpusIndexError): pass
class UnexpectedFileError(CorpusIndexError): pass
class ChunkSizeError(CorpusIndexError): pass
class SourceUnreadableError(CorpusIndexError): pass
class IntegrityMismatchError(CorpusIndexError): pass
class ManifestValidationError(CorpusIndexError): pass
class TotalBytesMismatchError(CorpusIndexError): pass


class _SymlinkTraversalError(OSError):
    pass


@dataclass(frozen=True)
class AssetKind:
    kind: str
    expected_count: int
    filename_pattern: str


@dataclass(frozen=True)
class RunCaptureMapping:
    run_id: str
    density: str
    capture: str


@dataclass(frozen=True)
class CorpusContract:
    canonical_locator: str
    asset_kinds: tuple[AssetKind, ...]
    expected_total_bytes: int
    excluded_paths: tuple[str, ...]
    run_capture_mapping: tuple[RunCaptureMapping, ...]
    idle_captures: tuple[str, ...]

    @property
    def expected_counts(self) -> dict[str, int]:
        return {asset_kind.kind: asset_kind.expected_count for asset_kind in self.asset_kinds}

    @property
    def run_ids(self) -> frozenset[str]:
        return frozenset(mapping.run_id for mapping in self.run_capture_mapping)

    @property
    def capture_files(self) -> frozenset[str]:
        return frozenset(
            [mapping.capture for mapping in self.run_capture_mapping]
            + list(self.idle_captures)
        )


@dataclass(frozen=True)
class DiscoveredFile:
    kind: str
    path: str
    source_path: Path


@dataclass(frozen=True)
class HashResult:
    size_bytes: int
    sha256: str


def _is_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _unknown_fields(value: Mapping[object, object], allowed: frozenset[str]) -> str:
    return ", ".join(sorted(str(key) for key in value if key not in allowed))


def _is_normalized_relative_path(value: object) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    if re.match(r"^[A-Za-z]:", value):
        return False
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        return False
    return str(path) == value


def _contract_path(value: object, field_name: str) -> str:
    if not _is_normalized_relative_path(value):
        raise ContractValidationError(
            "contract.path.invalid",
            f"{field_name} must be a normalized repository-relative path",
        )
    return str(value)


def _contract_filename(value: object, field_name: str) -> str:
    path = _contract_path(value, field_name)
    if len(PurePosixPath(path).parts) != 1:
        raise ContractValidationError(
            "contract.filename.invalid",
            f"{field_name} must be a root-level filename",
        )
    return path


def _required_string(
    value: object,
    field_name: str,
    *,
    code: str = "contract.field.invalid",
) -> str:
    if not isinstance(value, str) or not value:
        raise ContractValidationError(code, f"{field_name} must be a non-empty string")
    return value


def _require_descriptor_no_follow() -> tuple[int, int]:
    no_follow = getattr(os, "O_NOFOLLOW", 0)
    directory = getattr(os, "O_DIRECTORY", 0)
    if (
        no_follow == 0
        or directory == 0
        or not OPEN_SUPPORTS_DIR_FD
        or not STAT_SUPPORTS_DIR_FD
        or not STAT_SUPPORTS_NO_FOLLOW
    ):
        raise OSError("descriptor no-follow traversal is unavailable")
    return no_follow, directory


def _metadata_signature(metadata: os.stat_result) -> tuple[int, ...]:
    return (
        metadata.st_dev,
        metadata.st_ino,
        metadata.st_mode,
        metadata.st_size,
        metadata.st_mtime_ns,
        metadata.st_ctime_ns,
    )


def _read_regular_no_follow(
    path: Path,
    *,
    maximum_bytes: int | None = None,
) -> bytes:
    """Read a stable regular file through already-open no-follow descriptors."""
    no_follow, directory = _require_descriptor_no_follow()
    if maximum_bytes is not None and maximum_bytes < 0:
        raise ValueError("maximum_bytes must be non-negative")
    absolute = path.absolute()
    if not absolute.anchor:
        raise OSError("path must have an absolute anchor")
    close_on_exec = getattr(os, "O_CLOEXEC", 0)
    directory_flags = os.O_RDONLY | directory | no_follow | close_on_exec
    file_flags = os.O_RDONLY | no_follow | close_on_exec
    directory_descriptors: list[int] = []
    file_descriptor: int | None = None
    try:
        root_descriptor = os.open(
            absolute.anchor,
            os.O_RDONLY | directory | close_on_exec,
        )
        directory_descriptors.append(root_descriptor)
        parent_descriptor = root_descriptor
        parts = absolute.parts[1:]
        if not parts:
            raise OSError("path must identify a file")
        for part in parts[:-1]:
            before = os.stat(
                part,
                dir_fd=parent_descriptor,
                follow_symlinks=False,
            )
            if stat.S_ISLNK(before.st_mode):
                raise _SymlinkTraversalError("symlink component is not allowed")
            if not stat.S_ISDIR(before.st_mode):
                raise OSError("path component is not a directory")
            try:
                opened = os.open(
                    part,
                    directory_flags,
                    dir_fd=parent_descriptor,
                )
            except OSError as error:
                if error.errno == errno.ELOOP:
                    raise _SymlinkTraversalError(
                        "symlink component is not allowed"
                    ) from error
                raise
            try:
                after = os.fstat(opened)
            except BaseException:
                os.close(opened)
                raise
            if (
                not stat.S_ISDIR(after.st_mode)
                or (before.st_dev, before.st_ino)
                != (after.st_dev, after.st_ino)
            ):
                os.close(opened)
                raise OSError("directory identity changed during open")
            directory_descriptors.append(opened)
            parent_descriptor = opened

        leaf = parts[-1]
        before = os.stat(
            leaf,
            dir_fd=parent_descriptor,
            follow_symlinks=False,
        )
        if stat.S_ISLNK(before.st_mode):
            raise _SymlinkTraversalError("symlink leaf is not allowed")
        if not stat.S_ISREG(before.st_mode):
            raise OSError("path leaf is not a regular file")
        try:
            file_descriptor = os.open(
                leaf,
                file_flags,
                dir_fd=parent_descriptor,
            )
        except OSError as error:
            if error.errno == errno.ELOOP:
                raise _SymlinkTraversalError(
                    "symlink leaf is not allowed"
                ) from error
            raise
        opened = os.fstat(file_descriptor)
        if (
            not stat.S_ISREG(opened.st_mode)
            or (before.st_dev, before.st_ino)
            != (opened.st_dev, opened.st_ino)
        ):
            raise OSError("file identity changed during open")

        read_limit = opened.st_size
        if maximum_bytes is not None:
            read_limit = min(read_limit, maximum_bytes)
        chunks: list[bytes] = []
        remaining = read_limit
        while remaining:
            chunk = os.read(
                file_descriptor,
                min(METADATA_READ_CHUNK_SIZE, remaining),
            )
            if not chunk:
                raise OSError("file ended before its recorded size")
            chunks.append(chunk)
            remaining -= len(chunk)
        final = os.fstat(file_descriptor)
        if _metadata_signature(opened) != _metadata_signature(final):
            raise OSError("file changed while reading")
        return b"".join(chunks)
    finally:
        if file_descriptor is not None:
            os.close(file_descriptor)
        for descriptor in reversed(directory_descriptors):
            os.close(descriptor)


def _decode_metadata_json(
    payload: bytes,
    *,
    error_type: type[CorpusIndexError],
    invalid_code: str,
    invalid_message: str,
    duplicate_code: str,
    duplicate_message: str,
) -> object:
    def strict_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise error_type(duplicate_code, duplicate_message)
            result[key] = value
        return result

    try:
        return json.loads(
            payload.decode("utf-8"),
            object_pairs_hook=strict_pairs,
        )
    except error_type:
        raise
    except (UnicodeError, json.JSONDecodeError) as error:
        raise error_type(invalid_code, invalid_message) from error


def _parse_asset_kinds(value: object) -> tuple[AssetKind, ...]:
    if not isinstance(value, list):
        raise ContractValidationError(
            "contract.asset_kinds.type",
            "asset_kinds must be an array",
        )

    parsed: dict[str, AssetKind] = {}
    patterns: set[str] = set()
    for item in value:
        if not isinstance(item, dict):
            raise ContractValidationError(
                "contract.asset_kind.type",
                "each asset_kinds entry must be an object",
            )
        unknown_fields = _unknown_fields(item, ASSET_KIND_FIELDS)
        if unknown_fields:
            raise ContractValidationError(
                "contract.asset_kind.fields.unknown",
                f"unknown asset kind field(s): {unknown_fields}",
            )
        kind = _required_string(item.get("kind"), "asset_kinds.kind")
        if kind not in REQUIRED_KINDS:
            raise ContractValidationError(
                "contract.asset_kind.unknown",
                f"unknown asset kind: {kind}",
            )
        if kind in parsed:
            raise ContractValidationError(
                "contract.asset_kind.duplicate",
                f"duplicate asset kind: {kind}",
            )

        expected_count = item.get("expected_count")
        if not _is_int(expected_count) or expected_count < 1:
            raise ContractValidationError(
                "contract.expected_count.invalid",
                f"asset_kinds.{kind}.expected_count must be a positive integer",
            )

        pattern = item.get("filename_pattern")
        if not isinstance(pattern, str) or not pattern:
            raise ContractValidationError(
                "contract.filename_pattern.invalid",
                f"asset_kinds.{kind}.filename_pattern must be a non-empty regex string",
            )
        try:
            compiled = re.compile(pattern)
        except re.error as error:
            raise ContractValidationError(
                "contract.filename_pattern.invalid",
                f"asset_kinds.{kind}.filename_pattern must be a valid regex",
            ) from error
        if compiled.fullmatch("") is not None:
            raise ContractValidationError(
                "contract.filename_pattern.invalid",
                f"asset_kinds.{kind}.filename_pattern must not match an empty filename",
            )
        if kind in ("bin", "cfg") and "run_id" not in compiled.groupindex:
            raise ContractValidationError(
                "contract.filename_pattern.run_id",
                f"asset_kinds.{kind}.filename_pattern must define a run_id group",
            )
        if pattern in patterns:
            raise ContractValidationError(
                "contract.filename_pattern.duplicate",
                f"duplicate filename pattern: {pattern!r}",
            )
        patterns.add(pattern)
        parsed[kind] = AssetKind(kind, expected_count, pattern)

    missing = [kind for kind in REQUIRED_KINDS if kind not in parsed]
    if missing:
        raise ContractValidationError(
            "contract.asset_kind.missing",
            f"missing asset kind(s): {', '.join(missing)}",
        )
    return tuple(parsed[kind] for kind in REQUIRED_KINDS)


def _parse_excluded_paths(value: object) -> tuple[str, ...]:
    if not isinstance(value, list) or not value:
        raise ContractValidationError(
            "contract.excluded_paths.invalid",
            "excluded_paths must be a non-empty array",
        )
    parsed: list[str] = []
    for item in value:
        path = _contract_filename(item, "excluded_paths entry")
        if path in parsed:
            raise ContractValidationError(
                "contract.excluded_path.duplicate",
                f"duplicate excluded path: {path}",
            )
        parsed.append(path)
    return tuple(sorted(parsed))


def _parse_run_capture_mapping(
    value: object,
    asset_kinds: tuple[AssetKind, ...],
) -> tuple[RunCaptureMapping, ...]:
    if not isinstance(value, list) or not value:
        raise ContractValidationError(
            "contract.run_capture_mapping.invalid",
            "run_capture_mapping must be a non-empty array",
        )
    capture_kind = next(
        asset_kind
        for asset_kind in asset_kinds
        if asset_kind.kind == "capture"
    )
    parsed: list[RunCaptureMapping] = []
    seen_run_ids: set[str] = set()
    for item in value:
        if not isinstance(item, dict):
            raise ContractValidationError(
                "contract.run_capture_mapping.type",
                "each run_capture_mapping entry must be an object",
            )
        unknown_fields = _unknown_fields(item, RUN_CAPTURE_MAPPING_FIELDS)
        if unknown_fields:
            raise ContractValidationError(
                "contract.run_capture_mapping.fields.unknown",
                f"unknown run/capture mapping field(s): {unknown_fields}",
            )
        run_id = _required_string(item.get("run_id"), "run_capture_mapping.run_id")
        density = _required_string(item.get("density"), "run_capture_mapping.density")
        capture = _contract_filename(item.get("capture"), "run_capture_mapping.capture")
        if RUN_ID_PATTERN.fullmatch(run_id) is None:
            raise ContractValidationError(
                "contract.run_capture_mapping.run_id",
                f"run_id must match YYMMDD_HHMM: {run_id}",
            )
        if run_id in seen_run_ids:
            raise ContractValidationError(
                "contract.run_capture_mapping.duplicate",
                f"duplicate run_id: {run_id}",
            )
        if re.fullmatch(capture_kind.filename_pattern, capture) is None:
            raise ContractValidationError(
                "contract.run_capture_mapping.capture",
                f"capture does not match the capture kind: {capture}",
            )
        seen_run_ids.add(run_id)
        parsed.append(RunCaptureMapping(run_id, density, capture))
    return tuple(parsed)


def _parse_idle_captures(
    value: object,
    capture_kind: AssetKind,
) -> tuple[str, ...]:
    if not isinstance(value, list):
        raise ContractValidationError(
            "contract.idle_captures.type",
            "idle_captures must be an array",
        )
    parsed: list[str] = []
    for item in value:
        capture = _contract_filename(item, "idle_captures entry")
        if re.fullmatch(capture_kind.filename_pattern, capture) is None:
            raise ContractValidationError(
                "contract.idle_captures.capture",
                f"idle capture does not match the capture kind: {capture}",
            )
        if capture in parsed:
            raise ContractValidationError(
                "contract.idle_captures.duplicate",
                f"duplicate idle capture: {capture}",
            )
        parsed.append(capture)
    return tuple(parsed)


def load_contract_data(value: object) -> CorpusContract:
    if not isinstance(value, dict):
        raise ContractValidationError("contract.type", "contract must be an object")

    unknown_fields = _unknown_fields(value, CONTRACT_FIELDS)
    if unknown_fields:
        raise ContractValidationError(
            "contract.fields.unknown",
            f"unknown contract field(s): {unknown_fields}",
        )
    if "schema" not in value:
        raise ContractValidationError("contract.schema.missing", "schema is required")
    if value.get("schema") != CONTRACT_SCHEMA:
        raise ContractValidationError(
            "contract.schema.unsupported",
            f"schema must be '{CONTRACT_SCHEMA}'",
        )

    schema_version = value.get("schema_version")
    if not _is_int(schema_version):
        raise ContractValidationError(
            "contract.schema_version.type",
            "schema_version must be an integer",
        )
    if schema_version != CONTRACT_SCHEMA_VERSION:
        raise ContractValidationError(
            "contract.schema_version.unsupported",
            f"schema_version must be {CONTRACT_SCHEMA_VERSION}",
        )

    canonical_locator = _contract_path(
        value.get("canonical_locator"),
        "canonical_locator",
    )
    asset_kinds = _parse_asset_kinds(value.get("asset_kinds"))

    expected_total_bytes = value.get("expected_total_bytes")
    if not _is_int(expected_total_bytes) or expected_total_bytes < 0:
        raise ContractValidationError(
            "contract.expected_total_bytes.invalid",
            "expected_total_bytes must be a non-negative integer",
        )

    excluded_paths = _parse_excluded_paths(value.get("excluded_paths"))
    run_capture_mapping = _parse_run_capture_mapping(
        value.get("run_capture_mapping"),
        asset_kinds,
    )
    counts = {asset_kind.kind: asset_kind.expected_count for asset_kind in asset_kinds}
    if len(run_capture_mapping) != counts["cfg"]:
        raise ContractValidationError(
            "contract.run_capture_mapping.count",
            f"run/capture mapping expected {counts['cfg']} run(s), found {len(run_capture_mapping)}",
        )
    capture_kind = next(item for item in asset_kinds if item.kind == "capture")
    idle_captures = _parse_idle_captures(value.get("idle_captures"), capture_kind)
    mapped_captures = {mapping.capture for mapping in run_capture_mapping}
    overlap = sorted(mapped_captures.intersection(idle_captures))
    if overlap:
        raise ContractValidationError(
            "contract.capture_set.overlap",
            f"capture cannot be both run-mapped and idle: {overlap[0]}",
        )
    capture_count = len(mapped_captures.union(idle_captures))
    if capture_count != counts["capture"]:
        raise ContractValidationError(
            "contract.capture_set.count",
            f"capture set expected {counts['capture']} unique file(s), found {capture_count}",
        )
    return CorpusContract(
        canonical_locator=canonical_locator,
        asset_kinds=asset_kinds,
        expected_total_bytes=expected_total_bytes,
        excluded_paths=excluded_paths,
        run_capture_mapping=run_capture_mapping,
        idle_captures=idle_captures,
    )


def load_contract(path: Path) -> CorpusContract:
    try:
        payload = _read_regular_no_follow(path)
    except _SymlinkTraversalError as error:
        raise ContractValidationError(
            "contract.path.symlink",
            "contract must be a regular file without symlink traversal",
        ) from error
    except OSError as error:
        raise ContractValidationError(
            "contract.json.invalid",
            "contract must contain valid UTF-8 JSON",
        ) from error
    value = _decode_metadata_json(
        payload,
        error_type=ContractValidationError,
        invalid_code="contract.json.invalid",
        invalid_message="contract must contain valid UTF-8 JSON",
        duplicate_code="contract.json.duplicate_key",
        duplicate_message="contract must not contain duplicate JSON keys",
    )
    return load_contract_data(value)


def _relative_parts(path: str) -> tuple[str, ...]:
    return PurePosixPath(path).parts


def _validate_existing_directory(repo_root: Path, relative_path: str) -> Path:
    try:
        root = repo_root.resolve(strict=True)
    except OSError as error:
        raise PathValidationError(
            "path.root.invalid",
            "repository root must be an existing directory",
        ) from error
    if not root.is_dir():
        raise PathValidationError(
            "path.root.invalid",
            "repository root must be an existing directory",
        )

    current = root
    traversed: list[str] = []
    for part in _relative_parts(relative_path):
        current = current / part
        traversed.append(part)
        display_path = "/".join(traversed)
        try:
            metadata = current.lstat()
        except OSError as error:
            raise PathValidationError(
                "path.missing",
                f"path does not exist: {display_path}",
            ) from error
        if stat.S_ISLNK(metadata.st_mode):
            raise PathValidationError(
                "path.symlink",
                f"symlink is not allowed: {display_path}",
            )
    if not current.is_dir():
        raise PathValidationError(
            "path.not_directory",
            f"path is not a directory: {relative_path}",
        )
    return current


def _classify_file(name: str, contract: CorpusContract) -> str | None:
    matches: list[str] = []
    for asset_kind in contract.asset_kinds:
        match = re.fullmatch(asset_kind.filename_pattern, name)
        if match is None:
            continue
        if asset_kind.kind == "capture" and name not in contract.capture_files:
            continue
        if asset_kind.kind in ("bin", "cfg") and match.group("run_id") not in contract.run_ids:
            continue
        matches.append(asset_kind.kind)
    if len(matches) > 1:
        raise ContractValidationError(
            "contract.filename_pattern.ambiguous",
            f"filename matches multiple asset kinds: {name}",
        )
    return matches[0] if matches else None


def discover_files(repo_root: Path, contract: CorpusContract) -> tuple[DiscoveredFile, ...]:
    corpus_root = _validate_existing_directory(repo_root, contract.canonical_locator)
    excluded = set(contract.excluded_paths)
    discovered: list[DiscoveredFile] = []
    try:
        with os.scandir(corpus_root) as iterator:
            entries = sorted(iterator, key=lambda entry: entry.name)
    except OSError as error:
        raise SourceUnreadableError(
            "source.unreadable",
            "directory is not readable: .",
        ) from error
    for entry in entries:
        try:
            if entry.is_symlink():
                raise PathValidationError(
                    "path.symlink",
                    f"symlink is not allowed: {entry.name}",
                )
            is_directory = entry.is_dir(follow_symlinks=False)
            is_file = entry.is_file(follow_symlinks=False)
        except OSError as error:
            raise SourceUnreadableError(
                "source.unreadable",
                f"file is not readable: {entry.name}",
            ) from error
        if entry.name in excluded:
            if not is_directory:
                raise PathValidationError(
                    "path.exclusion.invalid",
                    f"excluded path is not a regular directory: {entry.name}",
                )
            continue
        if is_directory:
            raise PathValidationError(
                "path.layout.invalid",
                f"nested source directory is not allowed: {entry.name}",
            )
        if not is_file:
            raise PathValidationError(
                "path.type.invalid",
                f"source is not a regular file: {entry.name}",
            )
        kind = _classify_file(entry.name, contract)
        if kind is None:
            raise UnexpectedFileError(
                "discovery.unexpected_file",
                f"unexpected file: {entry.name}",
            )
        discovered.append(DiscoveredFile(kind, entry.name, Path(entry.path)))
    discovered.sort(key=lambda item: item.path)
    actual_counts = {
        kind: sum(item.kind == kind for item in discovered)
        for kind in REQUIRED_KINDS
    }
    for kind in REQUIRED_KINDS:
        expected = contract.expected_counts[kind]
        actual = actual_counts[kind]
        if actual != expected:
            raise CountMismatchError(
                "count.mismatch",
                f"{kind} expected {expected} file(s), found {actual}",
            )
    return tuple(discovered)


def _secure_binary_opener(path: Path, mode: str) -> BinaryIO:
    if mode != "rb":
        raise ValueError("binary source opener requires rb mode")
    try:
        metadata = path.lstat()
    except OSError:
        raise
    if stat.S_ISLNK(metadata.st_mode):
        raise OSError("symlink source is not allowed")
    if not stat.S_ISREG(metadata.st_mode):
        raise OSError("source is not a regular file")

    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    descriptor = os.open(path, flags)
    try:
        opened_metadata = os.fstat(descriptor)
        if not stat.S_ISREG(opened_metadata.st_mode):
            raise OSError("opened source is not a regular file")
        if (metadata.st_dev, metadata.st_ino) != (
            opened_metadata.st_dev,
            opened_metadata.st_ino,
        ):
            raise OSError("source identity changed during open")
        return os.fdopen(descriptor, "rb")
    except BaseException:
        os.close(descriptor)
        raise


def _source_signature(metadata: os.stat_result) -> tuple[int, ...]:
    return (
        metadata.st_dev,
        metadata.st_ino,
        metadata.st_mode,
        metadata.st_size,
        metadata.st_mtime_ns,
        metadata.st_ctime_ns,
    )


def hash_file(
    path: Path,
    *,
    chunk_size: int = DEFAULT_CHUNK_SIZE,
    display_path: str,
    opener: Callable[[Path, str], BinaryIO] | None = None,
) -> HashResult:
    if not _is_int(chunk_size) or chunk_size < 1:
        raise ChunkSizeError(
            "hash.chunk_size.invalid",
            "chunk_size must be a positive integer",
        )
    if not _is_normalized_relative_path(display_path):
        raise PathValidationError(
            "path.display.invalid",
            "display_path must be a normalized relative path",
        )

    digest = hashlib.sha256()
    size_bytes = 0
    open_binary = opener or _secure_binary_opener
    try:
        with open_binary(path, "rb") as source:
            initial_metadata = (
                os.fstat(source.fileno())
                if opener is None or opener is _secure_binary_opener
                else None
            )
            while True:
                chunk = source.read(chunk_size)
                if not isinstance(chunk, bytes):
                    raise OSError("source read did not return bytes")
                if not chunk:
                    break
                digest.update(chunk)
                size_bytes += len(chunk)
            if initial_metadata is not None:
                final_metadata = os.fstat(source.fileno())
                if (
                    _source_signature(initial_metadata) != _source_signature(final_metadata)
                    or size_bytes != final_metadata.st_size
                ):
                    raise OSError("source changed while reading")
    except OSError as error:
        raise SourceUnreadableError(
            "source.unreadable",
            f"file is not readable: {display_path}",
        ) from error
    return HashResult(size_bytes=size_bytes, sha256=digest.hexdigest())


def build_manifest(
    repo_root: Path,
    contract: CorpusContract,
    *,
    chunk_size: int = DEFAULT_CHUNK_SIZE,
) -> dict[str, Any]:
    discovered = discover_files(repo_root, contract)
    entries: list[dict[str, Any]] = []
    total_bytes = 0
    for item in discovered:
        identity = hash_file(
            item.source_path,
            chunk_size=chunk_size,
            display_path=item.path,
        )
        total_bytes += identity.size_bytes
        entries.append(
            {
                "kind": item.kind,
                "path": item.path,
                "sha256": identity.sha256,
                "size_bytes": identity.size_bytes,
            }
        )
    if total_bytes != contract.expected_total_bytes:
        raise TotalBytesMismatchError(
            "total_bytes.mismatch",
            f"total bytes expected {contract.expected_total_bytes}, found {total_bytes}",
        )

    return {
        "schema": MANIFEST_SCHEMA,
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "source_locator": contract.canonical_locator,
        "excluded_paths": list(contract.excluded_paths),
        "expected_counts": contract.expected_counts,
        "expected_total_bytes": contract.expected_total_bytes,
        "actual_total_bytes": total_bytes,
        "entries": entries,
    }


def serialize_manifest(manifest: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(
            manifest,
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")


def _manifest_error(code: str, message: str) -> ManifestValidationError:
    return ManifestValidationError(code, message)


def _validate_manifest_header(
    manifest: object,
    contract: CorpusContract,
) -> tuple[list[dict[str, Any]], int]:
    if not isinstance(manifest, dict):
        raise _manifest_error("manifest.type", "manifest must be an object")
    unknown_fields = _unknown_fields(manifest, MANIFEST_FIELDS)
    if unknown_fields:
        raise _manifest_error(
            "manifest.fields.unknown",
            f"unknown manifest field(s): {unknown_fields}",
        )
    if manifest.get("schema") != MANIFEST_SCHEMA:
        raise _manifest_error(
            "manifest.schema.unsupported",
            f"schema must be '{MANIFEST_SCHEMA}'",
        )
    schema_version = manifest.get("schema_version")
    if not _is_int(schema_version) or schema_version != MANIFEST_SCHEMA_VERSION:
        raise _manifest_error(
            "manifest.schema_version.unsupported",
            f"schema_version must be {MANIFEST_SCHEMA_VERSION}",
        )

    source_locator = manifest.get("source_locator")
    if not _is_normalized_relative_path(source_locator):
        raise _manifest_error(
            "manifest.source_locator.invalid",
            "source_locator must be a normalized repository-relative path",
        )
    if source_locator != contract.canonical_locator:
        raise _manifest_error(
            "manifest.source_locator.mismatch",
            "source_locator does not match the contract",
        )
    if manifest.get("excluded_paths") != list(contract.excluded_paths):
        raise _manifest_error(
            "manifest.excluded_paths.mismatch",
            "excluded_paths do not match the contract",
        )
    expected_counts = manifest.get("expected_counts")
    if (
        not isinstance(expected_counts, dict)
        or set(expected_counts) != set(REQUIRED_KINDS)
        or any(not _is_int(value) or value < 1 for value in expected_counts.values())
    ):
        raise _manifest_error(
            "manifest.expected_counts.invalid",
            "expected_counts must contain positive integer values for all required kinds",
        )
    if expected_counts != contract.expected_counts:
        raise _manifest_error(
            "manifest.expected_counts.mismatch",
            "expected_counts do not match the contract",
        )
    expected_total_bytes = manifest.get("expected_total_bytes")
    if not _is_int(expected_total_bytes) or expected_total_bytes < 0:
        raise _manifest_error(
            "manifest.expected_total_bytes.invalid",
            "expected_total_bytes must be a non-negative integer",
        )
    if expected_total_bytes != contract.expected_total_bytes:
        raise _manifest_error(
            "manifest.expected_total_bytes.mismatch",
            "expected_total_bytes does not match the contract",
        )
    actual_total_bytes = manifest.get("actual_total_bytes")
    if not _is_int(actual_total_bytes) or actual_total_bytes < 0:
        raise _manifest_error(
            "manifest.actual_total_bytes.invalid",
            "actual_total_bytes must be a non-negative integer",
        )
    if actual_total_bytes != contract.expected_total_bytes:
        raise _manifest_error(
            "manifest.actual_total_bytes.mismatch",
            "actual_total_bytes does not match the contract",
        )

    entries = manifest.get("entries")
    if not isinstance(entries, list):
        raise _manifest_error("manifest.entries.type", "entries must be an array")
    return entries, actual_total_bytes


def _validate_manifest_entries(
    entries: list[dict[str, Any]],
    contract: CorpusContract,
) -> dict[str, dict[str, Any]]:
    by_path: dict[str, dict[str, Any]] = {}
    ordered_paths: list[str] = []
    actual_counts = {kind: 0 for kind in REQUIRED_KINDS}
    for entry in entries:
        if not isinstance(entry, dict):
            raise _manifest_error(
                "manifest.entry.type",
                "each manifest entry must be an object",
            )
        unknown_fields = _unknown_fields(entry, MANIFEST_ENTRY_FIELDS)
        if unknown_fields:
            raise _manifest_error(
                "manifest.entry.fields.unknown",
                f"unknown manifest entry field(s): {unknown_fields}",
            )
        path = entry.get("path")
        if not _is_normalized_relative_path(path):
            raise _manifest_error(
                "manifest.path.invalid",
                "manifest entry path must be a normalized relative path",
            )
        if len(PurePosixPath(path).parts) != 1:
            raise _manifest_error(
                "manifest.path.layout",
                f"manifest entry must be root-level: {path}",
            )
        if path in by_path:
            raise _manifest_error(
                "manifest.path.duplicate",
                f"duplicate manifest path: {path}",
            )
        kind = entry.get("kind")
        if kind not in contract.expected_counts:
            raise _manifest_error(
                "manifest.kind.invalid",
                f"unknown manifest kind for {path}: {kind}",
            )
        classified_kind = _classify_file(path, contract)
        if classified_kind != kind:
            raise _manifest_error(
                "manifest.kind.mismatch",
                f"kind does not match filename grammar: {path}",
            )
        size_bytes = entry.get("size_bytes")
        if not _is_int(size_bytes) or size_bytes < 0:
            raise _manifest_error(
                "manifest.size.invalid",
                f"size_bytes must be a non-negative integer: {path}",
            )
        sha256 = entry.get("sha256")
        if not isinstance(sha256, str) or SHA256_PATTERN.fullmatch(sha256) is None:
            raise _manifest_error(
                "manifest.sha256.invalid",
                f"sha256 must be lowercase hexadecimal: {path}",
            )
        by_path[path] = entry
        ordered_paths.append(path)
        actual_counts[kind] += 1
    if ordered_paths != sorted(ordered_paths):
        raise _manifest_error(
            "manifest.order.invalid",
            "manifest entries must be sorted by path",
        )
    for kind in REQUIRED_KINDS:
        expected = contract.expected_counts[kind]
        actual = actual_counts[kind]
        if actual != expected:
            raise _manifest_error(
                "manifest.count.mismatch",
                f"{kind} expected {expected} manifest entry(s), found {actual}",
            )
    return by_path


def validate_manifest_metadata(
    contract: CorpusContract,
    manifest: object,
) -> dict[str, dict[str, Any]]:
    """Validate frozen manifest metadata without reading corpus assets."""
    entries, actual_total_bytes = _validate_manifest_header(manifest, contract)
    by_path = _validate_manifest_entries(entries, contract)
    if sum(entry["size_bytes"] for entry in by_path.values()) != actual_total_bytes:
        raise _manifest_error(
            "manifest.entry_total_bytes.mismatch",
            "manifest entry sizes do not match actual_total_bytes",
        )
    return by_path


def verify_manifest(
    repo_root: Path,
    contract: CorpusContract,
    manifest: object,
    *,
    chunk_size: int = DEFAULT_CHUNK_SIZE,
) -> None:
    manifest_by_path = validate_manifest_metadata(contract, manifest)
    discovered = discover_files(repo_root, contract)
    discovered_by_path = {item.path: item for item in discovered}

    missing = sorted(set(discovered_by_path) - set(manifest_by_path))
    unexpected = sorted(set(manifest_by_path) - set(discovered_by_path))
    if missing or unexpected:
        missing_text = ",".join(missing) if missing else "-"
        unexpected_text = ",".join(unexpected) if unexpected else "-"
        raise _manifest_error(
            "manifest.paths.mismatch",
            f"manifest paths differ: missing={missing_text}; unexpected={unexpected_text}",
        )

    recomputed_total = 0
    for path in sorted(manifest_by_path):
        entry = manifest_by_path[path]
        discovered_file = discovered_by_path[path]
        if entry["kind"] != discovered_file.kind:
            raise _manifest_error(
                "manifest.kind.mismatch",
                f"kind mismatch for {path}: recorded {entry['kind']}, actual {discovered_file.kind}",
            )
        identity = hash_file(
            discovered_file.source_path,
            chunk_size=chunk_size,
            display_path=path,
        )
        recomputed_total += identity.size_bytes
        if entry["size_bytes"] != identity.size_bytes:
            raise IntegrityMismatchError(
                "integrity.size_mismatch",
                f"size mismatch for {path}: recorded {entry['size_bytes']}, actual {identity.size_bytes}",
            )
        if entry["sha256"] != identity.sha256:
            raise IntegrityMismatchError(
                "integrity.sha256_mismatch",
                f"SHA-256 mismatch for {path}: recorded {entry['sha256']}, actual {identity.sha256}",
            )

    if recomputed_total != contract.expected_total_bytes:
        raise TotalBytesMismatchError(
            "total_bytes.mismatch",
            f"total bytes expected {contract.expected_total_bytes}, found {recomputed_total}",
        )
    if manifest["actual_total_bytes"] != recomputed_total:
        raise IntegrityMismatchError(
            "integrity.total_bytes_mismatch",
            f"total byte mismatch: recorded {manifest['actual_total_bytes']}, actual {recomputed_total}",
        )


def load_manifest(path: Path) -> object:
    try:
        payload = _read_regular_no_follow(path)
    except _SymlinkTraversalError as error:
        raise ManifestValidationError(
            "manifest.path.symlink",
            "manifest must be a regular file without symlink traversal",
        ) from error
    except OSError as error:
        raise ManifestValidationError(
            "manifest.json.invalid",
            "manifest must contain valid UTF-8 JSON",
        ) from error
    return _decode_metadata_json(
        payload,
        error_type=ManifestValidationError,
        invalid_code="manifest.json.invalid",
        invalid_message="manifest must contain valid UTF-8 JSON",
        duplicate_code="manifest.json.duplicate_key",
        duplicate_message="manifest must not contain duplicate JSON keys",
    )


def _cli_relative_path(value: str, field_name: str) -> str:
    if not _is_normalized_relative_path(value):
        raise PathValidationError(
            "path.cli.invalid",
            f"{field_name} must be a normalized repository-relative path",
        )
    return value


def _write_manifest(repo_root: Path, relative_output: str, payload: bytes) -> None:
    if relative_output != DEFAULT_MANIFEST_PATH:
        raise PathValidationError(
            "path.output.scope",
            f"output must be {DEFAULT_MANIFEST_PATH}",
        )
    parent_relative = str(PurePosixPath(relative_output).parent)
    if parent_relative == ".":
        parent = repo_root.resolve(strict=True)
    else:
        parent = _validate_existing_directory(repo_root, parent_relative)
    destination = parent / PurePosixPath(relative_output).name
    temporary_path: Path | None = None
    try:
        try:
            destination_metadata = destination.lstat()
        except FileNotFoundError:
            destination_metadata = None
        if destination_metadata is not None and stat.S_ISLNK(destination_metadata.st_mode):
            raise PathValidationError(
                "path.symlink",
                f"symlink is not allowed: {relative_output}",
            )
        if destination_metadata is not None and not stat.S_ISREG(destination_metadata.st_mode):
            raise PathValidationError(
                "path.output.invalid",
                f"output is not a regular file: {relative_output}",
            )
        output_mode = (
            stat.S_IMODE(destination_metadata.st_mode)
            if destination_metadata is not None
            else 0o644
        )
        descriptor, temporary_name = tempfile.mkstemp(
            dir=parent,
            prefix=f".{destination.name}.",
            suffix=".tmp",
        )
        temporary_path = Path(temporary_name)
        with os.fdopen(descriptor, "wb") as output:
            os.fchmod(output.fileno(), output_mode)
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_path, destination)
        temporary_path = None
    except CorpusIndexError:
        raise
    except OSError as error:
        if temporary_path is not None:
            try:
                temporary_path.unlink()
            except FileNotFoundError:
                pass
            except OSError as cleanup_error:
                error = cleanup_error
        raise PathValidationError(
            "path.output.unwritable",
            f"output is not writable: {relative_output}",
        ) from error


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build or verify the payload-free P0-C4 corpus manifest."
    )
    parser.add_argument("--repo-root", default=".", help="Repository root.")
    parser.add_argument(
        "--contract",
        default=DEFAULT_CONTRACT_PATH,
        help="Normalized repository-relative contract path.",
    )
    parser.add_argument(
        "--chunk-size",
        type=int,
        default=DEFAULT_CHUNK_SIZE,
        help="Bounded streaming read size in bytes.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    build_parser = subparsers.add_parser("build", help="Build a deterministic manifest.")
    build_parser.add_argument(
        "--output",
        required=True,
        help="Normalized repository-relative output path.",
    )

    verify_parser = subparsers.add_parser(
        "verify",
        help="Recompute source size and SHA-256 for every manifest entry.",
    )
    verify_parser.add_argument(
        "--manifest",
        required=True,
        help="Normalized repository-relative manifest path.",
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    parser = _build_parser()
    arguments = parser.parse_args(argv)
    try:
        repo_root = Path(arguments.repo_root)
        contract_relative = _cli_relative_path(arguments.contract, "contract")
        if contract_relative != DEFAULT_CONTRACT_PATH:
            raise PathValidationError(
                "path.contract.scope",
                f"contract must be {DEFAULT_CONTRACT_PATH}",
            )
        contract = load_contract(repo_root / PurePosixPath(contract_relative))
        if arguments.command == "build":
            output_relative = _cli_relative_path(arguments.output, "output")
            if output_relative != DEFAULT_MANIFEST_PATH:
                raise PathValidationError(
                    "path.output.scope",
                    f"output must be {DEFAULT_MANIFEST_PATH}",
                )
            locator_prefix = f"{contract.canonical_locator}/"
            if (
                output_relative == contract.canonical_locator
                or output_relative.startswith(locator_prefix)
            ):
                raise PathValidationError(
                    "path.output.asset",
                    "output must be outside the corpus asset locator",
                )
            manifest = build_manifest(
                repo_root,
                contract,
                chunk_size=arguments.chunk_size,
            )
            _write_manifest(repo_root, output_relative, serialize_manifest(manifest))
            print(f"manifest built: {output_relative}")
            return 0

        manifest_relative = _cli_relative_path(arguments.manifest, "manifest")
        manifest = load_manifest(repo_root / PurePosixPath(manifest_relative))
        verify_manifest(
            repo_root,
            contract,
            manifest,
            chunk_size=arguments.chunk_size,
        )
        print(f"manifest verified: {manifest_relative}")
        return 0
    except CorpusIndexError as error:
        print(f"ERROR {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
