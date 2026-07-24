from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, Sequence

from corpus_custody import load_custody_data, verify_custody
from corpus_index import (
    CorpusIndexError,
    build_manifest,
    load_contract_data,
    serialize_manifest,
    validate_manifest_metadata,
)
from corpus_relationships import (
    _SymlinkTraversalError,
    _read_regular_no_follow,
    build_relationship_evidence,
    load_relationship_contract_data,
    serialize_relationship_evidence,
    validate_relationship_evidence_data,
)


CLOSEOUT_SCHEMA = "analogboard.phase0.initial-recording-corpus-closeout"
CLOSEOUT_SCHEMA_VERSION = 1
DEFAULT_CLOSEOUT_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/closeout.json"
)
MAXIMUM_METADATA_BYTES = 1024 * 1024
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")

PLAN_PATH = "docs/plans/260710-analogboard-rebuild-plan.html"
INITIAL_ROOT = "docs/reference/initial-recording-corpus/2026-07-17"
USB_ROOT = "docs/reference/usb-recording-corpus/2026-07-17"
SOURCE_PATHS = {
    "plan": PLAN_PATH,
    "corpus_contract": f"{INITIAL_ROOT}/contract.json",
    "corpus_manifest": f"{INITIAL_ROOT}/manifest.json",
    "relationship_contract": f"{INITIAL_ROOT}/relationship-contract.json",
    "relationships": f"{INITIAL_ROOT}/relationships.json",
    "custody": f"{INITIAL_ROOT}/custody.json",
    "recovery_procedure": (
        "docs/operations/initial-recording-corpus/"
        "restore-and-reacquisition.md"
    ),
    "usb_index_readme": f"{USB_ROOT}/README.md",
    "usb_manifest": f"{USB_ROOT}/manifest.json",
    "usb_scenarios": f"{USB_ROOT}/scenarios.json",
    "manifest_tool": "scripts/corpus-index/corpus_index.py",
    "relationship_tool": "scripts/corpus-index/corpus_relationships.py",
    "custody_tool": "scripts/corpus-index/corpus_custody.py",
}
SOURCE_ROLES = tuple(SOURCE_PATHS)
SOURCE_SCHEMAS = {
    "corpus_contract": (
        "analogboard.phase0.initial-recording-corpus-contract",
        1,
    ),
    "corpus_manifest": (
        "analogboard.phase0.initial-recording-corpus-manifest",
        1,
    ),
    "relationship_contract": (
        "analogboard.phase0.initial-recording-corpus-relationship-contract",
        1,
    ),
    "relationships": (
        "analogboard.phase0.initial-recording-corpus-relationships",
        1,
    ),
    "custody": (
        "analogboard.phase0.initial-recording-corpus-custody",
        1,
    ),
    "usb_manifest": ("analogboard.phase0.usbpcap-corpus-index", 1),
    "usb_scenarios": ("analogboard.phase0.usbpcap-scenarios", 1),
}
JSON_SOURCE_ROLES = frozenset(SOURCE_SCHEMAS)

TOP_LEVEL_FIELDS = frozenset(
    {
        "schema",
        "schema_version",
        "scope",
        "source_references",
        "authority_dag",
        "p0_c1_c3_overlap",
        "acceptance_conditions",
        "open_item_references",
        "manual_gate",
    }
)
SCOPE_FIELDS = frozenset({"step_id", "status", "transition"})
SOURCE_JSON_FIELDS = frozenset(
    {"path", "sha256", "schema", "schema_version"}
)
SOURCE_PLAN_FIELDS = frozenset({"path", "sha256", "revision"})
SOURCE_FILE_FIELDS = frozenset({"path", "sha256"})
AUTHORITY_EDGE_FIELDS = frozenset({"consumer", "inputs"})
OVERLAP_FIELDS = frozenset(
    {
        "verdict",
        "historical_index_roles",
        "shared_capture_identity_roles",
        "binding_role",
        "evidence_role",
    }
)
ACCEPTANCE_FIELDS = frozenset(
    {"condition_id", "verdict", "evidence_roles"}
)
OPEN_ITEM_REFERENCE_FIELDS = frozenset({"source_role", "ids"})
MANUAL_GATE_FIELDS = frozenset(
    {
        "base_branch",
        "head_branch",
        "required_action",
        "pre_merge_action",
        "scope_completion",
        "initial_corpus_gate",
        "central_handoff",
        "next_scope_transition",
    }
)
PROHIBITED_METADATA_KEYS = frozenset(
    {
        "expected_counts",
        "expected_count",
        "expected_total_bytes",
        "actual_total_bytes",
        "total_bytes",
        "entry_count",
        "entries",
        "runs",
        "run_id",
        "pair_count",
        "canonical_locator",
        "payload",
        "raw_payload",
        "raw_rows",
        "packet_body",
        "host_locator",
    }
)

EXPECTED_SCOPE = {
    "step_id": "P0-C4",
    "status": "gate_ready",
    "transition": "pending_human_merge",
}
EXPECTED_AUTHORITY_DAG = [
    {"consumer": "corpus_manifest", "inputs": ["corpus_contract"]},
    {
        "consumer": "relationship_contract",
        "inputs": [
            "plan",
            "corpus_contract",
            "corpus_manifest",
            "usb_manifest",
        ],
    },
    {"consumer": "relationships", "inputs": ["relationship_contract"]},
    {
        "consumer": "custody",
        "inputs": [
            "plan",
            "corpus_contract",
            "corpus_manifest",
            "manifest_tool",
            "recovery_procedure",
        ],
    },
    {
        "consumer": "closeout",
        "inputs": [
            "plan",
            "corpus_contract",
            "corpus_manifest",
            "relationship_contract",
            "relationships",
            "custody",
            "recovery_procedure",
            "usb_index_readme",
            "usb_manifest",
            "usb_scenarios",
            "manifest_tool",
            "relationship_tool",
            "custody_tool",
        ],
    },
]
EXPECTED_OVERLAP = {
    "verdict": "verified",
    "historical_index_roles": [
        "usb_index_readme",
        "usb_manifest",
        "usb_scenarios",
    ],
    "shared_capture_identity_roles": [
        "corpus_manifest",
        "usb_manifest",
    ],
    "binding_role": "relationship_contract",
    "evidence_role": "relationships",
}
EXPECTED_ACCEPTANCE = [
    {
        "condition_id": "P0-C4-1",
        "evidence_roles": ["custody"],
        "verdict": "verified",
    },
    {
        "condition_id": "P0-C4-2",
        "evidence_roles": ["corpus_contract", "custody"],
        "verdict": "verified",
    },
    {
        "condition_id": "P0-C4-3",
        "evidence_roles": [
            "corpus_manifest",
            "manifest_tool",
            "custody",
        ],
        "verdict": "verified",
    },
    {
        "condition_id": "P0-C4-4",
        "evidence_roles": [
            "corpus_contract",
            "corpus_manifest",
            "manifest_tool",
        ],
        "verdict": "verified",
    },
    {
        "condition_id": "P0-C4-5",
        "evidence_roles": ["custody", "recovery_procedure"],
        "verdict": "verified",
    },
    {
        "condition_id": "P0-C4-6",
        "evidence_roles": [
            "relationship_contract",
            "relationships",
            "usb_manifest",
            "relationship_tool",
        ],
        "verdict": "verified",
    },
]
EXPECTED_OPEN_ITEM_REFERENCES = {
    "source_role": "custody",
    "ids": [
        "P0-C4-ASSET-OWNER",
        "P0-C4-RESTORE-SOURCE",
        "P0-C4-RETENTION",
    ],
}
EXPECTED_MANUAL_GATE = {
    "base_branch": "main",
    "head_branch": "analysis/phase0-corpus-index",
    "required_action": "human_merge",
    "pre_merge_action": "create_pr_then_stop",
    "scope_completion": "not_declared",
    "initial_corpus_gate": "not_closed",
    "central_handoff": "not_published",
    "next_scope_transition": "not_authorized",
}


class CloseoutError(CorpusIndexError):
    pass


class CloseoutValidationError(CloseoutError):
    pass


class CloseoutSourceError(CloseoutError):
    pass


class CloseoutPathError(CloseoutError):
    pass


class CloseoutIntegrityError(CloseoutError):
    pass


class CloseoutDependencyError(CloseoutError):
    pass


class _CloseoutArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        raise CloseoutPathError(
            "closeout.cli.arguments",
            "command arguments are invalid",
        )


@dataclass(frozen=True)
class CloseoutSnapshot:
    index: dict[str, Any]
    index_bytes: bytes
    sources: dict[str, bytes]
    contract: Any
    relationship_contract: Any
    custody: dict[str, Any]


def _is_int(value: object) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _normalized_relative_path(value: object) -> bool:
    if not isinstance(value, str) or not value or "\\" in value:
        return False
    if re.match(r"^[A-Za-z]:", value):
        return False
    path = PurePosixPath(value)
    return (
        not path.is_absolute()
        and all(part not in ("", ".", "..") for part in path.parts)
        and str(path) == value
    )


def _strict_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, child in pairs:
        if key in value:
            raise CloseoutValidationError(
                "closeout.json.duplicate_key",
                "duplicate JSON keys are not allowed",
            )
        value[key] = child
    return value


def _decode_strict_json(
    payload: bytes,
    *,
    error_type: type[CloseoutError],
    code: str,
    message: str,
) -> object:
    try:
        return json.loads(
            payload.decode("utf-8"),
            object_pairs_hook=_strict_pairs,
        )
    except CloseoutValidationError:
        raise
    except (
        UnicodeError,
        json.JSONDecodeError,
        RecursionError,
        ValueError,
    ) as error:
        raise error_type(code, message) from error


def _exact_object(
    value: object,
    fields: frozenset[str],
    *,
    code: str,
    message: str,
) -> Mapping[str, Any]:
    if not isinstance(value, dict) or set(value) != set(fields):
        raise CloseoutValidationError(code, message)
    return value


def _scan_prohibited_metadata(value: object) -> None:
    stack: list[tuple[object, int]] = [(value, 0)]
    while stack:
        current, depth = stack.pop()
        if depth > 128:
            raise CloseoutValidationError(
                "closeout.json.invalid",
                "closeout JSON nesting exceeds the supported bound",
            )
        if isinstance(current, dict):
            for key, child in current.items():
                if key in PROHIBITED_METADATA_KEYS:
                    raise CloseoutValidationError(
                        "closeout.metadata.prohibited",
                        "closeout must not duplicate authority or payload metadata",
                    )
                stack.append((child, depth + 1))
        elif isinstance(current, list):
            stack.extend((child, depth + 1) for child in current)


def _validate_source_references(value: object) -> None:
    if not isinstance(value, dict) or set(value) != set(SOURCE_ROLES):
        raise CloseoutSourceError(
            "closeout.source.roles",
            "source references must contain exactly the required roles",
        )
    for role in SOURCE_ROLES:
        source = value[role]
        if role == "plan":
            fields = SOURCE_PLAN_FIELDS
        elif role in JSON_SOURCE_ROLES:
            fields = SOURCE_JSON_FIELDS
        else:
            fields = SOURCE_FILE_FIELDS
        if not isinstance(source, dict) or set(source) != set(fields):
            raise CloseoutSourceError(
                "closeout.source.fields",
                "source reference fields are invalid",
            )
        if (
            not _normalized_relative_path(source.get("path"))
            or source["path"] != SOURCE_PATHS[role]
        ):
            raise CloseoutSourceError(
                "closeout.source.path",
                "source path is not the exact tracked path",
            )
        digest = source.get("sha256")
        if not isinstance(digest, str) or SHA256_PATTERN.fullmatch(digest) is None:
            raise CloseoutSourceError(
                "closeout.source.sha256",
                "source SHA-256 declaration is invalid",
            )
        if role == "plan" and source.get("revision") != "Draft 4.7":
            raise CloseoutSourceError(
                "closeout.source.declaration",
                "source revision declaration is unsupported",
            )
        if role in JSON_SOURCE_ROLES:
            schema, version = SOURCE_SCHEMAS[role]
            if (
                source.get("schema") != schema
                or not _is_int(source.get("schema_version"))
                or source.get("schema_version") != version
            ):
                raise CloseoutSourceError(
                    "closeout.source.declaration",
                    "source schema declaration is unsupported",
                )


def _validate_authority_dag(value: object) -> None:
    if not isinstance(value, list):
        raise CloseoutValidationError(
            "closeout.authority_dag",
            "authority DAG must be the exact acyclic dependency list",
        )
    for edge in value:
        if not isinstance(edge, dict) or set(edge) != set(AUTHORITY_EDGE_FIELDS):
            raise CloseoutValidationError(
                "closeout.authority_dag",
                "authority DAG must be the exact acyclic dependency list",
            )
    if value != EXPECTED_AUTHORITY_DAG:
        raise CloseoutValidationError(
            "closeout.authority_dag",
            "authority DAG must be the exact acyclic dependency list",
        )


def _validate_acceptance(value: object) -> None:
    if not isinstance(value, list):
        raise CloseoutValidationError(
            "closeout.acceptance",
            "acceptance conditions must be the exact ordered role mappings",
        )
    for condition in value:
        if (
            not isinstance(condition, dict)
            or set(condition) != set(ACCEPTANCE_FIELDS)
        ):
            raise CloseoutValidationError(
                "closeout.acceptance",
                "acceptance conditions must be the exact ordered role mappings",
            )
    if value != EXPECTED_ACCEPTANCE:
        raise CloseoutValidationError(
            "closeout.acceptance",
            "acceptance conditions must be the exact ordered role mappings",
        )


def load_closeout_data(value: object) -> dict[str, Any]:
    _scan_prohibited_metadata(value)
    if not isinstance(value, dict):
        raise CloseoutValidationError(
            "closeout.type",
            "closeout index must be an object",
        )
    if set(value) != set(TOP_LEVEL_FIELDS):
        raise CloseoutValidationError(
            "closeout.fields",
            "closeout index must contain exactly the authorized fields",
        )
    if value.get("schema") != CLOSEOUT_SCHEMA:
        raise CloseoutValidationError(
            "closeout.schema",
            "closeout schema is unsupported",
        )
    if (
        not _is_int(value.get("schema_version"))
        or value["schema_version"] != CLOSEOUT_SCHEMA_VERSION
    ):
        raise CloseoutValidationError(
            "closeout.schema_version",
            "closeout schema version is unsupported",
        )
    scope = _exact_object(
        value.get("scope"),
        SCOPE_FIELDS,
        code="closeout.status",
        message="scope must remain gate-ready pending human merge",
    )
    manual_gate = _exact_object(
        value.get("manual_gate"),
        MANUAL_GATE_FIELDS,
        code="closeout.status",
        message="manual gate must preserve the pre-merge stop boundary",
    )
    if scope != EXPECTED_SCOPE or manual_gate != EXPECTED_MANUAL_GATE:
        raise CloseoutValidationError(
            "closeout.status",
            "closeout must remain gate-ready pending human merge",
        )
    _validate_source_references(value.get("source_references"))
    _validate_authority_dag(value.get("authority_dag"))
    overlap = _exact_object(
        value.get("p0_c1_c3_overlap"),
        OVERLAP_FIELDS,
        code="closeout.overlap",
        message="P0-C1-C3 overlap declaration is invalid",
    )
    if overlap != EXPECTED_OVERLAP:
        raise CloseoutValidationError(
            "closeout.overlap",
            "P0-C1-C3 overlap declaration is invalid",
        )
    _validate_acceptance(value.get("acceptance_conditions"))
    open_items = _exact_object(
        value.get("open_item_references"),
        OPEN_ITEM_REFERENCE_FIELDS,
        code="closeout.open_items",
        message="open-item references must match the exact custody set",
    )
    if open_items != EXPECTED_OPEN_ITEM_REFERENCES:
        raise CloseoutValidationError(
            "closeout.open_items",
            "open-item references must match the exact custody set",
        )
    return value


def serialize_closeout(value: object) -> bytes:
    validated = load_closeout_data(value)
    return (
        json.dumps(
            validated,
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")


def _read_metadata_file(path: Path, _label: str) -> bytes:
    try:
        payload = _read_regular_no_follow(
            path,
            maximum_bytes=MAXIMUM_METADATA_BYTES + 1,
        )
    except _SymlinkTraversalError as error:
        raise CloseoutSourceError(
            "closeout.source.symlink",
            "source must not traverse a symlink",
        ) from error
    except (OSError, UnicodeError, ValueError) as error:
        raise CloseoutSourceError(
            "closeout.source.unreadable",
            "source must be a readable regular metadata file",
        ) from error
    if len(payload) > MAXIMUM_METADATA_BYTES:
        raise CloseoutSourceError(
            "closeout.source.oversize",
            "source exceeds the bounded metadata size",
        )
    return payload


def _read_index(repo_root: Path, index_path: str) -> bytes:
    try:
        payload = _read_regular_no_follow(
            repo_root / PurePosixPath(index_path),
            maximum_bytes=MAXIMUM_METADATA_BYTES + 1,
        )
    except _SymlinkTraversalError as error:
        raise CloseoutPathError(
            "closeout.index.symlink",
            "closeout index must not traverse a symlink",
        ) from error
    except (OSError, UnicodeError, ValueError) as error:
        raise CloseoutPathError(
            "closeout.index.unreadable",
            "closeout index must be a readable regular file",
        ) from error
    if len(payload) > MAXIMUM_METADATA_BYTES:
        raise CloseoutPathError(
            "closeout.index.oversize",
            "closeout index exceeds the bounded metadata size",
        )
    return payload


def _decode_source(payload: bytes, role: str) -> dict[str, Any]:
    try:
        value = _decode_strict_json(
            payload,
            error_type=CloseoutSourceError,
            code="closeout.source.json",
            message="source must contain strict UTF-8 JSON",
        )
    except CloseoutValidationError as error:
        raise CloseoutSourceError(
            "closeout.source.json",
            "source must not contain duplicate JSON keys",
        ) from error
    if not isinstance(value, dict):
        raise CloseoutSourceError(
            "closeout.source.json",
            "source JSON must be an object",
        )
    canonical = (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    # The accepted historical scenarios file uses compact objects within its
    # arrays. Its exact bytes are SHA-pinned, but it predates this closeout
    # serializer and is therefore not rewritten in P0-C4.
    if role != "usb_scenarios" and payload != canonical:
        raise CloseoutSourceError(
            "closeout.source.bytes",
            "source JSON must use canonical bytes",
        )
    return value


def _assert_embedded_reference(
    actual: object,
    expected: Mapping[str, Any],
) -> None:
    if actual != expected:
        raise CloseoutSourceError(
            "closeout.source.binding",
            "embedded source reference differs from the closeout pin",
        )


def _verify_embedded_bindings(
    index: Mapping[str, Any],
    decoded: Mapping[str, dict[str, Any]],
) -> None:
    references = index["source_references"]
    relationship_sources = decoded["relationship_contract"].get(
        "source_references"
    )
    if not isinstance(relationship_sources, dict):
        raise CloseoutSourceError(
            "closeout.source.binding",
            "relationship source references are missing",
        )
    for embedded_role, closeout_role in (
        ("plan", "plan"),
        ("primary_contract", "corpus_contract"),
        ("primary_manifest", "corpus_manifest"),
        ("usb_manifest", "usb_manifest"),
    ):
        _assert_embedded_reference(
            relationship_sources.get(embedded_role),
            references[closeout_role],
        )

    custody_sources = decoded["custody"].get("source_references")
    if not isinstance(custody_sources, dict):
        raise CloseoutSourceError(
            "closeout.source.binding",
            "custody source references are missing",
        )
    for role in (
        "plan",
        "corpus_contract",
        "corpus_manifest",
        "manifest_tool",
        "recovery_procedure",
    ):
        _assert_embedded_reference(custody_sources.get(role), references[role])


def _metadata_snapshot(
    repo_root: Path,
    index_path: str = DEFAULT_CLOSEOUT_PATH,
) -> CloseoutSnapshot:
    if (
        index_path != DEFAULT_CLOSEOUT_PATH
        or not _normalized_relative_path(index_path)
    ):
        raise CloseoutPathError(
            "closeout.index.path",
            "closeout index must be the exact tracked path",
        )
    index_bytes = _read_index(repo_root, index_path)
    value = _decode_strict_json(
        index_bytes,
        error_type=CloseoutValidationError,
        code="closeout.json.invalid",
        message="closeout index must contain strict UTF-8 JSON",
    )
    index = load_closeout_data(value)
    if index_bytes != serialize_closeout(index):
        raise CloseoutValidationError(
            "closeout.bytes",
            "closeout index must use canonical JSON bytes",
        )

    sources: dict[str, bytes] = {}
    for role in SOURCE_ROLES:
        payload = _read_metadata_file(
            repo_root / PurePosixPath(SOURCE_PATHS[role]),
            role,
        )
        if (
            hashlib.sha256(payload).hexdigest()
            != index["source_references"][role]["sha256"]
        ):
            raise CloseoutSourceError(
                "closeout.source.sha256_mismatch",
                "source differs from the pinned SHA-256 identity",
            )
        sources[role] = payload

    decoded: dict[str, dict[str, Any]] = {}
    for role in JSON_SOURCE_ROLES:
        source = _decode_source(sources[role], role)
        reference = index["source_references"][role]
        if (
            source.get("schema") != reference["schema"]
            or source.get("schema_version") != reference["schema_version"]
            or not _is_int(source.get("schema_version"))
        ):
            raise CloseoutSourceError(
                "closeout.source.schema_mismatch",
                "source schema differs from its pinned declaration",
            )
        decoded[role] = source

    _verify_embedded_bindings(index, decoded)
    try:
        contract = load_contract_data(decoded["corpus_contract"])
        validate_manifest_metadata(contract, decoded["corpus_manifest"])
        relationship_contract = load_relationship_contract_data(
            decoded["relationship_contract"]
        )
        validate_relationship_evidence_data(decoded["relationships"])
        custody = load_custody_data(decoded["custody"])
    except CorpusIndexError as error:
        raise CloseoutDependencyError(
            "closeout.dependency.metadata",
            "accepted metadata validation failed",
        ) from error
    custody_ids = [item.get("id") for item in custody.get("open_items", [])]
    if custody_ids != index["open_item_references"]["ids"]:
        raise CloseoutIntegrityError(
            "closeout.open_items.mismatch",
            "custody open items differ from closeout references",
        )
    return CloseoutSnapshot(
        index=index,
        index_bytes=index_bytes,
        sources=sources,
        contract=contract,
        relationship_contract=relationship_contract,
        custody=custody,
    )


def _verify_live_dependencies(
    repo_root: Path,
    snapshot: CloseoutSnapshot,
) -> None:
    try:
        regenerated_manifest = build_manifest(repo_root, snapshot.contract)
    except CorpusIndexError as error:
        raise CloseoutDependencyError(
            "closeout.dependency.manifest",
            "live manifest verification failed",
        ) from error
    if (
        serialize_manifest(regenerated_manifest)
        != snapshot.sources["corpus_manifest"]
    ):
        raise CloseoutIntegrityError(
            "closeout.manifest.bytes",
            "regenerated manifest differs from the tracked identity",
        )

    try:
        regenerated_relationships = build_relationship_evidence(
            repo_root,
            snapshot.relationship_contract,
        )
    except CorpusIndexError as error:
        raise CloseoutDependencyError(
            "closeout.dependency.relationship",
            "live relationship verification failed",
        ) from error
    if (
        serialize_relationship_evidence(regenerated_relationships)
        != snapshot.sources["relationships"]
    ):
        raise CloseoutIntegrityError(
            "closeout.relationships.bytes",
            "regenerated relationship evidence differs from the tracked identity",
        )

    try:
        custody_result = verify_custody(repo_root)
    except CorpusIndexError as error:
        raise CloseoutDependencyError(
            "closeout.dependency.custody",
            "custody verification failed",
        ) from error
    if custody_result != snapshot.custody:
        raise CloseoutIntegrityError(
            "closeout.custody.result_mismatch",
            "custody result differs from the held tracked policy",
        )


def _verify_snapshot_unchanged(
    repo_root: Path,
    snapshot: CloseoutSnapshot,
    index_path: str,
) -> None:
    if _read_index(repo_root, index_path) != snapshot.index_bytes:
        raise CloseoutIntegrityError(
            "closeout.snapshot.changed",
            "closeout metadata changed during verification",
        )
    for role in SOURCE_ROLES:
        if (
            _read_metadata_file(
                repo_root / PurePosixPath(SOURCE_PATHS[role]),
                role,
            )
            != snapshot.sources[role]
        ):
            raise CloseoutIntegrityError(
                "closeout.snapshot.changed",
                "closeout source snapshot changed during verification",
            )


def verify_closeout(
    repo_root: Path,
    index_path: str = DEFAULT_CLOSEOUT_PATH,
) -> dict[str, Any]:
    snapshot = _metadata_snapshot(repo_root, index_path)
    _verify_live_dependencies(repo_root, snapshot)
    _verify_snapshot_unchanged(repo_root, snapshot, index_path)
    return snapshot.index


def _build_parser() -> argparse.ArgumentParser:
    parser = _CloseoutArgumentParser(
        description=(
            "Verify the live, write-free P0-C4 phase closeout on the "
            "asset-retaining machine."
        )
    )
    parser.add_argument("--repo-root", default=".", help="Repository root.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    verify = subparsers.add_parser(
        "verify",
        help="Run source pins and all live phase-integrity checks.",
    )
    verify.add_argument(
        "--index",
        required=True,
        choices=(DEFAULT_CLOSEOUT_PATH,),
    )
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    try:
        arguments = _build_parser().parse_args(argv)
        verify_closeout(
            Path(arguments.repo_root),
            arguments.index,
        )
        print(
            "P0-C4 closeout verified: gate_ready; "
            "pending_human_merge"
        )
        return 0
    except CloseoutError as error:
        print(f"ERROR {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
