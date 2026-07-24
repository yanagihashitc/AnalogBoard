#!/usr/bin/env python3
"""Compare payload-free D17 candidate summaries with the fixed golden."""

from __future__ import annotations

import hashlib
import json
import os
import re
import stat
import sys
from collections import Counter
from pathlib import Path, PurePosixPath, PureWindowsPath


_GOLDEN_REFERENCE_PATH = (
    "docs/reference/d17-golden-regression/golden-reference-v1.json"
)
_GOLDEN_REFERENCE_SHA256 = (
    "581fa28e05d85d4fb6ff0b5157958c1e908326505acf39a3f732b1b720d25095"
)
_GOLDEN_REFERENCE_SIZE = 13_178
_PAIR_COUNT = 3
_CHANNEL_COUNT_PER_PAIR = 13
_MAX_CANDIDATE_DEPTH = 32
_MAX_CANDIDATE_NODES = 4_096
_MAX_CANDIDATE_STRING_LENGTH = 4_096
_MAX_CANDIDATE_DOCUMENT_SIZE = 65_536
_ALLOWED_LIST_FIELDS = frozenset({"pairs", "inputs", "channels", "shape"})

_CANDIDATE_ROOT_FIELDS = frozenset(
    {
        "schema",
        "schema_version",
        "reference",
        "pair_count",
        "channel_count_per_pair",
        "pairs",
    }
)
_REFERENCE_FIELDS = frozenset({"path", "sha256", "size_bytes"})
_PAIR_FIELDS = frozenset(
    {
        "density",
        "run_id",
        "ordinal",
        "inputs",
        "event_count",
        "channels",
    }
)
_INPUT_FIELDS = frozenset({"path", "sha256", "size_bytes", "stream"})
_CHANNEL_FIELDS = frozenset(
    {
        "dtype",
        "label",
        "physical_channel",
        "sha256",
        "shape",
        "source_index",
        "statistics",
        "stream",
    }
)
_STATISTICS_FIELDS = frozenset(
    {"element_count", "max", "min", "nonzero_count", "sum"}
)
_RESULT_FIELDS = frozenset(
    {
        "schema",
        "schema_version",
        "status",
        "reference",
        "candidate",
        "pair_count",
        "channel_count_per_pair",
        "compared_channel_count",
    }
)
_RESULT_CANDIDATE_FIELDS = frozenset({"sha256", "size_bytes"})

_HEX_SHA256_RE = re.compile(r"^[0-9a-f]{64}$")
_URI_RE = re.compile(r"^[A-Za-z][A-Za-z0-9+.-]*:")
_PROHIBITED_KEYS = frozenset(
    {
        "array",
        "arrays",
        "byte",
        "bytes",
        "data",
        "decoded",
        "payload",
        "sample",
        "samples",
        "waveform",
        "waveforms",
    }
)


class RegressionHarnessError(Exception):
    """Base class for stable, typed regression-harness failures."""

    def __init__(self, code: str, message: str) -> None:
        self.code = code
        self.message = message
        super().__init__(f"{code}: {message}")


class CandidateSchemaError(RegressionHarnessError):
    """The candidate does not satisfy the exact summary schema."""


class CandidatePayloadBoundaryError(RegressionHarnessError):
    """The candidate contains payload bytes, arrays, or a host locator."""


class GoldenSourceIdentityError(RegressionHarnessError):
    """The supplied golden source does not match the fixed P0-M1 pin."""


class CandidateReferenceIdentityError(RegressionHarnessError):
    """The candidate does not identify the supplied fixed reference."""


class CandidatePairIdentityError(RegressionHarnessError):
    """An ordered candidate pair does not identify its golden pair."""


class CandidateInputIdentityError(RegressionHarnessError):
    """An ordered candidate input does not identify its golden input."""


class CandidateCardinalityError(RegressionHarnessError):
    """Candidate pairs or physical channels have invalid multiplicity."""


class CandidatePermutationError(RegressionHarnessError):
    """Candidate physical-channel membership is complete but misordered."""


class CandidateLabelError(RegressionHarnessError):
    """A candidate physical channel carries a different label."""


class CandidateMappingError(RegressionHarnessError):
    """A candidate channel carries a different stream/source address."""


class CandidateDtypeError(RegressionHarnessError):
    """A candidate channel carries a different canonical dtype."""


class CandidateShapeError(RegressionHarnessError):
    """A candidate channel carries a different decoded shape."""


class CandidateValueError(RegressionHarnessError):
    """A candidate channel digest does not match the golden."""


class CandidateStatisticsError(RegressionHarnessError):
    """A candidate channel's bounded statistics do not match the golden."""


class _DuplicateKeyError(ValueError):
    pass


_VERIFIED_RESULT_TOKEN = object()


class _VerifiedRegressionResult(dict[str, object]):
    """A sealed Pass mapping produced only after the full comparison."""

    def __init__(
        self,
        value: dict[str, object],
        token: object,
    ) -> None:
        if token is not _VERIFIED_RESULT_TOKEN:
            raise TypeError("verified regression results are internal")
        super().__init__(value)
        self._seal = hashlib.sha256(_canonical_json_bytes(self)).digest()

    def is_sealed(self) -> bool:
        try:
            current = hashlib.sha256(_canonical_json_bytes(self)).digest()
        except (OverflowError, RecursionError, TypeError, ValueError):
            return False
        return self._seal == current


def _fail(
    error_type: type[RegressionHarnessError],
    code: str,
    message: str,
) -> None:
    raise error_type(code, message)


def _is_prohibited_key(key: str) -> bool:
    normalized = key.lower().replace("-", "_")
    if normalized in _PROHIBITED_KEYS:
        return True
    return (
        normalized.endswith(("_payload", "_samples", "_waveform", "_waveforms"))
        or (
            normalized.endswith("_bytes")
            and normalized != "size_bytes"
        )
        or normalized.startswith(("decoded_", "raw_payload", "raw_bytes"))
    )


def _is_absolute_locator(value: str) -> bool:
    return (
        value.startswith("/")
        or value.startswith("\\")
        or PureWindowsPath(value).is_absolute()
        or _URI_RE.match(value) is not None
    )


def _is_unsafe_relative_locator(value: str) -> bool:
    normalized = value.replace("\\", "/")
    return ".." in PurePosixPath(normalized).parts


def _validate_payload_boundary(value: object) -> None:
    """Reject payload and host locator content before schema inspection."""

    stack = [(value, 0)]
    scheduled_nodes = 1
    seen_containers: set[int] = set()
    while stack:
        current, depth = stack.pop()
        if depth > _MAX_CANDIDATE_DEPTH:
            _fail(
                CandidatePayloadBoundaryError,
                "candidate.payload.structure_limit",
                "candidate summary exceeds bounded structural limits",
            )
        if isinstance(current, (bytes, bytearray, memoryview)):
            _fail(
                CandidatePayloadBoundaryError,
                "candidate.payload.bytes",
                "candidate summary contains byte payload",
            )
        if type(current) in (dict, list):
            identity = id(current)
            if identity in seen_containers:
                _fail(
                    CandidatePayloadBoundaryError,
                    "candidate.payload.structure_limit",
                    "candidate summary exceeds bounded structural limits",
                )
            seen_containers.add(identity)
        if type(current) is dict:
            for key, child in current.items():
                if type(key) is not str or _is_prohibited_key(key):
                    _fail(
                        CandidatePayloadBoundaryError,
                        "candidate.payload.prohibited_key",
                        "candidate summary contains a prohibited payload field",
                    )
                if _is_absolute_locator(key):
                    _fail(
                        CandidatePayloadBoundaryError,
                        "candidate.payload.absolute_locator",
                        "candidate summary contains an absolute host locator",
                    )
                if _is_unsafe_relative_locator(key):
                    _fail(
                        CandidatePayloadBoundaryError,
                        "candidate.payload.unsafe_locator",
                        "candidate summary contains an unsafe relative locator",
                    )
                if (
                    type(child) is list
                    and key not in _ALLOWED_LIST_FIELDS
                ):
                    _fail(
                        CandidatePayloadBoundaryError,
                        "candidate.payload.prohibited_key",
                        "candidate summary contains a prohibited payload field",
                    )
                scheduled_nodes += 1
                if scheduled_nodes > _MAX_CANDIDATE_NODES:
                    _fail(
                        CandidatePayloadBoundaryError,
                        "candidate.payload.structure_limit",
                        "candidate summary exceeds bounded structural limits",
                    )
                stack.append((child, depth + 1))
            continue
        if type(current) is list:
            for child in current:
                scheduled_nodes += 1
                if scheduled_nodes > _MAX_CANDIDATE_NODES:
                    _fail(
                        CandidatePayloadBoundaryError,
                        "candidate.payload.structure_limit",
                        "candidate summary exceeds bounded structural limits",
                    )
                stack.append((child, depth + 1))
            continue
        if type(current) is str:
            if len(current) > _MAX_CANDIDATE_STRING_LENGTH:
                _fail(
                    CandidatePayloadBoundaryError,
                    "candidate.payload.structure_limit",
                    "candidate summary exceeds bounded structural limits",
                )
            if _is_absolute_locator(current):
                _fail(
                    CandidatePayloadBoundaryError,
                    "candidate.payload.absolute_locator",
                    "candidate summary contains an absolute host locator",
                )
            if _is_unsafe_relative_locator(current):
                _fail(
                    CandidatePayloadBoundaryError,
                    "candidate.payload.unsafe_locator",
                    "candidate summary contains an unsafe relative locator",
                )
            continue
        if current is not None and type(current) not in (bool, int, float):
            _fail(
                CandidatePayloadBoundaryError,
                "candidate.payload.non_json",
                "candidate summary contains a non-JSON value",
            )


def _schema_failure(message: str = "candidate summary is malformed") -> None:
    _fail(CandidateSchemaError, "candidate.schema.invalid", message)


def _require_exact_fields(
    value: object,
    fields: frozenset[str],
    message: str,
) -> dict[str, object]:
    if type(value) is not dict:
        _schema_failure()
    assert isinstance(value, dict)
    keys = set(value)
    if keys - fields:
        _fail(
            CandidateSchemaError,
            "candidate.schema.unexpected_field",
            message,
        )
    if keys != fields:
        _schema_failure()
    return value


def _is_nonempty_string(value: object) -> bool:
    return type(value) is str and bool(value)


def _is_nonnegative_integer(value: object) -> bool:
    return type(value) is int and value >= 0


def _is_positive_integer(value: object) -> bool:
    return type(value) is int and value > 0


def _is_sha256(value: object) -> bool:
    return type(value) is str and _HEX_SHA256_RE.fullmatch(value) is not None


def _validate_reference_schema(value: object) -> dict[str, object]:
    reference = _require_exact_fields(
        value,
        _REFERENCE_FIELDS,
        "candidate reference has unexpected fields",
    )
    if not (
        _is_nonempty_string(reference["path"])
        and _is_sha256(reference["sha256"])
        and _is_positive_integer(reference["size_bytes"])
    ):
        _schema_failure()
    return reference


def _validate_input_schema(value: object) -> dict[str, object]:
    input_record = _require_exact_fields(
        value,
        _INPUT_FIELDS,
        "candidate input has unexpected fields",
    )
    if not (
        _is_nonempty_string(input_record["path"])
        and _is_sha256(input_record["sha256"])
        and _is_positive_integer(input_record["size_bytes"])
        and _is_nonempty_string(input_record["stream"])
    ):
        _schema_failure()
    return input_record


def _validate_statistics_schema(value: object) -> dict[str, object]:
    statistics = _require_exact_fields(
        value,
        _STATISTICS_FIELDS,
        "candidate channel statistics have unexpected fields",
    )
    if not all(
        _is_nonnegative_integer(statistics[field])
        for field in _STATISTICS_FIELDS
    ):
        _schema_failure()
    return statistics


def _validate_channel_schema(value: object) -> dict[str, object]:
    channel = _require_exact_fields(
        value,
        _CHANNEL_FIELDS,
        "candidate channel has unexpected fields",
    )
    shape = channel["shape"]
    if not (
        _is_nonempty_string(channel["dtype"])
        and _is_nonempty_string(channel["label"])
        and _is_nonempty_string(channel["physical_channel"])
        and _is_sha256(channel["sha256"])
        and type(shape) is list
        and len(shape) == 2
        and all(_is_positive_integer(dimension) for dimension in shape)
        and _is_nonnegative_integer(channel["source_index"])
        and _is_nonempty_string(channel["stream"])
    ):
        _schema_failure()
    _validate_statistics_schema(channel["statistics"])
    return channel


def _validate_pair_schema(value: object) -> dict[str, object]:
    pair = _require_exact_fields(
        value,
        _PAIR_FIELDS,
        "candidate pair has unexpected fields",
    )
    if not (
        _is_nonempty_string(pair["density"])
        and _is_nonempty_string(pair["run_id"])
        and _is_positive_integer(pair["ordinal"])
        and _is_positive_integer(pair["event_count"])
        and type(pair["inputs"]) is list
        and type(pair["channels"]) is list
    ):
        _schema_failure()
    for input_record in pair["inputs"]:
        _validate_input_schema(input_record)
    for channel in pair["channels"]:
        _validate_channel_schema(channel)
    return pair


def _validate_candidate_schema(candidate: object) -> dict[str, object]:
    if type(candidate) is not dict:
        _schema_failure(
            "candidate must be an analogboard.d17.candidate-summary v1 object"
        )
    assert isinstance(candidate, dict)
    unexpected = set(candidate) - _CANDIDATE_ROOT_FIELDS
    if unexpected:
        _fail(
            CandidateSchemaError,
            "candidate.schema.unexpected_field",
            "candidate summary has unexpected root fields",
        )
    if (
        set(candidate) != _CANDIDATE_ROOT_FIELDS
        or candidate.get("schema") != "analogboard.d17.candidate-summary"
        or type(candidate.get("schema_version")) is not int
        or candidate.get("schema_version") != 1
    ):
        _schema_failure(
            "candidate must be an analogboard.d17.candidate-summary v1 object"
        )
    if not (
        _is_positive_integer(candidate["pair_count"])
        and _is_positive_integer(candidate["channel_count_per_pair"])
        and type(candidate["pairs"]) is list
    ):
        _schema_failure()
    _validate_reference_schema(candidate["reference"])
    for pair in candidate["pairs"]:
        _validate_pair_schema(pair)
    return candidate


def _load_fixed_golden(source: object) -> dict[str, object]:
    if not (
        type(source) is bytes
        and len(source) == _GOLDEN_REFERENCE_SIZE
        and hashlib.sha256(source).hexdigest() == _GOLDEN_REFERENCE_SHA256
    ):
        _fail(
            GoldenSourceIdentityError,
            "golden.source.identity_mismatch",
            "golden-reference source does not match the fixed P0-M1 pin",
        )
    try:
        golden = json.loads(source)
    except (UnicodeDecodeError, json.JSONDecodeError):
        _fail(
            GoldenSourceIdentityError,
            "golden.source.identity_mismatch",
            "golden-reference source does not match the fixed P0-M1 pin",
        )
    if type(golden) is not dict:
        _fail(
            GoldenSourceIdentityError,
            "golden.source.identity_mismatch",
            "golden-reference source does not match the fixed P0-M1 pin",
        )
    return golden


def _decode_candidate_source(source: object) -> dict[str, object]:
    if type(source) is not bytes:
        _fail(
            CandidateSchemaError,
            "candidate.source.invalid",
            "candidate source must be strict bounded JSON",
        )
    if len(source) > _MAX_CANDIDATE_DOCUMENT_SIZE:
        _fail(
            CandidatePayloadBoundaryError,
            "candidate.payload.structure_limit",
            "candidate summary exceeds bounded structural limits",
        )

    def unique_object(
        pairs: list[tuple[str, object]],
    ) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in pairs:
            if key in result:
                raise _DuplicateKeyError(key)
            result[key] = value
        return result

    def reject_constant(_value: str) -> None:
        raise ValueError("non-finite JSON number")

    try:
        candidate = json.loads(
            source,
            object_pairs_hook=unique_object,
            parse_constant=reject_constant,
        )
    except (
        _DuplicateKeyError,
        UnicodeDecodeError,
        json.JSONDecodeError,
        RecursionError,
        ValueError,
    ):
        _fail(
            CandidateSchemaError,
            "candidate.source.invalid",
            "candidate source must be strict bounded JSON",
        )
    if type(candidate) is not dict:
        _fail(
            CandidateSchemaError,
            "candidate.source.invalid",
            "candidate source must be strict bounded JSON",
        )
    return candidate


def _canonical_candidate_bytes(candidate: dict[str, object]) -> bytes:
    return _canonical_json_bytes(candidate)


def _canonical_json_bytes(value: object) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=True,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        + b"\n"
    )


def _require_fixed_cardinality(
    candidate: dict[str, object],
    golden: dict[str, object],
) -> tuple[list[dict[str, object]], list[dict[str, object]]]:
    candidate_pairs = candidate["pairs"]
    golden_pairs = golden["pairs"]
    assert isinstance(candidate_pairs, list)
    assert isinstance(golden_pairs, list)
    if candidate["pair_count"] != _PAIR_COUNT or len(candidate_pairs) != _PAIR_COUNT:
        _fail(
            CandidateCardinalityError,
            "candidate.pair_count.invalid",
            "candidate must contain exactly 3 ordered pairs",
        )
    if candidate["channel_count_per_pair"] != _CHANNEL_COUNT_PER_PAIR:
        _fail(
            CandidateCardinalityError,
            "candidate.channel_count.metadata",
            "candidate channel_count_per_pair must be exactly 13",
        )
    return candidate_pairs, golden_pairs


def _compare_pair_and_input_identity(
    pair_index: int,
    candidate_pair: dict[str, object],
    golden_pair: dict[str, object],
) -> None:
    pair_identity_fields = ("density", "run_id", "ordinal", "event_count")
    if any(
        candidate_pair[field] != golden_pair[field]
        for field in pair_identity_fields
    ):
        _fail(
            CandidatePairIdentityError,
            "candidate.pair.identity_mismatch",
            f"candidate pair[{pair_index}] identity does not match golden",
        )

    candidate_inputs = candidate_pair["inputs"]
    golden_inputs = golden_pair["inputs"]
    assert isinstance(candidate_inputs, list)
    assert isinstance(golden_inputs, list)
    if len(candidate_inputs) != len(golden_inputs):
        _fail(
            CandidateInputIdentityError,
            "candidate.input.identity_mismatch",
            f"candidate pair[{pair_index}] input set does not match golden",
        )
    for input_index, (candidate_input, golden_input) in enumerate(
        zip(candidate_inputs, golden_inputs)
    ):
        if candidate_input != golden_input:
            _fail(
                CandidateInputIdentityError,
                "candidate.input.identity_mismatch",
                (
                    f"candidate pair[{pair_index}] input[{input_index}] "
                    "identity does not match golden"
                ),
            )


def _require_channel_membership(
    pair_index: int,
    candidate_channels: list[dict[str, object]],
    golden_channels: list[dict[str, object]],
) -> None:
    found = len(candidate_channels)
    if found != _CHANNEL_COUNT_PER_PAIR:
        direction = "missing" if found < _CHANNEL_COUNT_PER_PAIR else "excess"
        _fail(
            CandidateCardinalityError,
            f"candidate.channel_count.{direction}",
            (
                f"candidate pair[{pair_index}] must contain exactly 13 "
                f"channels; found {found}"
            ),
        )
    candidate_membership = Counter(
        channel["physical_channel"] for channel in candidate_channels
    )
    golden_membership = Counter(
        channel["physical_channel"] for channel in golden_channels
    )
    if candidate_membership != golden_membership:
        _fail(
            CandidateCardinalityError,
            "candidate.channel_set.invalid",
            (
                f"candidate pair[{pair_index}] must contain each golden "
                "physical channel exactly once"
            ),
        )


def _compare_channel(
    pair_index: int,
    channel_index: int,
    candidate_channel: dict[str, object],
    golden_channel: dict[str, object],
) -> None:
    if candidate_channel["physical_channel"] != golden_channel["physical_channel"]:
        _fail(
            CandidatePermutationError,
            "candidate.channel_order.permutation",
            (
                f"candidate pair[{pair_index}] physical-channel order "
                "does not match golden"
            ),
        )
    if candidate_channel["label"] != golden_channel["label"]:
        _fail(
            CandidateLabelError,
            "candidate.channel.label_mismatch",
            (
                f"candidate pair[{pair_index}] channel[{channel_index}] "
                "label does not match golden"
            ),
        )
    if (
        candidate_channel["stream"] != golden_channel["stream"]
        or candidate_channel["source_index"] != golden_channel["source_index"]
    ):
        _fail(
            CandidateMappingError,
            "candidate.channel.mapping_mismatch",
            (
                f"candidate pair[{pair_index}] channel[{channel_index}] "
                "stream/source index does not match golden"
            ),
        )
    if candidate_channel["dtype"] != golden_channel["dtype"]:
        _fail(
            CandidateDtypeError,
            "candidate.channel.dtype_mismatch",
            (
                f"candidate pair[{pair_index}] channel[{channel_index}] "
                "dtype does not match golden"
            ),
        )
    if candidate_channel["shape"] != golden_channel["shape"]:
        _fail(
            CandidateShapeError,
            "candidate.channel.shape_mismatch",
            (
                f"candidate pair[{pair_index}] channel[{channel_index}] "
                "shape does not match golden"
            ),
        )
    if candidate_channel["sha256"] != golden_channel["sha256"]:
        _fail(
            CandidateValueError,
            "candidate.channel.digest_mismatch",
            (
                f"candidate pair[{pair_index}] channel[{channel_index}] "
                "digest does not match golden"
            ),
        )
    shape = candidate_channel["shape"]
    statistics = candidate_channel["statistics"]
    assert isinstance(shape, list)
    assert isinstance(statistics, dict)
    element_count = statistics["element_count"]
    minimum = statistics["min"]
    maximum = statistics["max"]
    nonzero_count = statistics["nonzero_count"]
    total = statistics["sum"]
    coherent = (
        element_count == shape[0] * shape[1]
        and nonzero_count <= element_count
        and minimum <= maximum <= 65_535
        and minimum * element_count <= total <= maximum * element_count
        and (
            (nonzero_count == 0 and minimum == maximum == total == 0)
            or (
                nonzero_count > 0
                and maximum > 0
                and nonzero_count <= total <= nonzero_count * maximum
                and (minimum == 0 or nonzero_count == element_count)
            )
        )
    )
    if not coherent:
        _fail(
            CandidateStatisticsError,
            "candidate.channel.statistics_invalid",
            (
                f"candidate pair[{pair_index}] channel[{channel_index}] "
                "statistics are internally inconsistent"
            ),
        )
    if candidate_channel["statistics"] != golden_channel["statistics"]:
        _fail(
            CandidateStatisticsError,
            "candidate.channel.statistics_mismatch",
            (
                f"candidate pair[{pair_index}] channel[{channel_index}] "
                "statistics do not match golden"
            ),
        )


def compare_candidate_to_golden(
    candidate: object,
    golden_source: object,
) -> dict[str, object]:
    """Compare all 39 candidate channels and return bounded Pass evidence."""

    _validate_payload_boundary(candidate)
    normalized_candidate = _validate_candidate_schema(candidate)
    golden = _load_fixed_golden(golden_source)

    reference = normalized_candidate["reference"]
    assert isinstance(reference, dict)
    expected_reference = {
        "path": _GOLDEN_REFERENCE_PATH,
        "sha256": _GOLDEN_REFERENCE_SHA256,
        "size_bytes": _GOLDEN_REFERENCE_SIZE,
    }
    if reference != expected_reference:
        _fail(
            CandidateReferenceIdentityError,
            "candidate.reference.identity_mismatch",
            "candidate golden-reference identity does not match supplied source",
        )

    candidate_pairs, golden_pairs = _require_fixed_cardinality(
        normalized_candidate,
        golden,
    )
    for pair_index, (candidate_pair, golden_pair) in enumerate(
        zip(candidate_pairs, golden_pairs)
    ):
        _compare_pair_and_input_identity(
            pair_index,
            candidate_pair,
            golden_pair,
        )
        candidate_channels = candidate_pair["channels"]
        golden_channels = golden_pair["channels"]
        assert isinstance(candidate_channels, list)
        assert isinstance(golden_channels, list)
        _require_channel_membership(
            pair_index,
            candidate_channels,
            golden_channels,
        )
        for channel_index, (candidate_channel, golden_channel) in enumerate(
            zip(candidate_channels, golden_channels)
        ):
            _compare_channel(
                pair_index,
                channel_index,
                candidate_channel,
                golden_channel,
            )

    candidate_source = _canonical_candidate_bytes(normalized_candidate)
    result = {
        "schema": "analogboard.d17.regression-result",
        "schema_version": 1,
        "status": "pass",
        "reference": expected_reference,
        "candidate": {
            "sha256": hashlib.sha256(candidate_source).hexdigest(),
            "size_bytes": len(candidate_source),
        },
        "pair_count": _PAIR_COUNT,
        "channel_count_per_pair": _CHANNEL_COUNT_PER_PAIR,
        "compared_channel_count": _PAIR_COUNT * _CHANNEL_COUNT_PER_PAIR,
    }
    return _VerifiedRegressionResult(result, _VERIFIED_RESULT_TOKEN)


def compare_candidate_bytes(
    candidate_source: object,
    golden_source: object,
) -> dict[str, object]:
    """Decode strict bounded candidate JSON and compare every channel."""

    candidate = _decode_candidate_source(candidate_source)
    return compare_candidate_to_golden(candidate, golden_source)


def _fixed_golden_source() -> bytes:
    def source_failure() -> None:
        _fail(
            GoldenSourceIdentityError,
            "golden.source.identity_mismatch",
            "golden-reference source does not match the fixed P0-M1 pin",
        )

    no_follow = getattr(os, "O_NOFOLLOW", 0)
    directory = getattr(os, "O_DIRECTORY", 0)
    if no_follow == 0 or directory == 0:
        source_failure()
    root = Path(__file__).resolve().parents[2]
    descriptors: list[int] = []
    file_descriptor: int | None = None
    try:
        if (
            root.resolve(strict=True) != root
            or not stat.S_ISDIR(root.lstat().st_mode)
            or stat.S_ISLNK(root.lstat().st_mode)
        ):
            source_failure()
        root_descriptor = os.open(
            root,
            os.O_RDONLY | directory | no_follow | getattr(os, "O_CLOEXEC", 0),
        )
        descriptors.append(root_descriptor)
        parent_descriptor = root_descriptor
        relative = PurePosixPath(_GOLDEN_REFERENCE_PATH)
        for part in relative.parts[:-1]:
            opened = os.open(
                part,
                os.O_RDONLY
                | directory
                | no_follow
                | getattr(os, "O_CLOEXEC", 0),
                dir_fd=parent_descriptor,
            )
            metadata = os.fstat(opened)
            if not stat.S_ISDIR(metadata.st_mode):
                source_failure()
            descriptors.append(opened)
            parent_descriptor = opened
        file_descriptor = os.open(
            relative.name,
            os.O_RDONLY | no_follow | getattr(os, "O_CLOEXEC", 0),
            dir_fd=parent_descriptor,
        )
        metadata = os.fstat(file_descriptor)
        if (
            not stat.S_ISREG(metadata.st_mode)
            or metadata.st_size != _GOLDEN_REFERENCE_SIZE
        ):
            source_failure()
        chunks: list[bytes] = []
        remaining = _GOLDEN_REFERENCE_SIZE + 1
        while remaining > 0:
            chunk = os.read(
                file_descriptor,
                min(1024 * 1024, remaining),
            )
            if not chunk:
                break
            chunks.append(chunk)
            remaining -= len(chunk)
        source = b"".join(chunks)
        if (
            len(source) != _GOLDEN_REFERENCE_SIZE
            or hashlib.sha256(source).hexdigest()
            != _GOLDEN_REFERENCE_SHA256
        ):
            source_failure()
        return source
    except RegressionHarnessError:
        raise
    except OSError:
        source_failure()
    finally:
        if file_descriptor is not None:
            os.close(file_descriptor)
        for descriptor in reversed(descriptors):
            os.close(descriptor)
    raise AssertionError("unreachable")


def serialize_regression_result(result: object) -> bytes:
    """Serialize only the exact bounded Pass-result schema canonically."""

    if (
        type(result) is not _VerifiedRegressionResult
        or not result.is_sealed()
    ):
        _fail(
            CandidateSchemaError,
            "result.unverified",
            "regression result must originate from a successful comparison",
        )
    normalized_result = dict(result)
    _validate_payload_boundary(normalized_result)
    normalized = _require_exact_fields(
        normalized_result,
        _RESULT_FIELDS,
        "regression result has unexpected fields",
    )
    reference = _require_exact_fields(
        normalized["reference"],
        _REFERENCE_FIELDS,
        "regression result reference has unexpected fields",
    )
    candidate = _require_exact_fields(
        normalized["candidate"],
        _RESULT_CANDIDATE_FIELDS,
        "regression result candidate has unexpected fields",
    )
    if not (
        normalized["schema"] == "analogboard.d17.regression-result"
        and type(normalized["schema_version"]) is int
        and normalized["schema_version"] == 1
        and normalized["status"] == "pass"
        and reference
        == {
            "path": _GOLDEN_REFERENCE_PATH,
            "sha256": _GOLDEN_REFERENCE_SHA256,
            "size_bytes": _GOLDEN_REFERENCE_SIZE,
        }
        and _is_sha256(candidate["sha256"])
        and _is_positive_integer(candidate["size_bytes"])
        and normalized["pair_count"] == _PAIR_COUNT
        and normalized["channel_count_per_pair"] == _CHANNEL_COUNT_PER_PAIR
        and normalized["compared_channel_count"]
        == _PAIR_COUNT * _CHANNEL_COUNT_PER_PAIR
    ):
        _fail(
            CandidateSchemaError,
            "result.schema.invalid",
            "regression result must be a bounded Pass v1 object",
        )
    return _canonical_json_bytes(normalized)


def main(argv: list[str] | None = None) -> int:
    """Compare one strict candidate document from stdin."""

    arguments = sys.argv[1:] if argv is None else argv
    if arguments != ["compare"]:
        print("usage: regression_harness.py compare", file=sys.stderr)
        return 2
    try:
        candidate_source = sys.stdin.buffer.read(
            _MAX_CANDIDATE_DOCUMENT_SIZE + 1
        )
        result = compare_candidate_bytes(
            candidate_source,
            _fixed_golden_source(),
        )
        sys.stdout.buffer.write(serialize_regression_result(result))
        sys.stdout.buffer.flush()
        return 0
    except RegressionHarnessError as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
