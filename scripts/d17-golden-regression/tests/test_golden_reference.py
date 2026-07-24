from __future__ import annotations

import copy
import hashlib
import io
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from pathlib import Path
from pathlib import PurePosixPath
from typing import Callable
from unittest import mock

SCRIPT_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = SCRIPT_ROOT.parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from golden_reference import (  # noqa: E402
    AssetIdentityError,
    DecodeError,
    DecodedDtypeError,
    DecodedShapeError,
    MappingAddressError,
    PayloadBoundaryError,
    ReaderProvenanceError,
    ReferenceOutputError,
    ReferenceSourceError,
    UnsafeAssetError,
    _container_image_identity,
    _read_fixed_source,
    build_golden_reference,
    main,
    serialize_golden_reference,
    verify_reference_source_identities,
    write_golden_reference,
)

import numpy as np  # noqa: E402


MAPPING_PATH = (
    "docs/reference/d17-golden-regression/channel-mapping-v1.json"
)
SELECTION_PATH = (
    "docs/reference/d17-golden-regression/golden-inputs-v1.json"
)
OUTPUT_PATH = (
    "docs/reference/d17-golden-regression/golden-reference-v1.json"
)
MAPPING_SHA256 = (
    "8e197eade3fff0f7427c7cf0e9d77409624b803a51a782c6a429e705f15fc99b"
)
MAPPING_SIZE = 1_349
SELECTION_SHA256 = (
    "30feceee3ea5f054d3ad43528f82c1e0228a0a855f8c2d55cb0b7f2732b42975"
)
SELECTION_SIZE = 1_953
REFERENCE_SHA256 = (
    "3f531bd624ad3ea8b763b7ec82da42f313fbd4976945c6cd1f636fab9636f53f"
)
REFERENCE_SIZE = 13_281
GCSA_COMMIT = "20689a991697217518ec2ff15aaaa2533b169eb0"
GCSA_READER_PATH = "src/gcsa/io/binary_reader.py"
GCSA_READER_SYMBOL = "BinaryReader"
GCSA_READER_VERSION = "v1"
GCSA_READER_SHA256 = (
    "620ab899b0fb75f75da0a1c8b5722a2f02212726910aea5115401506f8eb4254"
)
GCSA_PARSER_SHA256 = (
    "5035b9147ec42c2381cc2fd45a1f83a9f251edece7b21c4dd099f2da315a2964"
)
CONTAINER_IMAGE_ID = (
    "sha256:e65e9f8b0ffafef5b5d2b9711c9a341"
    "1649ae80fd036cc79f0febb80b4c0b06e"
)
ASSET_LOCATOR = "artifacts/field-session/2026-07-17-characterization"
FL_CONTENT = b"synthetic-fl-input"
FH_CONTENT = b"synthetic-fh-input"
LABELS = (
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
)


def valid_mapping() -> list[dict[str, object]]:
    mapping: list[dict[str, object]] = []
    for offset, label in enumerate(LABELS):
        stream = "FL" if offset < 8 else "FH"
        source_index = offset if stream == "FL" else offset - 8
        mapping.append(
            {
                "physical_channel": f"CH{offset + 1}",
                "label": label,
                "stream": stream,
                "source_index": source_index,
            }
        )
    return mapping


def selected_entry(stream: str, content: bytes) -> dict[str, object]:
    return {
        "stream": stream,
        "path": f"260717_1529_{stream.lower()}_1.bin",
        "sha256": hashlib.sha256(content).hexdigest(),
        "size_bytes": len(content),
    }


def valid_selection() -> dict[str, object]:
    return {
        "asset_locator": ASSET_LOCATOR,
        "pair_count": 1,
        "entry_count": 2,
        "pairs": [
            {
                "density": "low",
                "run_id": "260717_1529",
                "ordinal": 1,
                "entries": [
                    selected_entry("FL", FL_CONTENT),
                    selected_entry("FH", FH_CONTENT),
                ],
            }
        ],
    }


def valid_reader_provenance() -> dict[str, object]:
    return {
        "repository": "gcsa",
        "commit": GCSA_COMMIT,
        "path": GCSA_READER_PATH,
        "symbol": GCSA_READER_SYMBOL,
        "version": GCSA_READER_VERSION,
        "invocation": "BinaryReader(version='v1')",
        "reader_source_sha256": GCSA_READER_SHA256,
        "parser": {
            "path": "src/gcsa/io/parsers/v1.py",
            "sha256": GCSA_PARSER_SHA256,
        },
        "environment": {
            "kind": "container-image",
            "identity": CONTAINER_IMAGE_ID,
            "identity_attestation": {
                "kind": "operator-environment-attestation",
                "source": "P0_M1_CONTAINER_IMAGE_ID",
            },
            "python_version": "3.10.17",
            "numpy_version": "2.2.6",
            "logical_invocation": (
                "golden_reference.py generate "
                "--asset-root <fixed-custody-root>"
            ),
        },
    }


def valid_arrays() -> dict[str, np.ndarray]:
    fl = np.arange(8 * 2_400, dtype="<u2").reshape(1, 8, 2_400)
    fh = (
        np.arange(5 * 2_400, dtype="<u2").reshape(1, 5, 2_400)
        + np.uint16(101)
    )
    return {"FL": fl, "FH": fh}


def distinct_multi_event_arrays() -> dict[str, np.ndarray]:
    fl = np.empty((2, 8, 2_400), dtype="<u2")
    fh = np.empty((2, 5, 2_400), dtype="<u2")
    samples = np.arange(2_400, dtype="<u2")
    for event_index in range(2):
        for channel_index in range(8):
            fl[event_index, channel_index, :] = (
                event_index * 20_000
                + channel_index * 2_000
                + samples
            )
        for channel_index in range(5):
            fh[event_index, channel_index, :] = (
                40_000
                + event_index * 10_000
                + channel_index * 1_000
                + samples
            )
    return {"FL": fl, "FH": fh}


def independent_c_order_digest(values: np.ndarray) -> str:
    digest = hashlib.sha256()
    for event_index in range(values.shape[0]):
        for sample_index in range(values.shape[1]):
            digest.update(
                int(values[event_index, sample_index]).to_bytes(
                    2,
                    byteorder="little",
                )
            )
    return digest.hexdigest()


def fake_decoder(
    arrays: dict[str, object],
) -> Callable[[Path, str], object]:
    def decode(_path: Path, stream: str) -> object:
        value = arrays[stream]
        if isinstance(value, BaseException):
            raise value
        return value

    return decode


class AssetFixture:
    def __init__(self) -> None:
        self._temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self._temporary_directory.name)
        (self.root / "260717_1529_fl_1.bin").write_bytes(FL_CONTENT)
        (self.root / "260717_1529_fh_1.bin").write_bytes(FH_CONTENT)

    def close(self) -> None:
        self._temporary_directory.cleanup()


class GoldenReferenceTests(unittest.TestCase):
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
        self.assertEqual(
            f"{expected_code}: {expected_message}",
            str(raised.exception),
        )

    def build(
        self,
        asset_root: Path,
        *,
        mapping: list[dict[str, object]] | None = None,
        selection: dict[str, object] | None = None,
        provenance: dict[str, object] | None = None,
        arrays: dict[str, object] | None = None,
    ) -> dict[str, object]:
        return build_golden_reference(
            mapping=valid_mapping() if mapping is None else mapping,
            selection=valid_selection() if selection is None else selection,
            asset_root=asset_root,
            reader_provenance=(
                valid_reader_provenance()
                if provenance is None
                else provenance
            ),
            decode_entry=fake_decoder(
                valid_arrays() if arrays is None else arrays
            ),
        )

    def test_r_n_01_builds_thirteen_ordered_channel_records(self) -> None:
        # Given: One identity-pinned pair, the 13-entry map, and valid arrays.
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        arrays = valid_arrays()

        # When: The reference is built through the pinned-reader seam.
        reference = self.build(fixture.root, arrays=arrays)

        # Then: Every channel has canonical digest and bounded integer stats.
        self.assertEqual(
            "analogboard.d17.golden-reference",
            reference["schema"],
        )
        self.assertEqual(1, reference["schema_version"])
        self.assertEqual(1, reference["pair_count"])
        self.assertEqual(13, reference["channel_count_per_pair"])
        pair = reference["pairs"][0]
        self.assertEqual(13, len(pair["channels"]))
        self.assertEqual(list(LABELS), [row["label"] for row in pair["channels"]])
        self.assertEqual(
            [f"CH{index}" for index in range(1, 14)],
            [row["physical_channel"] for row in pair["channels"]],
        )
        self.assertEqual(
            [
                {
                    "stream": "FL",
                    "path": "260717_1529_fl_1.bin",
                    "sha256": hashlib.sha256(FL_CONTENT).hexdigest(),
                    "size_bytes": len(FL_CONTENT),
                },
                {
                    "stream": "FH",
                    "path": "260717_1529_fh_1.bin",
                    "sha256": hashlib.sha256(FH_CONTENT).hexdigest(),
                    "size_bytes": len(FH_CONTENT),
                },
            ],
            pair["inputs"],
        )

        first_values = np.ascontiguousarray(arrays["FL"][:, 0, :], dtype="<u2")
        first = pair["channels"][0]
        self.assertEqual("<u2", first["dtype"])
        self.assertEqual([1, 2_400], first["shape"])
        self.assertEqual(
            hashlib.sha256(first_values.tobytes(order="C")).hexdigest(),
            first["sha256"],
        )
        self.assertEqual(
            {
                "element_count": 2_400,
                "min": int(first_values.min()),
                "max": int(first_values.max()),
                "sum": int(first_values.sum(dtype=np.uint64)),
                "nonzero_count": int(np.count_nonzero(first_values)),
            },
            first["statistics"],
        )
        for channel in pair["channels"]:
            self.assertEqual(
                {"element_count", "min", "max", "sum", "nonzero_count"},
                set(channel["statistics"]),
            )
            self.assertTrue(
                all(
                    type(value) is int
                    for value in channel["statistics"].values()
                )
            )

    def test_r_n_02_serialization_is_byte_deterministic_and_payload_free(self) -> None:
        # Given: The same valid synthetic sources decoded independently twice.
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        first_reference = self.build(fixture.root)
        second_reference = self.build(fixture.root)

        # When: Both references are canonically serialized.
        first = serialize_golden_reference(first_reference)
        second = serialize_golden_reference(second_reference)

        # Then: The bytes, ordering, and newline are identical.
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        self.assertNotIn(FL_CONTENT, first)
        self.assertNotIn(FH_CONTENT, first)
        self.assertNotIn(str(fixture.root).encode(), first)

    def test_r_n_02b_digests_each_channel_in_c_event_sample_order(self) -> None:
        # Given: Two events with values unique to every stream and channel.
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        arrays = distinct_multi_event_arrays()

        # When: The reference projects and digests every mapped channel.
        reference = self.build(fixture.root, arrays=arrays)

        # Then: Each digest independently matches event-major C order.
        actual_channels = reference["pairs"][0]["channels"]
        expected_digests: list[str] = []
        for mapping_row in valid_mapping():
            stream = str(mapping_row["stream"])
            source_index = int(mapping_row["source_index"])
            values = arrays[stream][:, source_index, :]
            expected_digest = independent_c_order_digest(values)
            expected_digests.append(expected_digest)
            self.assertEqual(
                expected_digest,
                actual_channels[len(expected_digests) - 1]["sha256"],
            )
            self.assertNotEqual(
                expected_digest,
                hashlib.sha256(values.tobytes(order="F")).hexdigest(),
            )
            if source_index != 0:
                self.assertNotEqual(
                    expected_digest,
                    independent_c_order_digest(arrays[stream][:, 0, :]),
                )
        self.assertEqual(13, len(set(expected_digests)))

    def test_r_n_02c_records_environment_identity_as_attestation(self) -> None:
        # Given: The fixed image identity supplied by the operator environment.
        fixture = AssetFixture()
        self.addCleanup(fixture.close)

        # When: The reference records the pinned reader provenance.
        reference = self.build(fixture.root)

        # Then: The identity remains fixed without claiming runtime verification.
        environment = reference["reader"]["environment"]
        self.assertEqual(CONTAINER_IMAGE_ID, environment["identity"])
        self.assertEqual(
            {
                "kind": "operator-environment-attestation",
                "source": "P0_M1_CONTAINER_IMAGE_ID",
            },
            environment["identity_attestation"],
        )

    def test_r_n_03_uses_the_verified_contract_without_a_local_label_copy(self) -> None:
        # Given: A structurally valid mapping supplied by the verified contract seam.
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        mapping = valid_mapping()
        for index, row in enumerate(mapping, start=1):
            row["label"] = f"authority-label-{index:02d}"

        # When: The generic reference builder projects the supplied mapping.
        reference = self.build(fixture.root, mapping=mapping)

        # Then: It preserves the authority labels instead of consulting a local copy.
        self.assertEqual(
            [f"authority-label-{index:02d}" for index in range(1, 14)],
            [
                channel["label"]
                for channel in reference["pairs"][0]["channels"]
            ],
        )

    def test_r_b_01_accepts_exact_channel_and_sample_boundaries(self) -> None:
        # Given: FL=8, FH=5, one event, and exactly 2400 samples.
        fixture = AssetFixture()
        self.addCleanup(fixture.close)

        # When: The fixed shape boundary is decoded.
        reference = self.build(fixture.root)

        # Then: Exactly 13 channel summaries are accepted.
        self.assertEqual(13, len(reference["pairs"][0]["channels"]))
        self.assertEqual(
            [[1, 2_400]] * 13,
            [row["shape"] for row in reference["pairs"][0]["channels"]],
        )

    def test_r_a_01_rejects_mapping_or_selection_source_pin_drift(self) -> None:
        # Given: The exact tracked mapping/selection bytes and one-byte drifts.
        mapping = (REPOSITORY_ROOT / MAPPING_PATH).read_bytes()
        selection = (REPOSITORY_ROOT / SELECTION_PATH).read_bytes()
        self.assertEqual(MAPPING_SIZE, len(mapping))
        self.assertEqual(MAPPING_SHA256, hashlib.sha256(mapping).hexdigest())
        self.assertEqual(SELECTION_SIZE, len(selection))
        self.assertEqual(SELECTION_SHA256, hashlib.sha256(selection).hexdigest())
        verify_reference_source_identities(mapping, selection)

        cases = (
            (mapping + b"\n", selection),
            (mapping, selection + b"\n"),
        )
        for mapping_source, selection_source in cases:
            with self.subTest(
                mapping_size=len(mapping_source),
                selection_size=len(selection_source),
            ):
                # When/Then: Neither fixed input contract may silently drift.
                self.assert_failure(
                    ReferenceSourceError,
                    "reference.source.identity_mismatch",
                    "fixed mapping or selection source identity does not match its pin",
                    lambda mapping_source=mapping_source,
                    selection_source=selection_source:
                    verify_reference_source_identities(
                        mapping_source,
                        selection_source,
                    ),
                )

    def test_r_a_02_rejects_missing_symlinked_or_nonregular_assets(self) -> None:
        # Given: Asset candidates that are missing, symlinked, or non-regular.
        for case in ("missing", "symlink", "directory"):
            with self.subTest(case=case):
                fixture = AssetFixture()
                self.addCleanup(fixture.close)
                target = fixture.root / "260717_1529_fl_1.bin"
                target.unlink()
                if case == "symlink":
                    outside = fixture.root / "outside.bin"
                    outside.write_bytes(FL_CONTENT)
                    target.symlink_to(outside)
                elif case == "directory":
                    target.mkdir()

                # When/Then: Decode never follows or tolerates an unsafe input.
                self.assert_failure(
                    UnsafeAssetError,
                    "reference.asset.unsafe",
                    (
                        "selected asset must be a regular file beneath "
                        "the fixed asset root"
                    ),
                    lambda fixture=fixture: self.build(fixture.root),
                )

    def test_r_a_03_rejects_asset_size_or_digest_mismatch(self) -> None:
        # Given: A selected FL entry with a wrong size or SHA-256 identity.
        for field, value in (
            ("size_bytes", len(FL_CONTENT) + 1),
            ("sha256", "0" * 64),
        ):
            selection = valid_selection()
            selection["pairs"][0]["entries"][0][field] = value
            fixture = AssetFixture()
            self.addCleanup(fixture.close)

            with self.subTest(field=field):
                # When/Then: Integrity drift stops before reference acceptance.
                self.assert_failure(
                    AssetIdentityError,
                    "reference.asset.identity_mismatch",
                    (
                        "selected asset identity does not match "
                        "its canonical manifest pin"
                    ),
                    lambda fixture=fixture, selection=selection:
                    self.build(fixture.root, selection=selection),
                )

    def test_r_a_03b_rejects_asset_mutation_during_decode(self) -> None:
        # Given: A selected inode whose contents change inside the reader call.
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        arrays = valid_arrays()

        def mutating_decoder(_path: Path, stream: str) -> object:
            if stream == "FL":
                (fixture.root / "260717_1529_fl_1.bin").write_bytes(
                    FL_CONTENT + b"-changed-during-decode"
                )
            return arrays[stream]

        # When/Then: Post-decode integrity rejects the stale decoded result.
        self.assert_failure(
            AssetIdentityError,
            "reference.asset.identity_mismatch",
            "selected asset identity does not match its canonical manifest pin",
            lambda: build_golden_reference(
                mapping=valid_mapping(),
                selection=valid_selection(),
                asset_root=fixture.root,
                reader_provenance=valid_reader_provenance(),
                decode_entry=mutating_decoder,
            ),
        )

    def test_r_a_04_rejects_any_reader_provenance_drift(self) -> None:
        # Given: Each immutable reader authority field changed in isolation.
        cases: tuple[tuple[str, Callable[[dict[str, object]], None]], ...] = (
            ("commit", lambda value: value.__setitem__("commit", "0" * 40)),
            (
                "path",
                lambda value: value.__setitem__(
                    "path",
                    "src/gcsa/io/other_reader.py",
                ),
            ),
            ("symbol", lambda value: value.__setitem__("symbol", "OtherReader")),
            ("version", lambda value: value.__setitem__("version", "v2")),
            (
                "reader_source_sha256",
                lambda value: value.__setitem__(
                    "reader_source_sha256",
                    "0" * 64,
                ),
            ),
            (
                "parser",
                lambda value: value.__setitem__(
                    "parser",
                    {
                        "path": "src/gcsa/io/parsers/v1.py",
                        "sha256": "0" * 64,
                    },
                ),
            ),
            (
                "image",
                lambda value: value["environment"].__setitem__(
                    "identity",
                    "sha256:" + "0" * 64,
                ),
            ),
            (
                "attestation_kind",
                lambda value: value["environment"][
                    "identity_attestation"
                ].__setitem__("kind", "runtime-inspection"),
            ),
            (
                "attestation_source",
                lambda value: value["environment"][
                    "identity_attestation"
                ].__setitem__("source", "UNPINNED_IMAGE_ID"),
            ),
            (
                "missing_attestation",
                lambda value: value["environment"].pop(
                    "identity_attestation"
                ),
            ),
            (
                "missing_environment",
                lambda value: value.pop("environment"),
            ),
            (
                "unknown_field",
                lambda value: value.__setitem__("fallback", True),
            ),
        )
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        for field, mutate in cases:
            provenance = valid_reader_provenance()
            mutate(provenance)
            with self.subTest(field=field):
                # When/Then: No reader fallback or provenance rewrite is allowed.
                self.assert_failure(
                    ReaderProvenanceError,
                    "reference.reader.pin_mismatch",
                    (
                        "gcsa reader provenance does not match "
                        "the fixed P0-M1 pin"
                    ),
                    lambda provenance=provenance:
                    self.build(fixture.root, provenance=provenance),
                )

    def test_r_a_05_rejects_reader_exception_or_nonarray_result(self) -> None:
        # Given: A reader failure and values that are not numpy ndarrays.
        cases = (
            (
                RuntimeError(
                    "reader leaked locator " + "/" + "tmp/private.bin"
                ),
                "reference.decode.failed",
                "pinned gcsa reader failed for selected input",
            ),
            (
                [[1, 2, 3]],
                "reference.decode.non_array",
                "pinned gcsa reader must return a numpy ndarray",
            ),
        )
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        for value, code, message in cases:
            arrays = valid_arrays()
            arrays["FL"] = value
            with self.subTest(code=code):
                # When/Then: The stable error hides payload and host details.
                self.assert_failure(
                    DecodeError,
                    code,
                    message,
                    lambda arrays=arrays: self.build(
                        fixture.root,
                        arrays=arrays,
                    ),
                )
        arrays = valid_arrays()
        arrays["FL"] = RuntimeError(
            "reader leaked " + "/" + "tmp/private.bin"
        )
        with self.assertRaises(DecodeError) as raised:
            self.build(fixture.root, arrays=arrays)
        self.assertIsNone(raised.exception.__context__)

    def test_r_a_06_rejects_noncanonical_uint16_dtype(self) -> None:
        # Given: Value-compatible arrays with a wrong dtype or byte order.
        cases = (
            np.zeros((1, 8, 2_400), dtype=np.uint8),
            np.zeros((1, 8, 2_400), dtype=">u2"),
            np.zeros((1, 8, 2_400), dtype=np.float32),
        )
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        for value in cases:
            arrays = valid_arrays()
            arrays["FL"] = value
            with self.subTest(dtype=value.dtype.str):
                # When/Then: No coercion is allowed before the channel digest.
                self.assert_failure(
                    DecodedDtypeError,
                    "reference.dtype.mismatch",
                    "decoded stream dtype must be canonical <u2",
                    lambda arrays=arrays: self.build(
                        fixture.root,
                        arrays=arrays,
                    ),
                )

    def test_r_a_07_rejects_rank_channel_event_or_sample_shape_drift(self) -> None:
        # Given: Independent rank, channel, event, and sample-count drifts.
        cases = (
            (
                {"FL": np.zeros((8, 2_400), dtype="<u2")},
                "reference.shape.rank",
                "decoded stream must have rank 3",
            ),
            (
                {"FL": np.zeros((1, 7, 2_400), dtype="<u2")},
                "reference.shape.channels",
                "decoded stream channel count does not match the fixed mapping",
            ),
            (
                {"FH": np.zeros((1, 4, 2_400), dtype="<u2")},
                "reference.shape.channels",
                "decoded stream channel count does not match the fixed mapping",
            ),
            (
                {"FL": np.zeros((2, 8, 2_400), dtype="<u2")},
                "reference.shape.events",
                "decoded FL and FH streams must have the same positive event count",
            ),
            (
                {"FL": np.zeros((0, 8, 2_400), dtype="<u2")},
                "reference.shape.events",
                "decoded FL and FH streams must have the same positive event count",
            ),
            (
                {"FH": np.zeros((1, 5, 2_399), dtype="<u2")},
                "reference.shape.samples",
                "decoded stream must contain exactly 2400 samples per event",
            ),
        )
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        for mutation, code, message in cases:
            arrays = valid_arrays()
            arrays.update(mutation)
            with self.subTest(code=code, shape=next(iter(mutation.values())).shape):
                # When/Then: Every structural drift is a typed hard failure.
                self.assert_failure(
                    DecodedShapeError,
                    code,
                    message,
                    lambda arrays=arrays: self.build(
                        fixture.root,
                        arrays=arrays,
                    ),
                )

    def test_r_a_08_rejects_unaddressable_or_non_thirteen_mapping(self) -> None:
        # Given: A short mapping and an FL index outside the decoded stream.
        short_mapping = valid_mapping()[:-1]
        bad_index = valid_mapping()
        bad_index[7] = {**bad_index[7], "source_index": 8}
        cases = (
            (
                short_mapping,
                "reference.mapping.channel_count",
                "channel mapping must contain exactly 13 entries",
            ),
            (
                bad_index,
                "reference.mapping.address",
                "channel mapping cannot address the decoded stream",
            ),
        )
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        for mapping, code, message in cases:
            with self.subTest(code=code):
                # When/Then: No channel may be omitted, padded, or reindexed.
                self.assert_failure(
                    MappingAddressError,
                    code,
                    message,
                    lambda mapping=mapping: self.build(
                        fixture.root,
                        mapping=mapping,
                    ),
                )

    def test_r_a_09_rejects_payload_or_host_locator_serialization(self) -> None:
        # Given: A valid document independently contaminated by prohibited data.
        fixture = AssetFixture()
        self.addCleanup(fixture.close)
        reference = self.build(fixture.root)
        cases: list[tuple[dict[str, object], str, str]] = []

        samples = copy.deepcopy(reference)
        samples["pairs"][0]["channels"][0]["samples"] = [1, 2, 3]
        cases.append(
            (
                samples,
                "reference.payload.prohibited_key",
                "golden reference contains a prohibited payload field",
            )
        )

        payload_bytes = copy.deepcopy(reference)
        payload_bytes["pairs"][0]["channels"][0]["sha256"] = b"\x00"
        cases.append(
            (
                payload_bytes,
                "reference.payload.bytes",
                "golden reference must not contain bytes values",
            )
        )

        absolute_value = copy.deepcopy(reference)
        absolute_value["reader"]["environment"]["identity"] = (
            "/" + "tmp/private/image"
        )
        cases.append(
            (
                absolute_value,
                "reference.payload.absolute_locator",
                "golden reference must not contain an absolute host locator",
            )
        )

        absolute_key = copy.deepcopy(reference)
        absolute_key["reader"]["environment"][
            "/" + "var/private"
        ] = "leak"
        cases.append(
            (
                absolute_key,
                "reference.payload.absolute_locator",
                "golden reference must not contain an absolute host locator",
            )
        )

        windows_value = copy.deepcopy(reference)
        windows_value["reader"]["environment"]["identity"] = (
            "C:" + r"\Users\private\image"
        )
        cases.append(
            (
                windows_value,
                "reference.payload.absolute_locator",
                "golden reference must not contain an absolute host locator",
            )
        )

        for document, code, message in cases:
            with self.subTest(code=code, mutation=len(cases)):
                # When/Then: Serialization fails before any unsafe JSON is emitted.
                self.assert_failure(
                    PayloadBoundaryError,
                    code,
                    message,
                    lambda document=document: serialize_golden_reference(document),
                )

    def test_r_a_10_rejects_wrong_symlinked_or_nonregular_output(self) -> None:
        # Given: The fixed output parent with lexical and filesystem hazards.
        content = (REPOSITORY_ROOT / OUTPUT_PATH).read_bytes()
        self.assertEqual(REFERENCE_SIZE, len(content))
        self.assertEqual(REFERENCE_SHA256, hashlib.sha256(content).hexdigest())
        cases = ("wrong_path", "parent_symlink", "target_symlink", "target_directory")
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                parent = root / "docs/reference/d17-golden-regression"
                outside = root / "outside"
                outside.mkdir()
                outside_file = outside / "golden-reference-v1.json"
                outside_file.write_bytes(b"do-not-touch")
                output = Path(OUTPUT_PATH)

                if case == "parent_symlink":
                    (root / "docs").mkdir()
                    (root / "docs/reference").symlink_to(
                        outside,
                        target_is_directory=True,
                    )
                else:
                    parent.mkdir(parents=True)
                    target = root / output
                    if case == "wrong_path":
                        output = Path("artifacts/golden-reference-v1.json")
                    elif case == "target_symlink":
                        target.symlink_to(outside_file)
                    elif case == "target_directory":
                        target.mkdir()

                expected = {
                    "wrong_path": (
                        "reference.output.path",
                        f"reference output must be {OUTPUT_PATH}",
                    ),
                    "parent_symlink": (
                        "reference.output.parent",
                        (
                            "reference output parent must contain only "
                            "existing directories"
                        ),
                    ),
                    "target_symlink": (
                        "reference.output.target",
                        "reference output must be absent or a regular file",
                    ),
                    "target_directory": (
                        "reference.output.target",
                        "reference output must be absent or a regular file",
                    ),
                }[case]

                # When/Then: The fixed output fence fails without touching outside.
                self.assert_failure(
                    ReferenceOutputError,
                    expected[0],
                    expected[1],
                    lambda root=root, output=output: write_golden_reference(
                        root,
                        output,
                        content,
                    ),
                )
                self.assertEqual(b"do-not-touch", outside_file.read_bytes())

    def test_r_a_11_rejects_output_repin_and_hardlink_aliases(self) -> None:
        # Given: Exact generated bytes plus drifted or aliased output targets.
        content = (REPOSITORY_ROOT / OUTPUT_PATH).read_bytes()
        self.assertEqual(REFERENCE_SIZE, len(content))
        self.assertEqual(REFERENCE_SHA256, hashlib.sha256(content).hexdigest())

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / OUTPUT_PATH
            target.parent.mkdir(parents=True)
            target.write_bytes(b"drifted-reference\n")

            # When/Then: Regeneration cannot silently bless a drifted fixture.
            self.assert_failure(
                ReferenceOutputError,
                "reference.output.identity_mismatch",
                "generated or existing reference identity does not match its pin",
                lambda: write_golden_reference(
                    root,
                    Path(OUTPUT_PATH),
                    content,
                ),
            )
            self.assertEqual(b"drifted-reference\n", target.read_bytes())

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / OUTPUT_PATH
            target.parent.mkdir(parents=True)
            alias = root / "must-not-change.json"
            alias.write_bytes(content)
            os.link(alias, target)

            # When/Then: A regular hardlink cannot bypass the output fence.
            self.assert_failure(
                ReferenceOutputError,
                "reference.output.target",
                "reference output must be absent or a regular unaliased file",
                lambda: write_golden_reference(
                    root,
                    Path(OUTPUT_PATH),
                    content,
                ),
            )
            self.assertEqual(content, alias.read_bytes())
            self.assertEqual(2, alias.stat().st_nlink)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / OUTPUT_PATH).parent.mkdir(parents=True)

            # When/Then: Caller-provided bytes cannot self-repin the fixture.
            self.assert_failure(
                ReferenceOutputError,
                "reference.output.identity_mismatch",
                "generated or existing reference identity does not match its pin",
                lambda: write_golden_reference(
                    root,
                    Path(OUTPUT_PATH),
                    b"{}\n",
                ),
            )
            self.assertFalse((root / OUTPUT_PATH).exists())

    def test_r_n_04_writes_only_the_identity_pinned_reference(self) -> None:
        # Given: The exact tracked reference bytes and an absent fenced target.
        content = (REPOSITORY_ROOT / OUTPUT_PATH).read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / OUTPUT_PATH
            target.parent.mkdir(parents=True)

            # When: The exact fixture is written and regenerated.
            write_golden_reference(root, Path(OUTPUT_PATH), content)
            write_golden_reference(root, Path(OUTPUT_PATH), content)

            # Then: Both writes preserve the one pinned identity.
            self.assertEqual(content, target.read_bytes())
            self.assertEqual(1, target.stat().st_nlink)

    def test_r_a_12_requires_the_exact_container_image_identity(self) -> None:
        # Given: The pinned image identity and a nonempty but different identity.
        pinned = (
            "sha256:"
            "e65e9f8b0ffafef5b5d2b9711c9a3411649ae80fd036cc79f0febb80b4c0b06e"
        )
        self.assertEqual(pinned, _container_image_identity(pinned))

        # When/Then: A merely nonempty identity cannot substitute for the pin.
        self.assert_failure(
            ReaderProvenanceError,
            "reference.reader.pin_mismatch",
            "gcsa reader provenance does not match the fixed P0-M1 pin",
            lambda: _container_image_identity(
                "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
            ),
        )

    def test_r_a_13_cli_failure_is_locator_free(self) -> None:
        # Given: An underlying failure containing an absolute private locator.
        def leaking_generate(_asset_root: Path) -> None:
            try:
                raise RuntimeError(
                    "reader leaked " + "/" + "tmp/private.bin"
                )
            except RuntimeError:
                raise DecodeError(
                    "reference.decode.failed",
                    "pinned gcsa reader failed for selected input",
                )

        stderr = io.StringIO()
        with (
            mock.patch(
                "golden_reference._generate",
                side_effect=leaking_generate,
            ),
            redirect_stderr(stderr),
        ):
            # When: The production CLI handles the typed failure.
            result = main(
                [
                    "generate",
                    "--asset-root",
                    "/" + "tmp/private-custody-root",
                ]
            )

        # Then: Only the stable typed message is emitted.
        self.assertEqual(1, result)
        self.assertEqual(
            (
                "reference.decode.failed: "
                "pinned gcsa reader failed for selected input\n"
            ),
            stderr.getvalue(),
        )
        self.assertNotIn("/" + "tmp/", stderr.getvalue())
        self.assertNotIn("Traceback", stderr.getvalue())

    def test_r_a_14_rejects_oversized_fixed_source_before_read(self) -> None:
        # Given: A fixed source path whose size has drifted beyond its pin.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            relative = PurePosixPath(MAPPING_PATH)
            target = root / Path(relative)
            target.parent.mkdir(parents=True)
            target.write_bytes(b"x" * (MAPPING_SIZE + 1))

            # When/Then: Size drift is typed before any source payload read.
            with mock.patch(
                "golden_reference.os.read",
                side_effect=AssertionError("oversized source must not be read"),
            ):
                self.assert_failure(
                    ReferenceSourceError,
                    "reference.source.identity_mismatch",
                    (
                        "fixed mapping or selection source identity "
                        "does not match its pin"
                    ),
                    lambda: _read_fixed_source(
                        root,
                        relative,
                        MAPPING_SIZE,
                    ),
                )


if __name__ == "__main__":
    unittest.main()
