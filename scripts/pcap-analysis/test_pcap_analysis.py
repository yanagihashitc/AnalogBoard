from __future__ import annotations

import os
import io
import subprocess
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest import mock

SCRIPT_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPT_DIR))

import pcap_analysis as pa  # noqa: E402


def field_inventory(*, missing: str | None = None) -> str:
    lines = []
    for name in pa.REQUIRED_TSHARK_FIELDS:
        if name != missing:
            lines.append(f"F\t{name}\t{name}\tFT_STRING\tprotocol\t\t0x0\t")
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
        # Given: A complete synthetic TShark field inventory.
        text = field_inventory()
        # When: The inventory is parsed and validated.
        fields = pa.parse_field_inventory(text)
        pa.validate_required_fields(fields)
        # Then: Required fields are retained in deterministic order.
        self.assertEqual(tuple(fields[name].abbreviation for name in pa.REQUIRED_TSHARK_FIELDS), pa.REQUIRED_TSHARK_FIELDS)

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
        self.assertEqual(summary["endpoints"]["0x84_in"]["completion_count"], 1)
        self.assertEqual(summary["endpoints"]["0x86_in"]["data_length"]["total_bytes"], 65536)
        self.assertEqual(summary["lifecycle"]["ep2_set_candidate"]["first_frame"], 10)
        self.assertEqual(summary["lifecycle"]["ep6_bulk"]["first_frame"], 30)
        self.assertNotIn('"payload":', stable_json_text(summary).lower())

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

    def test_stable_json_is_byte_identical_and_ordered(self) -> None:
        # Given: Equivalent data with intentionally unsorted mapping insertion.
        data = {"z": 1, "a": {"d": 4, "b": 2}}
        # When: It is serialized twice.
        first = pa.stable_json_bytes(data)
        second = pa.stable_json_bytes(data)
        # Then: Bytes match and keys use a stable lexical order.
        self.assertEqual(first, second)
        self.assertLess(first.index(b'"a"'), first.index(b'"z"'))

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


if __name__ == "__main__":
    unittest.main()
