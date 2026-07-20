from __future__ import annotations

import io
import os
import re
import subprocess
import sys
import tempfile
import unittest
from dataclasses import replace
from decimal import Decimal
from pathlib import Path
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import pcap_analysis as pa  # noqa: E402


FIELD_INVENTORY_METADATA = {
    "frame.number": ("Frame Number", "FT_UINT32", "frame"),
    "frame.time_epoch": ("Epoch Arrival Time", "FT_ABSOLUTE_TIME", "frame"),
    "frame.time_relative": ("Time since reference or first frame", "FT_RELATIVE_TIME", "frame"),
    "frame.cap_len": ("Capture Length", "FT_UINT32", "frame"),
    "frame.len": ("Frame Length", "FT_UINT32", "frame"),
    "usb.bus_id": ("URB bus id", "FT_UINT16", "usb"),
    "usb.device_address": ("Device", "FT_UINT32", "usb"),
    "usb.irp_id": ("IRP ID", "FT_UINT64", "usb"),
    "usb.urb_type": ("URB type", "FT_CHAR", "usb"),
    "usb.irp_info.direction": ("Direction", "FT_UINT8", "usb"),
    "usb.function": ("URB Function", "FT_UINT16", "usb"),
    "usb.transfer_type": ("URB transfer type", "FT_UINT8", "usb"),
    "usb.endpoint_address": ("Endpoint", "FT_UINT8", "usb"),
    "usb.usbd_status": ("IRP USBD_STATUS", "FT_UINT32", "usb"),
    "usb.urb_len": ("URB length [bytes]", "FT_UINT32", "usb"),
    "usb.data_len": ("Data length [bytes]", "FT_UINT32", "usb"),
    "usb.idVendor": ("idVendor", "FT_UINT16", "usb"),
    "usb.idProduct": ("idProduct", "FT_UINT16", "usb"),
    "usb.bInterfaceNumber": ("bInterfaceNumber", "FT_UINT8", "usb"),
}


def field_inventory(*, missing: str | None = None) -> str:
    lines = []
    for abbreviation in pa.REQUIRED_TSHARK_FIELDS:
        if abbreviation != missing:
            name, field_type, protocol = FIELD_INVENTORY_METADATA[abbreviation]
            lines.append(f"F\t{name}\t{abbreviation}\t{field_type}\t{protocol}\t\t0x0\t")
    return "\n".join(lines)


def valid_capinfos() -> str:
    return """\
File name:           low.pcapng
File type:           pcapng
File encapsulation:  usb-usbpcap
Number of packets:   263819
File size:           408817408 bytes
Capture duration:    148.438122 seconds
Earliest packet time: 2026-07-17 15:38:39.394438
Latest packet time:   2026-07-17 15:41:07.832560
SHA256:              520e729622c75d031aeae14d4ade76597caa80b07c2f23e387069f859f77b26c
Number of interfaces in file: 1
Interface #0 info:
                     Name = \\\\.\\USBPcap1
                     Description = USBPcap1
                     Encapsulation = USB packets with USBPcap header (152 - usb-usbpcap)
                     Capture length = 8000000
                     Number of packets = 263819
"""


def valid_cells(**overrides: str) -> list[str]:
    values = {
        "frame.number": "1",
        "frame.time_epoch": "1784270319.394438",
        "frame.time_relative": "0.000000",
        "frame.cap_len": "64",
        "frame.len": "64",
        "usb.bus_id": "1",
        "usb.device_address": "7",
        "usb.irp_id": "0x0000000000001234",
        "usb.urb_type": "S",
        "usb.irp_info.direction": "0x00",
        "usb.function": "0x0009",
        "usb.transfer_type": "0x03",
        "usb.endpoint_address": "0x86",
        "usb.usbd_status": "0x00000000",
        "usb.urb_len": "16384",
        "usb.data_len": "0",
        "usb.idVendor": "0x04b4",
        "usb.idProduct": "0xfff2",
        "usb.bInterfaceNumber": "0",
    }
    values.update(overrides)
    return [values[name] for name in pa.ROW_FIELDS]


class FieldInventoryTests(unittest.TestCase):
    def test_accepts_complete_required_field_inventory(self) -> None:
        # Given: A complete field inventory using real TShark names, abbreviations, types, and protocols.
        text = field_inventory()
        # When: The inventory is parsed and validated.
        fields = pa.parse_field_inventory(text)
        pa.validate_required_fields(fields)
        # Then: Distinct metadata columns are parsed instead of being tautologically equal.
        self.assertEqual(tuple(fields[name].abbreviation for name in pa.REQUIRED_TSHARK_FIELDS), pa.REQUIRED_TSHARK_FIELDS)
        for abbreviation, (expected_name, expected_type, expected_protocol) in FIELD_INVENTORY_METADATA.items():
            with self.subTest(abbreviation=abbreviation):
                info = fields[abbreviation]
                self.assertEqual(info.name, expected_name)
                self.assertEqual(info.abbreviation, abbreviation)
                self.assertEqual(info.field_type, expected_type)
                self.assertEqual(info.protocol, expected_protocol)

    def test_rejects_one_missing_required_field(self) -> None:
        # Given: A field inventory missing exactly one required field.
        text = field_inventory(missing="usb.usbd_status")
        # When/Then: Validation reports the missing abbreviation.
        with self.assertRaisesRegex(pa.AnalyzerError, r"missing required TShark fields: usb\.usbd_status"):
            pa.validate_required_fields(pa.parse_field_inventory(text))

    def test_rejects_empty_field_inventory(self) -> None:
        # Given: An empty field inventory (the CLI equivalent of no records).
        # When/Then: Validation fails with every required field unavailable.
        with self.assertRaisesRegex(pa.AnalyzerError, "missing required TShark fields"):
            pa.validate_required_fields(pa.parse_field_inventory(""))

    def test_rejects_null_field_inventory_input(self) -> None:
        # Given: A NULL inventory supplied by a failed dependency boundary.
        # When/Then: Parsing fails with an explicit type/message.
        with self.assertRaisesRegex(pa.AnalyzerError, "field inventory text is required"):
            pa.parse_field_inventory(None)  # type: ignore[arg-type]


class CapinfosTests(unittest.TestCase):
    def test_parses_machine_readable_long_report(self) -> None:
        # Given: A complete Capinfos 4.6.7 machine-readable long report.
        # When: The report is normalized.
        info = pa.parse_capinfos(valid_capinfos(), "low.pcapng")
        # Then: Required identity, timing, and interface properties are distinct.
        self.assertEqual(info.file_type, "pcapng")
        self.assertEqual(info.file_encapsulation, "usb-usbpcap")
        self.assertEqual(info.packet_count, 263819)
        self.assertEqual(info.file_size_bytes, 408817408)
        self.assertEqual(info.duration_seconds, "148.438122")
        self.assertEqual(info.interfaces[0].name, r"\\.\USBPcap1")
        self.assertEqual(info.interfaces[0].packet_count, 263819)

    def test_rejects_missing_capinfos_property(self) -> None:
        # Given: A Capinfos report without capture duration.
        report = valid_capinfos().replace("Capture duration:    148.438122 seconds\n", "")
        # When/Then: Parsing identifies the missing property and capture.
        with self.assertRaisesRegex(pa.AnalyzerError, "low.pcapng: Capinfos report missing Capture duration"):
            pa.parse_capinfos(report, "low.pcapng")

    def test_rejects_invalid_capinfos_number(self) -> None:
        # Given: A non-numeric packet count.
        report = valid_capinfos().replace("Number of packets:   263819", "Number of packets:   not-a-number", 1)
        # When/Then: Parsing identifies the invalid value and property.
        with self.assertRaisesRegex(pa.AnalyzerError, "low.pcapng: invalid Number of packets: not-a-number"):
            pa.parse_capinfos(report, "low.pcapng")

    def test_rejects_null_capinfos_report(self) -> None:
        # Given: NULL output from the external tool boundary.
        # When/Then: Parsing fails explicitly instead of producing empty metadata.
        with self.assertRaisesRegex(pa.AnalyzerError, "low.pcapng: Capinfos report is required"):
            pa.parse_capinfos(None, "low.pcapng")  # type: ignore[arg-type]


class PathSafetyTests(unittest.TestCase):
    def test_accepts_designated_analysis_and_external_output_roots(self) -> None:
        # Given: A source root, its designated analysis child, and an external sibling.
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "source"
            source.mkdir()
            # When: Both allowed layouts are validated.
            analysis = pa.validate_output_root(source, source / "analysis")
            external = pa.validate_output_root(source, root / "external")
            # Then: Resolved safe paths are returned.
            self.assertEqual(analysis, (source / "analysis").resolve())
            self.assertEqual(external, (root / "external").resolve())

    def test_rejects_source_root_and_non_analysis_child(self) -> None:
        # Given: Output paths that could mix generated data with immutable captures.
        with tempfile.TemporaryDirectory() as temp_dir:
            source = Path(temp_dir) / "source"
            source.mkdir()
            # When/Then: The source root and arbitrary child are rejected.
            for output in (source, source / "other"):
                with self.subTest(output=output), self.assertRaisesRegex(pa.AnalyzerError, "unsafe output root"):
                    pa.validate_output_root(source, output)

    def test_rejects_missing_directory_symlink_and_zero_byte_capture(self) -> None:
        # Given: Missing, directory, symlink, and zero-byte capture candidates.
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            regular = root / "regular.pcapng"
            regular.write_bytes(b"x")
            zero = root / "zero.pcapng"
            zero.touch()
            directory = root / "directory.pcapng"
            directory.mkdir()
            symlink = root / "link.pcapng"
            symlink.symlink_to(regular)
            candidates = (root / "missing.pcapng", zero, directory, symlink)
            # When/Then: Every invalid filesystem boundary fails explicitly.
            for candidate in candidates:
                with self.subTest(candidate=candidate), self.assertRaisesRegex(pa.AnalyzerError, candidate.name):
                    pa.validate_capture_file(candidate)

    def test_rejects_symlink_source_root(self) -> None:
        # Given: A source-root symlink that could redirect the corpus outside its pinned locator.
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            actual = root / "actual"
            actual.mkdir()
            source_link = root / "source"
            source_link.symlink_to(actual, target_is_directory=True)
            # When/Then: Root validation rejects the indirection before inspecting captures.
            with self.assertRaisesRegex(pa.AnalyzerError, "source root must not be a symlink"):
                pa.validate_source_root(source_link)


class RowParsingTests(unittest.TestCase):
    def test_parses_minimum_valid_normalized_row(self) -> None:
        # Given: A valid first frame with zero relative time and data length.
        cells = valid_cells()
        # When: The TShark cells are normalized.
        row = pa.parse_tshark_record(pa.ROW_FIELDS, cells, "low.pcapng")
        # Then: Numeric/status fields remain typed and NT status remains unavailable.
        self.assertEqual(row.frame_number, 1)
        self.assertEqual(row.data_length, 0)
        self.assertEqual(row.endpoint, 0x86)
        self.assertEqual(row.usbd_status, 0)
        self.assertIsNone(row.nt_status)

    def test_rejects_negative_frame_number_and_length(self) -> None:
        # Given: Values one below each minimum boundary.
        invalid = (
            valid_cells(**{"frame.number": "-1"}),
            valid_cells(**{"frame.len": "-1"}),
        )
        # When/Then: Negative frame/length values fail with field context.
        for cells in invalid:
            with self.subTest(cells=cells), self.assertRaisesRegex(pa.AnalyzerError, "low.pcapng: invalid"):
                pa.parse_tshark_record(pa.ROW_FIELDS, cells, "low.pcapng")

    def test_rejects_malformed_column_count(self) -> None:
        # Given: A row with one field fewer than the fixed header.
        cells = valid_cells()[:-1]
        # When/Then: Parsing reports both expected and actual counts.
        with self.assertRaisesRegex(pa.AnalyzerError, r"low.pcapng: malformed TShark row: expected 19 columns, got 18"):
            pa.parse_tshark_record(pa.ROW_FIELDS, cells, "low.pcapng")

    def test_marks_truncation_without_converting_unknown_status_to_success(self) -> None:
        # Given: A captured-short row with no USBD status.
        cells = valid_cells(**{"frame.cap_len": "48", "frame.len": "64", "usb.usbd_status": ""})
        # When: The row is normalized.
        row = pa.parse_tshark_record(pa.ROW_FIELDS, cells, "low.pcapng")
        # Then: Truncation and unknown status remain separate facts.
        self.assertTrue(row.is_truncated)
        self.assertEqual(pa.classify_usbd_status(row.usbd_status), "unknown")

    def test_uses_usbpcap_irp_direction_when_urb_type_is_empty(self) -> None:
        # Given: Real USBPcap-style rows where usb.urb_type is unavailable.
        request = pa.parse_tshark_record(
            pa.ROW_FIELDS,
            valid_cells(**{"usb.urb_type": "", "usb.irp_info.direction": "0x00"}),
            "low.pcapng",
        )
        completion = replace(request, irp_direction=1)
        # When: Event kinds are resolved.
        # Then: IRP direction 0/1 maps to request/completion without guessing success.
        self.assertEqual(request.event_kind, "request")
        self.assertEqual(completion.event_kind, "completion")


class CorrelationTests(unittest.TestCase):
    def test_correlates_request_completion_and_short_transfer(self) -> None:
        # Given: A 16 KiB submission followed by a shorter successful completion.
        submit = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        complete = pa.parse_tshark_record(
            pa.ROW_FIELDS,
            valid_cells(**{"frame.number": "2", "usb.urb_type": "C", "usb.urb_len": "8192", "usb.data_len": "8192"}),
            "low.pcapng",
        )
        tracker = pa.CorrelationTracker()
        # When: Both rows are consumed and the tracker is finalized.
        tracker.consume(submit)
        tracker.consume(complete)
        result = tracker.finish()
        # Then: The pair and short transfer are counted without unmatched rows.
        self.assertEqual(result["correlated"], 1)
        self.assertEqual(result["short_transfer"], 1)
        self.assertEqual(result["unmatched_request"], 0)
        self.assertEqual(result["unmatched_completion"], 0)

    def test_separates_duplicate_and_unmatched_events(self) -> None:
        # Given: Duplicate active requests, an unmatched completion, and a recent duplicate completion.
        request = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        completion = replace(request, frame_number=2, urb_type="C")
        unmatched = replace(completion, frame_number=3, irp_id=0x9999)
        tracker = pa.CorrelationTracker(recent_limit=2)
        # When: The anomalous ordering is consumed.
        tracker.consume(request)
        tracker.consume(request)
        tracker.consume(completion)
        tracker.consume(completion)
        tracker.consume(unmatched)
        result = tracker.finish()
        # Then: Duplicate and unmatched categories remain distinct.
        self.assertEqual(result["duplicate_active_request"], 1)
        self.assertEqual(result["duplicate_recent_completion"], 1)
        self.assertEqual(result["unmatched_completion"], 1)
        self.assertEqual(result["evidence_frames"]["unmatched_completion"], [3])

    def test_supersedes_active_request_when_irp_pointer_is_reused(self) -> None:
        # Given: An IRP pointer is reused for a shorter request before the stale request completes.
        stale_request = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        current_request = replace(stale_request, frame_number=2, requested_length=8192)
        completion = replace(current_request, frame_number=3, urb_type="C", data_length=8192)
        tracker = pa.CorrelationTracker()
        # When: The reused request and its completion are consumed in capture order.
        tracker.consume(stale_request)
        tracker.consume(current_request)
        tracker.consume(completion)
        result = tracker.finish()
        # Then: Completion pairs with the current request and supersession stays bounded and observable.
        self.assertEqual(result["correlated"], 1)
        self.assertEqual(result["short_transfer"], 0)
        self.assertEqual(result["unmatched_request"], 0)
        self.assertEqual(result["duplicate_active_request"], 1)
        self.assertEqual(result["evidence_frames"]["duplicate_active_request"], [2])

    def test_refreshes_duplicate_completion_recency_when_irp_pointer_is_reused(self) -> None:
        # Given: A recent window where A is duplicated between newer B and C completions.
        request_a = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        completion_a = replace(request_a, frame_number=2, urb_type="C")
        completion_b = replace(completion_a, frame_number=3, irp_id=0xB)
        completion_c = replace(completion_a, frame_number=5, irp_id=0xC)
        tracker = pa.CorrelationTracker(recent_limit=2)
        # When: A is completed, duplicated, and then seen again after C advances the window.
        for row in (request_a, completion_a, completion_b, replace(completion_a, frame_number=4), completion_c):
            tracker.consume(row)
        tracker.consume(replace(completion_a, frame_number=6))
        result = tracker.finish()
        # Then: The first duplicate refreshed A, so the final A remains duplicate rather than unmatched.
        self.assertEqual(result["duplicate_recent_completion"], 2)
        self.assertEqual(result["unmatched_completion"], 2)
        self.assertEqual(result["evidence_frames"]["duplicate_recent_completion"], [4, 6])

    def test_counts_unknown_event_and_non_success_status_separately(self) -> None:
        # Given: One row with an unknown URB type and non-success USBD status.
        row = pa.parse_tshark_record(
            pa.ROW_FIELDS,
            valid_cells(**{"usb.urb_type": "?", "usb.usbd_status": "0xc0000011"}),
            "low.pcapng",
        )
        tracker = pa.CorrelationTracker()
        # When: The row is consumed.
        tracker.consume(row)
        result = tracker.finish()
        # Then: Unknown event and non-success status counts are both retained.
        self.assertEqual(result["unknown_event"], 1)
        self.assertEqual(result["status_counts"]["non_success"], 1)

    def test_keeps_unevaluable_short_transfer_unknown(self) -> None:
        # Given: A USBPcap pair whose request exposes no requested length.
        request = pa.parse_tshark_record(
            pa.ROW_FIELDS,
            valid_cells(**{"usb.urb_len": "", "usb.urb_type": "", "usb.irp_info.direction": "0x00"}),
            "low.pcapng",
        )
        completion = replace(request, frame_number=2, irp_direction=1, data_length=49152)
        tracker = pa.CorrelationTracker()
        # When: The pair is correlated.
        tracker.consume(request)
        tracker.consume(completion)
        result = tracker.finish()
        # Then: It is correlated but not counted as a proven non-short transfer.
        self.assertEqual(result["correlated"], 1)
        self.assertEqual(result["short_transfer_evaluable"], 0)
        self.assertEqual(result["short_transfer_unknown"], 1)


class CaptureAggregationTests(unittest.TestCase):
    @staticmethod
    def _summarize_ep6_completion_times(relative_seconds: tuple[str, ...]) -> dict[str, object]:
        base = pa.parse_tshark_record(
            pa.ROW_FIELDS,
            valid_cells(**{"usb.urb_type": "", "usb.irp_info.direction": "0x01", "usb.data_len": "1"}),
            "low.pcapng",
        )
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="9" * 64,
        )
        for index, relative in enumerate(relative_seconds, start=1):
            accumulator.consume(
                replace(base, frame_number=index, irp_id=index, relative_seconds=relative)
            )
        return accumulator.finish()["endpoints"]["0x86_in"]

    def test_discovers_target_device_and_builds_bounded_lifecycle_summary(self) -> None:
        # Given: Unrelated EP4 traffic and one device covering EP2/EP4/EP6.
        base = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        unrelated = replace(base, frame_number=1, device_address=3, endpoint=0x84, irp_id=1)
        rows = [unrelated]
        frame = 10
        for endpoint, irp_id, data_length in ((0x02, 2, 16), (0x84, 3, 512), (0x86, 4, 65536)):
            rows.append(
                replace(
                    base,
                    frame_number=frame,
                    relative_seconds=str(frame),
                    device_address=50,
                    endpoint=endpoint,
                    irp_id=irp_id,
                    urb_type="",
                    irp_direction=0,
                    data_length=0,
                    requested_length=data_length,
                )
            )
            rows.append(
                replace(
                    rows[-1],
                    frame_number=frame + 1,
                    relative_seconds=str(frame + 1),
                    irp_direction=1,
                    data_length=data_length,
                )
            )
            frame += 10
        rows.append(
            replace(
                base,
                frame_number=40,
                relative_seconds="40",
                device_address=50,
                endpoint=0x84,
                irp_id=5,
                urb_type="",
                irp_direction=0,
                data_length=0,
                requested_length=512,
            )
        )
        rows.append(replace(rows[-1], frame_number=41, relative_seconds="41", irp_direction=1, data_length=512))
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=408817408,
            source_sha256="a" * 64,
        )
        # When: Rows are streamed and finalized.
        for row in rows:
            accumulator.consume(row)
        summary = accumulator.finish()
        # Then: Per-capture discovery, endpoints, correlation, and lifecycle frames are bounded facts.
        self.assertEqual(summary["selected_device"]["bus_id"], 1)
        self.assertEqual(summary["selected_device"]["device_address"], 50)
        self.assertEqual(summary["endpoints"]["0x02_out"]["completion_count"], 1)
        self.assertEqual(summary["endpoints"]["0x84_in"]["completion_count"], 2)
        self.assertEqual(summary["endpoints"]["0x86_in"]["data_length"]["total_bytes"], 65536)
        self.assertEqual(summary["lifecycle"]["ep2_set_candidate"]["first_frame"], 10)
        self.assertEqual(summary["lifecycle"]["ep6_bulk"]["first_frame"], 30)
        sequence = summary["lifecycle"]["evidence_sequence"]
        self.assertEqual(
            [(event["phase"], event["frame"]) for event in sequence],
            [
                ("connect_observation", 10),
                ("ep2_set_completion", 11),
                ("ep4_completion_after_set", 21),
                ("ep6_completion_after_set", 31),
                ("ep6_final_completion", 31),
                ("ep4_successful_completion_after_final_ep6", 41),
                ("close_observation", 41),
            ],
        )
        self.assertNotIn('"payload":', stable_json_text(summary).lower())

    def test_binds_tail_success_to_the_ep4_completion_not_unrelated_status(self) -> None:
        # Given: An earlier failed control row, then EP6 followed by a successful EP4 completion.
        base = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        rows = [
            replace(base, frame_number=1, endpoint=0x00, usbd_status=0xC0000001),
            replace(base, frame_number=2, endpoint=0x86, urb_type="", irp_direction=1, data_length=65536),
            replace(base, frame_number=3, endpoint=0x84, urb_type="", irp_direction=1, data_length=512),
        ]
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="1" * 64,
        )
        # When: The bounded lifecycle evidence is finalized.
        for row in rows:
            accumulator.consume(row)
        tail = accumulator.finish()["lifecycle"]["stop_drain_graceful_close_observation"]
        # Then: The successful EP4 fact is retained despite an unrelated earlier failure.
        self.assertEqual(tail["classification"], "successful EP4 completion observed after final EP6 completion")
        self.assertEqual(tail["ep4_successful_completion_after_final_ep6_frame"], 3)

    def test_does_not_call_a_failed_tail_ep4_completion_successful(self) -> None:
        # Given: EP6 followed only by a failed EP4 completion.
        base = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        rows = [
            replace(base, frame_number=1, endpoint=0x86, urb_type="", irp_direction=1, data_length=65536),
            replace(
                base,
                frame_number=2,
                endpoint=0x84,
                urb_type="",
                irp_direction=1,
                usbd_status=0xC0000011,
            ),
        ]
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="2" * 64,
        )
        # When: The bounded lifecycle evidence is finalized.
        for row in rows:
            accumulator.consume(row)
        tail = accumulator.finish()["lifecycle"]["stop_drain_graceful_close_observation"]
        # Then: The first tail poll is recorded, but no successful tail poll is claimed.
        self.assertEqual(tail["classification"], "non-success EP4 completion observed after final EP6 completion")
        self.assertEqual(tail["ep4_first_completion_after_final_ep6_frame"], 2)
        self.assertIsNone(tail["ep4_successful_completion_after_final_ep6_frame"])
        self.assertEqual(tail["ep4_non_success_completion_after_final_ep6_count"], 1)

    def test_retains_non_success_and_recovery_tail_ep4_evidence_in_order(self) -> None:
        # Given: Final EP6 is followed by a failed EP4 completion and then a successful EP4 completion.
        base = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        rows = [
            replace(base, frame_number=1, endpoint=0x86, urb_type="", irp_direction=1, data_length=65536),
            replace(
                base,
                frame_number=2,
                endpoint=0x84,
                urb_type="",
                irp_direction=1,
                usbd_status=0xC0000011,
            ),
            replace(base, frame_number=3, endpoint=0x84, urb_type="", irp_direction=1, data_length=512),
        ]
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="3" * 64,
        )
        # When: The bounded lifecycle evidence is finalized.
        for row in rows:
            accumulator.consume(row)
        lifecycle = accumulator.finish()["lifecycle"]
        tail = lifecycle["stop_drain_graceful_close_observation"]
        # Then: Both distinct frames remain ordered and the tail is not classified as an unqualified clean sequence.
        self.assertEqual(
            [(event["phase"], event["frame"]) for event in lifecycle["evidence_sequence"]],
            [
                ("connect_observation", 1),
                ("ep6_final_completion", 1),
                ("ep4_completion_after_final_ep6", 2),
                ("ep4_successful_completion_after_final_ep6", 3),
                ("close_observation", 3),
            ],
        )
        self.assertEqual(
            tail["classification"],
            "non-success EP4 completion followed by successful EP4 completion after final EP6 completion",
        )
        self.assertEqual(tail["ep4_non_success_completion_after_final_ep6_count"], 1)
        self.assertEqual(
            tail["ep4_non_success_completion_after_final_ep6_evidence"],
            [{"frame": 2, "status": "non_success", "usbd_status": "0xc0000011"}],
        )

    def test_classifies_unknown_tail_ep4_status_as_incomplete(self) -> None:
        # Given: Final EP6 is followed by an EP4 completion whose USBD status field is unavailable.
        base = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="7" * 64,
        )
        accumulator.consume(
            replace(base, frame_number=1, endpoint=0x86, urb_type="", irp_direction=1, data_length=65536)
        )
        # When: The unknown-status tail completion is consumed and summarized.
        accumulator.consume(
            replace(
                base,
                frame_number=2,
                endpoint=0x84,
                urb_type="",
                irp_direction=1,
                usbd_status=None,
            )
        )
        summary = accumulator.finish()
        tail = summary["lifecycle"]["stop_drain_graceful_close_observation"]
        # Then: Unknown remains independently counted and the lifecycle is incomplete, not a known failure.
        self.assertEqual(tail["classification"], "capture-tail EP4 status observation incomplete")
        self.assertEqual(tail["ep4_non_success_completion_after_final_ep6_count"], 0)
        self.assertEqual(tail["ep4_non_success_completion_after_final_ep6_evidence"], [])
        self.assertEqual(summary["correlation"]["status_counts"]["unknown"], 1)
        self.assertEqual(summary["correlation"]["evidence_frames"]["unknown_status"], [2])

    def test_uses_direction_appropriate_usb_data_len_for_endpoint_bytes(self) -> None:
        # Given: EP2 OUT exposes payload length on its request while EP6 IN exposes it on completion.
        base = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        rows = [
            replace(
                base,
                frame_number=1,
                relative_seconds="0",
                endpoint=0x02,
                irp_id=1,
                urb_type="",
                irp_direction=0,
                data_length=16,
            ),
            replace(
                base,
                frame_number=2,
                relative_seconds="1",
                endpoint=0x02,
                irp_id=1,
                urb_type="",
                irp_direction=1,
                data_length=0,
            ),
            replace(
                base,
                frame_number=3,
                relative_seconds="2",
                endpoint=0x86,
                irp_id=2,
                urb_type="",
                irp_direction=0,
                data_length=0,
            ),
            replace(
                base,
                frame_number=4,
                relative_seconds="3",
                endpoint=0x86,
                irp_id=2,
                urb_type="",
                irp_direction=1,
                data_length=65536,
            ),
        ]
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="4" * 64,
        )
        # When: Endpoint byte distributions and rates are aggregated.
        for row in rows:
            accumulator.consume(row)
        endpoints = accumulator.finish()["endpoints"]
        # Then: Each direction uses the USBPcap row carrying transfer bytes and declares that basis without payload.
        self.assertEqual(endpoints["0x02_out"]["data_length"]["total_bytes"], 16)
        self.assertEqual(
            endpoints["0x02_out"]["data_length_basis"],
            {"endpoint_direction": "out", "row_event_kind": "request", "source_field": "usb.data_len"},
        )
        self.assertEqual(endpoints["0x86_in"]["data_length"]["total_bytes"], 65536)
        self.assertEqual(
            endpoints["0x86_in"]["data_length_basis"],
            {"endpoint_direction": "in", "row_event_kind": "completion", "source_field": "usb.data_len"},
        )
        self.assertNotIn('"payload":', stable_json_text(endpoints).lower())

    def test_excludes_superseded_out_request_bytes_when_irp_pointer_is_reused(self) -> None:
        # Given: One OUT IRP is resubmitted with 8 bytes before its stale 16-byte request completes.
        base = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        stale_request = replace(
            base,
            endpoint=0x02,
            urb_type="",
            irp_direction=0,
            requested_length=16,
            data_length=16,
        )
        current_request = replace(stale_request, frame_number=2, requested_length=8, data_length=8)
        completion = replace(current_request, frame_number=3, irp_direction=1, data_length=0)
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="8" * 64,
        )
        # When: Both requests and the current request's completion are aggregated.
        for row in (stale_request, current_request, completion):
            accumulator.consume(row)
        summary = accumulator.finish()
        # Then: Only the effective request contributes transfer bytes while reuse remains observable.
        data_length = summary["endpoints"]["0x02_out"]["data_length"]
        self.assertEqual(data_length["count"], 1)
        self.assertEqual(data_length["total_bytes"], 8)
        self.assertEqual(summary["correlation"]["duplicate_active_request"], 1)

    def test_retains_latest_unmatched_out_request_bytes_at_capture_end(self) -> None:
        # Given: A final OUT request with observed bytes but no completion before capture end.
        base = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        request = replace(
            base,
            endpoint=0x02,
            urb_type="",
            irp_direction=0,
            requested_length=16,
            data_length=16,
        )
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="6" * 64,
        )
        # When: The capture is finalized without a matching completion.
        accumulator.consume(request)
        summary = accumulator.finish()
        # Then: The latest request remains in observed byte totals and correlation reports it unmatched.
        data_length = summary["endpoints"]["0x02_out"]["data_length"]
        self.assertEqual(data_length["count"], 1)
        self.assertEqual(data_length["total_bytes"], 16)
        self.assertEqual(summary["correlation"]["unmatched_request"], 1)

    def test_bounds_tail_ep4_non_success_evidence_while_counting_all_completions(self) -> None:
        # Given: Final EP6 is followed by more failed EP4 completions than the evidence limit.
        base = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="5" * 64,
        )
        accumulator.consume(
            replace(base, frame_number=1, endpoint=0x86, urb_type="", irp_direction=1, data_length=65536)
        )
        # When: Ten non-success tail completions are consumed and summarized.
        for frame in range(2, 12):
            accumulator.consume(
                replace(
                    base,
                    frame_number=frame,
                    endpoint=0x84,
                    urb_type="",
                    irp_direction=1,
                    usbd_status=0xC0000001,
                )
            )
        tail = accumulator.finish()["lifecycle"]["stop_drain_graceful_close_observation"]
        # Then: The complete count remains visible while only the first eight status/frame facts are retained.
        self.assertEqual(tail["ep4_non_success_completion_after_final_ep6_count"], 10)
        self.assertEqual(tail["ep4_non_success_completion_evidence_limit"], 8)
        self.assertEqual(
            tail["ep4_non_success_completion_after_final_ep6_evidence"],
            [
                {"frame": frame, "status": "non_success", "usbd_status": "0xc0000001"}
                for frame in range(2, 10)
            ],
        )

    def test_classifies_completion_gap_bucket_boundaries_and_adjacent_values(self) -> None:
        # Given: Zero plus values immediately below, at, and above every 1/10/100/1000 ms boundary.
        cases = (
            ("0", "lte_1ms"),
            ("0.000999", "lte_1ms"),
            ("0.001", "lte_1ms"),
            ("0.001001", "lte_10ms"),
            ("0.009999", "lte_10ms"),
            ("0.010", "lte_10ms"),
            ("0.010001", "lte_100ms"),
            ("0.099999", "lte_100ms"),
            ("0.100", "lte_100ms"),
            ("0.100001", "lte_1000ms"),
            ("0.999999", "lte_1000ms"),
            ("1.000", "lte_1000ms"),
            ("1.000001", "gt_1000ms"),
        )
        # When: Each value is the only gap between two EP6 completions.
        for gap_seconds, expected_bucket in cases:
            with self.subTest(gap_seconds=gap_seconds):
                endpoint = self._summarize_ep6_completion_times(("0", gap_seconds))
                expected_buckets = {
                    "lte_1ms": 0,
                    "lte_10ms": 0,
                    "lte_100ms": 0,
                    "lte_1000ms": 0,
                    "gt_1000ms": 0,
                }
                expected_buckets[expected_bucket] = 1
                # Then: Exactly the inclusive upper-bound bucket is incremented and max preserves the gap.
                self.assertEqual(endpoint["gap_buckets"], expected_buckets)
                self.assertEqual(
                    endpoint["maximum_gap_seconds"],
                    format(Decimal(gap_seconds).quantize(Decimal("0.000000001")), "f"),
                )

    def test_excludes_negative_completion_gap_from_buckets_and_maximum(self) -> None:
        # Given: One positive completion gap followed by a timestamp regression.
        # When: Completion gaps are aggregated in capture order.
        endpoint = self._summarize_ep6_completion_times(("0", "1", "-1"))
        # Then: The regression is counted but does not change the positive maximum or any bucket.
        self.assertEqual(endpoint["negative_gap_count"], 1)
        self.assertEqual(endpoint["maximum_gap_seconds"], "1.000000000")
        self.assertEqual(
            endpoint["gap_buckets"],
            {
                "lte_1ms": 0,
                "lte_10ms": 0,
                "lte_100ms": 0,
                "lte_1000ms": 1,
                "gt_1000ms": 0,
            },
        )

    def test_reports_no_completion_gap_for_single_completion(self) -> None:
        # Given: Exactly one EP6 completion.
        # When: Endpoint timing is finalized.
        endpoint = self._summarize_ep6_completion_times(("0",))
        # Then: No gap, bucket, negative count, or maximum is invented.
        self.assertIsNone(endpoint["maximum_gap_seconds"])
        self.assertEqual(endpoint["negative_gap_count"], 0)
        self.assertEqual(endpoint["gap_buckets"], {key: 0 for key in endpoint["gap_buckets"]})

    def test_rejects_ambiguous_device_discovery(self) -> None:
        # Given: Two devices with identical endpoint coverage and no descriptor tie-breaker.
        base = pa.parse_tshark_record(pa.ROW_FIELDS, valid_cells(), "low.pcapng")
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="b" * 64,
        )
        for device in (10, 11):
            for endpoint in (0x02, 0x84):
                accumulator.consume(replace(base, device_address=device, endpoint=endpoint, irp_id=device * 100 + endpoint))
        accumulator.consume(replace(base, frame_number=2, device_address=11, endpoint=0x84, irp_id=1199))
        # When/Then: The capture cannot silently reuse or guess a device address.
        with self.assertRaisesRegex(pa.AnalyzerError, "low.pcapng: ambiguous USB device discovery"):
            accumulator.finish()

    def test_handles_zero_duration_without_division_by_zero(self) -> None:
        # Given: Two EP6 completions at the same relative timestamp.
        base = pa.parse_tshark_record(
            pa.ROW_FIELDS,
            valid_cells(**{"usb.urb_type": "", "usb.irp_info.direction": "0x01", "usb.data_len": "65536"}),
            "low.pcapng",
        )
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="c" * 64,
        )
        accumulator.consume(base)
        accumulator.consume(replace(base, frame_number=2, irp_id=2))
        # When: The summary is finalized.
        summary = accumulator.finish()
        # Then: Rate is unavailable rather than infinite or an exception.
        self.assertIsNone(summary["endpoints"]["0x86_in"]["rate_bytes_per_second"])

    def test_bounds_unique_length_distribution(self) -> None:
        # Given: More unique EP6 lengths than the configured bounded distribution.
        base = pa.parse_tshark_record(
            pa.ROW_FIELDS,
            valid_cells(**{"usb.urb_type": "", "usb.irp_info.direction": "0x01"}),
            "low.pcapng",
        )
        accumulator = pa.CaptureAccumulator(
            capture_name="low.pcapng",
            source_size_bytes=1,
            source_sha256="d" * 64,
            max_length_buckets=4,
        )
        for index in range(10):
            accumulator.consume(replace(base, frame_number=index + 1, irp_id=index + 1, data_length=index + 1))
        # When: The summary is finalized.
        distribution = accumulator.finish()["endpoints"]["0x86_in"]["data_length"]
        # Then: Exact buckets remain bounded and overflow cardinality is explicit.
        self.assertEqual(len(distribution["counts_by_bytes"]), 4)
        self.assertEqual(distribution["overflow_unique_lengths"], 6)

    def test_marks_four_gib_capture_boundary_without_claiming_device_failure(self) -> None:
        # Given: A high capture near the 4 GiB file boundary with only successful traffic.
        row = pa.parse_tshark_record(
            pa.ROW_FIELDS,
            valid_cells(**{"usb.urb_type": "", "usb.irp_info.direction": "0x01"}),
            "high1.pcapng",
        )
        accumulator = pa.CaptureAccumulator(
            capture_name="high1.pcapng",
            source_size_bytes=4_317_352_764,
            source_sha256="e" * 64,
        )
        accumulator.consume(row)
        # When: Boundary interpretation is produced.
        boundary = accumulator.finish()["capture_boundary"]
        # Then: File boundary and observed device failure remain distinct.
        self.assertTrue(boundary["near_four_gib"])
        self.assertFalse(boundary["observed_device_failure"])

    def test_classifies_four_gib_tolerance_boundaries_and_adjacent_bytes(self) -> None:
        # Given: The exact 4 GiB size plus both inclusive tolerance edges and their adjacent outside bytes.
        four_gib = 2**32
        tolerance = int(Decimal(four_gib) * Decimal("0.05"))
        cases = (
            (four_gib, True),
            (four_gib - tolerance, True),
            (four_gib - tolerance - 1, False),
            (four_gib + tolerance, True),
            (four_gib + tolerance + 1, False),
        )
        row = pa.parse_tshark_record(
            pa.ROW_FIELDS,
            valid_cells(**{"usb.urb_type": "", "usb.irp_info.direction": "0x01"}),
            "high1.pcapng",
        )
        # When: Each capture-file size is interpreted independently from successful USB traffic.
        for size_bytes, expected_near in cases:
            with self.subTest(size_bytes=size_bytes):
                accumulator = pa.CaptureAccumulator(
                    capture_name="high1.pcapng",
                    source_size_bytes=size_bytes,
                    source_sha256="6" * 64,
                )
                accumulator.consume(row)
                boundary = accumulator.finish()["capture_boundary"]
                # Then: The flag and human interpretation use the same inclusive tolerance rule.
                self.assertEqual(boundary["near_four_gib"], expected_near)
                self.assertFalse(boundary["observed_device_failure"])
                self.assertEqual(
                    boundary["interpretation"],
                    (
                        "capture file is near the 4 GiB boundary; this is not device/acquisition failure evidence"
                        if expected_near
                        else "capture file size and observed USB status are reported independently"
                    ),
                )

    def test_records_control_only_connect_and_close_observations(self) -> None:
        # Given: A descriptor/control-only target device such as the idle capture.
        row = pa.parse_tshark_record(
            pa.ROW_FIELDS,
            valid_cells(**{"usb.endpoint_address": "0x00", "usb.urb_type": "", "usb.irp_info.direction": "0x00"}),
            "idle_180_1700.pcapng",
        )
        accumulator = pa.CaptureAccumulator(
            capture_name="idle_180_1700.pcapng",
            source_size_bytes=1,
            source_sha256="f" * 64,
        )
        accumulator.consume(row)
        accumulator.consume(
            replace(
                row,
                frame_number=2,
                relative_seconds="180.0",
                epoch_seconds="1784270499.0",
                irp_direction=1,
            )
        )
        # When: The control-only stream is finalized.
        lifecycle = accumulator.finish()["lifecycle"]
        # Then: Connect/close observations retain frame/time while data endpoints remain absent.
        self.assertEqual(lifecycle["connect_observation"]["first_frame"], 1)
        self.assertEqual(lifecycle["close_observation"]["last_frame"], 2)
        self.assertEqual(lifecycle["close_observation"]["last_relative_seconds"], "180.0")


def stable_json_text(value: object) -> str:
    return pa.stable_json_bytes(value).decode("utf-8")


class CommandAndSerializationTests(unittest.TestCase):
    def test_builds_shell_free_payload_free_tshark_command(self) -> None:
        # Given: Tool and capture paths containing spaces.
        tool = Path("/mnt/c/Program Files/Wireshark/tshark.exe")
        capture = Path("/mnt/d/capture with spaces.pcapng")
        # When: The command is built.
        command = pa.build_tshark_command(tool, capture)
        # Then: It is an argument list and never requests raw payload bytes.
        self.assertIsInstance(command, list)
        self.assertEqual(command[0], str(tool))
        self.assertIn(r"D:\capture with spaces.pcapng", command)
        self.assertNotIn("usb.capdata", command)

    def test_rejects_non_mounted_path_for_windows_tool(self) -> None:
        # Given: A Windows tool and a Linux-only capture path.
        tool = Path("/mnt/c/Program Files/Wireshark/tshark.exe")
        capture = Path("/tmp/linux-only.pcapng")
        # When/Then: Command construction fails before invoking the tool.
        with self.assertRaisesRegex(pa.AnalyzerError, "not accessible to Windows tool"):
            pa.build_tshark_command(tool, capture)

    def test_surfaces_external_tool_exit_status_and_stderr(self) -> None:
        # Given: A tool dependency returning exit 2 and useful stderr.
        failure = subprocess.CompletedProcess(["tool"], 2, stdout="", stderr="bad capture")
        # When/Then: The error includes capture, stage, status, and stderr.
        with mock.patch("pcap_analysis.subprocess.run", return_value=failure), self.assertRaisesRegex(
            pa.AnalyzerError,
            r"low.pcapng: field extraction failed \(exit 2\): bad capture",
        ):
            pa.run_checked(["tool"], stage="field extraction", capture_name="low.pcapng")

    def test_converts_external_tool_launch_oserror_to_analyzer_error(self) -> None:
        # Given: An executable that disappears or cannot be launched at the dependency boundary.
        # When/Then: The exact stage and OS error are surfaced without leaking OSError.
        with mock.patch(
            "pcap_analysis.subprocess.run",
            side_effect=PermissionError("permission denied"),
        ), self.assertRaisesRegex(
            pa.AnalyzerError,
            r"tool: field inventory failed to start: permission denied",
        ):
            pa.run_checked(["tool"], stage="field inventory")

    def test_stream_surfaces_nonzero_exit_after_valid_rows_and_drains_stderr(self) -> None:
        # Given: TShark emits a valid header and row, then exits 2 with multiline stderr.
        header = "\t".join(pa.ROW_FIELDS)
        record = "\t".join(valid_cells())
        process = mock.Mock()
        process.stdout = io.StringIO(f"{header}\n{record}\n")
        process.stderr = io.StringIO("bad capture\npacket 2 malformed\n")
        process.wait.return_value = 2
        # When/Then: Exhausting rows raises the analyzer error with exit status and drained stderr.
        with mock.patch("pcap_analysis.subprocess.Popen", return_value=process), self.assertRaisesRegex(
            pa.AnalyzerError,
            r"low\.pcapng: TShark field extraction failed \(exit 2\): bad capture\npacket 2 malformed",
        ):
            list(pa.stream_tshark_rows(Path("tshark"), Path("low.pcapng")))
        self.assertTrue(process.stdout.closed)
        self.assertTrue(process.stderr.closed)

    def test_stream_cleans_up_process_when_consumer_raises(self) -> None:
        # Given: A running TShark process has yielded one valid row to a consumer.
        header = "\t".join(pa.ROW_FIELDS)
        record = "\t".join(valid_cells())
        process = mock.Mock()
        process.stdout = io.StringIO(f"{header}\n{record}\n")
        process.stderr = io.StringIO("")
        process.poll.return_value = None
        process.wait.return_value = 0
        stderr_thread = mock.Mock()

        def make_thread(*, target: object, **_: object) -> mock.Mock:
            stderr_thread.start.side_effect = target
            return stderr_thread

        with mock.patch("pcap_analysis.subprocess.Popen", return_value=process), mock.patch(
            "pcap_analysis.threading.Thread", side_effect=make_thread
        ):
            rows = pa.stream_tshark_rows(Path("tshark"), Path("low.pcapng"))
            next(rows)
            # When/Then: The consumer exception is preserved after process/thread cleanup.
            with self.assertRaisesRegex(ValueError, "consumer stopped"):
                rows.throw(ValueError("consumer stopped"))
        process.terminate.assert_called_once_with()
        process.wait.assert_called_once_with()
        stderr_thread.join.assert_called_once_with()
        self.assertTrue(process.stdout.closed)
        self.assertTrue(process.stderr.closed)

    def test_stable_json_is_byte_identical_and_ordered(self) -> None:
        # Given: Equivalent data with intentionally unsorted mapping insertion.
        data = {"z": 1, "a": {"d": 4, "b": 2}}
        # When: It is serialized twice.
        first = pa.stable_json_bytes(data)
        second = pa.stable_json_bytes(data)
        # Then: Bytes match and keys use a stable lexical order.
        self.assertEqual(first, second)
        self.assertLess(first.index(b'"a"'), first.index(b'"z"'))

    def test_stable_json_write_failures_remove_temp_and_preserve_destination(self) -> None:
        # Given: An existing destination and failures at each write/flush/fsync/replace boundary.
        real_named_temporary_file = tempfile.NamedTemporaryFile
        for stage in ("write", "flush", "fsync", "replace"):
            with self.subTest(stage=stage), tempfile.TemporaryDirectory() as temp_dir:
                destination = Path(temp_dir) / "result.json"
                destination.write_bytes(b"original\n")

                def failing_named_temporary_file(*args: object, **kwargs: object) -> object:
                    stream = real_named_temporary_file(*args, **kwargs)
                    if stage in {"write", "flush"}:
                        setattr(stream, stage, mock.Mock(side_effect=OSError(f"{stage} failed")))
                    return stream

                named_temp_patch = mock.patch(
                    "pcap_analysis.tempfile.NamedTemporaryFile",
                    side_effect=failing_named_temporary_file,
                )
                fsync_patch = mock.patch(
                    "pcap_analysis.os.fsync",
                    side_effect=OSError("fsync failed") if stage == "fsync" else None,
                )
                replace_patch = mock.patch(
                    "pcap_analysis.os.replace",
                    side_effect=OSError("replace failed") if stage == "replace" else None,
                )
                # When/Then: The original exception survives and no delete=False temp is leaked.
                with named_temp_patch, fsync_patch, replace_patch, self.assertRaisesRegex(OSError, f"{stage} failed"):
                    pa.write_stable_json(destination, {"new": True})
                self.assertEqual(destination.read_bytes(), b"original\n")
                self.assertEqual(list(destination.parent.glob(f".{destination.name}.*")), [])

    def test_parses_quoted_tshark_stream_with_exact_header(self) -> None:
        # Given: A TShark tab stream with an exact header and one quoted record.
        header = "\t".join(pa.ROW_FIELDS)
        record = "\t".join(f'"{value}"' for value in valid_cells())
        stream = io.StringIO(f"{header}\n{record}\n")
        # When: The stream is iterated.
        rows = list(pa.iter_tshark_records(stream, "low.pcapng"))
        # Then: One typed row is returned without buffering the capture.
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0].frame_number, 1)

    def test_rejects_empty_tshark_stream(self) -> None:
        # Given: A dependency that emitted no header or rows.
        # When/Then: Streaming fails with capture/stage context.
        with self.assertRaisesRegex(pa.AnalyzerError, "low.pcapng: TShark produced no header"):
            list(pa.iter_tshark_records(io.StringIO(""), "low.pcapng"))

    def test_verifies_source_identity_and_rejects_hash_mismatch(self) -> None:
        # Given: A source file and manifest entry with its exact size/hash.
        with tempfile.TemporaryDirectory() as temp_dir:
            capture = Path(temp_dir) / "low.pcapng"
            capture.write_bytes(b"capture")
            entry = {
                "filename": "low.pcapng",
                "size_bytes": 7,
                "sha256": "00" * 32,
            }
            entry["sha256"] = pa.sha256_file(capture)
            # When: Identity is verified, then the expected hash is changed by one value.
            verified = pa.verify_capture_identity(capture, entry)
            invalid = {**entry, "sha256": "ff" * 32}
            # Then: The match passes and mismatch fails with exact capture context.
            self.assertEqual(verified["sha256"], entry["sha256"])
            with self.assertRaisesRegex(pa.AnalyzerError, "low.pcapng: source SHA-256 differs from immutable manifest"):
                pa.verify_capture_identity(capture, invalid)

    def test_accepts_only_supported_source_manifest_schema_version(self) -> None:
        # Given: A source manifest using the supported integer schema version 1.
        with tempfile.TemporaryDirectory() as temp_dir:
            manifest_path = Path(temp_dir) / "source_manifest.json"
            pa.write_stable_json(
                manifest_path,
                {
                    "captures": [],
                    "schema": "analogboard.phase0.usbpcap-source-manifest",
                    "schema_version": 1,
                },
            )
            # When: The manifest is loaded.
            manifest = pa.load_source_manifest(manifest_path)
            # Then: Version 1 is accepted unchanged.
            self.assertEqual(manifest["schema_version"], 1)

    def test_rejects_missing_unsupported_and_non_integer_source_manifest_versions(self) -> None:
        # Given: Missing, boundary, future, string, boolean, and NULL schema versions.
        invalid_versions = (
            ("missing", None),
            ("null", None),
            ("zero", 0),
            ("future", 2),
            ("string", "1"),
            ("boolean", True),
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            # When/Then: Every incompatible value fails with expected/actual version context.
            for name, version in invalid_versions:
                with self.subTest(name=name, version=version):
                    manifest_path = root / f"{name}.json"
                    manifest = {
                        "captures": [],
                        "schema": "analogboard.phase0.usbpcap-source-manifest",
                    }
                    if name != "missing":
                        manifest["schema_version"] = version
                    pa.write_stable_json(manifest_path, manifest)
                    with self.assertRaisesRegex(
                        pa.AnalyzerError,
                        r"source manifest has unsupported schema version: .*expected 1, got",
                    ):
                        pa.load_source_manifest(manifest_path)

    def test_extract_rejects_missing_tshark_before_launch(self) -> None:
        # Given: A valid version-1 manifest and a missing TShark executable path.
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "source"
            source.mkdir()
            manifest_path = root / "source_manifest.json"
            pa.write_stable_json(
                manifest_path,
                {
                    "captures": [{"filename": name} for name in pa.CAPTURE_NAMES],
                    "schema": "analogboard.phase0.usbpcap-source-manifest",
                    "schema_version": 1,
                },
            )
            missing_tshark = root / "missing-tshark"
            # When/Then: Extraction rejects the path as AnalyzerError before subprocess launch.
            with self.assertRaisesRegex(
                pa.AnalyzerError,
                rf"TShark executable is required: {re.escape(str(missing_tshark))}",
            ):
                pa.build_extraction_bundle(
                    source_root=source,
                    output_root=root / "output",
                    source_manifest_path=manifest_path,
                    tshark=missing_tshark,
                    capture_names=pa.CAPTURE_NAMES,
                )


if __name__ == "__main__":
    unittest.main()
