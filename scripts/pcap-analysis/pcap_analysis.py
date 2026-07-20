from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
from collections import deque
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Any, Iterable, Sequence

from analysis_model import (
    AnalyzerError,
    CaptureAccumulator,
    CorrelationTracker,
    UsbRow,
    classify_usbd_status,
)

SCRIPT_DIR = Path(__file__).resolve().parent
REPO_ROOT = SCRIPT_DIR.parents[1]
DEFAULT_SOURCE_ROOT = REPO_ROOT / "artifacts/field-session/2026-07-17-characterization"
DEFAULT_OUTPUT_ROOT = DEFAULT_SOURCE_ROOT / "analysis"
DEFAULT_TSHARK = Path("/mnt/c/Program Files/Wireshark/tshark.exe")
DEFAULT_CAPINFOS = Path("/mnt/c/Program Files/Wireshark/capinfos.exe")

CAPTURE_NAMES = (
    "low_mid.pcapng",
    "mid.pcapng",
    "low.pcapng",
    "high1.pcapng",
    "high2.pcapng",
    "idle_180_1700.pcapng",
)

ROW_FIELDS = (
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

REQUIRED_TSHARK_FIELDS = ROW_FIELDS
SOURCE_MANIFEST_SCHEMA = "analogboard.phase0.usbpcap-source-manifest"
SOURCE_MANIFEST_SCHEMA_VERSION = 1


@dataclass(frozen=True)
class FieldInfo:
    name: str
    abbreviation: str
    field_type: str
    protocol: str


@dataclass(frozen=True)
class InterfaceInfo:
    index: int
    name: str
    description: str | None
    encapsulation: str
    capture_length: int | None
    packet_count: int | None


@dataclass(frozen=True)
class CaptureInfo:
    file_type: str
    file_encapsulation: str
    packet_count: int
    file_size_bytes: int
    duration_seconds: str
    earliest_packet_time: str
    latest_packet_time: str
    sha256: str
    interfaces: tuple[InterfaceInfo, ...]


def parse_field_inventory(text: str) -> dict[str, FieldInfo]:
    if text is None:
        raise AnalyzerError("field inventory text is required")
    fields: dict[str, FieldInfo] = {}
    for line in text.replace("\r", "").splitlines():
        parts = line.split("\t")
        if len(parts) < 5 or parts[0] != "F":
            continue
        info = FieldInfo(
            name=parts[1],
            abbreviation=parts[2],
            field_type=parts[3],
            protocol=parts[4],
        )
        fields[info.abbreviation] = info
    return fields


def validate_required_fields(fields: dict[str, FieldInfo] | None) -> None:
    available = fields or {}
    missing = [name for name in REQUIRED_TSHARK_FIELDS if name not in available]
    if missing:
        raise AnalyzerError(f"missing required TShark fields: {', '.join(missing)}")


def _required_property(properties: dict[str, str], name: str, capture_name: str) -> str:
    value = properties.get(name)
    if value is None or not value.strip():
        raise AnalyzerError(f"{capture_name}: Capinfos report missing {name}")
    return value.strip()


def _parse_non_negative_int(value: str, property_name: str, capture_name: str) -> int:
    normalized = value.strip()
    if normalized.lower().endswith(" bytes"):
        normalized = normalized[:-6].strip()
    try:
        parsed = int(normalized, 10)
    except ValueError as error:
        raise AnalyzerError(f"{capture_name}: invalid {property_name}: {value.strip()}") from error
    if parsed < 0:
        raise AnalyzerError(f"{capture_name}: invalid {property_name}: {value.strip()}")
    return parsed


def parse_capinfos(text: str, capture_name: str) -> CaptureInfo:
    if text is None:
        raise AnalyzerError(f"{capture_name}: Capinfos report is required")

    properties: dict[str, str] = {}
    interfaces: list[dict[str, str]] = []
    current_interface: dict[str, str] | None = None
    for raw_line in text.replace("\r", "").splitlines():
        interface_match = re.match(r"^Interface #(\d+) info:\s*$", raw_line)
        if interface_match:
            current_interface = {"index": interface_match.group(1)}
            interfaces.append(current_interface)
            continue
        if current_interface is not None:
            interface_property = re.match(r"^\s+([^=]+?)\s*=\s*(.*?)\s*$", raw_line)
            if interface_property:
                current_interface[interface_property.group(1).strip()] = interface_property.group(2).strip()
                continue
        top_level_property = re.match(r"^([^:]+):\s*(.*?)\s*$", raw_line)
        if top_level_property and current_interface is None:
            properties[top_level_property.group(1).strip()] = top_level_property.group(2).strip()

    packet_count = _parse_non_negative_int(
        _required_property(properties, "Number of packets", capture_name),
        "Number of packets",
        capture_name,
    )
    file_size = _parse_non_negative_int(
        _required_property(properties, "File size", capture_name),
        "File size",
        capture_name,
    )
    duration_raw = _required_property(properties, "Capture duration", capture_name)
    duration = duration_raw.removesuffix(" seconds").strip()
    try:
        if Decimal(duration) < 0:
            raise InvalidOperation
    except InvalidOperation as error:
        raise AnalyzerError(f"{capture_name}: invalid Capture duration: {duration_raw}") from error

    sha256 = _required_property(properties, "SHA256", capture_name).lower()
    if not re.fullmatch(r"[0-9a-f]{64}", sha256):
        raise AnalyzerError(f"{capture_name}: invalid SHA256: {sha256}")

    interface_count = _parse_non_negative_int(
        _required_property(properties, "Number of interfaces in file", capture_name),
        "Number of interfaces in file",
        capture_name,
    )
    if len(interfaces) != interface_count:
        raise AnalyzerError(
            f"{capture_name}: Capinfos interface count mismatch: declared {interface_count}, parsed {len(interfaces)}"
        )

    parsed_interfaces: list[InterfaceInfo] = []
    for values in interfaces:
        index = _parse_non_negative_int(values["index"], "Interface index", capture_name)
        name = values.get("Name", "").strip()
        encapsulation = values.get("Encapsulation", "").strip()
        if not name or not encapsulation:
            raise AnalyzerError(f"{capture_name}: interface #{index} missing Name or Encapsulation")
        capture_length = values.get("Capture length")
        interface_packets = values.get("Number of packets")
        parsed_interfaces.append(
            InterfaceInfo(
                index=index,
                name=name,
                description=values.get("Description"),
                encapsulation=encapsulation,
                capture_length=(
                    _parse_non_negative_int(capture_length, "Capture length", capture_name)
                    if capture_length is not None
                    else None
                ),
                packet_count=(
                    _parse_non_negative_int(interface_packets, "Interface packet count", capture_name)
                    if interface_packets is not None
                    else None
                ),
            )
        )

    return CaptureInfo(
        file_type=_required_property(properties, "File type", capture_name),
        file_encapsulation=_required_property(properties, "File encapsulation", capture_name),
        packet_count=packet_count,
        file_size_bytes=file_size,
        duration_seconds=duration,
        earliest_packet_time=_required_property(properties, "Earliest packet time", capture_name),
        latest_packet_time=_required_property(properties, "Latest packet time", capture_name),
        sha256=sha256,
        interfaces=tuple(sorted(parsed_interfaces, key=lambda item: item.index)),
    )


def validate_capture_file(path: Path) -> Path:
    candidate = Path(path)
    if candidate.is_symlink():
        raise AnalyzerError(f"{candidate.name}: source capture must not be a symlink")
    if not candidate.is_file():
        raise AnalyzerError(f"{candidate.name}: source capture is not a regular file")
    if not os.access(candidate, os.R_OK):
        raise AnalyzerError(f"{candidate.name}: source capture is not readable")
    if candidate.stat().st_size <= 0:
        raise AnalyzerError(f"{candidate.name}: source capture is empty")
    return candidate.resolve()


def validate_source_root(path: Path) -> Path:
    candidate = Path(path)
    if candidate.is_symlink():
        raise AnalyzerError(f"source root must not be a symlink: {candidate}")
    if not candidate.is_dir():
        raise AnalyzerError(f"source root is not a directory: {candidate}")
    return candidate.resolve()


def validate_output_root(source_root: Path, output_root: Path) -> Path:
    source = Path(source_root).resolve()
    output = Path(output_root).resolve()
    if output == source:
        raise AnalyzerError(f"unsafe output root: {output} is the immutable source root")
    try:
        relative = output.relative_to(source)
    except ValueError:
        return output
    if not relative.parts or relative.parts[0] != "analysis":
        raise AnalyzerError(f"unsafe output root: {output} is not the designated analysis directory")
    return output


def sha256_file(path: Path, chunk_size: int = 8 * 1024 * 1024) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(chunk_size), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_checked(args: Sequence[str], *, stage: str, capture_name: str | None = None) -> str:
    subject = capture_name or "tool"
    try:
        completed = subprocess.run(
            list(args),
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
    except OSError as error:
        raise AnalyzerError(f"{subject}: {stage} failed to start: {error}") from error
    if completed.returncode != 0:
        stderr = completed.stderr.strip() or "no stderr"
        raise AnalyzerError(f"{subject}: {stage} failed (exit {completed.returncode}): {stderr}")
    return completed.stdout.replace("\r", "")


def tool_version(tool: Path, label: str) -> str:
    output = run_checked([str(tool), "--version"], stage=f"{label} version")
    first_line = next((line.strip() for line in output.splitlines() if line.strip()), "")
    if not first_line:
        raise AnalyzerError(f"{label}: empty version output")
    return first_line


def validate_tool_file(path: Path, label: str) -> Path:
    resolved = Path(path).resolve()
    if not resolved.is_file():
        raise AnalyzerError(f"{label} executable is required: {resolved}")
    return resolved


def _path_for_tool(tool: Path, path: Path) -> str:
    resolved = Path(path).resolve().as_posix()
    if Path(tool).suffix.lower() != ".exe":
        return resolved
    match = re.fullmatch(r"/mnt/([A-Za-z])/(.+)", resolved)
    if not match:
        raise AnalyzerError(f"{resolved}: path is not accessible to Windows tool {Path(tool).name}")
    drive, relative = match.groups()
    windows_relative = relative.replace("/", "\\")
    return f"{drive.upper()}:\\{windows_relative}"


def build_capinfos_command(capinfos: Path, capture: Path) -> list[str]:
    return [
        str(capinfos),
        "-C",
        "-M",
        "-t",
        "-E",
        "-I",
        "-c",
        "-s",
        "-u",
        "-a",
        "-e",
        "-H",
        _path_for_tool(capinfos, capture),
    ]


def build_tshark_command(tshark: Path, capture: Path) -> list[str]:
    command = [
        str(tshark),
        "-n",
        "-r",
        _path_for_tool(tshark, capture),
        "-T",
        "fields",
        "-E",
        "header=y",
        "-E",
        "separator=/t",
        "-E",
        "quote=d",
        "-E",
        "occurrence=f",
    ]
    for field in ROW_FIELDS:
        command.extend(("-e", field))
    return command


def _optional_int(value: str, field_name: str, capture_name: str, *, minimum: int = 0) -> int | None:
    normalized = value.strip()
    if not normalized:
        return None
    try:
        base = 16 if normalized.lower().startswith("0x") else 10
        parsed = int(normalized, base)
    except ValueError as error:
        raise AnalyzerError(f"{capture_name}: invalid {field_name}: {value}") from error
    if parsed < minimum:
        raise AnalyzerError(f"{capture_name}: invalid {field_name}: {value}")
    return parsed


def _required_int(value: str, field_name: str, capture_name: str, *, minimum: int = 0) -> int:
    parsed = _optional_int(value, field_name, capture_name, minimum=minimum)
    if parsed is None:
        raise AnalyzerError(f"{capture_name}: invalid {field_name}: empty")
    return parsed


def _required_decimal(value: str, field_name: str, capture_name: str) -> str:
    normalized = value.strip()
    try:
        parsed = Decimal(normalized)
    except InvalidOperation as error:
        raise AnalyzerError(f"{capture_name}: invalid {field_name}: {value or 'empty'}") from error
    if parsed < 0:
        raise AnalyzerError(f"{capture_name}: invalid {field_name}: {value}")
    return normalized


def parse_tshark_record(headers: Sequence[str], cells: Sequence[str], capture_name: str) -> UsbRow:
    if len(headers) != len(set(headers)):
        raise AnalyzerError(f"{capture_name}: malformed TShark header: duplicate fields")
    if tuple(headers) != ROW_FIELDS:
        raise AnalyzerError(f"{capture_name}: malformed TShark header: unexpected field order")
    if len(cells) != len(headers):
        raise AnalyzerError(
            f"{capture_name}: malformed TShark row: expected {len(headers)} columns, got {len(cells)}"
        )
    values = dict(zip(headers, cells, strict=True))
    return UsbRow(
        frame_number=_required_int(values["frame.number"], "frame.number", capture_name, minimum=1),
        epoch_seconds=_required_decimal(values["frame.time_epoch"], "frame.time_epoch", capture_name),
        relative_seconds=_required_decimal(values["frame.time_relative"], "frame.time_relative", capture_name),
        captured_length=_required_int(values["frame.cap_len"], "frame.cap_len", capture_name),
        reported_length=_required_int(values["frame.len"], "frame.len", capture_name),
        bus_id=_optional_int(values["usb.bus_id"], "usb.bus_id", capture_name),
        device_address=_optional_int(values["usb.device_address"], "usb.device_address", capture_name),
        irp_id=_optional_int(values["usb.irp_id"], "usb.irp_id", capture_name),
        urb_type=values["usb.urb_type"],
        irp_direction=_optional_int(values["usb.irp_info.direction"], "usb.irp_info.direction", capture_name),
        urb_function=_optional_int(values["usb.function"], "usb.function", capture_name),
        transfer_type=_optional_int(values["usb.transfer_type"], "usb.transfer_type", capture_name),
        endpoint=_optional_int(values["usb.endpoint_address"], "usb.endpoint_address", capture_name),
        usbd_status=_optional_int(values["usb.usbd_status"], "usb.usbd_status", capture_name),
        nt_status=None,
        requested_length=_optional_int(values["usb.urb_len"], "usb.urb_len", capture_name),
        data_length=_optional_int(values["usb.data_len"], "usb.data_len", capture_name),
        vendor_id=_optional_int(values["usb.idVendor"], "usb.idVendor", capture_name),
        product_id=_optional_int(values["usb.idProduct"], "usb.idProduct", capture_name),
        interface_number=_optional_int(values["usb.bInterfaceNumber"], "usb.bInterfaceNumber", capture_name),
    )


def iter_tshark_records(lines: Iterable[str], capture_name: str) -> Iterable[UsbRow]:
    reader = csv.reader(lines, delimiter="\t", quotechar='"')
    try:
        headers = next(reader)
    except StopIteration as error:
        raise AnalyzerError(f"{capture_name}: TShark produced no header") from error
    if tuple(headers) != ROW_FIELDS:
        raise AnalyzerError(f"{capture_name}: malformed TShark header: unexpected field order")
    for cells in reader:
        yield parse_tshark_record(headers, cells, capture_name)


def stream_tshark_rows(tshark: Path, capture: Path) -> Iterable[UsbRow]:
    capture_name = Path(capture).name
    command = build_tshark_command(tshark, capture)
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        bufsize=1,
    )
    if process.stdout is None or process.stderr is None:
        process.terminate()
        raise AnalyzerError(f"{capture_name}: TShark field extraction failed to open pipes")
    stderr_lines: deque[str] = deque(maxlen=100)

    def drain_stderr() -> None:
        for line in process.stderr:
            stderr_lines.append(line.rstrip())

    stderr_thread = threading.Thread(target=drain_stderr, name=f"tshark-stderr-{capture_name}", daemon=True)
    stderr_thread.start()
    try:
        yield from iter_tshark_records(process.stdout, capture_name)
    except BaseException:
        if process.poll() is None:
            process.terminate()
        process.wait()
        stderr_thread.join()
        process.stderr.close()
        raise
    finally:
        process.stdout.close()
    return_code = process.wait()
    stderr_thread.join()
    process.stderr.close()
    if return_code != 0:
        stderr = "\n".join(stderr_lines).strip() or "no stderr"
        raise AnalyzerError(f"{capture_name}: TShark field extraction failed (exit {return_code}): {stderr}")


def verify_capture_identity(capture: Path, manifest_entry: dict[str, Any]) -> dict[str, Any]:
    capture_path = validate_capture_file(capture)
    capture_name = capture_path.name
    if manifest_entry.get("filename") != capture_name:
        raise AnalyzerError(f"{capture_name}: source filename differs from immutable manifest")
    expected_size = manifest_entry.get("size_bytes")
    if not isinstance(expected_size, int) or expected_size <= 0:
        raise AnalyzerError(f"{capture_name}: immutable manifest has invalid size")
    actual_size = capture_path.stat().st_size
    if actual_size != expected_size:
        raise AnalyzerError(f"{capture_name}: source size differs from immutable manifest")
    expected_sha256 = manifest_entry.get("sha256")
    if not isinstance(expected_sha256, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_sha256):
        raise AnalyzerError(f"{capture_name}: immutable manifest has invalid SHA-256")
    actual_sha256 = sha256_file(capture_path)
    if actual_sha256 != expected_sha256:
        raise AnalyzerError(f"{capture_name}: source SHA-256 differs from immutable manifest")
    return {"sha256": actual_sha256, "size_bytes": actual_size}


def load_source_manifest(path: Path) -> dict[str, Any]:
    manifest_path = Path(path)
    try:
        value = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise AnalyzerError(f"source manifest unreadable: {manifest_path}: {error}") from error
    if not isinstance(value, dict) or value.get("schema") != SOURCE_MANIFEST_SCHEMA:
        raise AnalyzerError(f"source manifest has unexpected schema: {manifest_path}")
    schema_version = value.get("schema_version")
    if type(schema_version) is not int or schema_version != SOURCE_MANIFEST_SCHEMA_VERSION:
        raise AnalyzerError(
            "source manifest has unsupported schema version: "
            f"{manifest_path}: expected {SOURCE_MANIFEST_SCHEMA_VERSION}, got {schema_version!r}"
        )
    captures = value.get("captures")
    if not isinstance(captures, list):
        raise AnalyzerError(f"source manifest has invalid captures: {manifest_path}")
    return value


def _capture_entries(manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
    entries: dict[str, dict[str, Any]] = {}
    for value in manifest["captures"]:
        if not isinstance(value, dict) or not isinstance(value.get("filename"), str):
            raise AnalyzerError("source manifest contains an invalid capture entry")
        name = value["filename"]
        if name in entries:
            raise AnalyzerError(f"source manifest contains duplicate capture entry: {name}")
        entries[name] = value
    missing = [name for name in CAPTURE_NAMES if name not in entries]
    if missing:
        raise AnalyzerError(f"source manifest missing captures: {', '.join(missing)}")
    return entries


def extract_capture(
    *,
    tshark: Path,
    capture: Path,
    manifest_entry: dict[str, Any],
) -> dict[str, Any]:
    identity_before = verify_capture_identity(capture, manifest_entry)
    accumulator = CaptureAccumulator(
        capture_name=Path(capture).name,
        source_size_bytes=identity_before["size_bytes"],
        source_sha256=identity_before["sha256"],
    )
    row_count = 0
    for row in stream_tshark_rows(tshark, capture):
        accumulator.consume(row)
        row_count += 1
    expected_packets = manifest_entry.get("packet_count")
    if not isinstance(expected_packets, int) or row_count != expected_packets:
        raise AnalyzerError(
            f"{Path(capture).name}: TShark row count {row_count} differs from manifest packet count {expected_packets}"
        )
    identity_after = verify_capture_identity(capture, manifest_entry)
    if identity_after != identity_before:
        raise AnalyzerError(f"{Path(capture).name}: source identity changed during extraction")
    summary = accumulator.finish()
    summary["tshark_row_count"] = row_count
    return summary


def build_extraction_bundle(
    *,
    source_root: Path,
    output_root: Path,
    source_manifest_path: Path,
    tshark: Path,
    capture_names: Sequence[str],
) -> dict[str, Any]:
    source = validate_source_root(source_root)
    output = validate_output_root(source, output_root)
    manifest_path = Path(source_manifest_path).resolve()
    manifest = load_source_manifest(manifest_path)
    entries = _capture_entries(manifest)
    requested = set(capture_names)
    unknown = sorted(requested.difference(CAPTURE_NAMES))
    if unknown:
        raise AnalyzerError(f"unknown capture names: {', '.join(unknown)}")
    if len(requested) != len(capture_names):
        raise AnalyzerError("capture names must not contain duplicates")

    tshark_path = validate_tool_file(tshark, "TShark")
    current_version = tool_version(tshark_path, "TShark")
    expected_version = manifest.get("tools", {}).get("tshark", {}).get("version")
    if current_version != expected_version:
        raise AnalyzerError(f"TShark version differs from immutable manifest: {current_version} != {expected_version}")
    fields = parse_field_inventory(run_checked([str(tshark_path), "-G", "fields"], stage="field inventory"))
    validate_required_fields(fields)

    summaries: list[dict[str, Any]] = []
    for capture_name in CAPTURE_NAMES:
        if capture_name not in requested:
            continue
        summary = extract_capture(
            tshark=tshark_path,
            capture=source / capture_name,
            manifest_entry=entries[capture_name],
        )
        summaries.append(summary)
        write_stable_json(output / f"{Path(capture_name).stem}.summary.json", summary)

    bundle = {
        "capture_summaries": summaries,
        "field_inventory": [name for name in REQUIRED_TSHARK_FIELDS],
        "provisional": True,
        "schema": "analogboard.phase0.usbpcap-extraction-bundle",
        "schema_version": 1,
        "source_manifest_sha256": sha256_file(manifest_path),
        "tool": {"name": tshark_path.name, "version": current_version},
    }
    write_stable_json(output / "bounded_summary.json", bundle)
    return bundle


def stable_json_bytes(value: Any) -> bytes:
    text = json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False)
    return (text + "\n").encode("utf-8")


def write_stable_json(path: Path, value: Any) -> None:
    destination = Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    data = stable_json_bytes(value)
    temporary: Path | None = None
    try:
        with tempfile.NamedTemporaryFile(
            dir=destination.parent,
            prefix=f".{destination.name}.",
            delete=False,
        ) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
    except BaseException:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
        raise


def _portable_path(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(REPO_ROOT).as_posix()
    except ValueError:
        return f"external/{resolved.name}"


def build_source_manifest(
    *,
    source_root: Path,
    tshark: Path,
    capinfos: Path,
) -> dict[str, Any]:
    source = validate_source_root(source_root)
    tshark_path = validate_tool_file(tshark, "TShark")
    capinfos_path = validate_tool_file(capinfos, "Capinfos")
    if tshark_path.parent != capinfos_path.parent:
        raise AnalyzerError("TShark and Capinfos must come from the same install root")

    tshark_version = tool_version(tshark_path, "TShark")
    capinfos_version = tool_version(capinfos_path, "Capinfos")
    inventory_text = run_checked([str(tshark_path), "-G", "fields"], stage="field inventory")
    fields = parse_field_inventory(inventory_text)
    validate_required_fields(fields)

    captures: list[dict[str, Any]] = []
    for capture_name in CAPTURE_NAMES:
        capture_path = validate_capture_file(source / capture_name)
        size = capture_path.stat().st_size
        digest = sha256_file(capture_path)
        report = run_checked(
            build_capinfos_command(capinfos_path, capture_path),
            stage="Capinfos readability",
            capture_name=capture_name,
        )
        info = parse_capinfos(report, capture_name)
        if info.file_size_bytes != size:
            raise AnalyzerError(
                f"{capture_name}: source size changed or Capinfos disagrees: filesystem {size}, Capinfos {info.file_size_bytes}"
            )
        if info.sha256 != digest:
            raise AnalyzerError(
                f"{capture_name}: source SHA-256 changed or Capinfos disagrees: streamed {digest}, Capinfos {info.sha256}"
            )
        captures.append(
            {
                "duration_seconds": info.duration_seconds,
                "earliest_packet_time": info.earliest_packet_time,
                "file_encapsulation": info.file_encapsulation,
                "file_type": info.file_type,
                "filename": capture_name,
                "interfaces": [
                    {
                        "capture_length": interface.capture_length,
                        "description": interface.description,
                        "encapsulation": interface.encapsulation,
                        "index": interface.index,
                        "name": interface.name,
                        "packet_count": interface.packet_count,
                    }
                    for interface in info.interfaces
                ],
                "latest_packet_time": info.latest_packet_time,
                "packet_count": info.packet_count,
                "sha256": digest,
                "size_bytes": size,
            }
        )

    return {
        "captures": captures,
        "field_inventory": [
            {
                "abbreviation": fields[name].abbreviation,
                "field_type": fields[name].field_type,
                "name": fields[name].name,
                "protocol": fields[name].protocol,
            }
            for name in REQUIRED_TSHARK_FIELDS
        ],
        "provisional": True,
        "schema": SOURCE_MANIFEST_SCHEMA,
        "schema_version": SOURCE_MANIFEST_SCHEMA_VERSION,
        "source_root": _portable_path(source),
        "timestamp_basis": "pcapng packet timestamps rendered by Capinfos; timezone is not declared by the report",
        "tools": {
            "capinfos": {"path": capinfos_path.name, "version": capinfos_version},
            "tshark": {"path": tshark_path.name, "version": tshark_version},
        },
    }


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Stream and summarize the Phase 0 USBPcap corpus")
    subparsers = parser.add_subparsers(dest="command", required=True)
    manifest = subparsers.add_parser("manifest", help="validate tools and generate the immutable source manifest")
    manifest.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE_ROOT)
    manifest.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    manifest.add_argument("--tshark", type=Path, default=DEFAULT_TSHARK)
    manifest.add_argument("--capinfos", type=Path, default=DEFAULT_CAPINFOS)
    extract = subparsers.add_parser("extract", help="stream captures into payload-free bounded summaries")
    extract.add_argument("--source-root", type=Path, default=DEFAULT_SOURCE_ROOT)
    extract.add_argument("--output-root", type=Path, default=DEFAULT_OUTPUT_ROOT)
    extract.add_argument("--source-manifest", type=Path, default=DEFAULT_OUTPUT_ROOT / "source_manifest.json")
    extract.add_argument("--tshark", type=Path, default=DEFAULT_TSHARK)
    extract.add_argument("--captures", nargs="+", default=list(CAPTURE_NAMES))
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = _build_parser().parse_args(argv)
    try:
        if args.command == "manifest":
            output_root = validate_output_root(args.source_root, args.output_root)
            manifest = build_source_manifest(
                source_root=args.source_root,
                tshark=args.tshark,
                capinfos=args.capinfos,
            )
            output_path = output_root / "source_manifest.json"
            write_stable_json(output_path, manifest)
            print(output_path)
            return 0
        if args.command == "extract":
            build_extraction_bundle(
                source_root=args.source_root,
                output_root=args.output_root,
                source_manifest_path=args.source_manifest,
                tshark=args.tshark,
                capture_names=args.captures,
            )
            output_path = validate_output_root(args.source_root, args.output_root) / "bounded_summary.json"
            print(output_path)
            return 0
        raise AnalyzerError(f"unsupported command: {args.command}")
    except AnalyzerError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
