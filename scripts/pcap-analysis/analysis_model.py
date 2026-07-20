from __future__ import annotations

import re
from collections import deque
from dataclasses import dataclass
from decimal import Decimal
from typing import Any


class AnalyzerError(RuntimeError):
    """A deterministic, user-actionable analyzer failure."""


@dataclass(frozen=True)
class UsbRow:
    frame_number: int
    epoch_seconds: str
    relative_seconds: str
    captured_length: int
    reported_length: int
    bus_id: int | None
    device_address: int | None
    irp_id: int | None
    urb_type: str
    irp_direction: int | None
    urb_function: int | None
    transfer_type: int | None
    endpoint: int | None
    usbd_status: int | None
    nt_status: int | None
    requested_length: int | None
    data_length: int | None
    vendor_id: int | None
    product_id: int | None
    interface_number: int | None

    @property
    def is_truncated(self) -> bool:
        return self.captured_length < self.reported_length

    @property
    def event_kind(self) -> str:
        normalized = self.urb_type.strip().upper()
        if normalized in {"S", "SUBMIT", "0X53", "83"}:
            return "request"
        if normalized in {"C", "COMPLETE", "0X43", "67"}:
            return "completion"
        if not normalized and self.irp_direction == 0:
            return "request"
        if not normalized and self.irp_direction == 1:
            return "completion"
        return "unknown"


def classify_usbd_status(status: int | None) -> str:
    if status is None:
        return "unknown"
    if status == 0:
        return "success"
    if status == 0xC0010000:
        return "cancelled"
    return "non_success"


class CorrelationTracker:
    def __init__(self, *, recent_limit: int = 4096) -> None:
        if recent_limit <= 0:
            raise AnalyzerError("recent completion limit must be positive")
        self._pending: dict[int, UsbRow] = {}
        self._recent_order: deque[int] = deque()
        self._recent_set: set[int] = set()
        self._recent_limit = recent_limit
        self._counts = {
            "correlated": 0,
            "duplicate_active_request": 0,
            "duplicate_recent_completion": 0,
            "unmatched_completion": 0,
            "unknown_event": 0,
            "short_transfer": 0,
            "short_transfer_evaluable": 0,
            "short_transfer_unknown": 0,
            "truncated": 0,
        }
        self._status_counts = {"success": 0, "cancelled": 0, "non_success": 0, "unknown": 0}
        self._evidence_frames: dict[str, list[int]] = {
            "cancelled_status": [],
            "duplicate_active_request": [],
            "duplicate_recent_completion": [],
            "non_success_status": [],
            "short_transfer": [],
            "truncated": [],
            "unknown_event": [],
            "unknown_status": [],
            "unmatched_completion": [],
        }

    def _record_evidence(self, category: str, frame_number: int) -> None:
        frames = self._evidence_frames[category]
        if len(frames) < 8:
            frames.append(frame_number)

    def _remember_completion(self, irp_id: int) -> None:
        if irp_id in self._recent_set:
            return
        self._recent_order.append(irp_id)
        self._recent_set.add(irp_id)
        if len(self._recent_order) > self._recent_limit:
            expired = self._recent_order.popleft()
            self._recent_set.remove(expired)

    def consume(self, row: UsbRow) -> None:
        status = classify_usbd_status(row.usbd_status)
        self._status_counts[status] += 1
        if status != "success":
            self._record_evidence(f"{status}_status", row.frame_number)
        if row.is_truncated:
            self._counts["truncated"] += 1
            self._record_evidence("truncated", row.frame_number)
        if row.event_kind == "request":
            if row.irp_id is None:
                self._counts["unknown_event"] += 1
                self._record_evidence("unknown_event", row.frame_number)
            elif row.irp_id in self._pending:
                self._counts["duplicate_active_request"] += 1
                self._record_evidence("duplicate_active_request", row.frame_number)
                self._pending[row.irp_id] = row
            else:
                self._pending[row.irp_id] = row
            return
        if row.event_kind == "completion":
            if row.irp_id is None:
                self._counts["unknown_event"] += 1
                self._record_evidence("unknown_event", row.frame_number)
                return
            request = self._pending.pop(row.irp_id, None)
            if request is not None:
                self._counts["correlated"] += 1
                actual_length = row.data_length if row.data_length is not None else row.requested_length
                if request.requested_length is None or actual_length is None:
                    self._counts["short_transfer_unknown"] += 1
                else:
                    self._counts["short_transfer_evaluable"] += 1
                if request.requested_length is not None and actual_length is not None and actual_length < request.requested_length:
                    self._counts["short_transfer"] += 1
                    self._record_evidence("short_transfer", row.frame_number)
            elif row.irp_id in self._recent_set:
                self._counts["duplicate_recent_completion"] += 1
                self._record_evidence("duplicate_recent_completion", row.frame_number)
            else:
                self._counts["unmatched_completion"] += 1
                self._record_evidence("unmatched_completion", row.frame_number)
            self._remember_completion(row.irp_id)
            return
        self._counts["unknown_event"] += 1
        self._record_evidence("unknown_event", row.frame_number)

    def finish(self) -> dict[str, Any]:
        unmatched_request_frames = sorted(row.frame_number for row in self._pending.values())[:8]
        return {
            **self._counts,
            "evidence_frames": {
                **{key: list(value) for key, value in self._evidence_frames.items()},
                "unmatched_request": unmatched_request_frames,
            },
            "unmatched_request": len(self._pending),
            "status_counts": dict(self._status_counts),
            "recent_completion_window": self._recent_limit,
        }
EXPECTED_ENDPOINTS = (0x02, 0x84, 0x86)
ENDPOINT_LABELS = {0x02: "0x02_out", 0x84: "0x84_in", 0x86: "0x86_in"}
TAIL_EVIDENCE_LIMIT = 8


def _format_decimal(value: Decimal, places: int = 6) -> str:
    quantum = Decimal(1).scaleb(-places)
    return format(value.quantize(quantum), "f")


def _data_length_basis(endpoint: int) -> dict[str, str]:
    endpoint_direction = "in" if endpoint & 0x80 else "out"
    return {
        "endpoint_direction": endpoint_direction,
        "row_event_kind": "completion" if endpoint_direction == "in" else "request",
        "source_field": "usb.data_len",
    }


class _LengthDistribution:
    def __init__(self, *, max_buckets: int, max_overflow_values: int = 1024) -> None:
        if max_buckets <= 0 or max_overflow_values <= 0:
            raise AnalyzerError("length distribution bounds must be positive")
        self._max_buckets = max_buckets
        self._max_overflow_values = max_overflow_values
        self._counts: dict[int, int] = {}
        self._overflow_values: set[int] = set()
        self._overflow_observations = 0
        self._overflow_saturated = False
        self.count = 0
        self.total_bytes = 0
        self.minimum: int | None = None
        self.maximum: int | None = None

    def consume(self, length: int) -> None:
        self.count += 1
        self.total_bytes += length
        self.minimum = length if self.minimum is None else min(self.minimum, length)
        self.maximum = length if self.maximum is None else max(self.maximum, length)
        if length in self._counts:
            self._counts[length] += 1
            return
        if len(self._counts) < self._max_buckets:
            self._counts[length] = 1
            return
        self._overflow_observations += 1
        if len(self._overflow_values) < self._max_overflow_values:
            self._overflow_values.add(length)
        elif length not in self._overflow_values:
            self._overflow_saturated = True

    def finish(self) -> dict[str, Any]:
        return {
            "count": self.count,
            "counts_by_bytes": {str(key): self._counts[key] for key in sorted(self._counts)},
            "maximum_bytes": self.maximum,
            "minimum_bytes": self.minimum,
            "overflow_observations": self._overflow_observations,
            "overflow_unique_lengths": len(self._overflow_values),
            "overflow_unique_lengths_saturated": self._overflow_saturated,
            "total_bytes": self.total_bytes,
        }


class _EndpointAccumulator:
    def __init__(self, *, data_length_basis: dict[str, str], max_length_buckets: int) -> None:
        self.request_count = 0
        self.completion_count = 0
        self.unknown_event_count = 0
        self.truncated_count = 0
        self.status_counts = {"success": 0, "cancelled": 0, "non_success": 0, "unknown": 0}
        self.data_lengths = _LengthDistribution(max_buckets=max_length_buckets)
        self._data_length_basis = dict(data_length_basis)
        self.first_frame: int | None = None
        self.last_frame: int | None = None
        self.first_relative: Decimal | None = None
        self.last_relative: Decimal | None = None
        self._last_completion_relative: Decimal | None = None
        self.maximum_gap_seconds: Decimal | None = None
        self.negative_gap_count = 0
        self.gap_buckets = {
            "lte_1ms": 0,
            "lte_10ms": 0,
            "lte_100ms": 0,
            "lte_1000ms": 0,
            "gt_1000ms": 0,
        }

    def consume(self, row: UsbRow) -> None:
        relative = Decimal(row.relative_seconds)
        if self.first_frame is None:
            self.first_frame = row.frame_number
            self.first_relative = relative
        self.last_frame = row.frame_number
        self.last_relative = relative
        if row.event_kind == "request":
            self.request_count += 1
        elif row.event_kind == "completion":
            self.completion_count += 1
        else:
            self.unknown_event_count += 1
        self.status_counts[classify_usbd_status(row.usbd_status)] += 1
        if row.is_truncated:
            self.truncated_count += 1
        if row.event_kind == self._data_length_basis["row_event_kind"] and row.data_length is not None:
            self.data_lengths.consume(row.data_length)
        if row.event_kind != "completion":
            return
        if self._last_completion_relative is not None:
            gap = relative - self._last_completion_relative
            if gap < 0:
                self.negative_gap_count += 1
            else:
                self.maximum_gap_seconds = gap if self.maximum_gap_seconds is None else max(self.maximum_gap_seconds, gap)
                milliseconds = gap * 1000
                if milliseconds <= 1:
                    self.gap_buckets["lte_1ms"] += 1
                elif milliseconds <= 10:
                    self.gap_buckets["lte_10ms"] += 1
                elif milliseconds <= 100:
                    self.gap_buckets["lte_100ms"] += 1
                elif milliseconds <= 1000:
                    self.gap_buckets["lte_1000ms"] += 1
                else:
                    self.gap_buckets["gt_1000ms"] += 1
        self._last_completion_relative = relative

    def finish(self) -> dict[str, Any]:
        completion_duration: Decimal | None = None
        if self.first_relative is not None and self.last_relative is not None:
            completion_duration = self.last_relative - self.first_relative
        rate: str | None = None
        if completion_duration is not None and completion_duration > 0:
            rate = _format_decimal(Decimal(self.data_lengths.total_bytes) / completion_duration)
        return {
            "completion_count": self.completion_count,
            "data_length": self.data_lengths.finish(),
            "data_length_basis": dict(self._data_length_basis),
            "first_frame": self.first_frame,
            "first_relative_seconds": str(self.first_relative) if self.first_relative is not None else None,
            "gap_buckets": dict(self.gap_buckets),
            "last_frame": self.last_frame,
            "last_relative_seconds": str(self.last_relative) if self.last_relative is not None else None,
            "maximum_gap_seconds": (
                _format_decimal(self.maximum_gap_seconds, places=9) if self.maximum_gap_seconds is not None else None
            ),
            "negative_gap_count": self.negative_gap_count,
            "rate_bytes_per_second": rate,
            "request_count": self.request_count,
            "status_counts": dict(self.status_counts),
            "truncated_count": self.truncated_count,
            "unknown_event_count": self.unknown_event_count,
        }


class _DeviceAccumulator:
    def __init__(self, *, bus_id: int, device_address: int, max_length_buckets: int) -> None:
        self.bus_id = bus_id
        self.device_address = device_address
        self.endpoints: dict[int, _EndpointAccumulator] = {}
        self.correlation = CorrelationTracker()
        self.vendor_ids: set[int] = set()
        self.product_ids: set[int] = set()
        self.interface_numbers: set[int] = set()
        self.first_frame: int | None = None
        self.last_frame: int | None = None
        self.first_epoch_seconds: str | None = None
        self.last_epoch_seconds: str | None = None
        self.first_relative_seconds: str | None = None
        self.last_relative_seconds: str | None = None
        self.descriptor_frames: list[int] = []
        self.ep2_set_completion_frame: int | None = None
        self.ep4_completion_after_set_frame: int | None = None
        self.ep6_completion_after_set_frame: int | None = None
        self.last_ep6_completion_frame: int | None = None
        self.ep4_first_completion_after_final_ep6_frame: int | None = None
        self.ep4_successful_completion_after_final_ep6_frame: int | None = None
        self.ep4_non_success_completion_after_final_ep6_count = 0
        self.ep4_non_success_completion_after_final_ep6_evidence: list[dict[str, Any]] = []
        self._max_length_buckets = max_length_buckets

    def consume(self, row: UsbRow) -> None:
        if self.first_frame is None:
            self.first_frame = row.frame_number
            self.first_epoch_seconds = row.epoch_seconds
            self.first_relative_seconds = row.relative_seconds
        self.last_frame = row.frame_number
        self.last_epoch_seconds = row.epoch_seconds
        self.last_relative_seconds = row.relative_seconds
        if row.vendor_id is not None:
            self.vendor_ids.add(row.vendor_id)
            if len(self.descriptor_frames) < 8:
                self.descriptor_frames.append(row.frame_number)
        if row.product_id is not None:
            self.product_ids.add(row.product_id)
        if row.interface_number is not None:
            self.interface_numbers.add(row.interface_number)
        if row.endpoint is not None:
            endpoint = self.endpoints.setdefault(
                row.endpoint,
                _EndpointAccumulator(
                    data_length_basis=_data_length_basis(row.endpoint),
                    max_length_buckets=self._max_length_buckets,
                ),
            )
            endpoint.consume(row)
        if row.event_kind == "completion":
            if row.endpoint == 0x02 and self.ep2_set_completion_frame is None:
                self.ep2_set_completion_frame = row.frame_number
            elif row.endpoint == 0x84:
                if self.ep2_set_completion_frame is not None and self.ep4_completion_after_set_frame is None:
                    self.ep4_completion_after_set_frame = row.frame_number
                if self.last_ep6_completion_frame is not None:
                    self.ep4_first_completion_after_final_ep6_frame = (
                        self.ep4_first_completion_after_final_ep6_frame or row.frame_number
                    )
                    status = classify_usbd_status(row.usbd_status)
                    if status == "success":
                        self.ep4_successful_completion_after_final_ep6_frame = (
                            self.ep4_successful_completion_after_final_ep6_frame or row.frame_number
                        )
                    else:
                        self.ep4_non_success_completion_after_final_ep6_count += 1
                        if len(self.ep4_non_success_completion_after_final_ep6_evidence) < TAIL_EVIDENCE_LIMIT:
                            self.ep4_non_success_completion_after_final_ep6_evidence.append(
                                {
                                    "frame": row.frame_number,
                                    "status": status,
                                    "usbd_status": (
                                        f"0x{row.usbd_status:08x}" if row.usbd_status is not None else None
                                    ),
                                }
                            )
            elif row.endpoint == 0x86:
                if self.ep2_set_completion_frame is not None and self.ep6_completion_after_set_frame is None:
                    self.ep6_completion_after_set_frame = row.frame_number
                self.last_ep6_completion_frame = row.frame_number
                self.ep4_first_completion_after_final_ep6_frame = None
                self.ep4_successful_completion_after_final_ep6_frame = None
                self.ep4_non_success_completion_after_final_ep6_count = 0
                self.ep4_non_success_completion_after_final_ep6_evidence = []
        self.correlation.consume(row)

    @property
    def expected_endpoint_coverage(self) -> int:
        return sum(1 for endpoint in EXPECTED_ENDPOINTS if endpoint in self.endpoints)

    @property
    def observed_row_count(self) -> int:
        return sum(
            endpoint.request_count + endpoint.completion_count + endpoint.unknown_event_count
            for endpoint in self.endpoints.values()
        )

    def candidate_summary(self) -> dict[str, Any]:
        return {
            "bus_id": self.bus_id,
            "descriptor_frames": sorted(set(self.descriptor_frames)),
            "device_address": self.device_address,
            "expected_endpoint_coverage": self.expected_endpoint_coverage,
            "first_epoch_seconds": self.first_epoch_seconds,
            "first_frame": self.first_frame,
            "first_relative_seconds": self.first_relative_seconds,
            "interface_numbers": sorted(self.interface_numbers),
            "last_epoch_seconds": self.last_epoch_seconds,
            "last_frame": self.last_frame,
            "last_relative_seconds": self.last_relative_seconds,
            "observed_endpoints": [f"0x{endpoint:02x}" for endpoint in sorted(self.endpoints)],
            "observed_row_count": self.observed_row_count,
            "product_ids": [f"0x{value:04x}" for value in sorted(self.product_ids)],
            "vendor_ids": [f"0x{value:04x}" for value in sorted(self.vendor_ids)],
        }


class CaptureAccumulator:
    def __init__(
        self,
        *,
        capture_name: str,
        source_size_bytes: int,
        source_sha256: str,
        max_length_buckets: int = 32,
    ) -> None:
        if source_size_bytes <= 0:
            raise AnalyzerError(f"{capture_name}: source size must be positive")
        if not re.fullmatch(r"[0-9a-f]{64}", source_sha256):
            raise AnalyzerError(f"{capture_name}: invalid source SHA-256")
        self.capture_name = capture_name
        self.source_size_bytes = source_size_bytes
        self.source_sha256 = source_sha256
        self._max_length_buckets = max_length_buckets
        self._devices: dict[tuple[int, int], _DeviceAccumulator] = {}
        self._rows_without_device = 0

    def consume(self, row: UsbRow) -> None:
        if row.bus_id is None or row.device_address is None:
            self._rows_without_device += 1
            return
        key = (row.bus_id, row.device_address)
        device = self._devices.setdefault(
            key,
            _DeviceAccumulator(
                bus_id=row.bus_id,
                device_address=row.device_address,
                max_length_buckets=self._max_length_buckets,
            ),
        )
        device.consume(row)

    def _select_device(self) -> _DeviceAccumulator:
        if not self._devices:
            raise AnalyzerError(f"{self.capture_name}: no USB device traffic found")

        def identity_score(device: _DeviceAccumulator) -> tuple[int, int, int]:
            expected_descriptor = int(0x04B4 in device.vendor_ids and 0xFFF2 in device.product_ids)
            return (
                expected_descriptor,
                device.expected_endpoint_coverage,
                int(0x86 in device.endpoints),
            )

        ranked = sorted(
            self._devices.values(),
            key=identity_score,
            reverse=True,
        )
        best = ranked[0]
        best_score = identity_score(best)
        ties = [device for device in ranked if identity_score(device) == best_score]
        if len(ties) != 1:
            identities = ", ".join(f"bus {item.bus_id}/device {item.device_address}" for item in ties)
            raise AnalyzerError(f"{self.capture_name}: ambiguous USB device discovery: {identities}")
        return best

    @staticmethod
    def _endpoint_lifecycle(endpoint: dict[str, Any], *, classification: str, limitation: str) -> dict[str, Any]:
        return {
            "classification": classification,
            "completion_count": endpoint["completion_count"],
            "first_frame": endpoint["first_frame"],
            "last_frame": endpoint["last_frame"],
            "limitation": limitation,
        }

    def finish(self) -> dict[str, Any]:
        selected = self._select_device()
        endpoint_summaries = {
            ENDPOINT_LABELS[endpoint]: selected.endpoints.get(
                endpoint,
                _EndpointAccumulator(
                    data_length_basis=_data_length_basis(endpoint),
                    max_length_buckets=self._max_length_buckets,
                ),
            ).finish()
            for endpoint in EXPECTED_ENDPOINTS
        }
        correlation = selected.correlation.finish()
        observed_device_failure = (
            correlation["status_counts"]["non_success"] > 0
            or correlation["status_counts"]["cancelled"] > 0
        )
        near_four_gib = abs(self.source_size_bytes - 2**32) <= int((2**32) * Decimal("0.05"))
        first_device_frame = selected.first_frame
        last_device_frame = selected.last_frame
        first_tail_ep4_frame = selected.ep4_first_completion_after_final_ep6_frame
        successful_tail_ep4_frame = selected.ep4_successful_completion_after_final_ep6_frame
        tail_ep4_candidates: list[tuple[str, int | None]] = []
        if first_tail_ep4_frame is not None:
            first_tail_phase = (
                "ep4_successful_completion_after_final_ep6"
                if first_tail_ep4_frame == successful_tail_ep4_frame
                else "ep4_completion_after_final_ep6"
            )
            tail_ep4_candidates.append((first_tail_phase, first_tail_ep4_frame))
        if successful_tail_ep4_frame is not None and successful_tail_ep4_frame != first_tail_ep4_frame:
            tail_ep4_candidates.append(
                ("ep4_successful_completion_after_final_ep6", successful_tail_ep4_frame)
            )
        sequence_candidates = [
            ("connect_observation", first_device_frame),
            ("ep2_set_completion", selected.ep2_set_completion_frame),
            ("ep4_completion_after_set", selected.ep4_completion_after_set_frame),
            ("ep6_completion_after_set", selected.ep6_completion_after_set_frame),
            ("ep6_final_completion", selected.last_ep6_completion_frame),
            *tail_ep4_candidates,
            ("close_observation", last_device_frame),
        ]
        evidence_sequence = [
            {"frame": frame, "phase": phase}
            for _, (phase, frame) in sorted(
                (
                    (order, (phase, frame))
                    for order, (phase, frame) in enumerate(sequence_candidates)
                    if frame is not None
                ),
                key=lambda item: (item[1][1], item[0]),
            )
        ]
        successful_tail_poll_observed = selected.ep4_successful_completion_after_final_ep6_frame is not None
        non_success_tail_poll_count = selected.ep4_non_success_completion_after_final_ep6_count
        if non_success_tail_poll_count > 0 and successful_tail_poll_observed:
            tail_classification = (
                "non-success EP4 completion followed by successful EP4 completion after final EP6 completion"
            )
        elif non_success_tail_poll_count > 0:
            tail_classification = "non-success EP4 completion observed after final EP6 completion"
        elif successful_tail_poll_observed:
            tail_classification = "successful EP4 completion observed after final EP6 completion"
        else:
            tail_classification = "capture-tail ordering observation incomplete"
        return {
            "capture": self.capture_name,
            "capture_boundary": {
                "interpretation": (
                    "capture file is near the 4 GiB boundary; this is not device/acquisition failure evidence"
                    if near_four_gib and not observed_device_failure
                    else "capture file size and observed USB status are reported independently"
                ),
                "near_four_gib": near_four_gib,
                "observed_device_failure": observed_device_failure,
                "source_size_bytes": self.source_size_bytes,
            },
            "correlation": correlation,
            "device_candidates": [
                device.candidate_summary()
                for device in sorted(self._devices.values(), key=lambda item: (item.bus_id, item.device_address))
            ],
            "endpoints": endpoint_summaries,
            "lifecycle": {
                "connect_observation": {
                    "classification": "first_observed_target_device_activity",
                    "descriptor_frames": sorted(set(selected.descriptor_frames)),
                    "first_epoch_seconds": selected.first_epoch_seconds,
                    "first_frame": first_device_frame,
                    "first_relative_seconds": selected.first_relative_seconds,
                    "limitation": "capture start or first observation is not proof of a physical reconnect",
                },
                "close_observation": {
                    "classification": "last_observed_target_device_activity",
                    "last_epoch_seconds": selected.last_epoch_seconds,
                    "last_frame": last_device_frame,
                    "last_relative_seconds": selected.last_relative_seconds,
                    "limitation": "last observation is not proof of a physical disconnect or process exit",
                },
                "evidence_sequence": evidence_sequence,
                "ep2_set_candidate": self._endpoint_lifecycle(
                    endpoint_summaries["0x02_out"],
                    classification="EP2 OUT application Set candidate",
                    limitation="packet ordering supports the application phase; command semantics are not decoded from bytes",
                ),
                "ep4_polling": self._endpoint_lifecycle(
                    endpoint_summaries["0x84_in"],
                    classification="EP4 IN status polling",
                    limitation="status bytes are not exported; USBD status remains separate",
                ),
                "ep6_bulk": self._endpoint_lifecycle(
                    endpoint_summaries["0x86_in"],
                    classification="EP6 IN bulk acquisition",
                    limitation="measurement bytes are not extracted or exported",
                ),
                "stop_drain_graceful_close_observation": {
                    "classification": tail_classification,
                    "ep2_set_completion_frame": selected.ep2_set_completion_frame,
                    "ep4_first_completion_after_final_ep6_frame": (
                        selected.ep4_first_completion_after_final_ep6_frame
                    ),
                    "ep4_completion_after_set_frame": selected.ep4_completion_after_set_frame,
                    "ep4_non_success_completion_after_final_ep6_count": non_success_tail_poll_count,
                    "ep4_non_success_completion_after_final_ep6_evidence": list(
                        selected.ep4_non_success_completion_after_final_ep6_evidence
                    ),
                    "ep4_non_success_completion_evidence_limit": TAIL_EVIDENCE_LIMIT,
                    "ep4_successful_completion_after_final_ep6_frame": (
                        selected.ep4_successful_completion_after_final_ep6_frame
                    ),
                    "ep6_completion_after_set_frame": selected.ep6_completion_after_set_frame,
                    "last_device_frame": last_device_frame,
                    "last_ep2_frame": endpoint_summaries["0x02_out"]["last_frame"],
                    "last_ep4_frame": endpoint_summaries["0x84_in"]["last_frame"],
                    "last_ep6_frame": endpoint_summaries["0x86_in"]["last_frame"],
                    "limitation": "successful Type C context supports a baseline; payload-free USB evidence cannot prove DDR drain bits or host cleanup state",
                },
            },
            "rows_without_device_identity": self._rows_without_device,
            "schema": "analogboard.phase0.usbpcap-bounded-summary",
            "schema_version": 2,
            "selected_device": {
                **selected.candidate_summary(),
                "selection_basis": "per-capture VID 0x04b4/PID 0xfff2 evidence, then EP2 OUT/EP4 IN/EP6 IN coverage; evidence ties fail",
            },
            "source_sha256": self.source_sha256,
        }
