from __future__ import annotations

import copy
import hashlib
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import contextmanager
from contextlib import redirect_stderr
from pathlib import Path
from typing import Any, Callable, Iterator
from unittest.mock import patch

SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from corpus_relationships import (  # noqa: E402
    ClockPolicyError,
    DEFAULT_RELATIONSHIP_CONTRACT_PATH,
    DEFAULT_RELATIONSHIPS_PATH,
    EvidenceValidationError,
    PairValidationError,
    RelationshipContractError,
    RelationshipMappingError,
    RelationshipPathError,
    SourceReferenceError,
    TelemetryValidationError,
    _load_evidence,
    _write_relationships,
    build_relationship_evidence,
    load_relationship_contract,
    load_relationship_contract_data,
    main,
    serialize_relationship_evidence,
    validate_relationship_evidence_data,
)
from corpus_index import ManifestValidationError  # noqa: E402


PRIMARY_CONTRACT_SCHEMA = "analogboard.phase0.initial-recording-corpus-contract"
PRIMARY_MANIFEST_SCHEMA = "analogboard.phase0.initial-recording-corpus-manifest"
USB_MANIFEST_SCHEMA = "analogboard.phase0.usbpcap-corpus-index"
RELATIONSHIP_CONTRACT_SCHEMA = (
    "analogboard.phase0.initial-recording-corpus-relationship-contract"
)
RELATIONSHIP_EVIDENCE_SCHEMA = (
    "analogboard.phase0.initial-recording-corpus-relationships"
)
TELEMETRY_HEADER = [
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
]
USB_REQUIRED_TSHARK_FIELDS = [
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
]


def _canonical_json(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _telemetry_csv(row_count: int) -> bytes:
    rows = [",".join(TELEMETRY_HEADER)]
    for index in range(row_count):
        cycle_id = index + 1
        trigger = cycle_id * 1000
        next_trigger = (cycle_id + 1) * 1000 if cycle_id < row_count else None
        fields = [
            str(cycle_id),
            "external",
            str(trigger),
            str(trigger + 10),
            str(trigger + 20),
            str(trigger + 15),
            str(trigger + 30),
            str(next_trigger) if next_trigger is not None else "",
            "20",
            str(next_trigger - (trigger + 30)) if next_trigger is not None else "",
        ]
        rows.append(",".join(fields))
    return ("\r\n".join(rows) + "\r\n").encode("utf-8")


def _run_id(index: int) -> str:
    return f"260717_{index:04d}"


@contextmanager
def _swap_leaf_before_read(
    candidate: Path,
    target: Path,
) -> Iterator[dict[str, bool]]:
    """Inject the same leaf replacement into path- and descriptor-based readers."""
    original_read_bytes = Path.read_bytes
    original_path_open = Path.open
    original_os_open = os.open
    state = {"swapped": False}

    def swap() -> None:
        candidate.unlink()
        candidate.symlink_to(target)
        state["swapped"] = True

    def raced_read_bytes(path: Path) -> bytes:
        if path == candidate and not state["swapped"]:
            swap()
        return original_read_bytes(path)

    def raced_path_open(path: Path, *args: object, **kwargs: object) -> object:
        if path == candidate and not state["swapped"]:
            swap()
        return original_path_open(path, *args, **kwargs)

    def raced_os_open(
        path: str | bytes | os.PathLike[str] | os.PathLike[bytes],
        flags: int,
        mode: int = 0o777,
        *,
        dir_fd: int | None = None,
    ) -> int:
        if (
            dir_fd is not None
            and os.fsdecode(path) == candidate.name
            and not state["swapped"]
        ):
            swap()
        if dir_fd is None:
            return original_os_open(path, flags, mode)
        return original_os_open(path, flags, mode, dir_fd=dir_fd)

    with (
        patch.object(Path, "read_bytes", raced_read_bytes),
        patch.object(Path, "open", raced_path_open),
        patch.object(os, "open", raced_os_open),
    ):
        yield state


@contextmanager
def _swap_component_before_leaf_open(
    candidate: Path,
    target_directory: Path,
) -> Iterator[dict[str, bool]]:
    """Replace a checked parent while preserving any already-open directory fd."""
    checked_directory = candidate.parent
    moved_directory = checked_directory.with_name(f"{checked_directory.name}-checked")
    original_read_bytes = Path.read_bytes
    original_os_open = os.open
    state = {"swapped": False}

    def swap() -> None:
        checked_directory.rename(moved_directory)
        checked_directory.symlink_to(target_directory, target_is_directory=True)
        state["swapped"] = True

    def raced_read_bytes(path: Path) -> bytes:
        if path == candidate and not state["swapped"]:
            swap()
        return original_read_bytes(path)

    def raced_os_open(
        path: str | bytes | os.PathLike[str] | os.PathLike[bytes],
        flags: int,
        mode: int = 0o777,
        *,
        dir_fd: int | None = None,
    ) -> int:
        if (
            dir_fd is not None
            and os.fsdecode(path) == candidate.name
            and not state["swapped"]
        ):
            swap()
        if dir_fd is None:
            return original_os_open(path, flags, mode)
        return original_os_open(path, flags, mode, dir_fd=dir_fd)

    with (
        patch.object(Path, "read_bytes", raced_read_bytes),
        patch.object(os, "open", raced_os_open),
    ):
        yield state


class SyntheticRelationshipCorpus:
    def __init__(
        self,
        repo_root: Path,
        *,
        run_count: int = 1,
        pairs_per_run: int = 1,
        shared_capture: bool = False,
        session_sizes: tuple[int, ...] | None = None,
    ) -> None:
        self.repo_root = repo_root
        self.metadata_root = repo_root / "metadata"
        self.corpus_root = repo_root / "fixtures/corpus"
        self.metadata_root.mkdir(parents=True)
        self.corpus_root.mkdir(parents=True)
        self.runs = [_run_id(index + 1) for index in range(run_count)]
        self.session_sizes = session_sizes or (run_count,)
        if sum(self.session_sizes) != run_count:
            raise ValueError("session sizes must cover every run")

        captures = (
            ["shared.pcapng"] * run_count
            if shared_capture
            else [f"capture_{index + 1}.pcapng" for index in range(run_count)]
        )
        self.primary_contract: dict[str, Any] = {
            "schema": PRIMARY_CONTRACT_SCHEMA,
            "schema_version": 1,
            "canonical_locator": "fixtures/corpus",
            "asset_kinds": [
                {
                    "kind": "bin",
                    "expected_count": run_count * pairs_per_run * 2,
                    "filename_pattern": (
                        r"(?P<run_id>[0-9]{6}_[0-9]{4})_"
                        r"(?:fl|fh)_[1-9][0-9]*[.]bin"
                    ),
                },
                {
                    "kind": "cfg",
                    "expected_count": run_count,
                    "filename_pattern": (
                        r"(?P<run_id>[0-9]{6}_[0-9]{4})_cfg[.]txt"
                    ),
                },
                {
                    "kind": "telemetry",
                    "expected_count": len(self.session_sizes),
                    "filename_pattern": (
                        r"[0-9]{6}_[0-9]{6}_rearm_telemetry[.]csv"
                    ),
                },
                {
                    "kind": "capture",
                    "expected_count": len(set(captures)),
                    "filename_pattern": r"[a-z0-9_]+[.]pcapng",
                },
            ],
            "expected_total_bytes": 0,
            "excluded_paths": ["analysis"],
            "run_capture_mapping": [
                {
                    "run_id": run_id,
                    "density": "synthetic",
                    "capture": captures[index],
                }
                for index, run_id in enumerate(self.runs)
            ],
            "idle_captures": [],
        }

        entries: list[dict[str, Any]] = []
        for run_id in self.runs:
            for sequence in range(1, pairs_per_run + 1):
                for channel in ("fl", "fh"):
                    entries.append(
                        {
                            "kind": "bin",
                            "path": f"{run_id}_{channel}_{sequence}.bin",
                            "sha256": _sha256(
                                f"{run_id}:{channel}:{sequence}".encode("ascii")
                            ),
                            "size_bytes": 1,
                        }
                    )
            entries.append(
                {
                    "kind": "cfg",
                    "path": f"{run_id}_cfg.txt",
                    "sha256": _sha256(f"{run_id}:cfg".encode("ascii")),
                    "size_bytes": 1,
                }
            )

        self.telemetry_sessions: list[dict[str, Any]] = []
        next_run = 0
        for session_index, row_count in enumerate(self.session_sizes, start=1):
            path = f"260717_{session_index:06d}_rearm_telemetry.csv"
            payload = _telemetry_csv(row_count)
            (self.corpus_root / path).write_bytes(payload)
            ordered_runs = self.runs[next_run : next_run + row_count]
            next_run += row_count
            self.telemetry_sessions.append(
                {"path": path, "ordered_runs": ordered_runs}
            )
            entries.append(
                {
                    "kind": "telemetry",
                    "path": path,
                    "sha256": _sha256(payload),
                    "size_bytes": len(payload),
                }
            )

        unique_captures = sorted(set(captures))
        self.usb_manifest: dict[str, Any] = {
            "analysis_root": "fixtures/corpus/analysis",
            "bounded_summary": {
                "byte_identity_runs": 2,
                "capture_summary_schema_version": 2,
                "path": "fixtures/corpus/analysis/bounded_summary.json",
                "schema": "analogboard.phase0.usbpcap-extraction-bundle",
                "schema_version": 1,
                "sha256": "a" * 64,
                "size_bytes": 1,
            },
            "captured_date": "2026-07-17",
            "schema": USB_MANIFEST_SCHEMA,
            "schema_version": 1,
            "timestamp_basis": (
                "pcapng packet timestamps rendered by Capinfos; "
                "timezone is not declared by the report"
            ),
            "captures": [],
            "constraints": {
                "d19": "synthetic local-only",
                "failure_trace_present": False,
                "raw_payload_tracked": False,
            },
            "interface": {
                "capture_length": 8_000_000,
                "description": "synthetic",
                "encapsulation": "USB packets with USBPcap header",
                "file_encapsulation": "usb-usbpcap",
                "file_type": "pcapng",
                "index": 0,
                "name": "USBPcap1",
            },
            "provisional": True,
            "required_tshark_fields": list(USB_REQUIRED_TSHARK_FIELDS),
            "source_manifest": {
                "path": "fixtures/corpus/analysis/source_manifest.json",
                "schema": "analogboard.phase0.usbpcap-source-manifest",
                "schema_version": 1,
                "sha256": "b" * 64,
                "size_bytes": 1,
            },
            "source_root": "fixtures/corpus",
            "tools": {
                "capinfos": {
                    "path": "capinfos.exe",
                    "version": "Capinfos synthetic",
                },
                "tshark": {
                    "path": "tshark.exe",
                    "version": "TShark synthetic",
                },
            },
        }
        for capture_index, capture in enumerate(unique_captures, start=1):
            capture_identity = _sha256(capture.encode("ascii"))
            entries.append(
                {
                    "kind": "capture",
                    "path": capture,
                    "sha256": capture_identity,
                    "size_bytes": capture_index,
                }
            )
            mapped_minutes = [
                int(run_id[-4:])
                for run_id, mapped_capture in zip(self.runs, captures)
                if mapped_capture == capture
            ]
            earliest_minute = min(mapped_minutes)
            latest_minute = max(mapped_minutes)
            self.usb_manifest["captures"].append(
                {
                    "analysis": {
                        "path": (
                            "fixtures/corpus/analysis/"
                            f"{Path(capture).stem}.summary.json"
                        ),
                        "schema": "analogboard.phase0.usbpcap-bounded-summary",
                        "schema_version": 2,
                        "sha256": "c" * 64,
                        "size_bytes": 1,
                    },
                    "duration_seconds": "60.000000",
                    "filename": capture,
                    "size_bytes": capture_index,
                    "sha256": capture_identity,
                    "earliest_packet_time": (
                        f"2026-07-17 {earliest_minute // 100:02d}:"
                        f"{earliest_minute % 100:02d}:00.000000"
                    ),
                    "latest_packet_time": (
                        f"2026-07-17 {latest_minute // 100:02d}:"
                        f"{latest_minute % 100 + 1:02d}:00.000000"
                    ),
                    "packet_count": 1,
                }
            )

        entries.sort(key=lambda item: item["path"])
        total_bytes = sum(item["size_bytes"] for item in entries)
        self.primary_contract["expected_total_bytes"] = total_bytes
        counts = {
            kind: sum(item["kind"] == kind for item in entries)
            for kind in ("bin", "cfg", "telemetry", "capture")
        }
        self.primary_manifest: dict[str, Any] = {
            "schema": PRIMARY_MANIFEST_SCHEMA,
            "schema_version": 1,
            "source_locator": "fixtures/corpus",
            "excluded_paths": ["analysis"],
            "expected_counts": counts,
            "expected_total_bytes": total_bytes,
            "actual_total_bytes": total_bytes,
            "entries": entries,
        }
        self.plan_payload = b"<html>synthetic plan</html>\n"
        self.relationship_data: dict[str, Any] = {
            "schema": RELATIONSHIP_CONTRACT_SCHEMA,
            "schema_version": 1,
            "source_references": {},
            "telemetry": {
                "header": TELEMETRY_HEADER,
                "sessions": copy.deepcopy(self.telemetry_sessions),
            },
            "clock_policy": {
                "run_label": {
                    "basis": "GetLocalTime YYMMDD_HHMM filename label",
                    "bucket": "half-open",
                    "quantization_seconds": 60,
                },
                "capture": {
                    "basis": "Capinfos packet timestamps",
                    "timezone": "undeclared",
                },
                "telemetry_filename": {
                    "basis": (
                        "GetLocalTime YYMMDD_HHMMSS filename label "
                        "at EP6 thread start"
                    ),
                    "quantization_seconds": 1,
                },
                "telemetry_rows": {
                    "basis": "GetTickCount64 session-local monotonic milliseconds"
                },
                "containment": "capture_fully_contains_run_minute_bucket",
                "additional_containment_tolerance_seconds": 0,
                "calibrated_cross_clock_skew_seconds": None,
                "filesystem_mtime_used": False,
            },
            "failure_trace": {
                "source": "usb_manifest.constraints.failure_trace_present",
                "required_value": False,
            },
        }
        self.write_sources()

    def write_sources(self) -> None:
        source_specs = {
            "primary_contract": (
                "metadata/contract.json",
                _canonical_json(self.primary_contract),
                {
                    "schema": PRIMARY_CONTRACT_SCHEMA,
                    "schema_version": 1,
                },
            ),
            "primary_manifest": (
                "metadata/manifest.json",
                _canonical_json(self.primary_manifest),
                {
                    "schema": PRIMARY_MANIFEST_SCHEMA,
                    "schema_version": 1,
                },
            ),
            "usb_manifest": (
                "metadata/usb.json",
                _canonical_json(self.usb_manifest),
                {
                    "schema": USB_MANIFEST_SCHEMA,
                    "schema_version": 1,
                },
            ),
            "plan": (
                "metadata/plan.html",
                self.plan_payload,
                {"revision": "Synthetic 1"},
            ),
        }
        references: dict[str, Any] = {}
        for role, (relative_path, payload, extra) in source_specs.items():
            destination = self.repo_root / relative_path
            destination.write_bytes(payload)
            references[role] = {
                "path": relative_path,
                "sha256": _sha256(payload),
                **extra,
            }
        self.relationship_data["source_references"] = references

    def relationship_contract(self) -> object:
        return load_relationship_contract_data(copy.deepcopy(self.relationship_data))

    def build(self) -> dict[str, Any]:
        return build_relationship_evidence(
            self.repo_root,
            self.relationship_contract(),
        )


class CorpusRelationshipTests(unittest.TestCase):
    def assert_failure(
        self,
        expected_type: type[Exception],
        expected_code: str,
        expected_message: str,
        callback: Callable[[], object],
    ) -> None:
        with self.assertRaises(expected_type) as raised:
            callback()
        self.assertIs(expected_type, type(raised.exception))
        self.assertEqual(expected_code, getattr(raised.exception, "code", None))
        self.assertEqual(f"{expected_code}: {expected_message}", str(raised.exception))

    def test_r3_n_01_minimum_complete_run_builds_bounded_evidence(self) -> None:
        # Given: One registered run with sequence 1 FL/FH, cfg, capture, and telemetry.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(Path(temporary_directory))

            # When: Relationship evidence is built entirely from metadata and CSV shape.
            evidence = fixture.build()

            # Then: Counts derive from the loaded sources and no sequence array is emitted.
            self.assertEqual(1, evidence["global"]["run_count"])
            self.assertEqual(1, evidence["global"]["pair_count"])
            self.assertEqual(1, evidence["runs"][0]["pair_count"])
            self.assertNotIn("sequences", evidence["runs"][0])
            self.assertNotIn("rows", evidence["telemetry_sessions"][0])

    def test_cli_requires_the_exact_tracked_relationship_contract(self) -> None:
        # Given: A valid but alternate relationship contract inside a synthetic repository.
        for command in ("build", "verify"):
            with self.subTest(command=command), tempfile.TemporaryDirectory() as temporary_directory:
                repo_root = Path(temporary_directory)
                fixture = SyntheticRelationshipCorpus(repo_root)
                alternate = repo_root / "metadata/alternate-relationship.json"
                alternate.write_bytes(_canonical_json(fixture.relationship_data))
                evidence_path = repo_root / DEFAULT_RELATIONSHIPS_PATH
                evidence_path.parent.mkdir(parents=True)
                evidence_path.write_bytes(
                    serialize_relationship_evidence(fixture.build())
                )
                command_arguments = (
                    ["build", "--output", DEFAULT_RELATIONSHIPS_PATH]
                    if command == "build"
                    else ["verify", "--evidence", DEFAULT_RELATIONSHIPS_PATH]
                )

                # When: Either production command selects the alternate contract.
                error_output = io.StringIO()
                with redirect_stderr(error_output):
                    exit_code = main(
                        [
                            "--repo-root",
                            str(repo_root),
                            "--relationship-contract",
                            "metadata/alternate-relationship.json",
                            *command_arguments,
                        ]
                    )

                # Then: The tracked relationship authority cannot be substituted.
                self.assertEqual(2, exit_code)
                self.assertIn("relationship.contract.scope", error_output.getvalue())

    def test_contract_and_evidence_symlinks_are_rejected_before_target_read(self) -> None:
        # Given: Leaf and path-component symlinks whose targets are deliberately invalid.
        cases = (
            (
                "contract",
                load_relationship_contract,
                RelationshipContractError,
                "relationship.contract.symlink",
                "relationship contract must be a regular file without symlink traversal",
            ),
            (
                "evidence",
                _load_evidence,
                EvidenceValidationError,
                "relationship.evidence.symlink",
                "relationship evidence must be a regular file without symlink traversal",
            ),
        )
        for name, loader, error_type, code, message in cases:
            for layout in ("leaf", "component"):
                with (
                    self.subTest(name=name, layout=layout),
                    tempfile.TemporaryDirectory() as temporary_directory,
                ):
                    root = Path(temporary_directory)
                    outside = root / "outside"
                    outside.mkdir()
                    target = outside / f"{name}.json"
                    target.write_bytes(b"TARGET_PAYLOAD_MUST_NOT_BE_READ")
                    if layout == "leaf":
                        candidate = root / f"{name}.json"
                        candidate.symlink_to(target)
                    else:
                        linked_directory = root / "linked"
                        linked_directory.symlink_to(outside, target_is_directory=True)
                        candidate = linked_directory / f"{name}.json"

                    # When/Then: Traversal fails as a symlink error, before JSON decoding.
                    self.assert_failure(
                        error_type,
                        code,
                        message,
                        lambda candidate=candidate, loader=loader: loader(candidate),
                    )

    def test_contract_and_evidence_leaf_swap_cannot_reach_target_bytes(self) -> None:
        # Given: A regular metadata leaf replaced by an invalid symlink target after its check.
        cases = (
            (
                "contract",
                load_relationship_contract,
                RelationshipContractError,
                "relationship.contract.symlink",
                "relationship contract must be a regular file without symlink traversal",
            ),
            (
                "evidence",
                _load_evidence,
                EvidenceValidationError,
                "relationship.evidence.symlink",
                "relationship evidence must be a regular file without symlink traversal",
            ),
        )
        for name, loader, error_type, code, message in cases:
            with (
                self.subTest(name=name),
                tempfile.TemporaryDirectory() as temporary_directory,
            ):
                root = Path(temporary_directory)
                candidate = root / f"{name}.json"
                target = root / f"{name}-target.json"
                candidate.write_bytes(b"{}")
                target.write_bytes(b"TARGET_BYTES_MUST_NOT_BE_READ")

                # When: The replacement occurs at the exact read/open boundary.
                # Then: The no-follow boundary reports the symlink without reading target bytes.
                with _swap_leaf_before_read(candidate, target) as state:
                    self.assert_failure(
                        error_type,
                        code,
                        message,
                        lambda loader=loader, candidate=candidate: loader(candidate),
                    )
                self.assertTrue(state["swapped"])

    def test_pinned_source_leaf_swap_cannot_reach_target_bytes(self) -> None:
        # Given: A pinned regular source replaced by a symlink after its component check.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
            candidate = fixture.metadata_root / "contract.json"
            target = fixture.repo_root / "alternate-contract.json"
            target.write_bytes(b"TARGET_BYTES_MUST_NOT_BE_READ")

            # When: The replacement occurs immediately before the pinned source opens.
            # Then: A source symlink error wins before target bytes or their SHA are consumed.
            with _swap_leaf_before_read(candidate, target) as state:
                self.assert_failure(
                    SourceReferenceError,
                    "relationship.source.symlink",
                    (
                        "source reference must not traverse a symlink: "
                        "primary_contract"
                    ),
                    fixture.build,
                )
            self.assertTrue(state["swapped"])

    def test_telemetry_leaf_swap_cannot_reach_target_bytes(self) -> None:
        # Given: A telemetry CSV replaced by a symlink after its component check.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
            candidate = fixture.corpus_root / fixture.telemetry_sessions[0]["path"]
            target = fixture.repo_root / "alternate-telemetry.csv"
            target.write_bytes(b"TARGET_BYTES_MUST_NOT_BE_READ")

            # When: The replacement occurs immediately before the bounded CSV open.
            # Then: The stable telemetry unreadable error is raised without reading target bytes.
            with _swap_leaf_before_read(candidate, target) as state:
                self.assert_failure(
                    TelemetryValidationError,
                    "telemetry.unreadable",
                    "telemetry CSV is not readable",
                    fixture.build,
                )
            self.assertTrue(state["swapped"])

    def test_component_swap_keeps_reading_the_already_open_directory(self) -> None:
        # Given: A valid contract whose checked parent is replaced by an invalid symlink target.
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            fixture = SyntheticRelationshipCorpus(root)
            candidate = fixture.metadata_root / "relationship.json"
            candidate.write_bytes(_canonical_json(fixture.relationship_data))
            target_directory = root / "alternate-metadata"
            target_directory.mkdir()
            (target_directory / candidate.name).write_bytes(
                b"TARGET_BYTES_MUST_NOT_BE_READ"
            )

            # When: The parent path changes after its directory fd opens but before leaf open.
            with _swap_component_before_leaf_open(
                candidate, target_directory
            ) as state:
                loaded = load_relationship_contract(candidate)

            # Then: The open directory identity, not the replacement path, supplies the bytes.
            self.assertTrue(state["swapped"])
            self.assertEqual(tuple(TELEMETRY_HEADER), loaded.telemetry_header)

    def test_manifest_schema_is_validated_before_relationship_text_is_used(self) -> None:
        # Given: A pinned manifest whose bin path contains unvalidated arbitrary text.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
            sentinel = "HOST_SECRET_PAYLOAD_SENTINEL"
            entry = next(
                item for item in fixture.primary_manifest["entries"]
                if item["kind"] == "bin"
            )
            entry["path"] = sentinel
            fixture.primary_manifest["entries"].sort(key=lambda item: item["path"])
            fixture.write_sources()

            # When: Relationship evidence loading reaches the primary manifest.
            with self.assertRaises(ManifestValidationError) as raised:
                fixture.build()

            # Then: The strict manifest boundary rejects it without echoing source text.
            self.assertEqual("manifest.kind.mismatch", raised.exception.code)
            self.assertNotIn(sentinel, str(raised.exception))

    def test_r3_n_02_shared_capture_maps_two_runs(self) -> None:
        # Given: Two runs whose primary contract deliberately shares one capture.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(
                Path(temporary_directory),
                run_count=2,
                shared_capture=True,
            )

            # When: Relationship evidence is built.
            evidence = fixture.build()

            # Then: Both ordered runs retain the same declared capture without duplication failure.
            self.assertEqual(
                ["shared.pcapng", "shared.pcapng"],
                [item["capture"] for item in evidence["runs"]],
            )

    def test_r3_n_03_contiguous_sequences_are_numeric_and_deterministic(self) -> None:
        # Given: One run with sequences 1..3 whose manifest order is lexical, not numeric.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(
                Path(temporary_directory),
                pairs_per_run=3,
            )

            # When: Evidence is generated and serialized twice.
            first = serialize_relationship_evidence(fixture.build())
            second = serialize_relationship_evidence(fixture.build())

            # Then: Numeric continuity is summarized and serialization is canonical.
            self.assertEqual(first, second)
            self.assertTrue(first.endswith(b"\n"))
            run = json.loads(first)["runs"][0]
            self.assertEqual((1, 3, 3), (
                run["sequence_first"],
                run["sequence_last"],
                run["pair_count"],
            ))

    def test_r3_a_01_missing_or_extra_pair_side_fails_closed(self) -> None:
        # Given: A count-valid pair set with one side replaced by an unmatched side.
        variants = (
            ("missing-fh", "fh", "fl"),
            ("missing-fl", "fl", "fh"),
        )
        for variant, removed_channel, extra_channel in variants:
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                replaced = next(
                    item for item in fixture.primary_manifest["entries"]
                    if item["path"] == f"260717_0001_{removed_channel}_1.bin"
                )
                replaced["path"] = f"260717_0001_{extra_channel}_2.bin"
                fixture.primary_manifest["entries"].sort(key=lambda item: item["path"])
                fixture.write_sources()

                # When: Relationship validation pairs the authoritative bin entries.
                # Then: Strict metadata remains valid and a pair error rejects asymmetry.
                self.assert_failure(
                    PairValidationError,
                    "relationship.pair.mismatch",
                    "FL/FH sequences differ for run 260717_0001",
                    fixture.build,
                )

    def test_r3_a_02_invalid_gap_duplicate_or_nonpositive_sequence_is_rejected(self) -> None:
        # Given: Gap, duplicate, leading-zero, zero, and negative sequence attacks.
        variants = (
            ("gap", "260717_0001_fl_2.bin", "260717_0001_fl_3.bin",
             "sequence must be continuous from 1 for run 260717_0001"),
            ("duplicate", "260717_0001_fl_1.bin", "260717_0001_fl_1.bin",
             "duplicate logical sequence for run 260717_0001"),
            ("leading-zero", "260717_0001_fl_1.bin", "260717_0001_fl_01.bin",
             "sequence must not contain a leading zero: 260717_0001_fl_01.bin"),
            ("zero", "260717_0001_fl_1.bin", "260717_0001_fl_0.bin",
             "sequence must be a positive integer: 260717_0001_fl_0.bin"),
            ("negative", "260717_0001_fl_1.bin", "260717_0001_fl_-1.bin",
             "sequence must be a positive integer: 260717_0001_fl_-1.bin"),
        )
        for name, old_path, new_path, message in variants:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(
                    Path(temporary_directory),
                    pairs_per_run=2 if name == "gap" else 1,
                )
                entry = next(
                    item for item in fixture.primary_manifest["entries"]
                    if item["path"] == old_path
                )
                if name == "duplicate":
                    fixture.primary_manifest["entries"].append(copy.deepcopy(entry))
                else:
                    entry["path"] = new_path
                if name in ("leading-zero", "zero", "negative"):
                    fixture.primary_contract["asset_kinds"][0][
                        "filename_pattern"
                    ] = (
                        r"(?P<run_id>[0-9]{6}_[0-9]{4})_"
                        r"(?:fl|fh)_-?[0-9]+[.]bin"
                    )
                if name == "gap":
                    matching_fh = next(
                        item for item in fixture.primary_manifest["entries"]
                        if item["path"] == "260717_0001_fh_2.bin"
                    )
                    matching_fh["path"] = "260717_0001_fh_3.bin"
                fixture.primary_manifest["entries"].sort(key=lambda item: item["path"])
                fixture.write_sources()

                # When: Numeric relationship parsing is requested.
                # Then: Duplicate metadata fails first; other count-valid attacks reach pairing.
                self.assert_failure(
                    ManifestValidationError
                    if name == "duplicate"
                    else PairValidationError,
                    (
                        "manifest.path.duplicate"
                        if name == "duplicate"
                        else (
                            "relationship.sequence.continuity"
                            if name == "gap"
                            else "relationship.sequence.invalid"
                        )
                    ),
                    (
                        "primary manifest metadata validation failed"
                        if name == "duplicate"
                        else message
                    ),
                    fixture.build,
                )

    def test_r3_a_03_unknown_run_and_odd_authoritative_bin_count_are_rejected(self) -> None:
        # Given: A grammar-valid unknown run or an odd authoritative bin count.
        variants = ("unknown", "odd")
        for variant in variants:
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                if variant == "unknown":
                    entry = next(
                        item for item in fixture.primary_manifest["entries"]
                        if item["kind"] == "bin"
                    )
                    entry["path"] = entry["path"].replace("260717_0001", "260717_2359")
                    expected = (
                        "relationship.run.unknown",
                        "bin entry references unknown run: 260717_2359",
                    )
                else:
                    fixture.primary_manifest["entries"] = [
                        item for item in fixture.primary_manifest["entries"]
                        if item["path"] != "260717_0001_fh_1.bin"
                    ]
                    fixture.primary_contract["asset_kinds"][0]["expected_count"] = 1
                    fixture.primary_manifest["expected_counts"]["bin"] = 1
                    fixture.primary_contract["expected_total_bytes"] -= 1
                    fixture.primary_manifest["expected_total_bytes"] -= 1
                    fixture.primary_manifest["actual_total_bytes"] -= 1
                    expected = (
                        "relationship.bin_count.odd",
                        "authoritative bin count must be even, found 1",
                    )
                fixture.primary_manifest["entries"].sort(key=lambda item: item["path"])
                fixture.write_sources()

                # When: The manifest run set and pair total are derived.
                # Then: Invalid source metadata fails first; valid odd authority reaches pairing.
                self.assert_failure(
                    ManifestValidationError
                    if variant == "unknown"
                    else PairValidationError,
                    (
                        "manifest.kind.mismatch"
                        if variant == "unknown"
                        else expected[0]
                    ),
                    (
                        "primary manifest metadata validation failed"
                        if variant == "unknown"
                        else expected[1]
                    ),
                    fixture.build,
                )

    def test_r3_a_04_cfg_must_be_exactly_one_per_registered_run(self) -> None:
        # Given: Missing, extra, duplicate, and wrong-run cfg mappings.
        variants = ("missing", "extra", "duplicate", "wrong-run")
        for variant in variants:
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                cfg = next(
                    item for item in fixture.primary_manifest["entries"]
                    if item["kind"] == "cfg"
                )
                if variant == "missing":
                    fixture.primary_manifest["entries"].remove(cfg)
                elif variant == "wrong-run":
                    cfg["path"] = "260717_2359_cfg.txt"
                else:
                    duplicate = copy.deepcopy(cfg)
                    duplicate["path"] = (
                        "260717_0001_cfg_copy.txt"
                        if variant == "extra"
                        else cfg["path"]
                    )
                    fixture.primary_manifest["entries"].append(duplicate)
                fixture.primary_manifest["entries"].sort(key=lambda item: item["path"])
                fixture.write_sources()

                # When: Invalid cfg cardinality or identity reaches strict metadata validation.
                # Then: The manifest boundary fails before relationship text is consumed.
                self.assert_failure(
                    ManifestValidationError,
                    (
                        "manifest.count.mismatch"
                        if variant == "missing"
                        else (
                            "manifest.path.duplicate"
                            if variant == "duplicate"
                            else "manifest.kind.mismatch"
                        )
                    ),
                    "primary manifest metadata validation failed",
                    fixture.build,
                )

    def test_r3_a_05_capture_sources_must_match_both_indexes(self) -> None:
        # Given: Capture missing/index mismatch/orphan/duplicate identity variants.
        variants = ("manifest-missing", "usb-missing", "size", "sha", "orphan", "duplicate")
        for variant in variants:
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                capture = next(
                    item for item in fixture.primary_manifest["entries"]
                    if item["kind"] == "capture"
                )
                usb_capture = fixture.usb_manifest["captures"][0]
                if variant == "manifest-missing":
                    fixture.primary_manifest["entries"].remove(capture)
                    code, message = (
                        "relationship.capture.manifest_set",
                        "primary manifest capture set does not match the primary contract",
                    )
                elif variant == "usb-missing":
                    fixture.usb_manifest["captures"].clear()
                    code, message = (
                        "relationship.capture.usb_set",
                        "USB manifest capture set does not match the primary contract",
                    )
                elif variant == "size":
                    usb_capture["size_bytes"] += 1
                    code, message = (
                        "relationship.capture.size",
                        "capture size differs between indexes: capture_1.pcapng",
                    )
                elif variant == "sha":
                    usb_capture["sha256"] = "b" * 64
                    code, message = (
                        "relationship.capture.sha256",
                        "capture SHA-256 differs between indexes: capture_1.pcapng",
                    )
                elif variant == "orphan":
                    fixture.usb_manifest["captures"].append(
                        {
                            **copy.deepcopy(usb_capture),
                            "filename": "orphan.pcapng",
                        }
                    )
                    code, message = (
                        "relationship.capture.usb_set",
                        "USB manifest capture set does not match the primary contract",
                    )
                else:
                    fixture.usb_manifest["captures"].append(copy.deepcopy(usb_capture))
                    code, message = (
                        "relationship.capture.duplicate",
                        "duplicate USB capture identity: capture_1.pcapng",
                    )
                fixture.primary_manifest["entries"].sort(key=lambda item: item["path"])
                fixture.write_sources()

                # When: Capture identity correspondence is validated.
                # Then: Both metadata authorities must agree exactly.
                self.assert_failure(
                    ManifestValidationError
                    if variant == "manifest-missing"
                    else RelationshipMappingError,
                    (
                        "manifest.count.mismatch"
                        if variant == "manifest-missing"
                        else code
                    ),
                    (
                        "primary manifest metadata validation failed"
                        if variant == "manifest-missing"
                        else message
                    ),
                    fixture.build,
                )

    def test_r3_n_04_two_sessions_cover_six_runs_without_row_values(self) -> None:
        # Given: Two telemetry sessions explicitly order two and four registered runs.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(
                Path(temporary_directory),
                run_count=6,
                session_sizes=(2, 4),
            )

            # When: Evidence is generated.
            evidence = fixture.build()

            # Then: The session/run bijection and aggregate row counts are retained only as metadata.
            self.assertEqual(6, evidence["global"]["telemetry_row_count"])
            self.assertEqual(
                [2, 4],
                [item["row_count"] for item in evidence["telemetry_sessions"]],
            )
            self.assertNotIn("trigger_detected_monotonic_ms", json.dumps(evidence))

    def test_r3_a_06_telemetry_mapping_must_be_nonempty_bijective_and_exact(self) -> None:
        # Given: NULL/empty/missing/extra/duplicate/orphan telemetry mappings.
        variants = ("null", "empty", "missing-session", "extra-session", "run-zero", "run-twice", "orphan")
        for variant in variants:
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(
                    Path(temporary_directory),
                    run_count=2,
                    session_sizes=(1, 1) if variant == "missing-session" else None,
                )
                sessions = fixture.relationship_data["telemetry"]["sessions"]
                if variant == "null":
                    fixture.relationship_data["telemetry"] = None
                    code, message = "relationship.telemetry.type", "telemetry must be an object"
                elif variant == "empty":
                    sessions.clear()
                    code, message = "relationship.telemetry.sessions", "telemetry.sessions must be a non-empty array"
                elif variant == "missing-session":
                    sessions.pop()
                    code, message = "relationship.telemetry.paths", "telemetry session paths must match the primary manifest"
                elif variant == "extra-session":
                    sessions.append(
                        {
                            "path": "260717_999999_rearm_telemetry.csv",
                            "ordered_runs": [fixture.runs[0]],
                        }
                    )
                    code, message = "relationship.telemetry.paths", "telemetry session paths must match the primary manifest"
                elif variant == "run-zero":
                    sessions[0]["ordered_runs"] = [fixture.runs[0]]
                    code, message = "relationship.telemetry.coverage", "each registered run must appear exactly once"
                elif variant == "run-twice":
                    sessions[0]["ordered_runs"][1] = fixture.runs[0]
                    code, message = "relationship.telemetry.coverage", "each registered run must appear exactly once"
                else:
                    sessions[0]["ordered_runs"][1] = "260717_2359"
                    code, message = "relationship.telemetry.run.unknown", "telemetry mapping references unknown run: 260717_2359"

                # When: The typed relationship contract or source mapping is validated.
                # Then: Filename-nearest inference is never used as a fallback.
                self.assert_failure(
                    RelationshipContractError
                    if variant in ("null", "empty")
                    else RelationshipMappingError,
                    code,
                    message,
                    fixture.build,
                )

    def test_r3_a_07_invalid_telemetry_csv_shape_or_dependency_is_typed(self) -> None:
        # Given: Empty/header-only/header/ragged/UTF-8/unreadable CSV variants.
        variants = ("empty", "header-only", "missing-header", "duplicate-header", "ragged", "utf8", "unreadable")
        for variant in variants:
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                path = fixture.corpus_root / fixture.telemetry_sessions[0]["path"]
                if variant == "empty":
                    path.write_bytes(b"")
                    code, message = "telemetry.csv.empty", "telemetry CSV must not be empty"
                elif variant == "header-only":
                    path.write_bytes((",".join(TELEMETRY_HEADER) + "\n").encode())
                    code, message = "telemetry.rows.empty", "telemetry CSV must contain at least one data row"
                elif variant == "missing-header":
                    path.write_bytes(_telemetry_csv(1).replace(b"cycle_id,", b"", 1))
                    code, message = "telemetry.header.mismatch", "telemetry CSV header must match the relationship contract"
                elif variant == "duplicate-header":
                    header = TELEMETRY_HEADER.copy()
                    header[-1] = header[0]
                    path.write_bytes((",".join(header) + "\n" + "1," * 9 + "1\n").encode())
                    code, message = "telemetry.header.duplicate", "telemetry CSV header must not contain duplicate columns"
                elif variant == "ragged":
                    path.write_bytes(_telemetry_csv(1).rsplit(b",", 1)[0] + b"\n")
                    code, message = "telemetry.row.width", "telemetry row 1 must contain exactly 10 columns"
                elif variant == "utf8":
                    path.write_bytes(b"\xff")
                    code, message = "telemetry.utf8.invalid", "telemetry CSV must contain valid UTF-8"
                else:
                    path.unlink()
                    code, message = "telemetry.unreadable", "telemetry CSV is not readable"

                # When: The bounded telemetry reader parses structural metadata.
                # Then: It fails without exposing payload or a host path.
                self.assert_failure(
                    TelemetryValidationError,
                    code,
                    message,
                    fixture.build,
                )

    def test_r3_a_08_invalid_monotonic_rows_and_boundary_order_are_rejected(self) -> None:
        # Given: Invalid cycle/timestamp/order telemetry row variants.
        variants = {
            "nonnumeric": (2, "NaN", "telemetry row 1 monotonic fields must be non-negative integers"),
            "nonfinite": (2, "inf", "telemetry row 1 monotonic fields must be non-negative integers"),
            "negative": (2, "-1", "telemetry row 1 monotonic fields must be non-negative integers"),
            "cycle-zero": (0, "0", "telemetry cycle_id must be continuous from 1"),
            "cycle-gap": (0, "2", "telemetry cycle_id must be continuous from 1"),
            "order": (4, "1001", "telemetry row 1 boundary order is invalid"),
        }
        for variant, (column, replacement, message) in variants.items():
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                path = fixture.corpus_root / fixture.telemetry_sessions[0]["path"]
                lines = _telemetry_csv(1).decode().splitlines()
                fields = lines[1].split(",")
                fields[column] = replacement
                path.write_text(lines[0] + "\n" + ",".join(fields) + "\n", encoding="utf-8")

                # When: Session-local monotonic structure is validated.
                # Then: Raw row values are not reflected in the typed failure.
                self.assert_failure(
                    TelemetryValidationError,
                    (
                        "telemetry.cycle.sequence"
                        if variant.startswith("cycle")
                        else (
                            "telemetry.boundary.order"
                            if variant == "order"
                            else "telemetry.monotonic.invalid"
                        )
                    ),
                    message,
                    fixture.build,
                )

    def test_telemetry_identity_must_match_the_primary_manifest(self) -> None:
        # Given: Structurally valid telemetry with a mismatched recorded size or SHA-256.
        for variant in ("size", "sha"):
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                entry = next(
                    item for item in fixture.primary_manifest["entries"]
                    if item["kind"] == "telemetry"
                )
                if variant == "size":
                    entry["size_bytes"] += 1
                    fixture.primary_contract["expected_total_bytes"] += 1
                    fixture.primary_manifest["expected_total_bytes"] += 1
                    fixture.primary_manifest["actual_total_bytes"] += 1
                    code, message = (
                        "telemetry.size",
                        "telemetry size differs from the primary manifest: "
                        "260717_000001_rearm_telemetry.csv",
                    )
                else:
                    entry["sha256"] = "b" * 64
                    code, message = (
                        "telemetry.sha256",
                        "telemetry SHA-256 differs from the primary manifest: "
                        "260717_000001_rearm_telemetry.csv",
                    )
                fixture.write_sources()

                # When: Bounded CSV parsing finishes and identity is compared.
                # Then: Pinned metadata drift is rejected without exposing row values.
                self.assert_failure(
                    TelemetryValidationError,
                    code,
                    message,
                    fixture.build,
                )

    def test_r3_n_05_exact_capture_containment_boundaries_are_accepted(self) -> None:
        # Given: A capture whose endpoints exactly equal the run minute bucket endpoints.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(Path(temporary_directory))

            # When: The half-open 60-second bucket is checked with zero extra tolerance.
            evidence = fixture.build()

            # Then: Exact lower/upper containment boundaries are accepted.
            self.assertTrue(evidence["runs"][0]["capture_contains_run_bucket"])

    def test_capture_timestamps_require_exact_fixed_width_text(self) -> None:
        # Given: Parseable but noncanonical Capinfos timestamp spellings.
        variants = (
            "2026-7-17 00:01:00.000000",
            "2026-07-17 0:01:00.000000",
            "2026-07-17 00:1:00.000000",
            "2026-07-17 00:01:00.0",
        )
        for timestamp in variants:
            with (
                self.subTest(timestamp=timestamp),
                tempfile.TemporaryDirectory() as temporary_directory,
            ):
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                fixture.usb_manifest["captures"][0][
                    "earliest_packet_time"
                ] = timestamp
                fixture.write_sources()

                # When: Capture containment parses the recorded timestamp.
                # Then: Only fixed-width YYYY-MM-DD HH:MM:SS.ffffff is accepted.
                self.assert_failure(
                    ClockPolicyError,
                    "relationship.capture.timestamp.canonical",
                    (
                        "capture timestamps must use fixed-width "
                        "YYYY-MM-DD HH:MM:SS.ffffff: capture_1.pcapng"
                    ),
                    fixture.build,
                )

    def test_pr9_cl_a05_impossible_calendar_run_id_is_run_label_error(self) -> None:
        # Given: A regex-valid run label with an impossible calendar date.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
            original_run_id = fixture.runs[0]
            invalid_run_id = "260231_0001"
            fixture.primary_contract["run_capture_mapping"][0][
                "run_id"
            ] = invalid_run_id
            for entry in fixture.primary_manifest["entries"]:
                if entry["path"].startswith(original_run_id):
                    entry["path"] = entry["path"].replace(
                        original_run_id,
                        invalid_run_id,
                        1,
                    )
            fixture.primary_manifest["entries"].sort(
                key=lambda entry: entry["path"]
            )
            fixture.relationship_data["telemetry"]["sessions"][0][
                "ordered_runs"
            ][0] = invalid_run_id
            fixture.write_sources()

            # When: Run-label and otherwise-valid capture clocks are parsed.
            # Then: The invalid calendar is attributed to the run label alone.
            self.assert_failure(
                ClockPolicyError,
                "relationship.run_label.timestamp",
                f"run label timestamp is invalid: {invalid_run_id}",
                fixture.build,
            )

    def test_r3_a_09_capture_containment_is_strict_to_one_microsecond(self) -> None:
        # Given: A capture starting 1us late, ending 1us early, or inverted.
        variants = (
            ("late", "2026-07-17 00:01:00.000001", "2026-07-17 00:02:00.000000"),
            ("early", "2026-07-17 00:01:00.000000", "2026-07-17 00:01:59.999999"),
            ("inverted", "2026-07-17 00:02:00.000000", "2026-07-17 00:01:00.000000"),
        )
        for variant, start, end in variants:
            with self.subTest(variant=variant), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                fixture.usb_manifest["captures"][0]["earliest_packet_time"] = start
                fixture.usb_manifest["captures"][0]["latest_packet_time"] = end
                fixture.write_sources()

                # When: Clock containment is validated.
                # Then: No undeclared device-drift tolerance is added.
                self.assert_failure(
                    ClockPolicyError,
                    (
                        "relationship.capture.interval"
                        if variant == "inverted"
                        else "relationship.capture.containment"
                    ),
                    (
                        "capture interval is inverted: capture_1.pcapng"
                        if variant == "inverted"
                        else "capture does not contain run minute bucket: 260717_0001"
                    ),
                    fixture.build,
                )

    def test_r3_a_10_clock_policy_is_exact_and_mtime_is_forbidden(self) -> None:
        # Given: Missing/unknown basis, invalid quantization/tolerance/skew, or mtime use.
        mutations = (
            ("missing", lambda p: p["run_label"].pop("basis"), "relationship.clock.run_label.fields", "run_label fields are invalid"),
            ("basis", lambda p: p["run_label"].update(basis="wall clock"), "relationship.clock.run_label.basis", "run_label basis is unsupported"),
            ("quant-null", lambda p: p["run_label"].update(quantization_seconds=None), "relationship.clock.run_label.quantization", "run label quantization_seconds must be 60"),
            ("quant-bool", lambda p: p["run_label"].update(quantization_seconds=True), "relationship.clock.run_label.quantization", "run label quantization_seconds must be 60"),
            ("quant-string", lambda p: p["run_label"].update(quantization_seconds="60"), "relationship.clock.run_label.quantization", "run label quantization_seconds must be 60"),
            ("quant-zero", lambda p: p["run_label"].update(quantization_seconds=0), "relationship.clock.run_label.quantization", "run label quantization_seconds must be 60"),
            ("quant-negative", lambda p: p["run_label"].update(quantization_seconds=-1), "relationship.clock.run_label.quantization", "run label quantization_seconds must be 60"),
            ("quant-other", lambda p: p["run_label"].update(quantization_seconds=59), "relationship.clock.run_label.quantization", "run label quantization_seconds must be 60"),
            ("tolerance", lambda p: p.update(additional_containment_tolerance_seconds=1), "relationship.clock.tolerance", "additional containment tolerance must be 0 seconds"),
            ("skew", lambda p: p.update(calibrated_cross_clock_skew_seconds=0), "relationship.clock.skew", "calibrated cross-clock skew must be null"),
            ("mtime", lambda p: p.update(filesystem_mtime_used=True), "relationship.clock.mtime", "filesystem mtime use is forbidden"),
        )
        for name, mutation, code, message in mutations:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                mutation(fixture.relationship_data["clock_policy"])

                # When: The relationship contract clock policy is loaded.
                # Then: Only the frozen bases and exact boundary values are accepted.
                self.assert_failure(
                    ClockPolicyError,
                    code,
                    message,
                    fixture.build,
                )

    def test_r3_a_11_source_references_are_normalized_pinned_and_closed(self) -> None:
        # Given: Escaping/absolute paths, digest/schema/version mismatch, and unknown fields.
        mutations = (
            ("escape", lambda r: r["primary_contract"].update(path="../contract.json"), "relationship.source.path", "source path must be normalized repository-relative metadata"),
            ("absolute", lambda r: r["primary_contract"].update(path="/contract.json"), "relationship.source.path", "source path must be normalized repository-relative metadata"),
            ("sha", lambda r: r["primary_contract"].update(sha256="0" * 64), "relationship.source.sha256_mismatch", "source SHA-256 does not match pinned reference: primary_contract"),
            ("schema", lambda r: r["primary_contract"].update(schema="unknown"), "relationship.source.schema", "source schema is unsupported: primary_contract"),
            ("version", lambda r: r["primary_contract"].update(schema_version=2), "relationship.source.schema_version", "source schema version is unsupported: primary_contract"),
            ("unknown", lambda r: r["primary_contract"].update(payload="forbidden"), "relationship.source.fields", "source reference fields are invalid: primary_contract"),
        )
        for name, mutation, code, message in mutations:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                mutation(fixture.relationship_data["source_references"])

                # When: Pinned metadata sources are loaded.
                # Then: A typed source error fails closed before relationship inference.
                self.assert_failure(
                    SourceReferenceError,
                    code,
                    message,
                    fixture.build,
                )

    def test_r3_n_06_failure_trace_false_is_retained_from_usb_authority(self) -> None:
        # Given: The pinned USB constraint declares boolean false.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(Path(temporary_directory))

            # When: Relationship evidence is built.
            evidence = fixture.build()

            # Then: Absence is retained without claiming causal failure evidence.
            self.assertIs(False, evidence["global"]["failure_trace_present"])

    def test_r3_a_12_failure_trace_missing_true_or_string_false_is_rejected(self) -> None:
        # Given: Missing, true, and string-false USB failure-trace constraints.
        variants = (("missing", None), ("true", True), ("string", "false"))
        for name, value in variants:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                constraints = fixture.usb_manifest["constraints"]
                if name == "missing":
                    constraints.pop("failure_trace_present")
                else:
                    constraints["failure_trace_present"] = value
                fixture.write_sources()

                # When: Failure-trace authority is validated.
                # Then: No failure trace is synthesized or coerced.
                self.assert_failure(
                    RelationshipMappingError,
                    "relationship.failure_trace",
                    "USB failure_trace_present must be boolean false",
                    fixture.build,
                )

    def test_usb_payload_tracking_constraint_must_remain_boolean_false(self) -> None:
        # Given: A pinned USB index that claims raw payload is tracked.
        with tempfile.TemporaryDirectory() as temporary_directory:
            fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
            fixture.usb_manifest["constraints"]["raw_payload_tracked"] = True
            fixture.write_sources()

            # When: The relationship evidence boundary is validated.
            # Then: Payload-bearing authority is rejected exactly.
            self.assert_failure(
                RelationshipMappingError,
                "relationship.payload_boundary",
                "USB raw_payload_tracked must be boolean false",
                fixture.build,
            )

    def test_pr9_cx_a01_usb_index_requires_complete_nested_schema(self) -> None:
        # Given: One required field is removed from each USB index object shape.
        cases = (
            (
                "top",
                lambda value: value.pop("provisional"),
                "relationship.usb.fields",
                "USB manifest fields are invalid",
            ),
            (
                "bounded-summary",
                lambda value: value["bounded_summary"].pop("path"),
                "relationship.usb.bounded_summary.fields",
                "USB bounded summary fields are invalid",
            ),
            (
                "capture",
                lambda value: value["captures"][0].pop("packet_count"),
                "relationship.usb.capture.fields",
                "USB capture fields are invalid",
            ),
            (
                "capture-analysis",
                lambda value: value["captures"][0]["analysis"].pop("schema"),
                "relationship.usb.capture.analysis.fields",
                "USB capture analysis fields are invalid",
            ),
            (
                "constraints",
                lambda value: value["constraints"].pop("d19"),
                "relationship.usb.constraints.fields",
                "USB constraints fields are invalid",
            ),
            (
                "interface",
                lambda value: value["interface"].pop("name"),
                "relationship.usb.interface.fields",
                "USB interface fields are invalid",
            ),
            (
                "source-manifest",
                lambda value: value["source_manifest"].pop("sha256"),
                "relationship.usb.source_manifest.fields",
                "USB source manifest fields are invalid",
            ),
            (
                "tool",
                lambda value: value["tools"]["tshark"].pop("version"),
                "relationship.usb.tool.fields",
                "USB tool fields are invalid",
            ),
        )
        for name, mutate, code, message in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                mutate(fixture.usb_manifest)
                fixture.write_sources()

                # When/Then: Re-pinning cannot make incomplete P0-C1-C3 evidence valid.
                self.assert_failure(
                    SourceReferenceError,
                    code,
                    message,
                    fixture.build,
                )

    def test_pr9_cx_a02_usb_index_rejects_invalid_required_values(self) -> None:
        # Given: Required USB evidence fields have boundary-invalid values.
        cases = (
            (
                "provisional",
                lambda value: value.update(provisional=False),
                "relationship.usb.provisional",
                "USB manifest must remain provisional",
            ),
            (
                "capture-size-bool",
                lambda value: value["captures"][0].update(size_bytes=True),
                "relationship.usb.capture.value",
                "USB capture values are invalid",
            ),
            (
                "bounded-digest",
                lambda value: value["bounded_summary"].update(sha256="invalid"),
                "relationship.usb.bounded_summary.value",
                "USB bounded summary values are invalid",
            ),
            (
                "source-path",
                lambda value: value["source_manifest"].update(path="../outside.json"),
                "relationship.usb.source_manifest.value",
                "USB source manifest values are invalid",
            ),
        )
        for name, mutate, code, message in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary_directory:
                fixture = SyntheticRelationshipCorpus(Path(temporary_directory))
                mutate(fixture.usb_manifest)
                fixture.write_sources()

                # When/Then: Invalid values fail before relationship evidence is emitted.
                self.assert_failure(
                    SourceReferenceError,
                    code,
                    message,
                    fixture.build,
                )

    def test_r3_n_07_semantic_order_changes_keep_evidence_byte_identical(self) -> None:
        # Given: Equivalent relationship/session/source object orderings.
        with tempfile.TemporaryDirectory() as first_dir, tempfile.TemporaryDirectory() as second_dir:
            first = SyntheticRelationshipCorpus(Path(first_dir), run_count=2, session_sizes=(1, 1))
            second = SyntheticRelationshipCorpus(Path(second_dir), run_count=2, session_sizes=(1, 1))
            second.relationship_data["telemetry"]["sessions"].reverse()
            second.usb_manifest["captures"].reverse()
            second.primary_contract["run_capture_mapping"].reverse()
            second.write_sources()

            # When: Both semantically equivalent inputs are serialized.
            first_bytes = serialize_relationship_evidence(first.build())
            second_bytes = serialize_relationship_evidence(second.build())

            # Then: Sorted runs/sessions and canonical JSON are byte-identical.
            self.assertEqual(first_bytes, second_bytes)

    def test_r3_a_13_duplicate_keys_payload_fields_and_host_paths_are_rejected(self) -> None:
        # Given: Duplicate JSON keys and payload/absolute-path evidence fields.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            duplicate = repo_root / "relationship.json"
            duplicate.write_text(
                '{"schema":"a","schema":"b"}',
                encoding="utf-8",
            )

            # When: Strict relationship JSON loading and evidence validation are requested.
            # Then: Duplicate keys, payload-like fields, and host paths fail closed.
            self.assert_failure(
                RelationshipContractError,
                "relationship.json.duplicate_key",
                "duplicate JSON key is not allowed: schema",
                lambda: load_relationship_contract(duplicate),
            )
            for field, value, code, message in (
                ("rows", [], "relationship.evidence.fields", "relationship evidence fields are invalid"),
                ("payload", "00", "relationship.evidence.fields", "relationship evidence fields are invalid"),
                ("source_locator", "C:/host/corpus", "relationship.evidence.path", "evidence paths must be normalized repository-relative metadata"),
                ("mtime", 1, "relationship.evidence.fields", "relationship evidence fields are invalid"),
            ):
                with self.subTest(field=field):
                    evidence = {
                        "schema": RELATIONSHIP_EVIDENCE_SCHEMA,
                        "schema_version": 1,
                        "global": {},
                        "runs": [],
                        "telemetry_sessions": [],
                        "clock_policy": {},
                        field: value,
                    }
                    self.assert_failure(
                        EvidenceValidationError,
                        code,
                        message,
                        lambda evidence=evidence: validate_relationship_evidence_data(evidence),
                    )

    def test_pr9_cl_a04_duplicate_evidence_key_uses_bounded_evidence_error(
        self,
    ) -> None:
        # Given: Relationship evidence JSON with a duplicate non-reflectable key.
        with tempfile.TemporaryDirectory() as temporary_directory:
            evidence_path = Path(temporary_directory) / "relationships.json"
            duplicated_key = "nonreflected-key"
            evidence_path.write_text(
                f'{{"{duplicated_key}":0,"{duplicated_key}":1}}',
                encoding="utf-8",
            )

            # When: The strict evidence loader decodes the JSON object.
            with self.assertRaises(EvidenceValidationError) as raised:
                _load_evidence(evidence_path)

            # Then: The exact evidence-context error is bounded and key-agnostic.
            self.assertIs(EvidenceValidationError, type(raised.exception))
            self.assertEqual(
                "relationship.evidence.json",
                raised.exception.code,
            )
            self.assertEqual(
                (
                    "relationship.evidence.json: "
                    "relationship evidence must contain valid UTF-8 JSON"
                ),
                str(raised.exception),
            )
            self.assertNotIn(duplicated_key, str(raised.exception))

    def test_relationship_output_is_exact_scoped_atomic_and_outside_assets(self) -> None:
        # Given: A repository and canonical relationship evidence bytes.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            output = (
                repo_root
                / "docs/reference/initial-recording-corpus/2026-07-17"
            )
            output.mkdir(parents=True)
            payload = b"{}\n"

            # When: The exact output is written and invalid destinations are attempted.
            _write_relationships(
                repo_root,
                "docs/reference/initial-recording-corpus/2026-07-17/relationships.json",
                payload,
            )

            # Then: Only the exact tracked path is replaced atomically outside assets.
            self.assertEqual(payload, (output / "relationships.json").read_bytes())
            for relative in (
                "artifacts/relationships.json",
                "docs/reference/initial-recording-corpus/2026-07-17/other.json",
            ):
                with self.subTest(relative=relative):
                    self.assert_failure(
                        RelationshipPathError,
                        "relationship.output.scope",
                        (
                            "output must be "
                            "docs/reference/initial-recording-corpus/2026-07-17/"
                            "relationships.json"
                        ),
                        lambda relative=relative: _write_relationships(
                            repo_root,
                            relative,
                            payload,
                        ),
                    )


if __name__ == "__main__":
    unittest.main()
