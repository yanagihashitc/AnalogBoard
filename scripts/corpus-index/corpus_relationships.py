from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
import re
import stat
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime, timedelta
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, Sequence

from corpus_index import (
    CorpusContract,
    CorpusIndexError,
    ManifestValidationError,
    SHA256_PATTERN,
    _SymlinkTraversalError,
    _read_regular_no_follow,
    load_contract_data,
    validate_manifest_metadata,
)

RELATIONSHIP_CONTRACT_SCHEMA = (
    "analogboard.phase0.initial-recording-corpus-relationship-contract"
)
RELATIONSHIP_CONTRACT_SCHEMA_VERSION = 1
RELATIONSHIP_EVIDENCE_SCHEMA = (
    "analogboard.phase0.initial-recording-corpus-relationships"
)
RELATIONSHIP_EVIDENCE_SCHEMA_VERSION = 1
PRIMARY_CONTRACT_SCHEMA = "analogboard.phase0.initial-recording-corpus-contract"
PRIMARY_MANIFEST_SCHEMA = "analogboard.phase0.initial-recording-corpus-manifest"
USB_MANIFEST_SCHEMA = "analogboard.phase0.usbpcap-corpus-index"
DEFAULT_RELATIONSHIP_CONTRACT_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/"
    "relationship-contract.json"
)
DEFAULT_RELATIONSHIPS_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/relationships.json"
)
SOURCE_ROLES = (
    "primary_contract",
    "primary_manifest",
    "usb_manifest",
    "plan",
)
SOURCE_SCHEMAS = {
    "primary_contract": (PRIMARY_CONTRACT_SCHEMA, 1),
    "primary_manifest": (PRIMARY_MANIFEST_SCHEMA, 1),
    "usb_manifest": (USB_MANIFEST_SCHEMA, 1),
}
TELEMETRY_HEADER = (
    "cycle_id",
    "trigger_source",
    "trigger_detected_monotonic_ms",
    "ddr_rd_end_confirmed_monotonic_ms",
    "host_drain_complete_monotonic_ms",
    "publish_cleanup_complete_monotonic_ms",
    "host_ready_monotonic_ms",
    "next_external_trigger_detected_monotonic_ms",
    "rearm_ms",
    "external_trigger_wait_ms",
)
RELATIONSHIP_FIELDS = frozenset(
    {
        "schema",
        "schema_version",
        "source_references",
        "telemetry",
        "clock_policy",
        "failure_trace",
    }
)
JSON_SOURCE_FIELDS = frozenset({"path", "sha256", "schema", "schema_version"})
PLAN_SOURCE_FIELDS = frozenset({"path", "sha256", "revision"})
TELEMETRY_FIELDS = frozenset({"header", "sessions"})
TELEMETRY_SESSION_FIELDS = frozenset({"path", "ordered_runs"})
CLOCK_FIELDS = frozenset(
    {
        "run_label",
        "capture",
        "telemetry_filename",
        "telemetry_rows",
        "containment",
        "additional_containment_tolerance_seconds",
        "calibrated_cross_clock_skew_seconds",
        "filesystem_mtime_used",
    }
)
RUN_LABEL_FIELDS = frozenset({"basis", "bucket", "quantization_seconds"})
CAPTURE_CLOCK_FIELDS = frozenset({"basis", "timezone"})
TELEMETRY_FILENAME_CLOCK_FIELDS = frozenset(
    {"basis", "quantization_seconds"}
)
TELEMETRY_ROW_CLOCK_FIELDS = frozenset({"basis"})
FAILURE_TRACE_FIELDS = frozenset({"source", "required_value"})
USB_FIELDS = frozenset(
    {
        "analysis_root",
        "bounded_summary",
        "captured_date",
        "captures",
        "constraints",
        "interface",
        "provisional",
        "required_tshark_fields",
        "schema",
        "schema_version",
        "source_manifest",
        "source_root",
        "timestamp_basis",
        "tools",
    }
)
USB_CAPTURE_FIELDS = frozenset(
    {
        "analysis",
        "duration_seconds",
        "earliest_packet_time",
        "filename",
        "latest_packet_time",
        "packet_count",
        "sha256",
        "size_bytes",
    }
)
USB_CONSTRAINT_FIELDS = frozenset(
    {"d19", "failure_trace_present", "raw_payload_tracked"}
)
USB_BOUNDED_SUMMARY_FIELDS = frozenset(
    {
        "byte_identity_runs",
        "capture_summary_schema_version",
        "path",
        "schema",
        "schema_version",
        "sha256",
        "size_bytes",
    }
)
USB_CAPTURE_ANALYSIS_FIELDS = frozenset(
    {"path", "schema", "schema_version", "sha256", "size_bytes"}
)
USB_INTERFACE_FIELDS = frozenset(
    {
        "capture_length",
        "description",
        "encapsulation",
        "file_encapsulation",
        "file_type",
        "index",
        "name",
    }
)
USB_SOURCE_MANIFEST_FIELDS = frozenset(
    {"path", "schema", "schema_version", "sha256", "size_bytes"}
)
USB_TOOLS_FIELDS = frozenset({"capinfos", "tshark"})
USB_TOOL_FIELDS = frozenset({"path", "version"})
USB_REQUIRED_TSHARK_FIELDS = (
    "frame.number",
    "frame.time_epoch",
    "frame.time_relative",
    "frame.cap_len",
    "frame.len",
    "usb.bus_id",
    "usb.device_address",
    "usb.irp_id",
    "usb.urb_type",
    "usb.irp_info.direction",
    "usb.function",
    "usb.transfer_type",
    "usb.endpoint_address",
    "usb.usbd_status",
    "usb.urb_len",
    "usb.data_len",
    "usb.idVendor",
    "usb.idProduct",
    "usb.bInterfaceNumber",
)
USB_CAPTURE_DURATION_PATTERN = re.compile(r"^[0-9]+[.][0-9]{6}$")
EVIDENCE_FIELDS = frozenset(
    {
        "schema",
        "schema_version",
        "global",
        "runs",
        "telemetry_sessions",
        "clock_policy",
    }
)
EVIDENCE_GLOBAL_FIELDS = frozenset(
    {
        "run_count",
        "pair_count",
        "cfg_count",
        "capture_count",
        "telemetry_session_count",
        "telemetry_row_count",
        "failure_trace_present",
    }
)
EVIDENCE_RUN_FIELDS = frozenset(
    {
        "run_id",
        "density",
        "cfg",
        "capture",
        "pair_count",
        "sequence_first",
        "sequence_last",
        "sequences_continuous",
        "pair_identity_sha256",
        "capture_contains_run_bucket",
        "telemetry_session",
        "telemetry_row_ordinal",
    }
)
EVIDENCE_SESSION_FIELDS = frozenset(
    {
        "path",
        "run_count",
        "row_count",
        "header_sha256",
        "cycle_id_first",
        "cycle_id_last",
        "cycle_ids_continuous",
        "monotonic_boundaries_valid",
    }
)
RUN_ID_PATTERN = re.compile(r"^[0-9]{6}_[0-9]{4}$")
BIN_RELATIONSHIP_PATTERN = re.compile(
    r"^(?P<run_id>[0-9]{6}_[0-9]{4})_"
    r"(?P<channel>fl|fh)_(?P<sequence>-?[0-9]+)[.]bin$"
)
CFG_RELATIONSHIP_PATTERN = re.compile(
    r"^(?P<run_id>[0-9]{6}_[0-9]{4})_cfg[.]txt$"
)
TELEMETRY_FILENAME_PATTERN = re.compile(
    r"^[0-9]{6}_[0-9]{6}_rearm_telemetry[.]csv$"
)
CAPTURE_TIME_FORMAT = "%Y-%m-%d %H:%M:%S.%f"
CAPTURE_TIME_PATTERN = re.compile(
    r"^[0-9]{4}-[0-9]{2}-[0-9]{2} "
    r"[0-9]{2}:[0-9]{2}:[0-9]{2}[.][0-9]{6}$"
)
class RelationshipError(CorpusIndexError): pass
class RelationshipContractError(RelationshipError): pass
class SourceReferenceError(RelationshipError): pass
class PairValidationError(RelationshipError): pass
class RelationshipMappingError(RelationshipError): pass
class ClockPolicyError(RelationshipError): pass
class TelemetryValidationError(RelationshipError): pass
class EvidenceValidationError(RelationshipError): pass
class RelationshipPathError(RelationshipError): pass


@dataclass(frozen=True)
class SourceReference:
    role: str
    path: str
    sha256: str
    schema: str | None = None
    schema_version: int | None = None
    revision: str | None = None


@dataclass(frozen=True)
class TelemetrySession:
    path: str
    ordered_runs: tuple[str, ...]


@dataclass(frozen=True)
class RelationshipContract:
    sources: tuple[SourceReference, ...]
    telemetry_header: tuple[str, ...]
    telemetry_sessions: tuple[TelemetrySession, ...]
    clock_policy: dict[str, Any]

    @property
    def source_by_role(self) -> dict[str, SourceReference]:
        return {source.role: source for source in self.sources}


@dataclass(frozen=True)
class ParsedTelemetry:
    path: str
    row_count: int
    cycle_id_first: int
    cycle_id_last: int


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
            raise RelationshipContractError(
                "relationship.json.duplicate_key",
                "duplicate JSON key is not allowed",
            )
        result[key] = value
    return result


def _decode_strict_json(
    payload: bytes,
    *,
    code: str,
    message: str,
) -> object:
    try:
        text = payload.decode("utf-8")
        return json.loads(text, object_pairs_hook=_strict_pairs)
    except RelationshipContractError:
        raise
    except (
        UnicodeError,
        json.JSONDecodeError,
        RecursionError,
        ValueError,
    ) as error:
        raise SourceReferenceError(code, message) from error


def _read_regular_metadata(
    path: Path,
    *,
    error_type: type[RelationshipError],
    symlink_code: str,
    symlink_message: str,
    unreadable_code: str,
    unreadable_message: str,
) -> bytes:
    try:
        return _read_regular_no_follow(path)
    except _SymlinkTraversalError as error:
        raise error_type(symlink_code, symlink_message) from error
    except RelationshipError:
        raise
    except OSError as error:
        raise error_type(unreadable_code, unreadable_message) from error


def load_relationship_contract(path: Path) -> RelationshipContract:
    payload = _read_regular_metadata(
        path,
        error_type=RelationshipContractError,
        symlink_code="relationship.contract.symlink",
        symlink_message=(
            "relationship contract must be a regular file without symlink traversal"
        ),
        unreadable_code="relationship.json.invalid",
        unreadable_message="relationship contract must contain valid UTF-8 JSON",
    )
    try:
        value = _decode_strict_json(
            payload,
            code="relationship.json.invalid",
            message="relationship contract must contain valid UTF-8 JSON",
        )
    except SourceReferenceError as error:
        raise RelationshipContractError(error.code, error.message) from error
    return load_relationship_contract_data(value)


def _parse_source_references(value: object) -> tuple[SourceReference, ...]:
    if not isinstance(value, dict) or set(value) != set(SOURCE_ROLES):
        raise SourceReferenceError(
            "relationship.sources",
            "source_references must contain the four required metadata sources",
        )
    parsed: list[SourceReference] = []
    for role in SOURCE_ROLES:
        item = value[role]
        allowed = PLAN_SOURCE_FIELDS if role == "plan" else JSON_SOURCE_FIELDS
        if not isinstance(item, dict) or set(item) != set(allowed):
            raise SourceReferenceError(
                "relationship.source.fields",
                f"source reference fields are invalid: {role}",
            )
        path = item.get("path")
        if not _normalized_relative_path(path):
            raise SourceReferenceError(
                "relationship.source.path",
                "source path must be normalized repository-relative metadata",
            )
        digest = item.get("sha256")
        if not isinstance(digest, str) or SHA256_PATTERN.fullmatch(digest) is None:
            raise SourceReferenceError(
                "relationship.source.sha256",
                f"source SHA-256 is invalid: {role}",
            )
        if role == "plan":
            revision = item.get("revision")
            if not isinstance(revision, str) or not revision:
                raise SourceReferenceError(
                    "relationship.source.revision",
                    "plan revision must be a non-empty string",
                )
            parsed.append(
                SourceReference(role, path, digest, revision=revision)
            )
            continue
        expected_schema, expected_version = SOURCE_SCHEMAS[role]
        if item.get("schema") != expected_schema:
            raise SourceReferenceError(
                "relationship.source.schema",
                f"source schema is unsupported: {role}",
            )
        version = item.get("schema_version")
        if not _is_int(version) or version != expected_version:
            raise SourceReferenceError(
                "relationship.source.schema_version",
                f"source schema version is unsupported: {role}",
            )
        parsed.append(
            SourceReference(
                role,
                path,
                digest,
                schema=expected_schema,
                schema_version=expected_version,
            )
        )
    return tuple(parsed)


def _parse_telemetry_contract(
    value: object,
) -> tuple[tuple[str, ...], tuple[TelemetrySession, ...]]:
    if not isinstance(value, dict):
        raise RelationshipContractError(
            "relationship.telemetry.type",
            "telemetry must be an object",
        )
    if set(value) != set(TELEMETRY_FIELDS):
        raise RelationshipContractError(
            "relationship.telemetry.fields",
            "telemetry fields are invalid",
        )
    header = value.get("header")
    if not isinstance(header, list) or tuple(header) != TELEMETRY_HEADER:
        raise RelationshipContractError(
            "relationship.telemetry.header",
            "telemetry header must match the exact 10-column contract",
        )
    sessions = value.get("sessions")
    if not isinstance(sessions, list) or not sessions:
        raise RelationshipContractError(
            "relationship.telemetry.sessions",
            "telemetry.sessions must be a non-empty array",
        )
    parsed: list[TelemetrySession] = []
    seen_paths: set[str] = set()
    for item in sessions:
        if not isinstance(item, dict) or set(item) != set(TELEMETRY_SESSION_FIELDS):
            raise RelationshipContractError(
                "relationship.telemetry.session.fields",
                "telemetry session fields are invalid",
            )
        path = item.get("path")
        if (
            not _normalized_relative_path(path)
            or len(PurePosixPath(path).parts) != 1
            or TELEMETRY_FILENAME_PATTERN.fullmatch(path) is None
        ):
            raise RelationshipContractError(
                "relationship.telemetry.path",
                "telemetry session path must be a normalized root-level filename",
            )
        if path in seen_paths:
            raise RelationshipContractError(
                "relationship.telemetry.path.duplicate",
                f"duplicate telemetry session path: {path}",
            )
        ordered_runs = item.get("ordered_runs")
        if not isinstance(ordered_runs, list) or not ordered_runs:
            raise RelationshipContractError(
                "relationship.telemetry.runs",
                "ordered_runs must be a non-empty array",
            )
        if any(
            not isinstance(run_id, str)
            or RUN_ID_PATTERN.fullmatch(run_id) is None
            for run_id in ordered_runs
        ):
            raise RelationshipContractError(
                "relationship.telemetry.run_id",
                "ordered_runs entries must match YYMMDD_HHMM",
            )
        seen_paths.add(path)
        parsed.append(TelemetrySession(path, tuple(ordered_runs)))
    return tuple(header), tuple(sorted(parsed, key=lambda item: item.path))


def _clock_object(
    value: object,
    fields: frozenset[str],
    *,
    code: str,
    message: str,
) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != set(fields):
        raise ClockPolicyError(code, message)
    return value


def _parse_clock_policy(value: object) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != set(CLOCK_FIELDS):
        raise ClockPolicyError(
            "relationship.clock.fields",
            "clock_policy fields are invalid",
        )
    run_label = _clock_object(
        value.get("run_label"),
        RUN_LABEL_FIELDS,
        code="relationship.clock.run_label.fields",
        message="run_label fields are invalid",
    )
    if run_label["basis"] != "GetLocalTime YYMMDD_HHMM filename label":
        raise ClockPolicyError(
            "relationship.clock.run_label.basis",
            "run_label basis is unsupported",
        )
    if run_label["bucket"] != "half-open":
        raise ClockPolicyError(
            "relationship.clock.run_label.bucket",
            "run label bucket must be half-open",
        )
    if (
        not _is_int(run_label["quantization_seconds"])
        or run_label["quantization_seconds"] != 60
    ):
        raise ClockPolicyError(
            "relationship.clock.run_label.quantization",
            "run label quantization_seconds must be 60",
        )
    capture = _clock_object(
        value.get("capture"),
        CAPTURE_CLOCK_FIELDS,
        code="relationship.clock.capture.fields",
        message="capture clock fields are invalid",
    )
    if capture["basis"] != "Capinfos packet timestamps":
        raise ClockPolicyError(
            "relationship.clock.capture.basis",
            "capture clock basis is unsupported",
        )
    if capture["timezone"] != "undeclared":
        raise ClockPolicyError(
            "relationship.clock.capture.timezone",
            "capture timezone must remain undeclared",
        )
    telemetry_filename = _clock_object(
        value.get("telemetry_filename"),
        TELEMETRY_FILENAME_CLOCK_FIELDS,
        code="relationship.clock.telemetry_filename.fields",
        message="telemetry filename clock fields are invalid",
    )
    if telemetry_filename["basis"] != (
        "GetLocalTime YYMMDD_HHMMSS filename label at EP6 thread start"
    ):
        raise ClockPolicyError(
            "relationship.clock.telemetry_filename.basis",
            "telemetry filename clock basis is unsupported",
        )
    if (
        not _is_int(telemetry_filename["quantization_seconds"])
        or telemetry_filename["quantization_seconds"] != 1
    ):
        raise ClockPolicyError(
            "relationship.clock.telemetry_filename.quantization",
            "telemetry filename quantization_seconds must be 1",
        )
    telemetry_rows = _clock_object(
        value.get("telemetry_rows"),
        TELEMETRY_ROW_CLOCK_FIELDS,
        code="relationship.clock.telemetry_rows.fields",
        message="telemetry row clock fields are invalid",
    )
    if telemetry_rows["basis"] != (
        "GetTickCount64 session-local monotonic milliseconds"
    ):
        raise ClockPolicyError(
            "relationship.clock.telemetry_rows.basis",
            "telemetry row clock basis is unsupported",
        )
    if value["containment"] != "capture_fully_contains_run_minute_bucket":
        raise ClockPolicyError(
            "relationship.clock.containment",
            "capture containment policy is unsupported",
        )
    tolerance = value["additional_containment_tolerance_seconds"]
    if not _is_int(tolerance) or tolerance != 0:
        raise ClockPolicyError(
            "relationship.clock.tolerance",
            "additional containment tolerance must be 0 seconds",
        )
    if value["calibrated_cross_clock_skew_seconds"] is not None:
        raise ClockPolicyError(
            "relationship.clock.skew",
            "calibrated cross-clock skew must be null",
        )
    if value["filesystem_mtime_used"] is not False:
        raise ClockPolicyError(
            "relationship.clock.mtime",
            "filesystem mtime use is forbidden",
        )
    return json.loads(json.dumps(value))


def load_relationship_contract_data(value: object) -> RelationshipContract:
    if not isinstance(value, dict):
        raise RelationshipContractError(
            "relationship.type",
            "relationship contract must be an object",
        )
    if set(value) != set(RELATIONSHIP_FIELDS):
        raise RelationshipContractError(
            "relationship.fields",
            "relationship contract fields are invalid",
        )
    if value.get("schema") != RELATIONSHIP_CONTRACT_SCHEMA:
        raise RelationshipContractError(
            "relationship.schema",
            f"schema must be '{RELATIONSHIP_CONTRACT_SCHEMA}'",
        )
    version = value.get("schema_version")
    if not _is_int(version) or version != RELATIONSHIP_CONTRACT_SCHEMA_VERSION:
        raise RelationshipContractError(
            "relationship.schema_version",
            "schema_version must be 1",
        )
    sources = _parse_source_references(value.get("source_references"))
    header, sessions = _parse_telemetry_contract(value.get("telemetry"))
    clock_policy = _parse_clock_policy(value.get("clock_policy"))
    failure_trace = value.get("failure_trace")
    if (
        not isinstance(failure_trace, dict)
        or set(failure_trace) != set(FAILURE_TRACE_FIELDS)
        or failure_trace.get("source")
        != "usb_manifest.constraints.failure_trace_present"
        or failure_trace.get("required_value") is not False
    ):
        raise RelationshipContractError(
            "relationship.failure_trace.contract",
            "failure_trace must require boolean false from the USB manifest constraint",
        )
    return RelationshipContract(sources, header, sessions, clock_policy)


def _read_pinned_source(repo_root: Path, reference: SourceReference) -> bytes:
    try:
        payload = _read_regular_no_follow(
            repo_root / PurePosixPath(reference.path)
        )
    except _SymlinkTraversalError as error:
        raise SourceReferenceError(
            "relationship.source.symlink",
            f"source reference must not traverse a symlink: {reference.role}",
        ) from error
    except SourceReferenceError:
        raise
    except OSError as error:
        raise SourceReferenceError(
            "relationship.source.unreadable",
            f"source reference is not readable: {reference.role}",
        ) from error
    if hashlib.sha256(payload).hexdigest() != reference.sha256:
        raise SourceReferenceError(
            "relationship.source.sha256_mismatch",
            f"source SHA-256 does not match pinned reference: {reference.role}",
        )
    return payload


def _load_sources(
    repo_root: Path,
    relationship: RelationshipContract,
) -> tuple[CorpusContract, object, dict[str, Any]]:
    payloads = {
        source.role: _read_pinned_source(repo_root, source)
        for source in relationship.sources
    }
    primary_contract_value = _decode_strict_json(
        payloads["primary_contract"],
        code="relationship.source.json",
        message="primary contract must contain valid UTF-8 JSON",
    )
    primary_manifest = _decode_strict_json(
        payloads["primary_manifest"],
        code="relationship.source.json",
        message="primary manifest must contain valid UTF-8 JSON",
    )
    usb_manifest = _decode_strict_json(
        payloads["usb_manifest"],
        code="relationship.source.json",
        message="USB manifest must contain valid UTF-8 JSON",
    )
    primary_contract = load_contract_data(primary_contract_value)
    if not isinstance(primary_manifest, dict):
        raise SourceReferenceError(
            "relationship.manifest.type",
            "primary manifest must be an object",
        )
    if not isinstance(usb_manifest, dict):
        raise SourceReferenceError(
            "relationship.usb.type",
            "USB manifest must be an object",
        )
    return primary_contract, primary_manifest, usb_manifest


def _manifest_entries(manifest: dict[str, Any]) -> list[dict[str, Any]]:
    entries = manifest.get("entries")
    if not isinstance(entries, list) or any(
        not isinstance(entry, dict) for entry in entries
    ):
        raise SourceReferenceError(
            "relationship.manifest.entries",
            "primary manifest entries must be an array of objects",
        )
    return entries


def _validate_pairs(
    primary_contract: CorpusContract,
    entries: list[dict[str, Any]],
) -> tuple[dict[str, dict[str, Any]], int]:
    authoritative_bin_count = primary_contract.expected_counts["bin"]
    if authoritative_bin_count % 2 != 0:
        raise PairValidationError(
            "relationship.bin_count.odd",
            f"authoritative bin count must be even, found {authoritative_bin_count}",
        )
    run_channels: dict[str, dict[str, dict[int, dict[str, Any]]]] = {
        run_id: {"fl": {}, "fh": {}} for run_id in primary_contract.run_ids
    }
    for entry in entries:
        if entry.get("kind") != "bin":
            continue
        path = entry.get("path")
        if not isinstance(path, str):
            raise PairValidationError(
                "relationship.sequence.invalid",
                "bin path does not match the relationship grammar: <non-string>",
            )
        matched = BIN_RELATIONSHIP_PATTERN.fullmatch(path)
        if matched is None:
            raise PairValidationError(
                "relationship.sequence.invalid",
                f"bin path does not match the relationship grammar: {path}",
            )
        run_id = matched.group("run_id")
        if run_id not in run_channels:
            raise PairValidationError(
                "relationship.run.unknown",
                f"bin entry references unknown run: {run_id}",
            )
        raw_sequence = matched.group("sequence")
        if raw_sequence.startswith("0") and raw_sequence != "0":
            raise PairValidationError(
                "relationship.sequence.invalid",
                f"sequence must not contain a leading zero: {path}",
            )
        sequence = int(raw_sequence)
        if sequence < 1:
            raise PairValidationError(
                "relationship.sequence.invalid",
                f"sequence must be a positive integer: {path}",
            )
        channel = matched.group("channel")
        if sequence in run_channels[run_id][channel]:
            raise PairValidationError(
                "relationship.sequence.duplicate",
                f"duplicate logical sequence for run {run_id}",
            )
        run_channels[run_id][channel][sequence] = entry

    pair_total = 0
    run_summaries: dict[str, dict[str, Any]] = {}
    for run_id in sorted(run_channels):
        fl = run_channels[run_id]["fl"]
        fh = run_channels[run_id]["fh"]
        if set(fl) != set(fh):
            raise PairValidationError(
                "relationship.pair.mismatch",
                f"FL/FH sequences differ for run {run_id}",
            )
        sequences = sorted(fl)
        expected = list(range(1, len(sequences) + 1))
        if not sequences or sequences != expected:
            raise PairValidationError(
                "relationship.sequence.continuity",
                f"sequence must be continuous from 1 for run {run_id}",
            )
        identity = hashlib.sha256()
        for sequence in sequences:
            identity.update(
                (
                    f"{run_id}\0{sequence}\0"
                    f"{fl[sequence].get('sha256')}\0{fh[sequence].get('sha256')}\n"
                ).encode("utf-8")
            )
        pair_total += len(sequences)
        run_summaries[run_id] = {
            "pair_count": len(sequences),
            "sequence_first": 1,
            "sequence_last": sequences[-1],
            "pair_identity_sha256": identity.hexdigest(),
        }
    if pair_total * 2 != authoritative_bin_count:
        raise PairValidationError(
            "relationship.bin_count.mismatch",
            "derived pair total does not match the authoritative bin count",
        )
    return run_summaries, pair_total


def _validate_cfg(
    primary_contract: CorpusContract,
    entries: list[dict[str, Any]],
) -> dict[str, str]:
    by_run: dict[str, list[str]] = {
        run_id: [] for run_id in primary_contract.run_ids
    }
    for entry in entries:
        if entry.get("kind") != "cfg":
            continue
        path = entry.get("path")
        matched = (
            CFG_RELATIONSHIP_PATTERN.fullmatch(path)
            if isinstance(path, str)
            else None
        )
        if matched is None:
            raise RelationshipMappingError(
                "relationship.cfg.path",
                f"cfg path does not match the relationship grammar: {path}",
            )
        run_id = matched.group("run_id")
        if run_id not in by_run:
            raise RelationshipMappingError(
                "relationship.cfg.run.unknown",
                f"cfg entry references unknown run: {run_id}",
            )
        by_run[run_id].append(path)
    result: dict[str, str] = {}
    for run_id in sorted(by_run):
        paths = by_run[run_id]
        if len(paths) != 1:
            raise RelationshipMappingError(
                "relationship.cfg.count",
                f"cfg expected exactly one entry for run {run_id}, found {len(paths)}",
            )
        result[run_id] = paths[0]
    return result


def _exact_usb_object(
    value: object,
    fields: frozenset[str],
    *,
    code: str,
    message: str,
) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != set(fields):
        raise SourceReferenceError(code, message)
    return value


def _validate_usb_file_reference(
    value: object,
    fields: frozenset[str],
    *,
    schema: str,
    schema_version: int,
    fields_code: str,
    fields_message: str,
    value_code: str,
    value_message: str,
) -> dict[str, Any]:
    reference = _exact_usb_object(
        value,
        fields,
        code=fields_code,
        message=fields_message,
    )
    if (
        not _normalized_relative_path(reference.get("path"))
        or reference.get("schema") != schema
        or not _is_int(reference.get("schema_version"))
        or reference["schema_version"] != schema_version
        or not isinstance(reference.get("sha256"), str)
        or SHA256_PATTERN.fullmatch(reference["sha256"]) is None
        or not _is_int(reference.get("size_bytes"))
        or reference["size_bytes"] < 0
    ):
        raise SourceReferenceError(value_code, value_message)
    return reference


def _validate_usb_manifest(value: dict[str, Any]) -> dict[str, dict[str, Any]]:
    if not isinstance(value, dict) or set(value) != set(USB_FIELDS):
        raise SourceReferenceError(
            "relationship.usb.fields",
            "USB manifest fields are invalid",
        )
    if value.get("schema") != USB_MANIFEST_SCHEMA:
        raise SourceReferenceError(
            "relationship.usb.schema",
            "USB manifest schema is unsupported",
        )
    if not _is_int(value.get("schema_version")) or value["schema_version"] != 1:
        raise SourceReferenceError(
            "relationship.usb.schema_version",
            "USB manifest schema_version must be 1",
        )
    source_root = value.get("source_root")
    analysis_root = value.get("analysis_root")
    captured_date = value.get("captured_date")
    try:
        parsed_captured_date = (
            datetime.strptime(captured_date, "%Y-%m-%d")
            if isinstance(captured_date, str)
            else None
        )
    except ValueError:
        parsed_captured_date = None
    if (
        not _normalized_relative_path(source_root)
        or not _normalized_relative_path(analysis_root)
        or analysis_root != f"{source_root}/analysis"
        or parsed_captured_date is None
        or parsed_captured_date.strftime("%Y-%m-%d") != captured_date
    ):
        raise SourceReferenceError(
            "relationship.usb.value",
            "USB manifest values are invalid",
        )
    if value.get("provisional") is not True:
        raise SourceReferenceError(
            "relationship.usb.provisional",
            "USB manifest must remain provisional",
        )
    if value.get("required_tshark_fields") != list(USB_REQUIRED_TSHARK_FIELDS):
        raise SourceReferenceError(
            "relationship.usb.required_tshark_fields",
            "USB required TShark fields are invalid",
        )
    if value.get("timestamp_basis") != (
        "pcapng packet timestamps rendered by Capinfos; "
        "timezone is not declared by the report"
    ):
        raise ClockPolicyError(
            "relationship.capture.timestamp_basis",
            "USB timestamp basis must remain Capinfos with undeclared timezone",
        )
    bounded_summary = _exact_usb_object(
        value.get("bounded_summary"),
        USB_BOUNDED_SUMMARY_FIELDS,
        code="relationship.usb.bounded_summary.fields",
        message="USB bounded summary fields are invalid",
    )
    if (
        bounded_summary.get("byte_identity_runs") != 2
        or bounded_summary.get("capture_summary_schema_version") != 2
        or not _normalized_relative_path(bounded_summary.get("path"))
        or not str(bounded_summary["path"]).startswith(f"{analysis_root}/")
        or bounded_summary.get("schema")
        != "analogboard.phase0.usbpcap-extraction-bundle"
        or not _is_int(bounded_summary.get("schema_version"))
        or bounded_summary["schema_version"] != 1
        or not isinstance(bounded_summary.get("sha256"), str)
        or SHA256_PATTERN.fullmatch(bounded_summary["sha256"]) is None
        or not _is_int(bounded_summary.get("size_bytes"))
        or bounded_summary["size_bytes"] < 0
    ):
        raise SourceReferenceError(
            "relationship.usb.bounded_summary.value",
            "USB bounded summary values are invalid",
        )
    interface = _exact_usb_object(
        value.get("interface"),
        USB_INTERFACE_FIELDS,
        code="relationship.usb.interface.fields",
        message="USB interface fields are invalid",
    )
    if (
        not _is_int(interface.get("capture_length"))
        or interface["capture_length"] < 1
        or not _is_int(interface.get("index"))
        or interface["index"] < 0
        or interface.get("file_type") != "pcapng"
        or interface.get("file_encapsulation") != "usb-usbpcap"
        or any(
            not isinstance(interface.get(field), str) or not interface[field]
            for field in ("description", "encapsulation", "name")
        )
    ):
        raise SourceReferenceError(
            "relationship.usb.interface.value",
            "USB interface values are invalid",
        )
    source_manifest = _validate_usb_file_reference(
        value.get("source_manifest"),
        USB_SOURCE_MANIFEST_FIELDS,
        schema="analogboard.phase0.usbpcap-source-manifest",
        schema_version=1,
        fields_code="relationship.usb.source_manifest.fields",
        fields_message="USB source manifest fields are invalid",
        value_code="relationship.usb.source_manifest.value",
        value_message="USB source manifest values are invalid",
    )
    if not str(source_manifest["path"]).startswith(f"{analysis_root}/"):
        raise SourceReferenceError(
            "relationship.usb.source_manifest.value",
            "USB source manifest values are invalid",
        )
    tools = _exact_usb_object(
        value.get("tools"),
        USB_TOOLS_FIELDS,
        code="relationship.usb.tools.fields",
        message="USB tools fields are invalid",
    )
    for tool_name in sorted(USB_TOOLS_FIELDS):
        tool = _exact_usb_object(
            tools.get(tool_name),
            USB_TOOL_FIELDS,
            code="relationship.usb.tool.fields",
            message="USB tool fields are invalid",
        )
        if any(
            not isinstance(tool.get(field), str) or not tool[field]
            for field in USB_TOOL_FIELDS
        ):
            raise SourceReferenceError(
                "relationship.usb.tool.value",
                "USB tool values are invalid",
            )
    captures = value.get("captures")
    if not isinstance(captures, list):
        raise RelationshipMappingError(
            "relationship.capture.usb_set",
            "USB manifest capture set does not match the primary contract",
        )
    by_name: dict[str, dict[str, Any]] = {}
    for capture in captures:
        if not isinstance(capture, dict) or set(capture) != set(USB_CAPTURE_FIELDS):
            raise SourceReferenceError(
                "relationship.usb.capture.fields",
                "USB capture fields are invalid",
            )
        name = capture.get("filename")
        if not isinstance(name, str):
            raise RelationshipMappingError(
                "relationship.capture.name",
                "USB capture filename must be a string",
            )
        analysis = _validate_usb_file_reference(
            capture.get("analysis"),
            USB_CAPTURE_ANALYSIS_FIELDS,
            schema="analogboard.phase0.usbpcap-bounded-summary",
            schema_version=2,
            fields_code="relationship.usb.capture.analysis.fields",
            fields_message="USB capture analysis fields are invalid",
            value_code="relationship.usb.capture.analysis.value",
            value_message="USB capture analysis values are invalid",
        )
        duration = capture.get("duration_seconds")
        if (
            not _normalized_relative_path(name)
            or len(PurePosixPath(name).parts) != 1
            or not isinstance(capture.get("sha256"), str)
            or SHA256_PATTERN.fullmatch(capture["sha256"]) is None
            or not _is_int(capture.get("size_bytes"))
            or capture["size_bytes"] < 0
            or not _is_int(capture.get("packet_count"))
            or capture["packet_count"] < 0
            or not isinstance(duration, str)
            or USB_CAPTURE_DURATION_PATTERN.fullmatch(duration) is None
            or float(duration) < 0
            or not isinstance(capture.get("earliest_packet_time"), str)
            or not isinstance(capture.get("latest_packet_time"), str)
            or not str(analysis["path"]).startswith(f"{analysis_root}/")
        ):
            raise SourceReferenceError(
                "relationship.usb.capture.value",
                "USB capture values are invalid",
            )
        if name in by_name:
            raise RelationshipMappingError(
                "relationship.capture.duplicate",
                f"duplicate USB capture identity: {name}",
            )
        by_name[name] = capture
    constraints = value.get("constraints")
    if not isinstance(constraints, dict):
        raise RelationshipMappingError(
            "relationship.failure_trace",
            "USB failure_trace_present must be boolean false",
        )
    if constraints.get("failure_trace_present") is not False:
        raise RelationshipMappingError(
            "relationship.failure_trace",
            "USB failure_trace_present must be boolean false",
        )
    if constraints.get("raw_payload_tracked") is not False:
        raise RelationshipMappingError(
            "relationship.payload_boundary",
            "USB raw_payload_tracked must be boolean false",
        )
    if set(constraints) != set(USB_CONSTRAINT_FIELDS):
        raise SourceReferenceError(
            "relationship.usb.constraints.fields",
            "USB constraints fields are invalid",
        )
    if not isinstance(constraints.get("d19"), str) or not constraints["d19"]:
        raise SourceReferenceError(
            "relationship.usb.constraints.value",
            "USB constraints values are invalid",
        )
    return by_name


def _validate_captures(
    primary_contract: CorpusContract,
    entries: list[dict[str, Any]],
    usb_manifest: dict[str, Any],
) -> dict[str, dict[str, Any]]:
    manifest_captures: dict[str, dict[str, Any]] = {}
    for entry in entries:
        if entry.get("kind") != "capture":
            continue
        path = entry.get("path")
        if not isinstance(path, str):
            raise RelationshipMappingError(
                "relationship.capture.manifest_set",
                "primary manifest capture set does not match the primary contract",
            )
        manifest_captures[path] = entry
    if set(manifest_captures) != set(primary_contract.capture_files):
        raise RelationshipMappingError(
            "relationship.capture.manifest_set",
            "primary manifest capture set does not match the primary contract",
        )
    usb_captures = _validate_usb_manifest(usb_manifest)
    if set(usb_captures) != set(primary_contract.capture_files):
        raise RelationshipMappingError(
            "relationship.capture.usb_set",
            "USB manifest capture set does not match the primary contract",
        )
    for name in sorted(manifest_captures):
        manifest_entry = manifest_captures[name]
        usb_entry = usb_captures[name]
        if manifest_entry.get("size_bytes") != usb_entry.get("size_bytes"):
            raise RelationshipMappingError(
                "relationship.capture.size",
                f"capture size differs between indexes: {name}",
            )
        if manifest_entry.get("sha256") != usb_entry.get("sha256"):
            raise RelationshipMappingError(
                "relationship.capture.sha256",
                f"capture SHA-256 differs between indexes: {name}",
            )
    return usb_captures


def _validate_telemetry_mapping(
    primary_contract: CorpusContract,
    entries: list[dict[str, Any]],
    relationship: RelationshipContract,
) -> dict[str, tuple[str, int]]:
    manifest_paths = {
        entry.get("path") for entry in entries if entry.get("kind") == "telemetry"
    }
    session_paths = {session.path for session in relationship.telemetry_sessions}
    if manifest_paths != session_paths:
        raise RelationshipMappingError(
            "relationship.telemetry.paths",
            "telemetry session paths must match the primary manifest",
        )
    run_mapping: dict[str, tuple[str, int]] = {}
    seen_runs: list[str] = []
    for session in relationship.telemetry_sessions:
        for ordinal, run_id in enumerate(session.ordered_runs, start=1):
            if run_id not in primary_contract.run_ids:
                raise RelationshipMappingError(
                    "relationship.telemetry.run.unknown",
                    f"telemetry mapping references unknown run: {run_id}",
                )
            seen_runs.append(run_id)
            if run_id not in run_mapping:
                run_mapping[run_id] = (session.path, ordinal)
    if (
        len(seen_runs) != len(primary_contract.run_ids)
        or set(seen_runs) != set(primary_contract.run_ids)
        or len(run_mapping) != len(seen_runs)
    ):
        raise RelationshipMappingError(
            "relationship.telemetry.coverage",
            "each registered run must appear exactly once",
        )
    return run_mapping


def _read_telemetry(
    repo_root: Path,
    locator: str,
    path: str,
    recorded_size: int,
) -> bytes:
    try:
        return _read_regular_no_follow(
            repo_root / PurePosixPath(locator) / path,
            maximum_bytes=recorded_size + 1,
        )
    except (OSError, ValueError) as error:
        raise TelemetryValidationError(
            "telemetry.unreadable",
            "telemetry CSV is not readable",
        ) from error


def _parse_nonnegative(value: str, row_number: int) -> int:
    if not value or not value.isascii() or not value.isdigit():
        raise TelemetryValidationError(
            "telemetry.monotonic.invalid",
            f"telemetry row {row_number} monotonic fields must be non-negative integers",
        )
    return int(value)


def _parse_optional_nonnegative(value: str, row_number: int) -> int | None:
    return None if value == "" else _parse_nonnegative(value, row_number)


def _parse_telemetry(
    payload: bytes,
    path: str,
    expected_rows: int,
) -> ParsedTelemetry:
    if not payload:
        raise TelemetryValidationError(
            "telemetry.csv.empty",
            "telemetry CSV must not be empty",
        )
    try:
        text = payload.decode("utf-8")
    except UnicodeError as error:
        raise TelemetryValidationError(
            "telemetry.utf8.invalid",
            "telemetry CSV must contain valid UTF-8",
        ) from error
    try:
        rows = list(csv.reader(io.StringIO(text, newline=""), strict=True))
    except csv.Error as error:
        raise TelemetryValidationError(
            "telemetry.csv.invalid",
            "telemetry CSV syntax is invalid",
        ) from error
    if not rows:
        raise TelemetryValidationError(
            "telemetry.csv.empty",
            "telemetry CSV must not be empty",
        )
    header = rows[0]
    if len(set(header)) != len(header):
        raise TelemetryValidationError(
            "telemetry.header.duplicate",
            "telemetry CSV header must not contain duplicate columns",
        )
    if tuple(header) != TELEMETRY_HEADER:
        raise TelemetryValidationError(
            "telemetry.header.mismatch",
            "telemetry CSV header must match the relationship contract",
        )
    data_rows = rows[1:]
    if not data_rows:
        raise TelemetryValidationError(
            "telemetry.rows.empty",
            "telemetry CSV must contain at least one data row",
        )
    parsed_rows: list[tuple[int, int, int | None]] = []
    for row_number, row in enumerate(data_rows, start=1):
        if len(row) != len(TELEMETRY_HEADER):
            raise TelemetryValidationError(
                "telemetry.row.width",
                f"telemetry row {row_number} must contain exactly 10 columns",
            )
        cycle_id = _parse_nonnegative(row[0], row_number)
        if cycle_id != row_number:
            raise TelemetryValidationError(
                "telemetry.cycle.sequence",
                "telemetry cycle_id must be continuous from 1",
            )
        if row[1] not in ("external", "manual"):
            raise TelemetryValidationError(
                "telemetry.trigger_source",
                f"telemetry row {row_number} trigger_source is invalid",
            )
        trigger, ddr_end, drain, publish, ready = (
            _parse_nonnegative(row[index], row_number)
            for index in range(2, 7)
        )
        next_trigger = _parse_optional_nonnegative(row[7], row_number)
        rearm = _parse_nonnegative(row[8], row_number)
        external_wait = _parse_optional_nonnegative(row[9], row_number)
        if not (
            trigger <= ddr_end <= drain <= ready
            and trigger <= publish <= ready
            and (next_trigger is None or ready <= next_trigger)
        ):
            raise TelemetryValidationError(
                "telemetry.boundary.order",
                f"telemetry row {row_number} boundary order is invalid",
            )
        if rearm != ready - ddr_end:
            raise TelemetryValidationError(
                "telemetry.duration.rearm",
                f"telemetry row {row_number} rearm duration is inconsistent",
            )
        if (next_trigger is None) != (external_wait is None):
            raise TelemetryValidationError(
                "telemetry.duration.external_wait",
                f"telemetry row {row_number} external wait presence is inconsistent",
            )
        if next_trigger is not None and external_wait != next_trigger - ready:
            raise TelemetryValidationError(
                "telemetry.duration.external_wait",
                f"telemetry row {row_number} external wait duration is inconsistent",
            )
        parsed_rows.append((cycle_id, trigger, next_trigger))
    for index, (_, _, next_trigger) in enumerate(parsed_rows[:-1]):
        if next_trigger != parsed_rows[index + 1][1]:
            raise TelemetryValidationError(
                "telemetry.boundary.next_trigger",
                f"telemetry row {index + 1} next trigger does not match the next row",
            )
    if parsed_rows[-1][2] is not None:
        raise TelemetryValidationError(
            "telemetry.boundary.final_trigger",
            "final telemetry row must not declare a next trigger",
        )
    if len(parsed_rows) != expected_rows:
        raise RelationshipMappingError(
            "relationship.telemetry.row_count",
            f"telemetry row count does not match ordered runs: {path}",
        )
    return ParsedTelemetry(
        path,
        len(parsed_rows),
        parsed_rows[0][0],
        parsed_rows[-1][0],
    )


def _validate_capture_containment(
    run_id: str,
    capture_name: str,
    capture: dict[str, Any],
) -> None:
    capture_start_value = capture.get("earliest_packet_time")
    capture_end_value = capture.get("latest_packet_time")
    if (
        not isinstance(capture_start_value, str)
        or CAPTURE_TIME_PATTERN.fullmatch(capture_start_value) is None
        or not isinstance(capture_end_value, str)
        or CAPTURE_TIME_PATTERN.fullmatch(capture_end_value) is None
    ):
        raise ClockPolicyError(
            "relationship.capture.timestamp.canonical",
            (
                "capture timestamps must use fixed-width "
                f"YYYY-MM-DD HH:MM:SS.ffffff: {capture_name}"
            ),
        )
    try:
        run_start = datetime.strptime(run_id, "%y%m%d_%H%M")
    except ValueError as error:
        raise ClockPolicyError(
            "relationship.run_label.timestamp",
            f"run label timestamp is invalid: {run_id}",
        ) from error
    run_end = run_start + timedelta(seconds=60)
    try:
        capture_start = datetime.strptime(capture_start_value, CAPTURE_TIME_FORMAT)
        capture_end = datetime.strptime(capture_end_value, CAPTURE_TIME_FORMAT)
    except ValueError as error:
        raise ClockPolicyError(
            "relationship.capture.timestamp",
            f"capture timestamps are invalid: {capture_name}",
        ) from error
    if capture_end < capture_start:
        raise ClockPolicyError(
            "relationship.capture.interval",
            f"capture interval is inverted: {capture_name}",
        )
    if capture_start > run_start or capture_end < run_end:
        raise ClockPolicyError(
            "relationship.capture.containment",
            f"capture does not contain run minute bucket: {run_id}",
        )


def build_relationship_evidence(
    repo_root: Path,
    relationship: RelationshipContract,
) -> dict[str, Any]:
    primary_contract, primary_manifest, usb_manifest = _load_sources(
        repo_root, relationship
    )
    try:
        manifest_by_path = validate_manifest_metadata(
            primary_contract, primary_manifest
        )
    except ManifestValidationError as error:
        raise ManifestValidationError(
            error.code,
            "primary manifest metadata validation failed",
        ) from error
    entries = list(manifest_by_path.values())
    run_pairs, pair_total = _validate_pairs(primary_contract, entries)
    cfg_by_run = _validate_cfg(primary_contract, entries)
    usb_captures = _validate_captures(
        primary_contract, entries, usb_manifest
    )
    telemetry_by_run = _validate_telemetry_mapping(
        primary_contract, entries, relationship
    )
    parsed_sessions: dict[str, ParsedTelemetry] = {}
    for session in relationship.telemetry_sessions:
        manifest_entry = manifest_by_path[session.path]
        payload = _read_telemetry(
            repo_root,
            primary_contract.canonical_locator,
            session.path,
            manifest_entry["size_bytes"],
        )
        parsed = _parse_telemetry(payload, session.path, len(session.ordered_runs))
        if len(payload) != manifest_entry["size_bytes"]:
            raise TelemetryValidationError(
                "telemetry.size",
                f"telemetry size differs from the primary manifest: {session.path}",
            )
        if hashlib.sha256(payload).hexdigest() != manifest_entry["sha256"]:
            raise TelemetryValidationError(
                "telemetry.sha256",
                f"telemetry SHA-256 differs from the primary manifest: {session.path}",
            )
        parsed_sessions[session.path] = parsed

    runs: list[dict[str, Any]] = []
    mapping_by_run = {
        mapping.run_id: mapping for mapping in primary_contract.run_capture_mapping
    }
    for run_id in sorted(primary_contract.run_ids):
        mapping = mapping_by_run[run_id]
        _validate_capture_containment(
            run_id, mapping.capture, usb_captures[mapping.capture]
        )
        session_path, row_ordinal = telemetry_by_run[run_id]
        pairs = run_pairs[run_id]
        runs.append(
            {
                "run_id": run_id,
                "density": mapping.density,
                "cfg": cfg_by_run[run_id],
                "capture": mapping.capture,
                **pairs,
                "sequences_continuous": True,
                "capture_contains_run_bucket": True,
                "telemetry_session": session_path,
                "telemetry_row_ordinal": row_ordinal,
            }
        )
    sessions = [
        {
            "path": path,
            "run_count": len(
                next(
                    session.ordered_runs
                    for session in relationship.telemetry_sessions
                    if session.path == path
                )
            ),
            "row_count": parsed.row_count,
            "header_sha256": hashlib.sha256(
                ",".join(relationship.telemetry_header).encode("ascii")
            ).hexdigest(),
            "cycle_id_first": parsed.cycle_id_first,
            "cycle_id_last": parsed.cycle_id_last,
            "cycle_ids_continuous": True,
            "monotonic_boundaries_valid": True,
        }
        for path, parsed in sorted(parsed_sessions.items())
    ]
    evidence = {
        "schema": RELATIONSHIP_EVIDENCE_SCHEMA,
        "schema_version": RELATIONSHIP_EVIDENCE_SCHEMA_VERSION,
        "global": {
            "run_count": len(primary_contract.run_ids),
            "pair_count": pair_total,
            "cfg_count": len(cfg_by_run),
            "capture_count": len(primary_contract.capture_files),
            "telemetry_session_count": len(sessions),
            "telemetry_row_count": sum(item["row_count"] for item in sessions),
            "failure_trace_present": False,
        },
        "runs": runs,
        "telemetry_sessions": sessions,
        "clock_policy": relationship.clock_policy,
    }
    validate_relationship_evidence_data(evidence)
    return evidence


def serialize_relationship_evidence(evidence: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(evidence, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def validate_relationship_evidence_data(value: object) -> None:
    if not isinstance(value, dict):
        raise EvidenceValidationError(
            "relationship.evidence.type",
            "relationship evidence must be an object",
        )
    source_locator = value.get("source_locator")
    if "source_locator" in value and not _normalized_relative_path(source_locator):
        raise EvidenceValidationError(
            "relationship.evidence.path",
            "evidence paths must be normalized repository-relative metadata",
        )
    if set(value) != set(EVIDENCE_FIELDS):
        raise EvidenceValidationError(
            "relationship.evidence.fields",
            "relationship evidence fields are invalid",
        )
    if value.get("schema") != RELATIONSHIP_EVIDENCE_SCHEMA:
        raise EvidenceValidationError(
            "relationship.evidence.schema",
            f"schema must be '{RELATIONSHIP_EVIDENCE_SCHEMA}'",
        )
    if (
        not _is_int(value.get("schema_version"))
        or value["schema_version"] != RELATIONSHIP_EVIDENCE_SCHEMA_VERSION
    ):
        raise EvidenceValidationError(
            "relationship.evidence.schema_version",
            "schema_version must be 1",
        )
    global_value = value.get("global")
    if (
        not isinstance(global_value, dict)
        or set(global_value) != set(EVIDENCE_GLOBAL_FIELDS)
        or any(
            not _is_int(global_value[field]) or global_value[field] < 0
            for field in EVIDENCE_GLOBAL_FIELDS - {"failure_trace_present"}
        )
        or global_value.get("failure_trace_present") is not False
    ):
        raise EvidenceValidationError(
            "relationship.evidence.global",
            "relationship evidence global aggregate is invalid",
        )
    runs = value.get("runs")
    if not isinstance(runs, list):
        raise EvidenceValidationError(
            "relationship.evidence.runs",
            "relationship evidence runs must be an array",
        )
    run_ids: list[str] = []
    for run in runs:
        if not isinstance(run, dict) or set(run) != set(EVIDENCE_RUN_FIELDS):
            raise EvidenceValidationError(
                "relationship.evidence.run.fields",
                "relationship evidence run fields are invalid",
            )
        paths = (run.get("cfg"), run.get("capture"), run.get("telemetry_session"))
        if any(
            not _normalized_relative_path(path)
            or len(PurePosixPath(path).parts) != 1
            for path in paths
        ):
            raise EvidenceValidationError(
                "relationship.evidence.path",
                "evidence paths must be normalized repository-relative metadata",
            )
        run_id = run.get("run_id")
        if not isinstance(run_id, str) or RUN_ID_PATTERN.fullmatch(run_id) is None:
            raise EvidenceValidationError(
                "relationship.evidence.run_id",
                "relationship evidence run_id is invalid",
            )
        run_ids.append(run_id)
        if (
            not isinstance(run.get("density"), str)
            or not run["density"]
            or any(
                not _is_int(run.get(field)) or run[field] < 1
                for field in (
                    "pair_count",
                    "sequence_first",
                    "sequence_last",
                    "telemetry_row_ordinal",
                )
            )
            or run.get("sequences_continuous") is not True
            or run.get("capture_contains_run_bucket") is not True
            or not isinstance(run.get("pair_identity_sha256"), str)
            or SHA256_PATTERN.fullmatch(run["pair_identity_sha256"]) is None
            or run.get("sequence_first") != 1
            or run.get("sequence_last") != run.get("pair_count")
        ):
            raise EvidenceValidationError(
                "relationship.evidence.run",
                "relationship evidence run aggregate is invalid",
            )
    if run_ids != sorted(run_ids) or len(run_ids) != len(set(run_ids)):
        raise EvidenceValidationError(
            "relationship.evidence.run.order",
            "relationship evidence runs must be uniquely sorted",
        )
    sessions = value.get("telemetry_sessions")
    if not isinstance(sessions, list):
        raise EvidenceValidationError(
            "relationship.evidence.sessions",
            "relationship evidence telemetry_sessions must be an array",
        )
    session_paths: list[str] = []
    for session in sessions:
        if (
            not isinstance(session, dict)
            or set(session) != set(EVIDENCE_SESSION_FIELDS)
        ):
            raise EvidenceValidationError(
                "relationship.evidence.session.fields",
                "relationship evidence telemetry session fields are invalid",
            )
        path = session.get("path")
        if (
            not _normalized_relative_path(path)
            or len(PurePosixPath(path).parts) != 1
        ):
            raise EvidenceValidationError(
                "relationship.evidence.path",
                "evidence paths must be normalized repository-relative metadata",
            )
        session_paths.append(path)
        if (
            any(
                not _is_int(session.get(field)) or session[field] < 1
                for field in (
                    "run_count",
                    "row_count",
                    "cycle_id_first",
                    "cycle_id_last",
                )
            )
            or session.get("cycle_ids_continuous") is not True
            or session.get("monotonic_boundaries_valid") is not True
            or not isinstance(session.get("header_sha256"), str)
            or SHA256_PATTERN.fullmatch(session["header_sha256"]) is None
        ):
            raise EvidenceValidationError(
                "relationship.evidence.session",
                "relationship evidence telemetry session aggregate is invalid",
            )
    if session_paths != sorted(session_paths) or len(session_paths) != len(set(session_paths)):
        raise EvidenceValidationError(
            "relationship.evidence.session.order",
            "relationship evidence telemetry sessions must be uniquely sorted",
        )
    try:
        _parse_clock_policy(value.get("clock_policy"))
    except ClockPolicyError as error:
        raise EvidenceValidationError(
            "relationship.evidence.clock_policy",
            "relationship evidence clock_policy is invalid",
        ) from error
    if (
        global_value["run_count"] != len(runs)
        or global_value["telemetry_session_count"] != len(sessions)
        or global_value["pair_count"]
        != sum(run["pair_count"] for run in runs)
        or global_value["cfg_count"] != len(runs)
        or global_value["telemetry_row_count"]
        != sum(session["row_count"] for session in sessions)
        or sum(session["run_count"] for session in sessions) != len(runs)
    ):
        raise EvidenceValidationError(
            "relationship.evidence.aggregate",
            "relationship evidence aggregate counts are inconsistent",
        )


def _write_relationships(
    repo_root: Path,
    relative_output: str,
    payload: bytes,
) -> None:
    if relative_output != DEFAULT_RELATIONSHIPS_PATH:
        raise RelationshipPathError(
            "relationship.output.scope",
            f"output must be {DEFAULT_RELATIONSHIPS_PATH}",
        )
    if not _normalized_relative_path(relative_output):
        raise RelationshipPathError(
            "relationship.output.path",
            "output must be a normalized repository-relative path",
        )
    try:
        root = repo_root.resolve(strict=True)
        parent = root
        for part in PurePosixPath(relative_output).parent.parts:
            parent = parent / part
            metadata = parent.lstat()
            if stat.S_ISLNK(metadata.st_mode) or not stat.S_ISDIR(metadata.st_mode):
                raise OSError
        destination = parent / PurePosixPath(relative_output).name
        try:
            destination_metadata = destination.lstat()
        except FileNotFoundError:
            destination_metadata = None
        if destination_metadata is not None and (
            stat.S_ISLNK(destination_metadata.st_mode)
            or not stat.S_ISREG(destination_metadata.st_mode)
        ):
            raise OSError
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
        temporary = Path(temporary_name)
        try:
            with os.fdopen(descriptor, "wb") as output:
                os.fchmod(output.fileno(), output_mode)
                output.write(payload)
                output.flush()
                os.fsync(output.fileno())
            os.replace(temporary, destination)
        except BaseException:
            try:
                temporary.unlink()
            except OSError:
                pass
            raise
    except OSError as error:
        raise RelationshipPathError(
            "relationship.output.unwritable",
            "relationship evidence output is not writable",
        ) from error


def _load_evidence(path: Path) -> tuple[object, bytes]:
    payload = _read_regular_metadata(
        path,
        error_type=EvidenceValidationError,
        symlink_code="relationship.evidence.symlink",
        symlink_message=(
            "relationship evidence must be a regular file without symlink traversal"
        ),
        unreadable_code="relationship.evidence.unreadable",
        unreadable_message="relationship evidence is not readable",
    )
    try:
        value = _decode_strict_json(
            payload,
            code="relationship.evidence.json",
            message="relationship evidence must contain valid UTF-8 JSON",
        )
    except (RelationshipContractError, SourceReferenceError) as error:
        raise EvidenceValidationError(
            "relationship.evidence.json",
            "relationship evidence must contain valid UTF-8 JSON",
        ) from error
    validate_relationship_evidence_data(value)
    return value, payload


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Build or verify deterministic P0-C4 relationship evidence."
    )
    parser.add_argument("--repo-root", default=".", help="Repository root.")
    parser.add_argument(
        "--relationship-contract",
        default=DEFAULT_RELATIONSHIP_CONTRACT_PATH,
        help="Normalized repository-relative relationship contract.",
    )
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build", help="Build relationships.json.")
    build.add_argument("--output", required=True)
    verify = subparsers.add_parser(
        "verify", help="Regenerate and byte-compare relationships.json."
    )
    verify.add_argument("--evidence", required=True)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    arguments = _build_parser().parse_args(argv)
    try:
        repo_root = Path(arguments.repo_root)
        if not _normalized_relative_path(arguments.relationship_contract):
            raise RelationshipPathError(
                "relationship.contract.path",
                "relationship contract path must be normalized repository-relative metadata",
            )
        if arguments.relationship_contract != DEFAULT_RELATIONSHIP_CONTRACT_PATH:
            raise RelationshipPathError(
                "relationship.contract.scope",
                f"relationship contract must be {DEFAULT_RELATIONSHIP_CONTRACT_PATH}",
            )
        contract = load_relationship_contract(
            repo_root / PurePosixPath(arguments.relationship_contract)
        )
        if arguments.command == "build":
            evidence = build_relationship_evidence(repo_root, contract)
            regenerated = serialize_relationship_evidence(evidence)
            _write_relationships(repo_root, arguments.output, regenerated)
            print(f"relationships built: {arguments.output}")
            return 0
        if arguments.evidence != DEFAULT_RELATIONSHIPS_PATH:
            raise RelationshipPathError(
                "relationship.evidence.scope",
                f"evidence must be {DEFAULT_RELATIONSHIPS_PATH}",
            )
        _, recorded = _load_evidence(
            repo_root / PurePosixPath(arguments.evidence)
        )
        evidence = build_relationship_evidence(repo_root, contract)
        regenerated = serialize_relationship_evidence(evidence)
        if recorded != regenerated:
            raise EvidenceValidationError(
                "relationship.evidence.bytes",
                "relationship evidence differs from deterministic regeneration",
            )
        print(f"relationships verified: {arguments.evidence}")
        return 0
    except CorpusIndexError as error:
        print(f"ERROR {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
