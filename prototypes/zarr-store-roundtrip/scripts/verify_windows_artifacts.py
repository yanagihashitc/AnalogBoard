#!/usr/bin/env python3
"""Verify pinned c-blosc COFF/CRT data and linked smoke dependencies."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
from typing import Sequence


EXPECTED_OBJECT_COUNT = 12
_MACHINE_LINE = re.compile(r"(?im)^\s*([0-9a-f]+) machine \(([^)]+)\)\s*$")
_DEFAULT_LIBRARY = re.compile(r"(?im)/DEFAULTLIB:([^\s]+)")
_DLL_LINE = re.compile(r"(?im)^\s+([A-Za-z0-9_.-]+\.dll)\s*$")
_RELEASE_EXACT_DLLS = {
    "bcrypt.dll",
    "kernel32.dll",
    "msvcp140.dll",
    "vcruntime140.dll",
    "vcruntime140_1.dll",
}
_DEBUG_EXACT_DLLS = {
    "bcrypt.dll",
    "kernel32.dll",
    "msvcp140d.dll",
    "vcruntime140d.dll",
    "vcruntime140_1d.dll",
    "ucrtbased.dll",
}
_RELEASE_UCRT_PREFIX = "api-ms-win-crt-"


class VerificationError(RuntimeError):
    """Report one stable fail-closed Windows artifact verification error."""


def _configuration(value: str) -> str:
    if value not in {"Release", "Debug"}:
        raise VerificationError(f"unsupported configuration: {value}")
    return value


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_library_reports(
    headers: str,
    directives: str,
    *,
    configuration: str,
) -> dict[str, object]:
    """Require 12 x64 objects and the configuration's dynamic CRT directive."""
    configuration = _configuration(configuration)
    machines = _MACHINE_LINE.findall(headers)
    if len(machines) != EXPECTED_OBJECT_COUNT:
        raise VerificationError(
            "c-blosc library must contain exactly "
            f"{EXPECTED_OBJECT_COUNT} x64 objects; dumpbin reported {len(machines)}"
        )
    non_x64 = [
        f"{code} ({name})"
        for code, name in machines
        if code != "8664" or name != "x64"
    ]
    if non_x64:
        raise VerificationError(
            "c-blosc library contains a non-x64 object: " + non_x64[0]
        )

    default_libraries = {
        library.upper() for library in _DEFAULT_LIBRARY.findall(directives)
    }
    expected_crt = "MSVCRT" if configuration == "Release" else "MSVCRTD"
    forbidden_crt = "MSVCRTD" if configuration == "Release" else "MSVCRT"
    if expected_crt not in default_libraries:
        raise VerificationError(
            f"{configuration} c-blosc library lacks /DEFAULTLIB:{expected_crt}"
        )
    if forbidden_crt in default_libraries:
        raise VerificationError(
            f"{configuration} c-blosc library contains forbidden /DEFAULTLIB:{forbidden_crt}"
        )
    return {
        "configuration": configuration,
        "crt": expected_crt,
        "default_libraries": sorted(default_libraries),
        "object_count": len(machines),
    }


def verify_smoke_report(
    report: str,
    *,
    configuration: str,
) -> dict[str, object]:
    """Require x64 and only the approved Windows/MSVC/UCRT system DLL set."""
    configuration = _configuration(configuration)
    machines = _MACHINE_LINE.findall(report)
    if not machines or machines[0] != ("8664", "x64"):
        raise VerificationError(f"{configuration} smoke executable is not x64")

    dependencies = {name.lower() for name in _DLL_LINE.findall(report)}
    if "bcrypt.dll" not in dependencies:
        raise VerificationError(
            f"{configuration} smoke executable does not import bcrypt.dll"
        )
    if configuration == "Release":
        unknown = sorted(
            dependency
            for dependency in dependencies
            if dependency not in _RELEASE_EXACT_DLLS
            and not dependency.startswith(_RELEASE_UCRT_PREFIX)
        )
        required = _RELEASE_EXACT_DLLS
    else:
        unknown = sorted(dependencies - _DEBUG_EXACT_DLLS)
        required = _DEBUG_EXACT_DLLS
    if unknown:
        raise VerificationError(
            f"{configuration} smoke executable imports forbidden or unknown DLL: "
            f"{unknown[0]}"
        )
    missing = sorted(required - dependencies)
    if missing:
        raise VerificationError(
            f"{configuration} smoke executable lacks expected runtime DLL: {missing[0]}"
        )
    return {
        "architecture": "x64",
        "configuration": configuration,
        "dependencies": sorted(dependencies),
    }


def _read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8-sig", errors="strict")
    except (OSError, UnicodeError) as error:
        raise VerificationError(f"cannot read dumpbin report {path}: {error}") from error


def _report_record(path: Path, result: dict[str, object]) -> dict[str, object]:
    return {
        "path": path.as_posix(),
        "sha256": sha256_file(path),
        "verification": result,
    }


def _write_json_atomic(path: Path, document: dict[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    serialized = json.dumps(document, indent=2, sort_keys=True) + "\n"
    with tempfile.NamedTemporaryFile(
        "w",
        encoding="utf-8",
        newline="\n",
        dir=path.parent,
        prefix=f".{path.name}.",
        suffix=".tmp",
        delete=False,
    ) as stream:
        stream.write(serialized)
        stream.flush()
        os.fsync(stream.fileno())
        temporary = Path(stream.name)
    try:
        temporary.replace(path)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise


def verify_from_args(arguments: argparse.Namespace) -> dict[str, object]:
    result: dict[str, object] = {"libraries": {}, "smoke_executables": []}
    for configuration, headers_path, directives_path in (
        ("Release", arguments.release_headers, arguments.release_directives),
        ("Debug", arguments.debug_headers, arguments.debug_directives),
    ):
        verification = verify_library_reports(
            _read_text(headers_path),
            _read_text(directives_path),
            configuration=configuration,
        )
        result["libraries"][configuration] = {
            "directives": _report_record(directives_path, verification),
            "headers": _report_record(headers_path, verification),
        }
    for configuration, paths in (
        ("Release", arguments.release_smoke),
        ("Debug", arguments.debug_smoke),
    ):
        for path in paths:
            verification = verify_smoke_report(
                _read_text(path), configuration=configuration
            )
            result["smoke_executables"].append(_report_record(path, verification))
    result["smoke_executables"].sort(key=lambda record: str(record["path"]))
    return result


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--release-headers", type=Path, required=True)
    parser.add_argument("--release-directives", type=Path, required=True)
    parser.add_argument("--debug-headers", type=Path, required=True)
    parser.add_argument("--debug-directives", type=Path, required=True)
    parser.add_argument("--release-smoke", type=Path, action="append", required=True)
    parser.add_argument("--debug-smoke", type=Path, action="append", required=True)
    parser.add_argument("--evidence", type=Path)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    try:
        arguments = _parser().parse_args(argv)
        result = verify_from_args(arguments)
        if arguments.evidence is not None:
            _write_json_atomic(arguments.evidence, result)
        print(json.dumps(result, indent=2, sort_keys=True))
        return 0
    except VerificationError as error:
        print(f"verification failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
