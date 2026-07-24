from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, Sequence

from corpus_index import (
    CONTRACT_SCHEMA,
    CONTRACT_SCHEMA_VERSION,
    MANIFEST_SCHEMA,
    MANIFEST_SCHEMA_VERSION,
    CorpusIndexError,
    SHA256_PATTERN,
    load_contract_data,
    validate_manifest_metadata,
)
from corpus_relationships import (
    _SymlinkTraversalError,
    _read_regular_no_follow,
)

CUSTODY_SCHEMA = "analogboard.phase0.initial-recording-corpus-custody"
SUPPORTED_CUSTODY_SCHEMA_VERSIONS = frozenset({1, 2})
DEFAULT_CUSTODY_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/custody.json"
)
PLAN_PATH = "docs/plans/260710-analogboard-rebuild-plan.html"
CONTRACT_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/contract.json"
)
MANIFEST_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/manifest.json"
)
MANIFEST_TOOL_PATH = "scripts/corpus-index/corpus_index.py"
RECOVERY_PROCEDURE_PATH = (
    "docs/operations/initial-recording-corpus/restore-and-reacquisition.md"
)
APPROVED_RECOVERY_PROCEDURE_SHA256 = (
    "64f9497959092742a1fb5b5733e63b76f23e7d946b2692408c9ad6748fa37261"
)
SOURCE_PATHS = {
    "plan": PLAN_PATH,
    "corpus_contract": CONTRACT_PATH,
    "corpus_manifest": MANIFEST_PATH,
    "manifest_tool": MANIFEST_TOOL_PATH,
    "recovery_procedure": RECOVERY_PROCEDURE_PATH,
}
SOURCE_ROLES = tuple(SOURCE_PATHS)
TOP_LEVEL_FIELDS = frozenset(
    {
        "schema",
        "schema_version",
        "canonical_locator",
        "source_references",
        "asset_owner",
        "retention",
        "at_rest",
        "availability",
        "restore",
        "reacquisition",
        "open_items",
    }
)
JSON_SOURCE_FIELDS = frozenset(
    {"path", "sha256", "schema", "schema_version"}
)
PLAN_SOURCE_FIELDS = frozenset({"path", "sha256", "revision"})
FILE_SOURCE_FIELDS = frozenset({"path", "sha256"})
UNRESOLVED_ASSET_OWNER_FIELDS = frozenset(
    {"verdict", "identity", "open_item"}
)
RESOLVED_ASSET_OWNER_FIELDS = frozenset(
    {"verdict", "identity", "item_id", "decided_on", "authority"}
)
UNRESOLVED_RETENTION_FIELDS = frozenset(
    {"verdict", "policy", "open_item"}
)
RESOLVED_RETENTION_FIELDS = frozenset(
    {
        "verdict",
        "policy",
        "item_id",
        "decided_on",
        "authority",
        "constraints",
        "reevaluation_requires",
    }
)
AUTHORITY_FIELDS = frozenset({"document", "anchor"})
AT_REST_FIELDS = frozenset(
    {
        "state",
        "d19_protection",
        "git_exclusion",
        "export",
        "authorized_location",
        "relocation_performed",
        "reprotection_performed",
        "reacquisition_performed",
        "decision_date",
        "decision_source",
    }
)
AVAILABILITY_FIELDS = frozenset(
    {
        "verdict",
        "scope",
        "source",
        "checks",
        "result",
        "restore_inferred",
    }
)
RESTORE_FIELDS = frozenset(
    {
        "verdict",
        "source_status",
        "source_identity",
        "verification",
        "open_item",
    }
)
REACQUISITION_FIELDS = frozenset(
    {
        "verdict",
        "result_scope",
        "same_sha256_required",
        "current_corpus_replaced",
    }
)
OPEN_ITEM_FIELDS = frozenset({"id", "reason", "status"})
RESOLVED_ITEM_FIELDS = frozenset({"id", "resolution", "status"})
RESOLUTION_FIELDS = frozenset({"authority", "decided_on"})
AVAILABILITY_CHECKS = (
    "source_set_exact",
    "present",
    "regular_file",
    "readable",
    "size_bytes",
    "sha256",
)
OPEN_ITEM_VALUES = (
    (
        "P0-C4-ASSET-OWNER",
        "asset_owner_decision_required",
        "open",
    ),
    (
        "P0-C4-RESTORE-SOURCE",
        "restore_source_not_identified",
        "open",
    ),
    (
        "P0-C4-RETENTION",
        "retention_policy_decision_required",
        "open",
    ),
)
DECISION_AUTHORITY = {
    "document": "task_management/260710-cross-repo-execution-roadmap.html",
    "anchor": "dispatch-owner-decisions-20260724-2",
}
RETENTION_CONSTRAINTS = (
    "expiry_unset",
    "deletion_prohibited",
)
RETENTION_REEVALUATION_REQUIRES = (
    "c9-frozen-corpus-materialized",
    "verified-restore-source-established",
    "owner-explicit-reevaluation",
)
PROHIBITED_METADATA_KEYS = frozenset(
    {
        "expected_counts",
        "expected_count",
        "entry_count",
        "expected_total_bytes",
        "actual_total_bytes",
        "total_bytes",
        "payload",
        "raw_rows",
        "packet_body",
        "host_locator",
    }
)
REQUIRED_PROCEDURE_HEADINGS = (
    "# P0-C4 Restore and Reacquisition Procedure",
    "## Scope and authority",
    "## Current custody verdicts",
    "## At-rest boundary",
    "## Restore path",
    "## Reacquisition path",
    "## Stop conditions",
)
REQUIRED_PROCEDURE_TOKENS = (
    "owner decision required",
    "retention decision required",
    "restore not performed",
    "pre-d19 plaintext local-only",
    "git exclusion is not at-rest protection",
    "export is prohibited",
    "availability is not restore verification",
    "separately authorized restore source",
    "same sha-256 for every manifest entry",
    "never overwrite the canonical corpus",
    "separate corpus version",
    "reacquisition is not restore",
)
ALLOWED_PROCEDURE_COMMAND = (
    "PYTHONDONTWRITEBYTECODE=1 python3 scripts/corpus-index/corpus_index.py \\\n"
    "  verify \\\n"
    "  --manifest "
    "docs/reference/initial-recording-corpus/2026-07-17/manifest.json"
)
FENCED_BLOCK_PATTERN = re.compile(
    r"```(?P<language>[A-Za-z0-9_-]*)\n(?P<body>.*?)\n```",
    re.DOTALL,
)
HOST_PATH_PATTERN = re.compile(
    r"(?:^|[\"'\s])(?:/[A-Za-z0-9_.-]+/|[A-Za-z]:[\\/])"
)
SECRET_PATTERN = re.compile(
    r"(?:token|password|secret|api[_-]?key)\s*[:=]",
    re.IGNORECASE,
)


class CustodyError(CorpusIndexError):
    pass


class CustodyValidationError(CustodyError):
    pass


class CustodySourceError(CustodyError):
    pass


class CustodyProcedureError(CustodyError):
    pass


class CustodyPathError(CustodyError):
    pass


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
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise CustodyValidationError(
                "custody.json.duplicate_key",
                "duplicate JSON key is not allowed",
            )
        result[key] = value
    return result


def _decode_strict_json(
    payload: bytes,
    *,
    error_type: type[CustodyError],
    code: str,
    message: str,
) -> object:
    try:
        text = payload.decode("utf-8")
        return json.loads(text, object_pairs_hook=_strict_pairs)
    except CustodyValidationError:
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
    name: str,
) -> Mapping[str, Any]:
    if not isinstance(value, dict) or set(value) != set(fields):
        raise CustodyValidationError(
            code,
            f"{name} must contain exactly the authorized fields",
        )
    return value


def _scan_prohibited_metadata(value: object) -> None:
    if isinstance(value, dict):
        for key, child in value.items():
            if key in PROHIBITED_METADATA_KEYS:
                raise CustodyValidationError(
                    "custody.metadata.prohibited",
                    f"custody policy must not contain prohibited metadata: {key}",
                )
            _scan_prohibited_metadata(child)
    elif isinstance(value, list):
        for child in value:
            _scan_prohibited_metadata(child)


def _validate_source_references(
    value: object,
    schema_version: int,
) -> None:
    if not isinstance(value, dict) or set(value) != set(SOURCE_ROLES):
        raise CustodySourceError(
            "custody.source.roles",
            "source_references must contain exactly the five required roles",
        )
    for role in SOURCE_ROLES:
        source = value[role]
        if role == "plan":
            fields = PLAN_SOURCE_FIELDS
        elif role in ("corpus_contract", "corpus_manifest"):
            fields = JSON_SOURCE_FIELDS
        else:
            fields = FILE_SOURCE_FIELDS
        if not isinstance(source, dict) or set(source) != set(fields):
            raise CustodySourceError(
                "custody.source.fields",
                f"source reference fields are invalid: {role}",
            )
        path = source.get("path")
        if (
            not _normalized_relative_path(path)
            or path != SOURCE_PATHS[role]
        ):
            raise CustodySourceError(
                "custody.source.path",
                f"source path is not the exact tracked path: {role}",
            )
        sha256 = source.get("sha256")
        if (
            not isinstance(sha256, str)
            or SHA256_PATTERN.fullmatch(sha256) is None
        ):
            raise CustodySourceError(
                "custody.source.sha256",
                f"source SHA-256 is invalid: {role}",
            )
        if role == "plan":
            expected_revision = (
                "Draft 4.7" if schema_version == 1 else "Draft 4.11"
            )
            if source.get("revision") != expected_revision:
                raise CustodySourceError(
                    "custody.source.revision",
                    f"plan revision must be {expected_revision}",
                )
        if role == "corpus_contract" and (
            source.get("schema") != CONTRACT_SCHEMA
            or source.get("schema_version") != CONTRACT_SCHEMA_VERSION
            or not _is_int(source.get("schema_version"))
        ):
            raise CustodySourceError(
                "custody.source.schema",
                "corpus contract source schema is unsupported",
            )
        if role == "corpus_manifest" and (
            source.get("schema") != MANIFEST_SCHEMA
            or source.get("schema_version") != MANIFEST_SCHEMA_VERSION
            or not _is_int(source.get("schema_version"))
        ):
            raise CustodySourceError(
                "custody.source.schema",
                "corpus manifest source schema is unsupported",
            )


def _validate_authority(value: object, *, code: str) -> None:
    authority = _exact_object(
        value,
        AUTHORITY_FIELDS,
        code=code,
        name="authority",
    )
    if authority != DECISION_AUTHORITY:
        raise CustodyValidationError(
            code,
            "authority must reference the exact owner-decision roadmap anchor",
        )


def _validate_asset_owner(value: object, schema_version: int) -> None:
    if schema_version == 2:
        owner = _exact_object(
            value,
            RESOLVED_ASSET_OWNER_FIELDS,
            code="custody.asset_owner",
            name="asset_owner",
        )
        _validate_authority(
            owner["authority"],
            code="custody.asset_owner",
        )
        expected = {
            "verdict": "resolved",
            "identity": "yanagihashi",
            "item_id": "P0-C4-ASSET-OWNER",
            "decided_on": "2026-07-24",
            "authority": DECISION_AUTHORITY,
        }
        if owner != expected:
            raise CustodyValidationError(
                "custody.asset_owner",
                "asset owner must match the exact resolved owner decision",
            )
        return
    owner = _exact_object(
        value,
        UNRESOLVED_ASSET_OWNER_FIELDS,
        code="custody.asset_owner",
        name="asset_owner",
    )
    if (
        owner.get("verdict") != "owner_decision_required"
        or owner.get("identity") is not None
        or not isinstance(owner.get("open_item"), str)
        or not owner.get("open_item")
    ):
        raise CustodyValidationError(
            "custody.asset_owner",
            "asset owner must remain an unresolved owner decision",
        )


def _validate_retention(value: object, schema_version: int) -> None:
    if schema_version == 2:
        retention = _exact_object(
            value,
            RESOLVED_RETENTION_FIELDS,
            code="custody.retention",
            name="retention",
        )
        _validate_authority(
            retention["authority"],
            code="custody.retention",
        )
        expected = {
            "verdict": "resolved",
            "policy": "retain-until-superseded",
            "item_id": "P0-C4-RETENTION",
            "decided_on": "2026-07-24",
            "authority": DECISION_AUTHORITY,
            "constraints": list(RETENTION_CONSTRAINTS),
            "reevaluation_requires": list(
                RETENTION_REEVALUATION_REQUIRES
            ),
        }
        if retention != expected:
            raise CustodyValidationError(
                "custody.retention",
                "retention must match the exact resolved no-deletion policy",
            )
        return
    retention = _exact_object(
        value,
        UNRESOLVED_RETENTION_FIELDS,
        code="custody.retention",
        name="retention",
    )
    if retention != {
        "verdict": "owner_decision_required",
        "policy": None,
        "open_item": "P0-C4-RETENTION",
    }:
        raise CustodyValidationError(
            "custody.retention",
            "retention must remain an unresolved owner decision",
        )


def _validate_at_rest(value: object) -> None:
    at_rest = _exact_object(
        value,
        AT_REST_FIELDS,
        code="custody.at_rest",
        name="at_rest",
    )
    expected = {
        "state": "pre_d19_plaintext_local_only",
        "d19_protection": "not_applied",
        "git_exclusion": "not_at_rest_protection",
        "export": "prohibited",
        "authorized_location": "asset-retaining-machine",
        "relocation_performed": False,
        "reprotection_performed": False,
        "reacquisition_performed": False,
        "decision_date": "2026-07-20",
        "decision_source": "plan",
    }
    if at_rest != expected:
        raise CustodyValidationError(
            "custody.at_rest",
            "at_rest must preserve the authorized pre-D19 local-only boundary",
        )


def _validate_availability(value: object) -> None:
    availability = _exact_object(
        value,
        AVAILABILITY_FIELDS,
        code="custody.availability",
        name="availability",
    )
    expected = {
        "verdict": "verified",
        "scope": "all_manifest_entries",
        "source": "corpus_manifest",
        "checks": list(AVAILABILITY_CHECKS),
        "result": "all_verified",
        "restore_inferred": False,
    }
    if availability != expected:
        raise CustodyValidationError(
            "custody.availability",
            "availability must record the exact verified checks without inferring restore",
        )


def _validate_restore(value: object) -> None:
    restore = _exact_object(
        value,
        RESTORE_FIELDS,
        code="custody.restore",
        name="restore",
    )
    expected = {
        "verdict": "not_performed",
        "source_status": "not_identified",
        "source_identity": None,
        "verification": None,
        "open_item": "P0-C4-RESTORE-SOURCE",
    }
    if restore != expected:
        raise CustodyValidationError(
            "custody.restore",
            "restore must remain not performed with no identified source",
        )


def _validate_reacquisition(value: object) -> None:
    reacquisition = _exact_object(
        value,
        REACQUISITION_FIELDS,
        code="custody.reacquisition",
        name="reacquisition",
    )
    expected = {
        "verdict": "out_of_scope_not_performed",
        "result_scope": "new_corpus_version",
        "same_sha256_required": False,
        "current_corpus_replaced": False,
    }
    if reacquisition != expected:
        raise CustodyValidationError(
            "custody.reacquisition",
            "reacquisition must remain an out-of-scope new corpus version",
        )


def _validate_open_items_v1(value: object) -> None:
    if not isinstance(value, list):
        raise CustodyValidationError(
            "custody.open_items",
            "open_items must be the exact sorted three-item array",
        )
    actual: list[tuple[object, object, object]] = []
    for item in value:
        if not isinstance(item, dict) or set(item) != set(OPEN_ITEM_FIELDS):
            raise CustodyValidationError(
                "custody.open_items",
                "each open item must contain exactly id, reason, and status",
            )
        if any(not isinstance(item[field], str) or not item[field] for field in item):
            raise CustodyValidationError(
                "custody.open_items",
                "open item identifiers, reasons, and states must be non-empty",
            )
        actual.append((item["id"], item["reason"], item["status"]))
    if tuple(actual) != OPEN_ITEM_VALUES:
        raise CustodyValidationError(
            "custody.open_items",
            "open_items must be the exact sorted unresolved set",
        )


def _resolved_item(item_id: str) -> dict[str, object]:
    return {
        "id": item_id,
        "resolution": {
            "authority": DECISION_AUTHORITY,
            "decided_on": "2026-07-24",
        },
        "status": "resolved",
    }


def _validate_open_items_v2(value: object) -> None:
    if not isinstance(value, list) or len(value) != 3:
        raise CustodyValidationError(
            "custody.open_items",
            "open_items must be the exact sorted three-item resolution array",
        )
    expected = [
        _resolved_item("P0-C4-ASSET-OWNER"),
        {
            "id": "P0-C4-RESTORE-SOURCE",
            "reason": "restore_source_not_identified",
            "status": "open",
        },
        _resolved_item("P0-C4-RETENTION"),
    ]
    for index, item in enumerate(value):
        fields = (
            OPEN_ITEM_FIELDS
            if index == 1
            else RESOLVED_ITEM_FIELDS
        )
        if not isinstance(item, dict) or set(item) != set(fields):
            raise CustodyValidationError(
                "custody.open_items",
                "open item fields must match their exact open/resolved state",
            )
        if index != 1:
            resolution = item.get("resolution")
            _exact_object(
                resolution,
                RESOLUTION_FIELDS,
                code="custody.open_items",
                name="resolution",
            )
            _validate_authority(
                resolution["authority"],
                code="custody.open_items",
            )
    if value != expected:
        raise CustodyValidationError(
            "custody.open_items",
            "open_items must resolve owner and retention while restore stays open",
        )


def load_custody_data(value: object) -> dict[str, Any]:
    _scan_prohibited_metadata(value)
    if not isinstance(value, dict):
        raise CustodyValidationError(
            "custody.type",
            "custody policy must be an object",
        )
    if set(value) != set(TOP_LEVEL_FIELDS):
        raise CustodyValidationError(
            "custody.fields",
            "custody policy must contain exactly the authorized fields",
        )
    if value.get("schema") != CUSTODY_SCHEMA:
        raise CustodyValidationError(
            "custody.schema",
            f"schema must be {CUSTODY_SCHEMA}",
        )
    version = value.get("schema_version")
    if (
        not _is_int(version)
        or version not in SUPPORTED_CUSTODY_SCHEMA_VERSIONS
    ):
        raise CustodyValidationError(
            "custody.schema_version",
            "schema_version must be a supported version",
        )
    locator = value.get("canonical_locator")
    if not _normalized_relative_path(locator):
        raise CustodyValidationError(
            "custody.canonical_locator",
            "canonical_locator must be normalized repository-relative metadata",
        )
    _validate_source_references(value.get("source_references"), version)
    _validate_asset_owner(value.get("asset_owner"), version)
    _validate_retention(value.get("retention"), version)
    _validate_at_rest(value.get("at_rest"))
    _validate_availability(value.get("availability"))
    _validate_restore(value.get("restore"))
    _validate_reacquisition(value.get("reacquisition"))
    if version == 1:
        _validate_open_items_v1(value.get("open_items"))
        if (
            value["asset_owner"]["open_item"] != "P0-C4-ASSET-OWNER"
            or value["retention"]["open_item"] != "P0-C4-RETENTION"
            or value["restore"]["open_item"] != "P0-C4-RESTORE-SOURCE"
        ):
            raise CustodyValidationError(
                "custody.open_items",
                "open-item links must match the current unresolved states",
            )
    else:
        _validate_open_items_v2(value.get("open_items"))
        if value["restore"]["open_item"] != "P0-C4-RESTORE-SOURCE":
            raise CustodyValidationError(
                "custody.open_items",
                "restore open-item link must remain unresolved",
            )
    return value


def serialize_custody(value: object) -> bytes:
    validated = load_custody_data(value)
    payload = (
        json.dumps(
            validated,
            ensure_ascii=False,
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")
    text = payload.decode("utf-8")
    if HOST_PATH_PATTERN.search(text) or SECRET_PATTERN.search(text):
        raise CustodyValidationError(
            "custody.content.prohibited",
            "custody policy contains an absolute host locator or secret-like content",
        )
    return payload


def _read_source(repo_root: Path, role: str) -> bytes:
    path = repo_root / PurePosixPath(SOURCE_PATHS[role])
    try:
        return _read_regular_no_follow(path)
    except _SymlinkTraversalError as error:
        raise CustodySourceError(
            "custody.source.symlink",
            f"source must not traverse a symlink: {role}",
        ) from error
    except OSError as error:
        raise CustodySourceError(
            "custody.source.unreadable",
            f"source must be a readable regular file: {role}",
        ) from error


def _verify_source_identity(
    role: str,
    payload: bytes,
    source: Mapping[str, Any],
) -> None:
    actual = hashlib.sha256(payload).hexdigest()
    if actual != source["sha256"]:
        raise CustodySourceError(
            "custody.source.sha256_mismatch",
            f"source SHA-256 differs from the pinned identity: {role}",
        )


def _decode_metadata_source(payload: bytes, role: str) -> object:
    try:
        return _decode_strict_json(
            payload,
            error_type=CustodySourceError,
            code="custody.source.json",
            message=f"source must contain strict UTF-8 JSON: {role}",
        )
    except CustodyValidationError as error:
        raise CustodySourceError(
            "custody.source.json",
            f"source must not contain duplicate JSON keys: {role}",
        ) from error


def _lint_procedure_semantics(text: str) -> None:
    folded = re.sub(r"\s+", " ", text.casefold())
    if any(heading not in text for heading in REQUIRED_PROCEDURE_HEADINGS):
        raise CustodyProcedureError(
            "custody.procedure",
            "recovery procedure is missing a required section",
        )
    if any(token not in folded for token in REQUIRED_PROCEDURE_TOKENS):
        raise CustodyProcedureError(
            "custody.procedure",
            "recovery procedure is missing a required policy statement",
        )
    blocks = list(FENCED_BLOCK_PATTERN.finditer(text))
    if (
        len(blocks) != 1
        or blocks[0].group("language") != "bash"
        or blocks[0].group("body").strip() != ALLOWED_PROCEDURE_COMMAND
    ):
        raise CustodyProcedureError(
            "custody.procedure",
            "recovery procedure may contain only the canonical read-only verifier command",
        )
    if SECRET_PATTERN.search(text):
        raise CustodyProcedureError(
            "custody.procedure",
            "recovery procedure must not contain secret-like content",
        )


def _validate_procedure(payload: bytes) -> None:
    try:
        text = payload.decode("utf-8")
    except UnicodeError as error:
        raise CustodyProcedureError(
            "custody.procedure",
            "recovery procedure must be valid UTF-8",
        ) from error
    if (
        hashlib.sha256(payload).hexdigest()
        != APPROVED_RECOVERY_PROCEDURE_SHA256
    ):
        raise CustodyProcedureError(
            "custody.procedure",
            "recovery procedure must match the exact approved identity",
        )
    _lint_procedure_semantics(text)


def verify_custody(
    repo_root: Path,
    policy_path: str = DEFAULT_CUSTODY_PATH,
) -> dict[str, Any]:
    if policy_path != DEFAULT_CUSTODY_PATH or not _normalized_relative_path(
        policy_path
    ):
        raise CustodyPathError(
            "custody.policy.path",
            f"policy must be {DEFAULT_CUSTODY_PATH}",
        )
    try:
        recorded = _read_regular_no_follow(
            repo_root / PurePosixPath(policy_path)
        )
    except _SymlinkTraversalError as error:
        raise CustodyPathError(
            "custody.policy.symlink",
            "custody policy must not traverse a symlink",
        ) from error
    except OSError as error:
        raise CustodyPathError(
            "custody.policy.unreadable",
            "custody policy must be a readable regular file",
        ) from error
    value = _decode_strict_json(
        recorded,
        error_type=CustodyValidationError,
        code="custody.json.invalid",
        message="custody policy must contain strict UTF-8 JSON",
    )
    policy = load_custody_data(value)
    canonical = serialize_custody(policy)
    if recorded != canonical:
        raise CustodyValidationError(
            "custody.bytes",
            "custody policy must use canonical JSON bytes",
        )

    sources = policy["source_references"]
    payloads: dict[str, bytes] = {}
    for role in SOURCE_ROLES:
        payload = _read_source(repo_root, role)
        _verify_source_identity(role, payload, sources[role])
        payloads[role] = payload

    contract_value = _decode_metadata_source(
        payloads["corpus_contract"],
        "corpus_contract",
    )
    manifest_value = _decode_metadata_source(
        payloads["corpus_manifest"],
        "corpus_manifest",
    )
    if (
        not isinstance(contract_value, dict)
        or contract_value.get("schema") != sources["corpus_contract"]["schema"]
        or contract_value.get("schema_version")
        != sources["corpus_contract"]["schema_version"]
        or not isinstance(manifest_value, dict)
        or manifest_value.get("schema") != sources["corpus_manifest"]["schema"]
        or manifest_value.get("schema_version")
        != sources["corpus_manifest"]["schema_version"]
    ):
        raise CustodySourceError(
            "custody.source.schema",
            "source schema does not match its pinned declaration",
        )
    try:
        contract = load_contract_data(contract_value)
        validate_manifest_metadata(contract, manifest_value)
    except CorpusIndexError as error:
        raise CustodySourceError(
            "custody.source.metadata",
            "contract and manifest metadata are inconsistent",
        ) from error
    if (
        policy["canonical_locator"] != contract.canonical_locator
        or manifest_value.get("source_locator") != contract.canonical_locator
    ):
        raise CustodySourceError(
            "custody.locator.mismatch",
            "policy locator must match the contract and manifest",
        )
    _validate_procedure(payloads["recovery_procedure"])
    return policy


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Verify the static P0-C4 custody index."
    )
    parser.add_argument("--repo-root", default=".", help="Repository root.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    verify = subparsers.add_parser(
        "verify",
        help="Verify the exact tracked custody policy and metadata sources.",
    )
    verify.add_argument("--policy", required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        verify_custody(Path(arguments.repo_root), arguments.policy)
        print(f"custody verified: {DEFAULT_CUSTODY_PATH}")
        return 0
    except CustodyError as error:
        print(f"ERROR {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
