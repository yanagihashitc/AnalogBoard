#!/usr/bin/env python3
"""Validate and extract the immutable P0-S dependency bundle without network I/O."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
from typing import Iterable
import zipfile


EXPECTED_ARCHIVE_NAME = "analogboard-p0-s-dependencies-20260721.tar.gz"
EXPECTED_ARCHIVE_SHA256 = (
    "4fbae209a47b050ee21f8d48091393a093e02ed396c108e90bf1cf59c0af019f"
)
EXPECTED_BUNDLE_ROOT = "analogboard-p0-s-dependencies-20260721"
EXPECTED_MANIFEST_SHA256 = (
    "71886c544cc15b101eca4ae67e149a9c9d284b76096df4011af747e7336282d0"
)
EXPECTED_SOURCE_ARCHIVE = (
    "sources/c-blosc-616f4b7343a8479f7e71dd3d7025bd92c9a6bbd0.zip"
)
EXPECTED_SOURCE_ARCHIVE_SHA256 = (
    "31b005197faa7ffd63983fadca9d36a3259371eb98f17ad21073987d0a112afc"
)
EXPECTED_SOURCE_MANIFEST = "sources/c-blosc-source-tree.sha256"
EXPECTED_SOURCE_MANIFEST_SHA256 = (
    "700070935e041ced3695b5314bb44a56770ee4bdab021890033a6d3723f6675e"
)
EXPECTED_SOURCE_ROOT = "c-blosc-616f4b7343a8479f7e71dd3d7025bd92c9a6bbd0"
EXPECTED_SOURCE_FILES = 490
EXPECTED_SOURCE_DIRECTORIES = 60
DEPENDENCY_BUNDLE_ENV = "ANALOGBOARD_P0S_DEPENDENCY_BUNDLE"
_MANIFEST_LINE = re.compile(r"([0-9a-f]{64})  ([^\r\n]+)\Z")
_BINARY_MAGICS = (
    b"\x7fELF",
    b"MZ",
    b"!<arch>\n",
    b"PK\x03\x04",
    b"\x1f\x8b",
    b"7z\xbc\xaf\x27\x1c",
)


class ValidationError(RuntimeError):
    """Report one stable fail-closed bundle validation error."""


def sha256_file(path: Path) -> str:
    """Hash a file without loading it into memory."""
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_repository_destination(destination: Path) -> Path:
    """Require the single ignored repository-local dependency cache path."""
    repository_root = Path(__file__).resolve().parents[3]
    expected = repository_root / ".deps" / "p0-s"
    resolved_expected = expected.resolve()
    candidate = destination.resolve()
    if candidate != resolved_expected:
        raise ValidationError(
            "dependency extraction destination must be the repository-local "
            f"ignored cache {expected}"
        )
    for path in (expected.parent, expected):
        if path.is_symlink():
            raise ValidationError(
                f"dependency extraction path must not be a symlink: {path}"
            )
        if path.exists() and not path.is_dir():
            raise ValidationError(
                f"dependency extraction path is not a directory: {path}"
            )
    return resolved_expected


def _validate_relative_path(value: str, *, label: str) -> PurePosixPath:
    """Return a canonical relative POSIX path or fail before extraction."""
    path = PurePosixPath(value)
    if (
        not value
        or "\\" in value
        or path.is_absolute()
        or str(path) != value
        or any(part in {"", ".", ".."} for part in path.parts)
    ):
        raise ValidationError(f"unsafe {label} path: {value!r}")
    return path


def validate_archive_identity(archive: Path) -> None:
    """Require the exact owner-approved archive name and SHA-256."""
    if not archive.is_file():
        raise ValidationError(
            "approved dependency bundle is absent: expected "
            f"{EXPECTED_ARCHIVE_NAME} with SHA-256 {EXPECTED_ARCHIVE_SHA256}"
        )
    if archive.name != EXPECTED_ARCHIVE_NAME:
        raise ValidationError(
            "unexpected dependency archive name: expected "
            f"{EXPECTED_ARCHIVE_NAME}, got {archive.name}"
        )
    actual = sha256_file(archive)
    if actual != EXPECTED_ARCHIVE_SHA256:
        raise ValidationError(
            "dependency archive SHA-256 mismatch: expected "
            f"{EXPECTED_ARCHIVE_SHA256}, got {actual}"
        )


def validate_member(member: tarfile.TarInfo) -> None:
    """Reject traversal, links, and special archive member types."""
    _validate_relative_path(member.name.rstrip("/"), label="archive member")
    if not (member.isfile() or member.isdir()):
        raise ValidationError(
            f"unsupported archive member type for {member.name!r}"
        )


def parse_manifest(
    text: str,
    *,
    require_dot_prefix: bool = False,
) -> dict[str, str]:
    """Parse canonical sha256sum output and reject duplicate paths."""
    entries: dict[str, str] = {}
    if not text.endswith("\n"):
        raise ValidationError("MANIFEST.sha256 must end with one newline")
    for line_number, line in enumerate(text.splitlines(), start=1):
        match = _MANIFEST_LINE.fullmatch(line)
        if match is None:
            raise ValidationError(
                f"invalid MANIFEST.sha256 line {line_number}: {line!r}"
            )
        digest, raw_path = match.groups()
        if require_dot_prefix:
            if not raw_path.startswith("./"):
                raise ValidationError(
                    f"source manifest entry lacks ./ prefix: {raw_path}"
                )
            raw_path = raw_path[2:]
        path = str(_validate_relative_path(raw_path, label="manifest entry"))
        if path in entries:
            raise ValidationError(f"duplicate manifest entry: {path}")
        entries[path] = digest
    if not entries:
        raise ValidationError("MANIFEST.sha256 has no entries")
    return entries


def _expected_directories(file_paths: Iterable[str]) -> set[str]:
    directories = {"."}
    for value in file_paths:
        parent = PurePosixPath(value).parent
        while str(parent) != ".":
            directories.add(str(parent))
            parent = parent.parent
    return directories


def verify_extracted_tree(root: Path, manifest: dict[str, str]) -> None:
    """Reject missing, extra, linked, or hash-mismatched extracted files."""
    if root.is_symlink() or not root.is_dir():
        raise ValidationError(f"extracted bundle root is invalid: {root}")
    expected_files = set(manifest) | {"MANIFEST.sha256"}
    actual_files: set[str] = set()
    actual_directories = {"."}
    for current, directories, files in os.walk(root, followlinks=False):
        current_path = Path(current)
        relative_current = current_path.relative_to(root)
        relative_current_text = (
            "." if relative_current == Path(".") else relative_current.as_posix()
        )
        actual_directories.add(relative_current_text)
        for directory in directories:
            candidate = current_path / directory
            if candidate.is_symlink():
                raise ValidationError(f"symlink is forbidden: {candidate}")
        for filename in files:
            candidate = current_path / filename
            relative = candidate.relative_to(root).as_posix()
            if candidate.is_symlink():
                raise ValidationError(f"symlink is forbidden: {candidate}")
            if not candidate.is_file():
                raise ValidationError(f"non-file payload is forbidden: {candidate}")
            actual_files.add(relative)
    missing = sorted(expected_files - actual_files)
    extra = sorted(actual_files - expected_files)
    if missing:
        raise ValidationError(f"missing extracted file: {missing[0]}")
    if extra:
        raise ValidationError(f"unexpected extracted file: {extra[0]}")
    expected_directories = _expected_directories(expected_files)
    extra_directories = sorted(actual_directories - expected_directories)
    if extra_directories:
        raise ValidationError(
            f"unexpected extracted directory: {extra_directories[0]}"
        )
    for relative, expected_digest in manifest.items():
        actual_digest = sha256_file(root / relative)
        if actual_digest != expected_digest:
            raise ValidationError(
                f"extracted file SHA-256 mismatch for {relative}: "
                f"expected {expected_digest}, got {actual_digest}"
            )


def safe_extract(archive: Path, destination: Path) -> None:
    """Validate every tar member before writing an otherwise-empty destination."""
    if destination.is_symlink() or (
        destination.exists() and not destination.is_dir()
    ):
        raise ValidationError(f"extraction destination is invalid: {destination}")
    with tarfile.open(archive, "r:gz") as stream:
        members = stream.getmembers()
        seen_paths: set[str] = set()
        for member in members:
            validate_member(member)
            canonical = member.name.rstrip("/")
            if canonical in seen_paths:
                raise ValidationError(f"duplicate archive member: {canonical}")
            seen_paths.add(canonical)
        if destination.exists() and any(destination.iterdir()):
            raise ValidationError(
                f"extraction destination is not empty: {destination}"
            )
        destination.mkdir(parents=True, exist_ok=True)
        for member in members:
            target = destination.joinpath(*PurePosixPath(member.name).parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = stream.extractfile(member)
            if source is None:
                raise ValidationError(f"cannot read archive member: {member.name}")
            with source, target.open("xb") as output:
                shutil.copyfileobj(source, output, length=1024 * 1024)


def _read_embedded_manifest(archive: Path) -> tuple[str, dict[str, str]]:
    """Load the pinned embedded manifest before extracting any payload."""
    manifest_member = f"{EXPECTED_BUNDLE_ROOT}/MANIFEST.sha256"
    with tarfile.open(archive, "r:gz") as stream:
        try:
            member = stream.getmember(manifest_member)
        except KeyError as error:
            raise ValidationError("approved archive is missing MANIFEST.sha256") from error
        source = stream.extractfile(member)
        if source is None:
            raise ValidationError("approved archive MANIFEST.sha256 is unreadable")
        raw = source.read()
        actual_digest = hashlib.sha256(raw).hexdigest()
        if actual_digest != EXPECTED_MANIFEST_SHA256:
            raise ValidationError(
                "embedded MANIFEST.sha256 mismatch: expected "
                f"{EXPECTED_MANIFEST_SHA256}, got {actual_digest}"
            )
        try:
            text = raw.decode("utf-8", errors="strict")
        except UnicodeDecodeError as error:
            raise ValidationError("MANIFEST.sha256 is not valid UTF-8") from error
    return text, parse_manifest(text)


def _validate_archive_inventory(archive: Path, manifest: dict[str, str]) -> None:
    """Require exactly the root, manifest, payload files, and their parents."""
    expected_files = {
        f"{EXPECTED_BUNDLE_ROOT}/MANIFEST.sha256",
        *(f"{EXPECTED_BUNDLE_ROOT}/{path}" for path in manifest),
    }
    expected_directories = {f"{EXPECTED_BUNDLE_ROOT}/"}
    for path in expected_files:
        parent = PurePosixPath(path).parent
        while str(parent) != ".":
            expected_directories.add(f"{parent}/")
            parent = parent.parent
    with tarfile.open(archive, "r:gz") as stream:
        members = stream.getmembers()
        for member in members:
            validate_member(member)
        file_names = {member.name for member in members if member.isfile()}
        directory_names = {
            member.name if member.name.endswith("/") else f"{member.name}/"
            for member in members
            if member.isdir()
        }
    missing = sorted(expected_files - file_names)
    extra = sorted(file_names - expected_files)
    if missing:
        raise ValidationError(f"archive is missing expected file: {missing[0]}")
    if extra:
        raise ValidationError(f"archive contains unexpected file: {extra[0]}")
    missing_directories = sorted(expected_directories - directory_names)
    extra_directories = sorted(directory_names - expected_directories)
    if missing_directories:
        raise ValidationError(
            f"archive is missing expected directory: {missing_directories[0]}"
        )
    if extra_directories:
        raise ValidationError(
            f"archive contains unexpected directory: {extra_directories[0]}"
        )


def _run_sha256sum_check(root: Path, manifest_path: Path) -> None:
    """Run the requested platform sha256sum verifier without shell expansion."""
    try:
        result = subprocess.run(
            ["sha256sum", "-c", str(manifest_path)],
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError as error:
        raise ValidationError("sha256sum is required but unavailable") from error
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise ValidationError(
            f"sha256sum -c failed with exit {result.returncode}: {detail}"
        )


def _validate_zip_path(value: str) -> PurePosixPath:
    """Accept canonical ZIP paths while allowing a directory trailing slash."""
    return _validate_relative_path(value.rstrip("/"), label="source ZIP entry")


def _source_payload_kind(prefix: bytes) -> str | None:
    for magic in _BINARY_MAGICS:
        if prefix.startswith(magic):
            return magic.hex()
    return None


def _audit_source_zip(source_archive: Path) -> dict[str, int]:
    """Audit source ZIP members, links, module metadata, and binary signatures."""
    file_count = 0
    directory_count = 0
    symlink_count = 0
    unsafe_count = 0
    unexpected_binary_count = 0
    gitmodules_present = False
    with zipfile.ZipFile(source_archive) as stream:
        seen_paths: set[str] = set()
        for info in stream.infolist():
            try:
                path = _validate_zip_path(info.filename)
            except ValidationError:
                unsafe_count += 1
                continue
            canonical = str(path)
            if canonical in seen_paths:
                raise ValidationError(f"duplicate source ZIP entry: {canonical}")
            seen_paths.add(canonical)
            mode = info.external_attr >> 16
            if stat.S_ISLNK(mode):
                symlink_count += 1
                continue
            if info.is_dir():
                directory_count += 1
                continue
            file_count += 1
            if path.name == ".gitmodules":
                gitmodules_present = True
            with stream.open(info) as source:
                kind = _source_payload_kind(source.read(8))
            if kind is not None:
                unexpected_binary_count += 1
    if unsafe_count:
        raise ValidationError(
            f"source ZIP contains {unsafe_count} unsafe traversal entries"
        )
    if symlink_count:
        raise ValidationError(f"source ZIP contains {symlink_count} symlinks")
    if gitmodules_present:
        raise ValidationError("source ZIP unexpectedly contains .gitmodules")
    if file_count != EXPECTED_SOURCE_FILES or directory_count != EXPECTED_SOURCE_DIRECTORIES:
        raise ValidationError(
            "source ZIP entry count mismatch: expected "
            f"{EXPECTED_SOURCE_FILES} files/{EXPECTED_SOURCE_DIRECTORIES} directories, "
            f"got {file_count} files/{directory_count} directories"
        )
    if unexpected_binary_count:
        raise ValidationError(
            "source ZIP contains unexpected ELF/PE/shared/archive payload: "
            f"count={unexpected_binary_count}"
        )
    return {
        "archive_traversal_entries": unsafe_count,
        "directories": directory_count,
        "files": file_count,
        "gitmodules_present": int(gitmodules_present),
        "symlinks": symlink_count,
        "unexpected_binary_payloads": unexpected_binary_count,
    }


def _safe_extract_zip(source_archive: Path, destination: Path) -> None:
    """Extract a validated regular-file ZIP into an empty directory."""
    if destination.is_symlink() or (
        destination.exists() and not destination.is_dir()
    ):
        raise ValidationError(
            f"source extraction destination is invalid: {destination}"
        )
    if destination.exists() and any(destination.iterdir()):
        raise ValidationError(f"source extraction destination is not empty: {destination}")
    with zipfile.ZipFile(source_archive) as stream:
        infos = stream.infolist()
        seen_paths: set[str] = set()
        for info in infos:
            path = _validate_zip_path(info.filename)
            canonical = str(path)
            if canonical in seen_paths:
                raise ValidationError(f"duplicate source ZIP entry: {canonical}")
            seen_paths.add(canonical)
            mode = info.external_attr >> 16
            if stat.S_ISLNK(mode):
                raise ValidationError(f"source ZIP symlink is forbidden: {info.filename}")
        destination.mkdir(parents=True, exist_ok=True)
        for info in infos:
            target = destination.joinpath(*PurePosixPath(info.filename).parts)
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            with stream.open(info) as source, target.open("xb") as output:
                shutil.copyfileobj(source, output, length=1024 * 1024)


def _verify_source_tree(source_root: Path, manifest_path: Path) -> None:
    """Require the extracted source tree to match every pinned file hash."""
    manifest_digest = sha256_file(manifest_path)
    if manifest_digest != EXPECTED_SOURCE_MANIFEST_SHA256:
        raise ValidationError(
            "source-tree manifest SHA-256 mismatch: expected "
            f"{EXPECTED_SOURCE_MANIFEST_SHA256}, got {manifest_digest}"
        )
    manifest = parse_manifest(
        manifest_path.read_text(encoding="utf-8"),
        require_dot_prefix=True,
    )
    if len(manifest) != EXPECTED_SOURCE_FILES:
        raise ValidationError(
            f"source-tree manifest file count mismatch: {len(manifest)}"
        )
    verify_extracted_tree_without_manifest(source_root, manifest)
    _run_sha256sum_check(source_root, manifest_path.resolve())


def verify_extracted_tree_without_manifest(
    root: Path,
    manifest: dict[str, str],
) -> None:
    """Verify a tree whose external manifest must not appear inside the tree."""
    if root.is_symlink() or not root.is_dir():
        raise ValidationError(f"extracted source root is invalid: {root}")
    actual_files: set[str] = set()
    directory_count = 0
    for current, directories, files in os.walk(root, followlinks=False):
        directory_count += 1
        current_path = Path(current)
        for directory in directories:
            if (current_path / directory).is_symlink():
                raise ValidationError(f"source symlink is forbidden: {directory}")
        for filename in files:
            candidate = current_path / filename
            if candidate.is_symlink() or not candidate.is_file():
                raise ValidationError(f"source non-file payload is forbidden: {candidate}")
            relative = candidate.relative_to(root).as_posix()
            actual_files.add(relative)
            with candidate.open("rb") as stream:
                prefix = stream.read(8)
            if _source_payload_kind(prefix) is not None:
                raise ValidationError(
                    f"source binary/archive payload is forbidden: {relative}"
                )
    expected_files = set(manifest)
    missing = sorted(expected_files - actual_files)
    extra = sorted(actual_files - expected_files)
    if missing:
        raise ValidationError(f"missing source file: {missing[0]}")
    if extra:
        raise ValidationError(f"unexpected source file: {extra[0]}")
    if directory_count != EXPECTED_SOURCE_DIRECTORIES:
        raise ValidationError(
            "source directory count mismatch: expected "
            f"{EXPECTED_SOURCE_DIRECTORIES}, got {directory_count}"
        )
    for relative, expected_digest in manifest.items():
        actual_digest = sha256_file(root / relative)
        if actual_digest != expected_digest:
            raise ValidationError(
                f"source file SHA-256 mismatch for {relative}: "
                f"expected {expected_digest}, got {actual_digest}"
            )


def validate_and_extract(archive: Path, destination: Path) -> dict[str, object]:
    """Validate all pins and return deterministic evidence for the extracted bundle."""
    validate_archive_identity(archive)
    _, manifest = _read_embedded_manifest(archive)
    _validate_archive_inventory(archive, manifest)
    bundle_root = destination / EXPECTED_BUNDLE_ROOT
    if bundle_root.exists():
        verify_extracted_tree(bundle_root, manifest)
    else:
        safe_extract(archive, destination)
        verify_extracted_tree(bundle_root, manifest)
    _run_sha256sum_check(bundle_root, bundle_root / "MANIFEST.sha256")

    source_archive = bundle_root / EXPECTED_SOURCE_ARCHIVE
    if sha256_file(source_archive) != EXPECTED_SOURCE_ARCHIVE_SHA256:
        raise ValidationError("approved c-blosc source archive identity changed")
    source_audit = _audit_source_zip(source_archive)
    source_parent = destination / "source"
    source_root = source_parent / EXPECTED_SOURCE_ROOT
    if not source_root.exists():
        _safe_extract_zip(source_archive, source_parent)
    _verify_source_tree(source_root, bundle_root / EXPECTED_SOURCE_MANIFEST)

    return {
        "archive": {
            "name": EXPECTED_ARCHIVE_NAME,
            "sha256": EXPECTED_ARCHIVE_SHA256,
        },
        "bundle": {
            "manifest_entries": len(manifest),
            "manifest_sha256": EXPECTED_MANIFEST_SHA256,
            "root": EXPECTED_BUNDLE_ROOT,
        },
        "policy": {
            "download": "forbidden",
            "external_lz4": "forbidden",
            "source_vendoring": "forbidden",
        },
        "source": {
            **source_audit,
            "archive_sha256": EXPECTED_SOURCE_ARCHIVE_SHA256,
            "root": EXPECTED_SOURCE_ROOT,
            "tree_manifest_sha256": EXPECTED_SOURCE_MANIFEST_SHA256,
        },
        "status": "pass",
    }


def _write_json_atomic(path: Path, value: dict[str, object]) -> None:
    """Publish deterministic evidence using a same-directory atomic rename."""
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = json.dumps(value, indent=2, sort_keys=True) + "\n"
    descriptor, temporary_name = tempfile.mkstemp(
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
        text=True,
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary_name, path)
    except BaseException:
        Path(temporary_name).unlink(missing_ok=True)
        raise


def _parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--archive",
        type=Path,
        help=f"exact {EXPECTED_ARCHIVE_NAME}; defaults to {DEPENDENCY_BUNDLE_ENV}",
    )
    parser.add_argument(
        "--destination",
        type=Path,
        default=Path(__file__).resolve().parents[3] / ".deps" / "p0-s",
    )
    parser.add_argument("--evidence", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = _parse_args(sys.argv[1:] if argv is None else argv)
    archive = args.archive
    if archive is None:
        configured = os.environ.get(DEPENDENCY_BUNDLE_ENV)
        if configured:
            archive = Path(configured)
    if archive is None:
        raise ValidationError(
            f"{DEPENDENCY_BUNDLE_ENV} is unset: expected {EXPECTED_ARCHIVE_NAME} "
            f"with SHA-256 {EXPECTED_ARCHIVE_SHA256}; no download is permitted"
        )
    destination = validate_repository_destination(args.destination)
    evidence = validate_and_extract(archive.resolve(), destination)
    if args.evidence is not None:
        _write_json_atomic(args.evidence.resolve(), evidence)
    sys.stdout.write(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
    return 0


def cli(argv: list[str] | None = None) -> int:
    """Map expected filesystem/archive failures to one stable CLI surface."""
    try:
        return main(argv)
    except (
        ValidationError,
        OSError,
        tarfile.TarError,
        zipfile.BadZipFile,
    ) as error:
        sys.stderr.write(f"bundle validation failed: {error}\n")
        return 2


if __name__ == "__main__":
    raise SystemExit(cli())
