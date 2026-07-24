#!/usr/bin/env python3
"""Build and verify the bounded P0-M1 closeout evidence."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import stat
import sys
from typing import Any, Callable, Mapping, Sequence


CLOSEOUT_SCHEMA = "analogboard.d17.golden-regression-closeout"
CLOSEOUT_SCHEMA_VERSION = 1
DEFAULT_CLOSEOUT_PATH = (
    "docs/reference/d17-golden-regression/closeout-v1.json"
)
MAX_CLOSEOUT_BYTES = 32_768
MAX_TREE_DEPTH = 24
MAX_TREE_NODES = 2_048
MAX_STRING_LENGTH = 4_096

GCSA_COMMIT = "20689a991697217518ec2ff15aaaa2533b169eb0"
REFACTOR_PROFILE_BLOB = "d60f1eb1e82f9d060bd9035f973e38c0dc22dcbc"
REVIEW_PROFILE_BLOB = "16a514b3835216bf8d12726db5aa733c29531b95"


class CloseoutFailure(Exception):
    """Stable, locator-free closeout failure."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        super().__init__(f"{code}: {message}")


@dataclass(frozen=True)
class SourceDeclaration:
    path: str
    sha256: str
    size_bytes: int
    schema: str | None = None
    schema_version: int | None = None
    git_blob: str | None = None


@dataclass(frozen=True)
class AcceptanceCondition:
    condition_id: str
    evidence_roles: tuple[str, ...]


FIXED_SOURCE_DECLARATIONS: dict[str, SourceDeclaration] = {
    "plan": SourceDeclaration(
        path="docs/plans/260710-analogboard-rebuild-plan.html",
        sha256="3d67f43d7a3dd85cad8f08c0f973925cb4478cf422b348b22467491b90120721",
        size_bytes=294_784,
    ),
    "p0_c4_contract": SourceDeclaration(
        path="docs/reference/initial-recording-corpus/2026-07-17/contract.json",
        sha256="825503ad2cb84a6262a2b2a6c88e661973a0fba4a37f77be6d04dc451b7c51e6",
        size_bytes=1_489,
        schema="analogboard.phase0.initial-recording-corpus-contract",
        schema_version=1,
    ),
    "p0_c4_manifest": SourceDeclaration(
        path="docs/reference/initial-recording-corpus/2026-07-17/manifest.json",
        sha256="f51fc2759598dba71bfc51ede5d9128e63ab90a5dd70dd31e3c02bb015e56002",
        size_bytes=656_570,
        schema="analogboard.phase0.initial-recording-corpus-manifest",
        schema_version=1,
    ),
    "p0_c4_closeout": SourceDeclaration(
        path="docs/reference/initial-recording-corpus/2026-07-17/closeout.json",
        sha256="aea10f08e685212a60a15cc1968f37a575d501a7a07fece00955a42ae973109a",
        size_bytes=6_315,
        schema="analogboard.phase0.initial-recording-corpus-closeout",
        schema_version=1,
    ),
    "channel_mapping": SourceDeclaration(
        path="docs/reference/d17-golden-regression/channel-mapping-v1.json",
        sha256="8e197eade3fff0f7427c7cf0e9d77409624b803a51a782c6a429e705f15fc99b",
        size_bytes=1_349,
        schema="analogboard.d17.channel-mapping",
        schema_version=1,
    ),
    "mapping_tool": SourceDeclaration(
        path="scripts/d17-golden-regression/mapping_contract.py",
        sha256="912fee15ca24a095f64bff68fa73c8411823b14326282717b57eed09d7ebf38c",
        size_bytes=22_642,
    ),
    "mapping_tests": SourceDeclaration(
        path="scripts/d17-golden-regression/tests/test_mapping_contract.py",
        sha256="a482ffbb3ed86e25597fd1f51f89bf84560fd28d6fa38d7c788436bcd389b8bc",
        size_bytes=41_540,
    ),
    "golden_inputs": SourceDeclaration(
        path="docs/reference/d17-golden-regression/golden-inputs-v1.json",
        sha256="30feceee3ea5f054d3ad43528f82c1e0228a0a855f8c2d55cb0b7f2732b42975",
        size_bytes=1_953,
        schema="analogboard.d17.golden-input-selection",
        schema_version=1,
    ),
    "corpus_index_validator": SourceDeclaration(
        path="scripts/corpus-index/corpus_index.py",
        sha256="d6589cdf99c733f1eea8455fcb4cdfa1c3f4289aa9662375c7382769648d87f7",
        size_bytes=47_861,
    ),
    "selection_tool": SourceDeclaration(
        path="scripts/d17-golden-regression/golden_selection.py",
        sha256="f597356e23d422a1c98aec6d992d7eab69e46366bdc3b9b1cf4728c05c80e90f",
        size_bytes=23_076,
    ),
    "selection_tests": SourceDeclaration(
        path="scripts/d17-golden-regression/tests/test_golden_selection.py",
        sha256="0207640732a364e5d8d0e42d4f1d658dbb6a4d2221493a6fb21c4f9115f94d83",
        size_bytes=36_584,
    ),
    "golden_reference": SourceDeclaration(
        path="docs/reference/d17-golden-regression/golden-reference-v1.json",
        sha256="3f531bd624ad3ea8b763b7ec82da42f313fbd4976945c6cd1f636fab9636f53f",
        size_bytes=13_281,
        schema="analogboard.d17.golden-reference",
        schema_version=1,
    ),
    "reference_tool": SourceDeclaration(
        path="scripts/d17-golden-regression/golden_reference.py",
        sha256="cb475a216bbab2880527990536d4b7a5a2b042e36d6f58d5c6f5d51786c3e7d6",
        size_bytes=40_236,
    ),
    "reference_tests": SourceDeclaration(
        path="scripts/d17-golden-regression/tests/test_golden_reference.py",
        sha256="3c2b685a3486e5c1d50030cea70a493c315def43d7b16f63f710ab626d1360c7",
        size_bytes=41_450,
    ),
    "regression_harness": SourceDeclaration(
        path="scripts/d17-golden-regression/regression_harness.py",
        sha256="faadda0ad5da03b48a8a3251efdf9d3a9070c6218f28328e6ca6679fe9dbc914",
        size_bytes=34_415,
    ),
    "regression_tests": SourceDeclaration(
        path="scripts/d17-golden-regression/tests/test_regression_harness.py",
        sha256="097b505e48f9d1e7b3113fa32fead103337c24ffbb289e6a1a174b33b6c40a82",
        size_bytes=41_076,
    ),
    "closeout_tests": SourceDeclaration(
        path="scripts/d17-golden-regression/tests/test_closeout.py",
        sha256="1179f5c0a2f6c067d93a6ae8320257f642a1667c09302f986ccef8d3954c3ce3",
        size_bytes=23_394,
    ),
    "phase1_contract": SourceDeclaration(
        path="docs/reference/d17-golden-regression/phase1-connection-v1.md",
        sha256="fa657a82e055068f04e8b809500931fac93cec9e17932c7a154bcc7aba1ea02e",
        size_bytes=3_693,
    ),
    "refactor_profile": SourceDeclaration(
        path=".agent/checkpoint-profiles/p0-m1-v1/refactor.md",
        sha256="10e4dea487d53b9be4c700705e2df75def2dc0bdb8ca663c75415aabce1cd644",
        size_bytes=5_991,
        git_blob=REFACTOR_PROFILE_BLOB,
    ),
    "review_profile": SourceDeclaration(
        path=".agent/checkpoint-profiles/p0-m1-v1/review.md",
        sha256="8688b65c140ab628520dabeb07bb6c4ac2cf182e9638b98f3249e6083d4502ab",
        size_bytes=6_067,
        git_blob=REVIEW_PROFILE_BLOB,
    ),
}


ACCEPTANCE_CONDITIONS = (
    AcceptanceCondition(
        "P0-M1-1",
        ("channel_mapping", "mapping_tool", "mapping_tests"),
    ),
    AcceptanceCondition(
        "P0-M1-2",
        (
            "p0_c4_contract",
            "p0_c4_manifest",
            "p0_c4_closeout",
            "golden_inputs",
            "corpus_index_validator",
            "selection_tool",
            "selection_tests",
        ),
    ),
    AcceptanceCondition(
        "P0-M1-3",
        ("golden_reference", "reference_tool", "reference_tests"),
    ),
    AcceptanceCondition(
        "P0-M1-4",
        ("regression_harness", "regression_tests"),
    ),
    AcceptanceCondition(
        "P0-M1-5",
        ("phase1_contract", "regression_harness"),
    ),
    AcceptanceCondition(
        "P0-M1-6",
        (
            "plan",
            "p0_c4_closeout",
            "refactor_profile",
            "review_profile",
            "closeout_tests",
        ),
    ),
)


MANDATORY_MUTATIONS = (
    "channel_permutation",
    "label_mapping",
    "channel_missing",
    "channel_excess",
    "dtype_drift",
    "shape_drift",
    "value_digest_drift",
)


AUTHORITY_DAG = [
    {
        "consumer": "channel_mapping",
        "inputs": ["plan"],
        "external_pin": {
            "commit": GCSA_COMMIT,
            "path": "src/gcsa/constants.py",
            "repository": "gcsa",
            "symbols": [
                "FL_CHANNEL_MAP",
                "FH_CHANNEL_MAP",
                "FL_CHANNEL_NAMES",
                "FH_CHANNEL_NAMES",
                "ALL_CHANNEL_NAMES",
            ],
        },
    },
    {
        "consumer": "golden_inputs",
        "inputs": [
            "p0_c4_contract",
            "p0_c4_manifest",
            "corpus_index_validator",
        ],
    },
    {
        "consumer": "golden_reference",
        "inputs": ["channel_mapping", "golden_inputs"],
        "external_pin": {
            "commit": GCSA_COMMIT,
            "path": "src/gcsa/io/binary_reader.py",
            "repository": "gcsa",
            "symbol": "BinaryReader",
            "version": "v1",
        },
    },
    {
        "consumer": "regression_harness",
        "inputs": ["golden_reference"],
    },
    {
        "consumer": "phase1_contract",
        "inputs": [
            "channel_mapping",
            "golden_inputs",
            "golden_reference",
            "regression_harness",
        ],
    },
    {
        "consumer": "closeout",
        "inputs": list(FIXED_SOURCE_DECLARATIONS),
    },
]


REGRESSION_EVIDENCE = {
    "candidate_schema": "analogboard.d17.candidate-summary",
    "candidate_schema_version": 1,
    "channel_count_per_pair": 13,
    "channel_records": 39,
    "failure_policy": "typed_fail_closed",
    "mandatory_mutations": list(MANDATORY_MUTATIONS),
    "pair_count": 3,
    "pass_rule": "all_39_mapping_dtype_shape_digest_statistics_match",
}


FROZEN_CORPUS_CONSISTENCY = {
    "canonical_contract_role": "p0_c4_contract",
    "canonical_manifest_role": "p0_c4_manifest",
    "p0_c4_closeout_role": "p0_c4_closeout",
    "selected_asset_bytes": 18_720_000,
    "selected_entry_count": 6,
    "selected_pair_count": 3,
    "selection_role": "golden_inputs",
    "verdict": "verified",
}


BOUNDARIES = {
    "additional_hardware_run": "not_performed",
    "artifacts": "read_only",
    "d17": "unchanged",
    "gcsa": "read_only",
    "git_evidence": "digests_and_bounded_statistics_only",
    "product_decoder_writer": "phase_1_out_of_scope",
}


SCOPE = {
    "status": "gate_ready",
    "step_id": "P0-M1",
    "transition": "pending_human_merge_and_live_verification",
}


MANUAL_GATE = {
    "base_branch": "main",
    "head_branch": "analysis/phase0-d17-golden",
    "next_scope_transition": "not_authorized",
    "pre_merge_action": "create_pr_then_stop",
    "required_action": "human_merge",
    "scope_completion": "not_declared",
}


ROOT_FIELDS = {
    "acceptance_conditions",
    "authority_dag",
    "boundaries",
    "corpus_consistency",
    "manual_gate",
    "regression_evidence",
    "schema",
    "schema_version",
    "scope",
    "source_references",
}


PROHIBITED_KEYS = {
    "array",
    "arrays",
    "asset_bytes",
    "decoded_array",
    "decoded_arrays",
    "payload_bytes",
    "raw_bytes",
    "sample",
    "samples",
    "waveform",
    "waveforms",
}


URI_SCHEME = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*://")
WINDOWS_ROOT = re.compile(r"^[A-Za-z]:[\\/]")


def _fail(code: str, message: str) -> None:
    raise CloseoutFailure(code, message)


def _canonical_json(value: object) -> bytes:
    try:
        text = json.dumps(
            value,
            ensure_ascii=True,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        )
    except (TypeError, ValueError):
        _fail("closeout.json.invalid", "closeout must contain strict JSON values")
    return (text + "\n").encode("utf-8")


def _strict_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            _fail(
                "closeout.json.duplicate_key",
                "closeout JSON contains a duplicate key",
            )
        result[key] = value
    return result


def _reject_nonfinite(_: str) -> None:
    _fail(
        "closeout.json.nonfinite",
        "closeout JSON contains a non-finite number",
    )


def _parse_json(payload: bytes, *, maximum: int) -> object:
    if len(payload) > maximum:
        _fail("closeout.json.oversize", "closeout JSON exceeds the size bound")
    try:
        text = payload.decode("utf-8", errors="strict")
    except UnicodeDecodeError:
        _fail("closeout.json.invalid", "closeout must be strict UTF-8 JSON")
    try:
        return json.loads(
            text,
            object_pairs_hook=_strict_object,
            parse_constant=_reject_nonfinite,
        )
    except CloseoutFailure:
        raise
    except (TypeError, ValueError, json.JSONDecodeError):
        _fail("closeout.json.invalid", "closeout must be strict JSON")


def _validate_tree(value: object) -> None:
    nodes = 0
    seen: set[int] = set()
    stack: list[tuple[object, int]] = [(value, 0)]
    while stack:
        current, depth = stack.pop()
        nodes += 1
        if nodes > MAX_TREE_NODES:
            _fail("closeout.payload", "closeout structure exceeds the node bound")
        if depth > MAX_TREE_DEPTH:
            _fail("closeout.payload", "closeout structure exceeds the depth bound")
        if current is None or isinstance(current, (bool, int)):
            continue
        if isinstance(current, float):
            _fail("closeout.payload", "closeout must not contain floating values")
        if isinstance(current, str):
            if len(current) > MAX_STRING_LENGTH:
                _fail("closeout.payload", "closeout string exceeds the length bound")
            if (
                current.startswith(("/", "\\"))
                or WINDOWS_ROOT.match(current)
                or URI_SCHEME.match(current)
                or any(part == ".." for part in current.replace("\\", "/").split("/"))
            ):
                _fail("closeout.locator", "closeout contains an unsafe locator")
            continue
        if isinstance(current, (bytes, bytearray, memoryview)):
            _fail("closeout.payload", "closeout must not contain byte payloads")
        if not isinstance(current, (dict, list)):
            _fail("closeout.payload", "closeout contains a non-JSON value")
        identity = id(current)
        if identity in seen:
            _fail("closeout.payload", "closeout contains an alias or cycle")
        seen.add(identity)
        if isinstance(current, list):
            stack.extend((item, depth + 1) for item in reversed(current))
            continue
        for key, item in reversed(list(current.items())):
            if not isinstance(key, str):
                _fail("closeout.payload", "closeout object keys must be strings")
            if key.lower() in PROHIBITED_KEYS:
                _fail("closeout.payload", "closeout contains a prohibited payload field")
            stack.append((key, depth + 1))
            stack.append((item, depth + 1))


def _safe_relative_path(value: object, *, code: str) -> str:
    if not isinstance(value, str) or not value:
        _fail(code, "source path must be a repository-relative string")
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ("", ".", "..") for part in path.parts):
        _fail(code, "source path must be normalized and repository-relative")
    if "\\" in value or URI_SCHEME.match(value) or WINDOWS_ROOT.match(value):
        _fail(code, "source path must be normalized and repository-relative")
    return value


def _source_reference(declaration: SourceDeclaration) -> dict[str, object]:
    result: dict[str, object] = {
        "path": declaration.path,
        "sha256": declaration.sha256,
        "size_bytes": declaration.size_bytes,
    }
    if declaration.schema is not None:
        result["schema"] = declaration.schema
        result["schema_version"] = declaration.schema_version
    if declaration.git_blob is not None:
        result["git_blob"] = declaration.git_blob
    return result


def _identity_reference(declaration: SourceDeclaration) -> dict[str, object]:
    return {
        "path": declaration.path,
        "sha256": declaration.sha256,
        "size_bytes": declaration.size_bytes,
    }


def _validate_declarations(
    declarations: Mapping[str, SourceDeclaration],
) -> None:
    if list(declarations) != list(FIXED_SOURCE_DECLARATIONS):
        _fail("closeout.source.roles", "closeout source roles differ from the fixed set")
    for declaration in declarations.values():
        if not isinstance(declaration, SourceDeclaration):
            _fail(
                "closeout.source.declaration",
                "closeout source declaration has an invalid type",
            )
        _safe_relative_path(declaration.path, code="closeout.source.path")
        if not re.fullmatch(r"[0-9a-f]{64}", declaration.sha256):
            _fail(
                "closeout.source.declaration",
                "closeout source SHA-256 is invalid",
            )
        if not isinstance(declaration.size_bytes, int) or declaration.size_bytes <= 0:
            _fail(
                "closeout.source.declaration",
                "closeout source size is invalid",
            )
        if declaration.git_blob is not None and not re.fullmatch(
            r"[0-9a-f]{40}", declaration.git_blob
        ):
            _fail(
                "closeout.source.declaration",
                "closeout source Git blob is invalid",
            )


def _validate_closeout(
    value: object,
    declarations: Mapping[str, SourceDeclaration],
    expected_corpus_consistency: Mapping[str, object] = FROZEN_CORPUS_CONSISTENCY,
) -> dict[str, Any]:
    _validate_tree(value)
    if not isinstance(value, dict):
        _fail("closeout.type", "closeout must be an object")
    if set(value) != ROOT_FIELDS:
        _fail("closeout.fields", "closeout fields differ from the fixed schema")
    if value["schema"] != CLOSEOUT_SCHEMA:
        _fail("closeout.schema", "closeout schema is unsupported")
    if value["schema_version"] != CLOSEOUT_SCHEMA_VERSION:
        _fail("closeout.schema_version", "closeout schema version is unsupported")
    if value["scope"] != SCOPE:
        _fail("closeout.scope", "closeout scope state differs from the fixed gate")
    if value["manual_gate"] != MANUAL_GATE:
        _fail("closeout.manual_gate", "closeout manual gate differs from the fixed gate")
    if value["authority_dag"] != AUTHORITY_DAG:
        _fail("closeout.authority_dag", "closeout authority DAG differs")
    if value["regression_evidence"] != REGRESSION_EVIDENCE:
        _fail("closeout.regression", "closeout regression evidence differs")
    if value["corpus_consistency"] != expected_corpus_consistency:
        _fail("closeout.corpus", "closeout corpus consistency evidence differs")
    if value["boundaries"] != BOUNDARIES:
        _fail("closeout.boundaries", "closeout boundaries differ")

    expected_acceptance = [
        {
            "condition_id": condition.condition_id,
            "evidence_roles": list(condition.evidence_roles),
            "verdict": "verified",
        }
        for condition in ACCEPTANCE_CONDITIONS
    ]
    if value["acceptance_conditions"] != expected_acceptance:
        _fail(
            "closeout.acceptance",
            "closeout acceptance conditions differ from the fixed six",
        )

    _validate_declarations(declarations)
    references = value["source_references"]
    if not isinstance(references, dict) or set(references) != set(declarations):
        _fail("closeout.source.roles", "closeout source roles differ")
    for role, declaration in declarations.items():
        if references.get(role) != _source_reference(declaration):
            _fail(
                "closeout.source.binding",
                "closeout source reference differs from its fixed declaration",
            )
    return value


def load_closeout_data(value: object) -> dict[str, Any]:
    """Validate closeout data against the production source declarations."""

    return _validate_closeout(value, FIXED_SOURCE_DECLARATIONS)


def load_closeout_bytes(payload: bytes) -> dict[str, Any]:
    """Parse and validate strict bounded closeout JSON."""

    if not isinstance(payload, bytes):
        _fail("closeout.json.type", "closeout input must be bytes")
    value = _parse_json(payload, maximum=MAX_CLOSEOUT_BYTES)
    return load_closeout_data(value)


def serialize_closeout(value: object) -> bytes:
    """Serialize validated closeout data in canonical JSON form."""

    validated = load_closeout_data(value)
    payload = _canonical_json(validated)
    if len(payload) > MAX_CLOSEOUT_BYTES:
        _fail("closeout.json.oversize", "closeout JSON exceeds the size bound")
    return payload


def _directory_flags() -> int:
    flags = os.O_RDONLY
    if hasattr(os, "O_DIRECTORY"):
        flags |= os.O_DIRECTORY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    return flags


def _open_relative_parent(
    repo_root: Path,
    relative: PurePosixPath,
    *,
    prefix: str,
) -> tuple[int, str]:
    try:
        directory = os.open(repo_root, _directory_flags())
    except OSError:
        _fail(f"{prefix}.open", "repository root could not be opened safely")
    try:
        if not stat.S_ISDIR(os.fstat(directory).st_mode):
            _fail(f"{prefix}.open", "repository root is not a directory")
        for component in relative.parts[:-1]:
            try:
                metadata = os.stat(
                    component,
                    dir_fd=directory,
                    follow_symlinks=False,
                )
            except FileNotFoundError:
                missing_code = (
                    "closeout.source.missing"
                    if prefix == "closeout.source"
                    else "closeout.output.parent"
                )
                _fail(missing_code, "required parent directory is missing")
            if stat.S_ISLNK(metadata.st_mode):
                _fail(f"{prefix}.symlink", "path traverses a symlink")
            if not stat.S_ISDIR(metadata.st_mode):
                _fail(f"{prefix}.type", "path parent is not a directory")
            try:
                next_directory = os.open(
                    component,
                    _directory_flags(),
                    dir_fd=directory,
                )
            except OSError:
                _fail(f"{prefix}.open", "path parent could not be opened safely")
            os.close(directory)
            directory = next_directory
        return directory, relative.parts[-1]
    except BaseException:
        os.close(directory)
        raise


def _read_descriptor_bounded(descriptor: int, maximum: int) -> bytes:
    payload_buffer = bytearray()
    while len(payload_buffer) <= maximum:
        chunk = os.read(
            descriptor,
            min(65_536, maximum + 1 - len(payload_buffer)),
        )
        if not chunk:
            break
        payload_buffer.extend(chunk)
    return bytes(payload_buffer)


def _read_exact_source(
    repo_root: Path,
    declaration: SourceDeclaration,
) -> bytes:
    relative = PurePosixPath(declaration.path)
    parent, filename = _open_relative_parent(
        repo_root,
        relative,
        prefix="closeout.source",
    )
    try:
        try:
            metadata = os.stat(filename, dir_fd=parent, follow_symlinks=False)
        except FileNotFoundError:
            _fail("closeout.source.missing", "required closeout source is missing")
        if stat.S_ISLNK(metadata.st_mode):
            _fail("closeout.source.symlink", "closeout source must not be a symlink")
        if not stat.S_ISREG(metadata.st_mode):
            _fail("closeout.source.type", "closeout source must be a regular file")
        if metadata.st_size != declaration.size_bytes:
            _fail(
                "closeout.source.size_mismatch",
                "closeout source byte size differs from its fixed identity",
            )
        flags = os.O_RDONLY
        if hasattr(os, "O_NOFOLLOW"):
            flags |= os.O_NOFOLLOW
        try:
            descriptor = os.open(filename, flags, dir_fd=parent)
        except OSError:
            _fail("closeout.source.open", "closeout source could not be opened safely")
    finally:
        os.close(parent)
    try:
        opened = os.fstat(descriptor)
        if (
            not stat.S_ISREG(opened.st_mode)
            or opened.st_dev != metadata.st_dev
            or opened.st_ino != metadata.st_ino
        ):
            _fail("closeout.source.changed", "closeout source changed before read")
        payload = _read_descriptor_bounded(descriptor, declaration.size_bytes)
        after = os.fstat(descriptor)
    finally:
        os.close(descriptor)
    if len(payload) != declaration.size_bytes:
        _fail(
            "closeout.source.size_mismatch",
            "closeout source byte size differs from its fixed identity",
        )
    if (
        after.st_dev != opened.st_dev
        or after.st_ino != opened.st_ino
        or after.st_size != opened.st_size
        or after.st_mtime_ns != opened.st_mtime_ns
    ):
        _fail("closeout.source.changed", "closeout source changed during read")
    if hashlib.sha256(payload).hexdigest() != declaration.sha256:
        _fail(
            "closeout.source.sha256_mismatch",
            "closeout source SHA-256 differs from its fixed identity",
        )
    if declaration.git_blob is not None:
        header = f"blob {len(payload)}\0".encode("ascii")
        if hashlib.sha1(header + payload).hexdigest() != declaration.git_blob:
            _fail(
                "closeout.source.git_blob_mismatch",
                "checkpoint profile Git blob differs from its fixed identity",
            )
    return payload


def _source_json(
    payloads: Mapping[str, bytes],
    role: str,
) -> dict[str, Any]:
    value = _parse_json(
        payloads[role],
        maximum=len(payloads[role]),
    )
    if not isinstance(value, dict):
        _fail("closeout.source.json", "closeout JSON source must be an object")
    return value


def _require_schema(
    role: str,
    value: Mapping[str, Any],
    declaration: SourceDeclaration,
) -> None:
    if (
        value.get("schema") != declaration.schema
        or value.get("schema_version") != declaration.schema_version
    ):
        _fail(
            f"closeout.{role}.schema",
            "closeout source schema differs from its fixed declaration",
        )


def _validate_mapping(value: Mapping[str, Any]) -> None:
    expected_labels = [
        "FSC",
        "SSC",
        "FL1",
        "FL2",
        "FL3",
        "FL4",
        "FL5",
        "FL6",
        "fsGMI",
        "ssGMI",
        "flGMI",
        "dGMI",
        "bfGMI",
    ]
    mapping = value.get("mapping")
    provenance = value.get("provenance")
    if (
        value.get("channel_count") != 13
        or not isinstance(mapping, list)
        or len(mapping) != 13
        or not isinstance(provenance, dict)
        or provenance.get("repository") != "gcsa"
        or provenance.get("commit") != GCSA_COMMIT
        or provenance.get("path") != "src/gcsa/constants.py"
        or provenance.get("symbols")
        != [
            "FL_CHANNEL_MAP",
            "FH_CHANNEL_MAP",
            "FL_CHANNEL_NAMES",
            "FH_CHANNEL_NAMES",
            "ALL_CHANNEL_NAMES",
        ]
    ):
        _fail("closeout.mapping", "channel mapping authority is not the frozen set")
    for index, row in enumerate(mapping):
        if (
            not isinstance(row, dict)
            or row.get("physical_channel") != f"CH{index + 1}"
            or row.get("label") != expected_labels[index]
            or row.get("stream") != ("FL" if index < 8 else "FH")
            or row.get("source_index") != (index if index < 8 else index - 8)
        ):
            _fail("closeout.mapping", "channel mapping order is not the frozen set")


def _validate_selection(
    value: Mapping[str, Any],
    declarations: Mapping[str, SourceDeclaration],
    manifest_value: Mapping[str, Any],
) -> dict[str, object]:
    pairs = value.get("pairs")
    if (
        value.get("pair_count") != 3
        or value.get("entry_count") != 6
        or not isinstance(pairs, list)
        or len(pairs) != 3
        or [pair.get("density") for pair in pairs if isinstance(pair, dict)]
        != ["low", "mid", "high"]
    ):
        _fail("closeout.selection", "golden selection is not three complete pairs")
    entries: list[Mapping[str, Any]] = []
    for pair in pairs:
        if not isinstance(pair, dict):
            _fail("closeout.selection", "golden selection pair is invalid")
        inputs = pair.get("entries")
        if not isinstance(inputs, list) or len(inputs) != 2:
            _fail("closeout.selection", "golden selection pair is incomplete")
        if [entry.get("stream") for entry in inputs if isinstance(entry, dict)] != [
            "FL",
            "FH",
        ]:
            _fail("closeout.selection", "golden selection stream order differs")
        if any(not isinstance(entry, dict) for entry in inputs):
            _fail("closeout.selection", "golden selection entry is invalid")
        entries.extend(inputs)
    selected_asset_bytes = 0
    for entry in entries:
        size_bytes = entry.get("size_bytes")
        if type(size_bytes) is not int or size_bytes <= 0:
            _fail("closeout.selection", "golden selection byte total differs")
        selected_asset_bytes += size_bytes
    sources = value.get("sources")
    manifest = sources.get("manifest") if isinstance(sources, dict) else None
    contract = sources.get("corpus_contract") if isinstance(sources, dict) else None
    validator = (
        sources.get("corpus_index_validator")
        if isinstance(sources, dict)
        else None
    )
    if not isinstance(sources, dict) or set(sources) != {
        "manifest",
        "corpus_contract",
        "corpus_index_validator",
    }:
        _fail("closeout.corpus_binding", "selection source roles differ")
    if manifest != _identity_reference(declarations["p0_c4_manifest"]):
        _fail("closeout.corpus_binding", "selection manifest binding differs")
    if contract != _identity_reference(declarations["p0_c4_contract"]):
        _fail("closeout.corpus_binding", "selection contract binding differs")
    if validator != _identity_reference(declarations["corpus_index_validator"]):
        _fail("closeout.corpus_binding", "selection validator binding differs")
    manifest_entries = manifest_value.get("entries")
    if (
        value.get("asset_locator") != manifest_value.get("source_locator")
        or not isinstance(manifest_entries, list)
    ):
        _fail("closeout.corpus_binding", "selection corpus locator binding differs")
    manifest_by_path: dict[str, Mapping[str, Any]] = {}
    for manifest_entry in manifest_entries:
        if not isinstance(manifest_entry, dict):
            _fail("closeout.corpus_binding", "P0-C4 manifest entry is invalid")
        path = manifest_entry.get("path")
        if not isinstance(path, str) or path in manifest_by_path:
            _fail("closeout.corpus_binding", "P0-C4 manifest paths are ambiguous")
        manifest_by_path[path] = manifest_entry
    for entry in entries:
        expected_manifest_entry = {
            "kind": "bin",
            "path": entry.get("path"),
            "sha256": entry.get("sha256"),
            "size_bytes": entry.get("size_bytes"),
        }
        if manifest_by_path.get(entry.get("path")) != expected_manifest_entry:
            _fail(
                "closeout.corpus_binding",
                "selected identity differs from its P0-C4 manifest row",
            )
    return {
        "canonical_contract_role": "p0_c4_contract",
        "canonical_manifest_role": "p0_c4_manifest",
        "p0_c4_closeout_role": "p0_c4_closeout",
        "selected_asset_bytes": selected_asset_bytes,
        "selected_entry_count": len(entries),
        "selected_pair_count": len(pairs),
        "selection_role": "golden_inputs",
        "verdict": "verified",
    }


def _validate_reference(
    value: Mapping[str, Any],
    declarations: Mapping[str, SourceDeclaration],
    mapping: Mapping[str, Any],
    selection: Mapping[str, Any],
) -> None:
    pairs = value.get("pairs")
    if (
        value.get("pair_count") != 3
        or value.get("channel_count_per_pair") != 13
        or not isinstance(pairs, list)
        or len(pairs) != 3
    ):
        _fail("closeout.reference", "golden reference cardinality differs")
    expected_mapping = mapping["mapping"]
    selected_pairs = selection.get("pairs")
    if not isinstance(selected_pairs, list) or len(selected_pairs) != 3:
        _fail("closeout.reference", "golden reference selection join is invalid")
    channel_records = 0
    for pair, selected_pair in zip(pairs, selected_pairs, strict=True):
        if not isinstance(pair, dict) or not isinstance(pair.get("channels"), list):
            _fail("closeout.reference", "golden reference pair is invalid")
        if not isinstance(selected_pair, dict):
            _fail("closeout.reference", "golden reference selection join is invalid")
        if (
            pair.get("density") != selected_pair.get("density")
            or pair.get("run_id") != selected_pair.get("run_id")
            or pair.get("ordinal") != selected_pair.get("ordinal")
            or pair.get("inputs") != selected_pair.get("entries")
            or type(pair.get("event_count")) is not int
            or pair.get("event_count") != 100
        ):
            _fail("closeout.reference", "golden reference pair join differs")
        event_count = pair["event_count"]
        channels = pair["channels"]
        if len(channels) != 13:
            _fail("closeout.reference", "golden reference channel count differs")
        for expected, channel in zip(expected_mapping, channels, strict=True):
            channel_records += 1
            statistics = channel.get("statistics") if isinstance(channel, dict) else None
            shape = channel.get("shape") if isinstance(channel, dict) else None
            if (
                not isinstance(channel, dict)
                or set(channel)
                != {
                    "physical_channel",
                    "label",
                    "stream",
                    "source_index",
                    "dtype",
                    "shape",
                    "sha256",
                    "statistics",
                }
                or channel.get("physical_channel")
                != expected.get("physical_channel")
                or channel.get("label") != expected.get("label")
                or channel.get("stream") != expected.get("stream")
                or channel.get("source_index") != expected.get("source_index")
                or channel.get("dtype") != "<u2"
                or shape != [event_count, 2400]
                or not isinstance(channel.get("sha256"), str)
                or re.fullmatch(r"[0-9a-f]{64}", channel["sha256"]) is None
            ):
                _fail("closeout.reference", "golden reference representation differs")
            element_count = event_count * 2_400
            if (
                not isinstance(statistics, dict)
                or set(statistics)
                != {"element_count", "min", "max", "sum", "nonzero_count"}
                or any(type(item) is not int for item in statistics.values())
                or statistics["element_count"] != element_count
                or not 0 <= statistics["min"] <= statistics["max"] <= 65_535
                or not 0 <= statistics["nonzero_count"] <= element_count
                or not (
                    statistics["min"] * element_count
                    <= statistics["sum"]
                    <= statistics["max"] * element_count
                )
            ):
                _fail("closeout.reference", "golden reference statistics differ")
    if channel_records != 39:
        _fail("closeout.reference", "golden reference record count differs")
    expected_sources = {
        "channel_mapping": _identity_reference(declarations["channel_mapping"]),
        "golden_inputs": _identity_reference(declarations["golden_inputs"]),
    }
    if value.get("sources") != expected_sources:
        _fail("closeout.reference", "golden reference source bindings differ")
    expected_reader = {
        "commit": GCSA_COMMIT,
        "environment": {
            "identity": (
                "sha256:e65e9f8b0ffafef5b5d2b9711c9a341"
                "1649ae80fd036cc79f0febb80b4c0b06e"
            ),
            "identity_attestation": {
                "kind": "operator-environment-attestation",
                "source": "P0_M1_CONTAINER_IMAGE_ID",
            },
            "kind": "container-image",
            "logical_invocation": (
                "golden_reference.py generate --asset-root <fixed-custody-root>"
            ),
            "numpy_version": "2.2.6",
            "python_version": "3.10.17",
        },
        "invocation": "BinaryReader(version='v1')",
        "parser": {
            "path": "src/gcsa/io/parsers/v1.py",
            "sha256": (
                "5035b9147ec42c2381cc2fd45a1f83a9f251edece7b21c4dd099f2da315a2964"
            ),
        },
        "path": "src/gcsa/io/binary_reader.py",
        "reader_source_sha256": (
            "620ab899b0fb75f75da0a1c8b5722a2f02212726910aea5115401506f8eb4254"
        ),
        "repository": "gcsa",
        "symbol": "BinaryReader",
        "version": "v1",
    }
    if value.get("reader") != expected_reader:
        _fail("closeout.reference", "golden reference reader provenance differs")


def _validate_corpus_sources(
    contract: Mapping[str, Any],
    manifest: Mapping[str, Any],
    closeout: Mapping[str, Any],
    declarations: Mapping[str, SourceDeclaration],
) -> None:
    if (
        manifest.get("expected_total_bytes") != contract.get("expected_total_bytes")
        or manifest.get("actual_total_bytes") != contract.get("expected_total_bytes")
        or not isinstance(manifest.get("entries"), list)
        or len(manifest["entries"]) != 3_534
    ):
        _fail("closeout.corpus_binding", "P0-C4 manifest differs from its contract")
    source_references = closeout.get("source_references")
    scope = closeout.get("scope")
    if (
        not isinstance(source_references, dict)
        or source_references.get("corpus_contract")
        != {
            "path": declarations["p0_c4_contract"].path,
            "schema": declarations["p0_c4_contract"].schema,
            "schema_version": declarations["p0_c4_contract"].schema_version,
            "sha256": declarations["p0_c4_contract"].sha256,
        }
        or source_references.get("corpus_manifest")
        != {
            "path": declarations["p0_c4_manifest"].path,
            "schema": declarations["p0_c4_manifest"].schema,
            "schema_version": declarations["p0_c4_manifest"].schema_version,
            "sha256": declarations["p0_c4_manifest"].sha256,
        }
        or scope
        != {
            "status": "gate_ready",
            "step_id": "P0-C4",
            "transition": "pending_human_merge",
        }
    ):
        _fail("closeout.corpus_binding", "P0-C4 closeout binding differs")


def _validate_plan_and_contract(payloads: Mapping[str, bytes]) -> None:
    plan = payloads["plan"]
    if (
        b"<strong>Version:</strong> Draft 4.9" not in plan
        or b'id="p0-m1-golden"' not in plan
    ):
        _fail("closeout.plan", "source plan is not the fixed P0-M1 authority")
    contract = payloads["phase1_contract"]
    required = (
        b"analogboard.d17.candidate-summary",
        b"canonical little-endian `<u2`",
        b"C row-major",
        b"all 39",
        b"exit status 0",
        b"Tier 1/2 integration",
        b"D17 remains unchanged",
    )
    if any(marker not in contract for marker in required):
        _fail("closeout.phase1_contract", "Phase 1 connection contract is incomplete")


def _build_value(
    declarations: Mapping[str, SourceDeclaration],
    corpus_consistency: Mapping[str, object],
) -> dict[str, object]:
    return {
        "acceptance_conditions": [
            {
                "condition_id": condition.condition_id,
                "evidence_roles": list(condition.evidence_roles),
                "verdict": "verified",
            }
            for condition in ACCEPTANCE_CONDITIONS
        ],
        "authority_dag": AUTHORITY_DAG,
        "boundaries": BOUNDARIES,
        "corpus_consistency": dict(corpus_consistency),
        "manual_gate": MANUAL_GATE,
        "regression_evidence": REGRESSION_EVIDENCE,
        "schema": CLOSEOUT_SCHEMA,
        "schema_version": CLOSEOUT_SCHEMA_VERSION,
        "scope": SCOPE,
        "source_references": {
            role: _source_reference(declaration)
            for role, declaration in declarations.items()
        },
    }


def build_closeout(
    repo_root: Path,
    source_declarations: Mapping[str, SourceDeclaration] = FIXED_SOURCE_DECLARATIONS,
) -> dict[str, Any]:
    """Build closeout data after exact identity and semantic verification."""

    _validate_declarations(source_declarations)
    if not isinstance(repo_root, Path) or not repo_root.is_absolute():
        _fail("closeout.root", "repository root must be an absolute path")
    try:
        root_metadata = repo_root.lstat()
    except FileNotFoundError:
        _fail("closeout.root", "repository root is missing")
    if stat.S_ISLNK(root_metadata.st_mode) or not stat.S_ISDIR(root_metadata.st_mode):
        _fail("closeout.root", "repository root must be a non-symlink directory")

    payloads = {
        role: _read_exact_source(repo_root, declaration)
        for role, declaration in source_declarations.items()
    }
    json_sources = {
        role: _source_json(payloads, role)
        for role in (
            "p0_c4_contract",
            "p0_c4_manifest",
            "p0_c4_closeout",
            "channel_mapping",
            "golden_inputs",
            "golden_reference",
        )
    }
    for role, value in json_sources.items():
        _require_schema(role, value, source_declarations[role])

    _validate_mapping(json_sources["channel_mapping"])
    corpus_consistency = _validate_selection(
        json_sources["golden_inputs"],
        source_declarations,
        json_sources["p0_c4_manifest"],
    )
    _validate_reference(
        json_sources["golden_reference"],
        source_declarations,
        json_sources["channel_mapping"],
        json_sources["golden_inputs"],
    )
    _validate_corpus_sources(
        json_sources["p0_c4_contract"],
        json_sources["p0_c4_manifest"],
        json_sources["p0_c4_closeout"],
        source_declarations,
    )
    _validate_plan_and_contract(payloads)
    return _validate_closeout(
        _build_value(source_declarations, corpus_consistency),
        source_declarations,
        corpus_consistency,
    )


def _output_parent(repo_root: Path) -> tuple[int, str]:
    relative = _safe_relative_path(
        DEFAULT_CLOSEOUT_PATH,
        code="closeout.output.path",
    )
    return _open_relative_parent(
        repo_root,
        PurePosixPath(relative),
        prefix="closeout.output",
    )


def _output_metadata(parent: int, filename: str) -> os.stat_result | None:
    try:
        return os.stat(filename, dir_fd=parent, follow_symlinks=False)
    except FileNotFoundError:
        return None


def _read_existing_output(
    parent: int,
    filename: str,
    metadata: os.stat_result,
) -> bytes:
    if stat.S_ISLNK(metadata.st_mode):
        _fail("closeout.output.symlink", "closeout output must not be a symlink")
    if not stat.S_ISREG(metadata.st_mode):
        _fail("closeout.output.type", "closeout output must be a regular file")
    if metadata.st_nlink != 1:
        _fail("closeout.output.hardlink", "closeout output must not be hard-linked")
    flags = os.O_RDONLY
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    try:
        descriptor = os.open(filename, flags, dir_fd=parent)
    except OSError:
        _fail("closeout.output.read", "closeout output could not be opened safely")
    try:
        opened = os.fstat(descriptor)
        if (
            not stat.S_ISREG(opened.st_mode)
            or opened.st_dev != metadata.st_dev
            or opened.st_ino != metadata.st_ino
            or opened.st_nlink != 1
        ):
            _fail("closeout.output.changed", "closeout output changed before read")
        payload = _read_descriptor_bounded(descriptor, MAX_CLOSEOUT_BYTES)
        after = os.fstat(descriptor)
    finally:
        os.close(descriptor)
    if (
        after.st_dev != opened.st_dev
        or after.st_ino != opened.st_ino
        or after.st_size != opened.st_size
        or after.st_mtime_ns != opened.st_mtime_ns
        or after.st_nlink != 1
    ):
        _fail("closeout.output.changed", "closeout output changed during read")
    return payload


def generate_closeout(repo_root: Path) -> bytes:
    """Generate the sole fixed closeout output without overwriting drift."""

    payload = serialize_closeout(build_closeout(repo_root))
    parent, filename = _output_parent(repo_root)
    try:
        metadata = _output_metadata(parent, filename)
        if metadata is None:
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
            if hasattr(os, "O_NOFOLLOW"):
                flags |= os.O_NOFOLLOW
            try:
                descriptor = os.open(
                    filename,
                    flags,
                    0o644,
                    dir_fd=parent,
                )
            except OSError:
                _fail("closeout.output.create", "closeout output could not be created")
            try:
                view = memoryview(payload)
                while view:
                    written = os.write(descriptor, view)
                    if written <= 0:
                        _fail("closeout.output.write", "closeout output write failed")
                    view = view[written:]
            finally:
                os.close(descriptor)
            return payload
        existing = _read_existing_output(parent, filename, metadata)
    finally:
        os.close(parent)
    if existing != payload:
        _fail("closeout.output.mismatch", "existing closeout output differs")
    return payload


def verify_closeout(repo_root: Path) -> dict[str, Any]:
    """Verify exact sources, canonical output bytes, and semantic bindings."""

    expected = serialize_closeout(build_closeout(repo_root))
    parent, filename = _output_parent(repo_root)
    try:
        metadata = _output_metadata(parent, filename)
        if metadata is None:
            _fail("closeout.output.missing", "tracked closeout output is missing")
        payload = _read_existing_output(parent, filename, metadata)
    finally:
        os.close(parent)
    parsed = load_closeout_bytes(payload)
    if payload != serialize_closeout(parsed) or payload != expected:
        _fail("closeout.bytes", "closeout bytes differ from canonical live evidence")
    return parsed


def _repository_root() -> Path:
    script = Path(__file__)
    if script.is_symlink():
        _fail("closeout.script", "closeout script must not be a symlink")
    root = script.absolute().parents[2]
    if not root.is_dir() or root.is_symlink():
        _fail("closeout.root", "repository root is unsafe")
    return root


def main(arguments: Sequence[str] | None = None) -> int:
    argv = list(sys.argv[1:] if arguments is None else arguments)
    try:
        if argv == ["generate"]:
            payload = generate_closeout(_repository_root())
            sys.stdout.write(
                "P0-M1 closeout generated: gate_ready; "
                f"{len(payload)} bytes\n"
            )
            return 0
        if argv == ["verify"]:
            closeout = verify_closeout(_repository_root())
            sys.stdout.write(
                "P0-M1 closeout verified: "
                f"{len(closeout['acceptance_conditions'])}/6 conditions; "
                "gate_ready\n"
            )
            return 0
        _fail(
            "closeout.cli.arguments",
            "usage: closeout.py generate|verify",
        )
    except CloseoutFailure as error:
        sys.stderr.write(f"{error}\n")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
