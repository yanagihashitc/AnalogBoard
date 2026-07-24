from __future__ import annotations

import copy
import hashlib
import json
import subprocess
import sys
import unittest
from pathlib import Path
from typing import Callable

SCRIPT_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = SCRIPT_ROOT.parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from regression_harness import (  # noqa: E402
    CandidateCardinalityError,
    CandidateDtypeError,
    CandidateInputIdentityError,
    CandidateLabelError,
    CandidateMappingError,
    CandidatePairIdentityError,
    CandidatePayloadBoundaryError,
    CandidatePermutationError,
    CandidateReferenceIdentityError,
    CandidateSchemaError,
    CandidateShapeError,
    CandidateStatisticsError,
    CandidateValueError,
    GoldenSourceIdentityError,
    compare_candidate_bytes,
    compare_candidate_to_golden,
    serialize_regression_result,
)


GOLDEN_REFERENCE_PATH = (
    "docs/reference/d17-golden-regression/golden-reference-v1.json"
)
GOLDEN_REFERENCE_FILE = REPOSITORY_ROOT / GOLDEN_REFERENCE_PATH


def golden_source_bytes() -> bytes:
    return GOLDEN_REFERENCE_FILE.read_bytes()


def valid_candidate() -> dict[str, object]:
    """Build a product-neutral candidate mechanically from the tracked golden."""

    source = golden_source_bytes()
    golden = json.loads(source)
    return {
        "schema": "analogboard.d17.candidate-summary",
        "schema_version": 1,
        "reference": {
            "path": GOLDEN_REFERENCE_PATH,
            "sha256": hashlib.sha256(source).hexdigest(),
            "size_bytes": len(source),
        },
        "pair_count": golden["pair_count"],
        "channel_count_per_pair": golden["channel_count_per_pair"],
        "pairs": [
            {
                "density": pair["density"],
                "run_id": pair["run_id"],
                "ordinal": pair["ordinal"],
                "inputs": copy.deepcopy(pair["inputs"]),
                "event_count": pair["event_count"],
                "channels": copy.deepcopy(pair["channels"]),
            }
            for pair in golden["pairs"]
        ],
    }


def canonical_json_bytes(value: object) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        ).encode("utf-8")
        + b"\n"
    )


def expected_pass_result() -> dict[str, object]:
    source = golden_source_bytes()
    candidate_source = canonical_json_bytes(valid_candidate())
    return {
        "schema": "analogboard.d17.regression-result",
        "schema_version": 1,
        "status": "pass",
        "reference": {
            "path": GOLDEN_REFERENCE_PATH,
            "sha256": hashlib.sha256(source).hexdigest(),
            "size_bytes": len(source),
        },
        "candidate": {
            "sha256": hashlib.sha256(candidate_source).hexdigest(),
            "size_bytes": len(candidate_source),
        },
        "pair_count": 3,
        "channel_count_per_pair": 13,
        "compared_channel_count": 39,
    }


class RegressionHarnessTests(unittest.TestCase):
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
        self.assertNotIn(str(REPOSITORY_ROOT), str(raised.exception))

    def test_h_n_01_matching_candidate_returns_bounded_pass(self) -> None:
        # Given: A payload-free candidate mechanically copied from the golden.
        candidate = valid_candidate()

        # When: All candidate pairs and channels are compared to the golden.
        result = compare_candidate_bytes(
            canonical_json_bytes(candidate),
            golden_source_bytes(),
        )

        # Then: The bounded result proves all 39 channels passed.
        self.assertEqual(expected_pass_result(), result)
        self.assertNotIn("channels", result)
        self.assertNotIn("inputs", result)
        self.assertNotIn("reader", result)
        self.assertNotIn("sources", result)
        self.assertNotIn("reader", candidate)
        self.assertNotIn("sources", candidate)

    def test_h_n_02_pass_result_serialization_is_byte_deterministic(self) -> None:
        # Given: Two comparisons of the same exact candidate and golden bytes.
        candidate = valid_candidate()
        first_result = compare_candidate_bytes(
            canonical_json_bytes(copy.deepcopy(candidate)),
            golden_source_bytes(),
        )
        second_result = compare_candidate_bytes(
            canonical_json_bytes(copy.deepcopy(candidate)),
            golden_source_bytes(),
        )

        # When: Both bounded Pass results are canonically serialized.
        first = serialize_regression_result(first_result)
        second = serialize_regression_result(second_result)

        # Then: The evidence is byte-identical, canonical, and payload-free.
        self.assertIs(bytes, type(first))
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        self.assertFalse(first.endswith(b"\n\n"))
        self.assertEqual(expected_pass_result(), json.loads(first))

    def test_h_b_01_exact_three_by_thirteen_boundary_is_accepted(self) -> None:
        # Given: Exactly three ordered input pairs with 13 channels per pair.
        candidate = valid_candidate()
        self.assertEqual(3, len(candidate["pairs"]))
        self.assertEqual(
            [13, 13, 13],
            [len(pair["channels"]) for pair in candidate["pairs"]],
        )

        # When: The fixed cardinality boundary is compared.
        result = compare_candidate_bytes(
            canonical_json_bytes(candidate),
            golden_source_bytes(),
        )

        # Then: No partial or excess channel set is silently accepted.
        self.assertEqual(3, result["pair_count"])
        self.assertEqual(13, result["channel_count_per_pair"])
        self.assertEqual(39, result["compared_channel_count"])

    def test_h_a_01_channel_swap_is_typed_permutation_failure(self) -> None:
        # Given: Two complete adjacent channel records are swapped.
        candidate = valid_candidate()
        channels = candidate["pairs"][0]["channels"]
        channels[0], channels[1] = channels[1], channels[0]

        # When: Physical-channel order is compared before label/value fields.
        # Then: The swap is rejected specifically as a permutation.
        self.assert_failure(
            CandidatePermutationError,
            "candidate.channel_order.permutation",
            "candidate pair[0] physical-channel order does not match golden",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_02_label_drift_is_typed_label_failure(self) -> None:
        # Given: CH1 remains in place but carries a different label.
        candidate = valid_candidate()
        candidate["pairs"][0]["channels"][0]["label"] = "changed-label"

        # When: The channel label is compared after order is established.
        # Then: Label drift cannot be downgraded to a warning.
        self.assert_failure(
            CandidateLabelError,
            "candidate.channel.label_mismatch",
            "candidate pair[0] channel[0] label does not match golden",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_02_stream_drift_is_typed_mapping_failure(self) -> None:
        # Given: CH1 keeps its order and label but names the wrong stream.
        candidate = valid_candidate()
        candidate["pairs"][0]["channels"][0]["stream"] = "FH"

        # When: The remaining mapping address is compared.
        # Then: Stream drift is rejected as a mapping failure.
        self.assert_failure(
            CandidateMappingError,
            "candidate.channel.mapping_mismatch",
            (
                "candidate pair[0] channel[0] stream/source index "
                "does not match golden"
            ),
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_02_source_index_drift_is_typed_mapping_failure(self) -> None:
        # Given: CH1 keeps its order and label but uses another source index.
        candidate = valid_candidate()
        candidate["pairs"][0]["channels"][0]["source_index"] = 1

        # When: The remaining mapping address is compared.
        # Then: Source-index drift is rejected as a mapping failure.
        self.assert_failure(
            CandidateMappingError,
            "candidate.channel.mapping_mismatch",
            (
                "candidate pair[0] channel[0] stream/source index "
                "does not match golden"
            ),
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_03_missing_channel_is_typed_cardinality_failure(self) -> None:
        # Given: One channel is removed from the first complete pair.
        candidate = valid_candidate()
        candidate["pairs"][0]["channels"].pop()

        # When: Per-pair cardinality is checked before order and values.
        # Then: The 13 - 1 boundary fails closed.
        self.assert_failure(
            CandidateCardinalityError,
            "candidate.channel_count.missing",
            "candidate pair[0] must contain exactly 13 channels; found 12",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_03_excess_channel_is_typed_cardinality_failure(self) -> None:
        # Given: A duplicate channel is appended to the first complete pair.
        candidate = valid_candidate()
        candidate["pairs"][0]["channels"].append(
            copy.deepcopy(candidate["pairs"][0]["channels"][-1])
        )

        # When: Per-pair cardinality is checked before order and values.
        # Then: The 13 + 1 boundary fails closed.
        self.assert_failure(
            CandidateCardinalityError,
            "candidate.channel_count.excess",
            "candidate pair[0] must contain exactly 13 channels; found 14",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_03_duplicate_and_missing_at_thirteen_is_cardinality_failure(
        self,
    ) -> None:
        # Given: There are still 13 records, but CH1 is replaced by a second CH2.
        candidate = valid_candidate()
        channels = candidate["pairs"][0]["channels"]
        channels[0] = copy.deepcopy(channels[1])
        self.assertEqual(13, len(channels))

        # When: Channel membership is counted before permutation comparison.
        # Then: Duplicate-plus-missing cannot masquerade as a pure permutation.
        self.assert_failure(
            CandidateCardinalityError,
            "candidate.channel_set.invalid",
            (
                "candidate pair[0] must contain each golden physical "
                "channel exactly once"
            ),
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_04_dtype_drift_is_typed_dtype_failure(self) -> None:
        # Given: One channel claims a different byte order and dtype.
        candidate = valid_candidate()
        candidate["pairs"][0]["channels"][0]["dtype"] = ">u2"

        # When: Canonical dtype is compared after mapping.
        # Then: Representation drift is rejected as a dtype failure.
        self.assert_failure(
            CandidateDtypeError,
            "candidate.channel.dtype_mismatch",
            "candidate pair[0] channel[0] dtype does not match golden",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_04_shape_drift_is_typed_shape_failure(self) -> None:
        # Given: One channel has one extra sample per event.
        candidate = valid_candidate()
        candidate["pairs"][0]["channels"][0]["shape"][1] += 1

        # When: The complete channel shape is compared after dtype.
        # Then: Dimensional drift is rejected as a shape failure.
        self.assert_failure(
            CandidateShapeError,
            "candidate.channel.shape_mismatch",
            "candidate pair[0] channel[0] shape does not match golden",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_05_digest_drift_is_typed_value_failure(self) -> None:
        # Given: One syntactically valid channel digest is changed.
        candidate = valid_candidate()
        candidate["pairs"][0]["channels"][0]["sha256"] = "0" * 64

        # When: Digest identity is compared after shape.
        # Then: Value drift is rejected as a digest failure.
        self.assert_failure(
            CandidateValueError,
            "candidate.channel.digest_mismatch",
            "candidate pair[0] channel[0] digest does not match golden",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_05_statistics_drift_is_typed_value_failure(self) -> None:
        # Given: One bounded statistic changes while its digest stays fixed.
        candidate = valid_candidate()
        candidate["pairs"][0]["channels"][0]["statistics"]["sum"] += 1

        # When: Bounded statistics are compared after the digest.
        # Then: Summary drift is rejected as a statistics failure.
        self.assert_failure(
            CandidateStatisticsError,
            "candidate.channel.statistics_mismatch",
            "candidate pair[0] channel[0] statistics do not match golden",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_06_null_or_empty_candidate_is_typed_schema_failure(self) -> None:
        # Given: NULL and empty-object candidate boundaries.
        invalid_candidates = (None, {})

        # When: A comparison is requested without the required summary schema.
        # Then: Both boundaries fail with the same stable typed error.
        for candidate in invalid_candidates:
            with self.subTest(candidate=candidate):
                self.assert_failure(
                    CandidateSchemaError,
                    "candidate.schema.invalid",
                    (
                        "candidate must be an "
                        "analogboard.d17.candidate-summary v1 object"
                    ),
                    lambda candidate=candidate: compare_candidate_to_golden(
                        candidate,
                        golden_source_bytes(),
                    ),
                )

    def test_h_a_06_schema_drift_is_typed_schema_failure(self) -> None:
        # Given: An otherwise valid candidate names another schema version.
        candidate = valid_candidate()
        candidate["schema_version"] = 2

        # When: Candidate schema identity is validated.
        # Then: Schema drift is rejected before reference comparison.
        self.assert_failure(
            CandidateSchemaError,
            "candidate.schema.invalid",
            (
                "candidate must be an "
                "analogboard.d17.candidate-summary v1 object"
            ),
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_06_reference_pin_drift_is_typed_identity_failure(self) -> None:
        # Given: The candidate points at a different golden-reference digest.
        candidate = valid_candidate()
        candidate["reference"]["sha256"] = "0" * 64

        # When: The supplied golden bytes are checked against the candidate pin.
        # Then: Reference drift fails closed without exposing a host locator.
        self.assert_failure(
            CandidateReferenceIdentityError,
            "candidate.reference.identity_mismatch",
            (
                "candidate golden-reference identity does not match "
                "supplied source"
            ),
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_06_substituted_golden_source_is_typed_identity_failure(
        self,
    ) -> None:
        # Given: Candidate and source agree with each other but not the fixed pin.
        candidate = valid_candidate()
        substituted = json.loads(golden_source_bytes())
        substituted["pair_count"] = 2
        substituted_bytes = (
            json.dumps(
                substituted,
                ensure_ascii=True,
                sort_keys=True,
                separators=(",", ":"),
            ).encode("utf-8")
            + b"\n"
        )
        candidate["reference"]["sha256"] = hashlib.sha256(
            substituted_bytes
        ).hexdigest()
        candidate["reference"]["size_bytes"] = len(substituted_bytes)

        # When: An internally consistent but unpinned source is compared.
        # Then: The fixed P0-M1 source identity rejects the substitution.
        self.assert_failure(
            GoldenSourceIdentityError,
            "golden.source.identity_mismatch",
            "golden-reference source does not match the fixed P0-M1 pin",
            lambda: compare_candidate_to_golden(
                candidate,
                substituted_bytes,
            ),
        )

    def test_h_a_06_pair_identity_drift_is_typed_identity_failure(self) -> None:
        # Given: One ordered pair identifies a different run.
        candidate = valid_candidate()
        candidate["pairs"][0]["run_id"] = "000000_0000"

        # When: Pair identity is compared before channel cardinality.
        # Then: The mismatched pair fails with its bounded ordinal only.
        self.assert_failure(
            CandidatePairIdentityError,
            "candidate.pair.identity_mismatch",
            "candidate pair[0] identity does not match golden",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_06_input_identity_drift_is_typed_identity_failure(self) -> None:
        # Given: One input has a different but syntactically valid digest.
        candidate = valid_candidate()
        candidate["pairs"][0]["inputs"][0]["sha256"] = "0" * 64

        # When: Pair input identity is compared before channel cardinality.
        # Then: The mismatch is located without echoing the asset path.
        self.assert_failure(
            CandidateInputIdentityError,
            "candidate.input.identity_mismatch",
            "candidate pair[0] input[0] identity does not match golden",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_06_fl_fh_input_permutation_is_typed_identity_failure(
        self,
    ) -> None:
        # Given: The complete FL and FH input identities are swapped.
        candidate = valid_candidate()
        inputs = candidate["pairs"][0]["inputs"]
        inputs[0], inputs[1] = inputs[1], inputs[0]

        # When: Ordered pair-input identities are compared.
        # Then: The stream permutation is rejected at the first input ordinal.
        self.assert_failure(
            CandidateInputIdentityError,
            "candidate.input.identity_mismatch",
            "candidate pair[0] input[0] identity does not match golden",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_06_reader_or_sources_root_field_is_schema_failure(
        self,
    ) -> None:
        # Given: Product candidates try to copy gcsa reader/source provenance.
        for prohibited_root in ("reader", "sources"):
            candidate = valid_candidate()
            candidate[prohibited_root] = {}

            # When: The exact root-key contract is validated.
            # Then: Reader authority stays in the golden, not the candidate.
            with self.subTest(prohibited_root=prohibited_root):
                self.assert_failure(
                    CandidateSchemaError,
                    "candidate.schema.unexpected_field",
                    "candidate summary has unexpected root fields",
                    lambda candidate=candidate: compare_candidate_to_golden(
                        candidate,
                        golden_source_bytes(),
                    ),
                )

    def test_h_a_07_array_field_is_typed_payload_failure(self) -> None:
        # Given: A candidate channel embeds decoded sample values.
        candidate = valid_candidate()
        candidate["pairs"][0]["channels"][0]["decoded_samples"] = [1, 2]

        # When: The payload boundary is checked before structural comparison.
        # Then: The embedded array is rejected without serializing its values.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.prohibited_key",
            "candidate summary contains a prohibited payload field",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_07_bytes_are_typed_payload_failure(self) -> None:
        # Given: An extension value embeds raw bytes under a neutral key.
        candidate = valid_candidate()
        candidate["extension"] = b"\x00\x01"

        # When: The payload boundary recursively scans candidate values.
        # Then: Byte payload is rejected before schema normalization.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.bytes",
            "candidate summary contains byte payload",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_07_absolute_locator_is_typed_payload_failure(self) -> None:
        # Given: One input path is replaced with an absolute host locator.
        candidate = valid_candidate()
        candidate["pairs"][0]["inputs"][0]["path"] = (
            "/" + "private/custody/selected.bin"
        )

        # When: Candidate strings are checked before identity comparison.
        # Then: The path is rejected and never repeated in the failure message.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.absolute_locator",
            "candidate summary contains an absolute host locator",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_07_parent_traversal_is_typed_payload_failure(self) -> None:
        # Given: One input path attempts to escape its logical pair boundary.
        candidate = valid_candidate()
        candidate["pairs"][0]["inputs"][0]["path"] = "../selected.bin"

        # When: Candidate locators are checked before identity comparison.
        # Then: Traversal is rejected without repeating the locator.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.unsafe_locator",
            "candidate summary contains an unsafe relative locator",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_08_cyclic_candidate_is_typed_bounded_failure(self) -> None:
        # Given: An extension creates a cyclic non-JSON object graph.
        candidate = valid_candidate()
        extension: dict[str, object] = {}
        extension["cycle"] = extension
        candidate["extension"] = extension

        # When: The payload-first scanner traverses the candidate.
        # Then: The cycle fails closed instead of leaking RecursionError.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.structure_limit",
            "candidate summary exceeds bounded structural limits",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_08_deep_candidate_is_typed_bounded_failure(self) -> None:
        # Given: An extension nests beyond the bounded JSON-tree depth.
        candidate = valid_candidate()
        extension: dict[str, object] = {}
        cursor = extension
        for _ in range(40):
            child: dict[str, object] = {}
            cursor["nest"] = child
            cursor = child
        candidate["extension"] = extension

        # When: The payload-first scanner reaches the depth boundary.
        # Then: The deep tree fails closed before schema normalization.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.structure_limit",
            "candidate summary exceeds bounded structural limits",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_08_wide_candidate_is_typed_bounded_failure(self) -> None:
        # Given: The allowed pairs list exceeds the bounded summary node limit.
        candidate = valid_candidate()
        candidate["pairs"].extend([None] * 5_000)

        # When: The payload-first scanner counts candidate nodes.
        # Then: The oversized tree fails closed without unbounded processing.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.structure_limit",
            "candidate summary exceeds bounded structural limits",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_09_element_count_must_equal_shape_product(self) -> None:
        # Given: A channel statistic claims one fewer element than its shape.
        candidate = valid_candidate()
        statistics = candidate["pairs"][0]["channels"][0]["statistics"]
        statistics["element_count"] -= 1

        # When: Statistics invariants are checked after the digest.
        # Then: The internally invalid summary receives a stable typed failure.
        self.assert_failure(
            CandidateStatisticsError,
            "candidate.channel.statistics_invalid",
            (
                "candidate pair[0] channel[0] statistics are "
                "internally inconsistent"
            ),
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_09_nonzero_count_must_not_exceed_element_count(self) -> None:
        # Given: A channel reports more nonzero values than total elements.
        candidate = valid_candidate()
        statistics = candidate["pairs"][0]["channels"][0]["statistics"]
        statistics["nonzero_count"] = statistics["element_count"] + 1

        # When: Statistics invariants are checked after the digest.
        # Then: The impossible count receives a stable typed failure.
        self.assert_failure(
            CandidateStatisticsError,
            "candidate.channel.statistics_invalid",
            (
                "candidate pair[0] channel[0] statistics are "
                "internally inconsistent"
            ),
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_09_minimum_must_not_exceed_maximum(self) -> None:
        # Given: A channel's bounded minimum is greater than its maximum.
        candidate = valid_candidate()
        statistics = candidate["pairs"][0]["channels"][0]["statistics"]
        statistics["min"] = statistics["max"] + 1

        # When: Statistics invariants are checked after the digest.
        # Then: The impossible range receives a stable typed failure.
        self.assert_failure(
            CandidateStatisticsError,
            "candidate.channel.statistics_invalid",
            (
                "candidate pair[0] channel[0] statistics are "
                "internally inconsistent"
            ),
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_09_sum_must_be_coherent_with_range_and_nonzero_count(
        self,
    ) -> None:
        # Given: Positive values/nonzero elements paired with an impossible sum.
        candidate = valid_candidate()
        statistics = candidate["pairs"][0]["channels"][0]["statistics"]
        self.assertGreater(statistics["min"], 0)
        self.assertGreater(statistics["nonzero_count"], 0)
        statistics["sum"] = 0

        # When: Statistics coherence is checked after the matching digest.
        # Then: The impossible sum receives the same stable typed failure.
        self.assert_failure(
            CandidateStatisticsError,
            "candidate.channel.statistics_invalid",
            (
                "candidate pair[0] channel[0] statistics are "
                "internally inconsistent"
            ),
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_10_fabricated_pass_result_cannot_be_serialized(self) -> None:
        # Given: A caller hand-constructs the expected-looking Pass dictionary.
        fabricated = expected_pass_result()

        # When: It bypasses the all-channel comparator.
        # Then: The evidence serializer requires a comparison-produced result.
        self.assert_failure(
            CandidateSchemaError,
            "result.unverified",
            "regression result must originate from a successful comparison",
            lambda: serialize_regression_result(fabricated),
        )

    def test_h_a_10_mutated_verified_result_fails_typed(self) -> None:
        # Given: A valid result is mutated into a cyclic graph after comparison.
        result = compare_candidate_bytes(
            canonical_json_bytes(valid_candidate()),
            golden_source_bytes(),
        )
        result["candidate"]["cycle"] = result

        # When/Then: Seal verification fails typed without JSON encoder leakage.
        self.assert_failure(
            CandidateSchemaError,
            "result.unverified",
            "regression result must originate from a successful comparison",
            lambda: serialize_regression_result(result),
        )

    def test_h_a_10_long_string_is_typed_bounded_failure(self) -> None:
        # Given: One neutral extension contains an individually oversized string.
        candidate = valid_candidate()
        candidate["extension"] = "x" * 10_000

        # When: The payload-first scanner accounts for scalar size.
        # Then: The candidate fails before schema or canonical serialization.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.structure_limit",
            "candidate summary exceeds bounded structural limits",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_10_non_json_container_is_typed_payload_failure(self) -> None:
        # Given: A tuple attempts to bypass recursive JSON-tree inspection.
        candidate = valid_candidate()
        candidate["extension"] = (1, 2, 3)

        # When: The payload-first scanner encounters the non-JSON value.
        # Then: It fails typed instead of falling through to generic schema.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.non_json",
            "candidate summary contains a non-JSON value",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_10_windows_rooted_locator_is_typed_payload_failure(self) -> None:
        # Given: A Windows root-relative locator without a drive or UNC prefix.
        candidate = valid_candidate()
        candidate["pairs"][0]["inputs"][0]["path"] = (
            "\\" + "private\\selected.bin"
        )

        # When: Candidate strings are checked before input identity.
        # Then: Root-relative Windows input cannot bypass locator fencing.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.absolute_locator",
            "candidate summary contains an absolute host locator",
            lambda: compare_candidate_to_golden(
                candidate,
                golden_source_bytes(),
            ),
        )

    def test_h_a_11_duplicate_json_key_is_typed_source_failure(self) -> None:
        # Given: A later canonical schema version would overwrite an earlier key.
        canonical = canonical_json_bytes(valid_candidate())
        duplicated = b'{"schema_version":2,' + canonical[1:]

        # When: The public document seam parses the candidate.
        # Then: Duplicate keys cannot inherit json.loads last-key-wins behavior.
        self.assert_failure(
            CandidateSchemaError,
            "candidate.source.invalid",
            "candidate source must be strict bounded JSON",
            lambda: compare_candidate_bytes(
                duplicated,
                golden_source_bytes(),
            ),
        )

    def test_h_a_11_nonfinite_json_is_typed_source_failure(self) -> None:
        # Given: A standard-library extension token appears in a required field.
        canonical = canonical_json_bytes(valid_candidate())
        nonfinite = canonical.replace(
            b'"ordinal":1',
            b'"ordinal":NaN',
            1,
        )
        self.assertNotEqual(canonical, nonfinite)

        # When/Then: The owned parser rejects non-standard numeric tokens.
        self.assert_failure(
            CandidateSchemaError,
            "candidate.source.invalid",
            "candidate source must be strict bounded JSON",
            lambda: compare_candidate_bytes(
                nonfinite,
                golden_source_bytes(),
            ),
        )

    def test_h_a_11_candidate_document_size_is_bounded_before_parse(self) -> None:
        # Given: Candidate input exceeds the fixed 64 KiB document boundary.
        oversized = b" " * (65_536 + 1)

        # When/Then: Size fails typed before JSON parsing.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.structure_limit",
            "candidate summary exceeds bounded structural limits",
            lambda: compare_candidate_bytes(
                oversized,
                golden_source_bytes(),
            ),
        )

    def test_h_a_11_neutral_array_field_is_typed_payload_failure(self) -> None:
        # Given: An unauthorized root field embeds a JSON array.
        candidate = valid_candidate()
        candidate["extension"] = [1, 2, 3]

        # When: The payload-first scanner follows the owned parsed document.
        # Then: Arrays remain restricted to the four allowlisted field names.
        self.assert_failure(
            CandidatePayloadBoundaryError,
            "candidate.payload.prohibited_key",
            "candidate summary contains a prohibited payload field",
            lambda: compare_candidate_bytes(
                canonical_json_bytes(candidate),
                golden_source_bytes(),
            ),
        )

    def test_h_a_11_channel_count_metadata_has_specific_failure(self) -> None:
        # Given: Pair count is correct but channel-count metadata drifts.
        candidate = valid_candidate()
        candidate["channel_count_per_pair"] = 12

        # When/Then: The drift is not mislabeled as a pair-count failure.
        self.assert_failure(
            CandidateCardinalityError,
            "candidate.channel_count.metadata",
            "candidate channel_count_per_pair must be exactly 13",
            lambda: compare_candidate_bytes(
                canonical_json_bytes(candidate),
                golden_source_bytes(),
            ),
        )

    def test_h_n_03_stdin_cli_is_a_stable_document_entrypoint(self) -> None:
        # Given: A canonical product-neutral candidate document on standard input.
        candidate_source = canonical_json_bytes(valid_candidate())

        # When: The tracked harness script owns parsing and fixed-golden loading.
        completed = subprocess.run(
            [sys.executable, str(SCRIPT_ROOT / "regression_harness.py"), "compare"],
            input=candidate_source,
            capture_output=True,
            check=False,
        )

        # Then: It emits only canonical bounded Pass evidence.
        self.assertEqual(0, completed.returncode)
        self.assertEqual(b"", completed.stderr)
        self.assertEqual(expected_pass_result(), json.loads(completed.stdout))

    def test_h_a_12_cli_failure_is_stable_and_locator_free(self) -> None:
        # Given: Duplicate-key candidate input at the production CLI boundary.
        canonical = canonical_json_bytes(valid_candidate())
        duplicated = b'{"schema_version":2,' + canonical[1:]

        # When: The strict parser rejects the candidate.
        completed = subprocess.run(
            [sys.executable, str(SCRIPT_ROOT / "regression_harness.py"), "compare"],
            input=duplicated,
            capture_output=True,
            check=False,
        )

        # Then: No traceback, host path, or untrusted value is emitted.
        self.assertEqual(1, completed.returncode)
        self.assertEqual(b"", completed.stdout)
        self.assertEqual(
            (
                b"candidate.source.invalid: "
                b"candidate source must be strict bounded JSON\n"
            ),
            completed.stderr,
        )
        self.assertNotIn(b"Traceback", completed.stderr)
        self.assertNotIn(str(REPOSITORY_ROOT).encode(), completed.stderr)


if __name__ == "__main__":
    unittest.main()
