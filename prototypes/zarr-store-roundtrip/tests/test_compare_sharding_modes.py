from __future__ import annotations

import copy
import importlib.util
import json
import sys
import tempfile
import types
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = (
    Path(__file__).parents[1] / "scripts" / "compare_sharding_modes.py"
)

joint = types.ModuleType("validate_gcsa_roundtrip")
joint.CheckFailure = RuntimeError
joint.EXPECTED_GCSA_SNAPSHOT_COMMIT = "20689a991697217518ec2ff15aaaa2533b169eb0"
joint.EXPECTED_GCSA_PACKAGE_TREE_SHA256 = (
    "c63c79c4add3a8034cd1486921470818ad71d024ace1e8e356ae4f8dbf396d14"
)
joint.EXPECTED_GCSA_IMAGE_ID = (
    "sha256:e65e9f8b0ffafef5b5d2b9711c9a3411649ae80fd036cc79f0febb80b4c0b06e"
)
joint.EXPECTED_GCSA_RUNTIME_POLICY = {
    "container_lifecycle": "fresh--rm",
    "image_pull": "never",
    "network": "none",
    "privileges": "no-new-privileges",
    "repository_mount": "read-only",
    "rootfs": "read-only",
    "writable_tmpfs": "/tmp-64m",
}
joint.EXPECTED_GOLDEN_RELATIVE_PATH = Path(
    "docs/reference/zarr-store-contract/phase0-roundtrip/"
    "joint-roundtrip-golden.json"
)
joint.CONTRACT_ID = "gcsa-store-a4a-rc1"
joint.EXPECTED_PUBLIC_KAT_SHA256 = (
    "cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa"
)
joint.load_strict_json_object = lambda path: json.loads(path.read_text())
joint.require_expected_joint_provenance = lambda path: None

SPEC = importlib.util.spec_from_file_location("compare_sharding_modes", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load comparator: {SCRIPT_PATH}")
comparator = importlib.util.module_from_spec(SPEC)
with mock.patch.dict(sys.modules, {"validate_gcsa_roundtrip": joint}):
    SPEC.loader.exec_module(comparator)


class MappingTests(unittest.TestCase):
    def test_both_candidate_mappings_cover_five_events_once(self) -> None:
        # Given: The fixed five-event, two-partition comparison fixture.
        # When: Both candidate mappings are calculated.
        round_robin = comparator.global_events_by_partition("round-robin")
        sequential = comparator.global_events_by_partition("append-sequential")

        # Then: Each is discriminating, aligned, and covers the same event domain.
        self.assertEqual(round_robin, ((0, 2, 4), (1, 3)))
        self.assertEqual(sequential, ((0, 1), (2, 3, 4)))
        for mapping in (round_robin, sequential):
            self.assertEqual(sorted(mapping[0] + mapping[1]), list(range(5)))

    def test_unknown_mapping_fails_loud(self) -> None:
        with self.assertRaisesRegex(ValueError, "unsupported sharding mode"):
            comparator.global_events_by_partition("striped")


class ObservationTests(unittest.TestCase):
    def test_three_wall_samples_have_stable_integer_summary(self) -> None:
        # Given: Three alternating-run observations in non-sorted order.
        # When: The bounded observational summary is built.
        summary = comparator.summarize_wall_times([19, 11, 13])

        # Then: Raw samples and deterministic min/median/max are retained.
        self.assertEqual(
            summary,
            {"samples_ms": [19, 11, 13], "min_ms": 11, "median_ms": 13, "max_ms": 19},
        )

    def test_wall_samples_reject_wrong_count_bool_and_negative(self) -> None:
        invalid = ([1, 2], [1, True, 3], [1, -1, 3])
        for samples in invalid:
            with self.subTest(samples=samples), self.assertRaises(ValueError):
                comparator.summarize_wall_times(samples)

    def test_directory_metrics_count_only_regular_files_and_chunks(self) -> None:
        # Given: A miniature store tree with two measurement chunks and metadata.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = (
                root
                / "datasets/tube_1/pulse_features/partition_0.zarr/0.0"
            )
            second = root / "datasets/tube_1/gmi_waveform/partition_1.zarr/0.0.0"
            metadata = first.parent / ".zarray"
            for path, payload in (
                (first, b"123"),
                (second, b"12345"),
                (metadata, b"{}"),
            ):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(payload)

            # When: Bounded byte/file metrics are collected.
            observed = comparator.collect_directory_metrics(root)

            # Then: Measurement wires are separated from total regular files.
            self.assertEqual(observed["regular_files"], 3)
            self.assertEqual(observed["total_regular_file_bytes"], 10)
            self.assertEqual(observed["measurement_chunk_files"], 2)
            self.assertEqual(observed["measurement_wire_bytes"], 8)
            self.assertEqual(observed["array_partition_directories"], 2)


class EvidenceManifestTests(unittest.TestCase):
    @staticmethod
    def _manifest(root: Path) -> tuple[Path, dict[str, object]]:
        source = root / "source.txt"
        source.write_text("accepted source\n", encoding="utf-8")
        source_sha = comparator.hashlib.sha256(source.read_bytes()).hexdigest()
        document: dict[str, object] = {
            "schema": "analogboard-p0-s-evidence-manifest-v1",
            "generated_at": "2026-07-22",
            "command": "scripts/zarr-roundtrip/run-focused-verification.sh sharding",
            "identities": {
                "analogboard_baseline_commit": (
                    "da818ba245be252a3717bf9bf3d55c57fa20594e"
                ),
                "gcsa_commit": joint.EXPECTED_GCSA_SNAPSHOT_COMMIT,
                "gcsa_package_tree_sha256": (
                    joint.EXPECTED_GCSA_PACKAGE_TREE_SHA256
                ),
                "gcsa_image_id": joint.EXPECTED_GCSA_IMAGE_ID,
                "gcsa_runtime_policy": joint.EXPECTED_GCSA_RUNTIME_POLICY,
                "contract_id": joint.CONTRACT_ID,
                "public_kat_sha256": joint.EXPECTED_PUBLIC_KAT_SHA256,
            },
            "files_sha256": {"source.txt": source_sha},
            "exclusions": {
                "manifest_self_hash": True,
                "parent_plan_and_central_handoff": True,
                "generated_measurement_payload": True,
            },
        }
        manifest = (
            root
            / "docs/reference/zarr-store-contract/phase0-roundtrip"
            / "phase0-roundtrip-manifest.json"
        )
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(json.dumps(document), encoding="utf-8")
        return manifest, document

    def test_manifest_accepts_exact_identity_and_source_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, _ = self._manifest(root)

            with mock.patch.object(
                comparator,
                "EXPECTED_MANIFEST_SOURCE_PATHS",
                ("source.txt",),
            ):
                comparator.verify_evidence_manifest(manifest)

    def test_manifest_pins_and_executes_joint_validator_provenance(self) -> None:
        # Given: The sharding manifest inventory and a valid tracked joint
        # golden whose validator is imported by the comparator.
        required = {
            "prototypes/zarr-store-roundtrip/scripts/validate_gcsa_roundtrip.py",
            "prototypes/zarr-store-roundtrip/tests/test_focused_verification_script.py",
            "prototypes/zarr-store-roundtrip/tests/test_validate_gcsa_roundtrip.py",
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest, _ = self._manifest(root)

            # When: Manifest validation completes with an exact source set.
            with mock.patch.object(
                comparator,
                "EXPECTED_MANIFEST_SOURCE_PATHS",
                ("source.txt",),
            ), mock.patch.object(
                joint,
                "require_expected_joint_provenance",
                create=True,
            ) as require_joint:
                comparator.verify_evidence_manifest(manifest)

            # Then: The real inventory includes the imported validator/test and
            # validation delegates to the immutable joint golden provenance.
            self.assertTrue(
                required.issubset(comparator.EXPECTED_MANIFEST_SOURCE_PATHS)
            )
            require_joint.assert_called_once_with(
                root / joint.EXPECTED_GOLDEN_RELATIVE_PATH
            )

    def test_manifest_rejects_identity_source_and_structure_drift(self) -> None:
        mutations = {
            "identity": lambda document: document["identities"].update(
                {"contract_id": "wrong"}
            ),
            "source": lambda document: document["files_sha256"].update(
                {"source.txt": "0" * 64}
            ),
            "structure": lambda document: document.update({"unexpected": True}),
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for label, mutate in mutations.items():
                manifest, document = self._manifest(root)
                mutate(document)
                manifest.write_text(json.dumps(document), encoding="utf-8")

                with self.subTest(label=label), mock.patch.object(
                    comparator,
                    "EXPECTED_MANIFEST_SOURCE_PATHS",
                    ("source.txt",),
                ), self.assertRaises(RuntimeError):
                    comparator.verify_evidence_manifest(manifest)


class ExpectedEvidenceTests(unittest.TestCase):
    @staticmethod
    def _summary() -> dict[str, object]:
        return {
            "schema": "analogboard-p0-s-sharding-comparison-v1",
            "observational_metrics": {
                "wall_time": {
                    "round-robin": comparator.summarize_wall_times([10, 12, 11]),
                    "append-sequential": comparator.summarize_wall_times(
                        [9, 8, 10]
                    ),
                }
            },
            "verification": {
                "positive_checks": 37,
                "expected_rejections": 9,
            },
            "decision": {"selected": "round-robin"},
        }

    def test_expected_evidence_allows_only_valid_wall_time_variation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = root / comparator.EXPECTED_COMPARISON_RELATIVE_PATH
            evidence.parent.mkdir(parents=True)
            summary = self._summary()
            expected = copy.deepcopy(summary)
            expected["observational_metrics"]["wall_time"] = {
                "round-robin": comparator.summarize_wall_times([20, 22, 21]),
                "append-sequential": comparator.summarize_wall_times([19, 18, 20]),
            }
            evidence.write_text(json.dumps(expected), encoding="utf-8")

            comparator.verify_expected_evidence(evidence, summary, root)

    def test_expected_evidence_rejects_semantic_and_timing_summary_drift(self) -> None:
        mutations = {
            "decision": lambda document: document["decision"].update(
                {"selected": "append-sequential"}
            ),
            "timing": lambda document: document["observational_metrics"][
                "wall_time"
            ]["round-robin"].update({"median_ms": 999}),
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = root / comparator.EXPECTED_COMPARISON_RELATIVE_PATH
            evidence.parent.mkdir(parents=True)
            for label, mutate in mutations.items():
                expected = self._summary()
                mutate(expected)
                evidence.write_text(json.dumps(expected), encoding="utf-8")

                with self.subTest(label=label), self.assertRaises(RuntimeError):
                    comparator.verify_expected_evidence(
                        evidence,
                        self._summary(),
                        root,
                    )


class DecisionTests(unittest.TestCase):
    def test_only_current_compatible_candidate_selects_round_robin(self) -> None:
        self.assertEqual(
            comparator.select_sharding_mode(
                round_robin_compatible=True,
                append_sequential_compatible=False,
            ),
            "round-robin",
        )

    def test_ambiguous_or_incompatible_results_refuse_a_decision(self) -> None:
        for round_robin, sequential in ((False, False), (False, True), (True, True)):
            with self.subTest(
                round_robin=round_robin,
                sequential=sequential,
            ), self.assertRaisesRegex(RuntimeError, "cannot adopt round-robin"):
                comparator.select_sharding_mode(
                    round_robin_compatible=round_robin,
                    append_sequential_compatible=sequential,
                )

    def test_expected_failure_rejects_return_and_wrong_exception_type(self) -> None:
        checks = types.SimpleNamespace(positive=0, negative=0)

        with self.assertRaisesRegex(RuntimeError, "accepted invalid input"):
            comparator.require_expected_failure(
                "sequential strict",
                lambda: None,
                (ValueError,),
                "distribution mismatch",
                checks,
            )

        def wrong_failure() -> None:
            raise TypeError("distribution mismatch")

        with self.assertRaisesRegex(RuntimeError, "unexpected failure type"):
            comparator.require_expected_failure(
                "sequential strict",
                wrong_failure,
                (ValueError,),
                "distribution mismatch",
                checks,
            )

        def expected_failure() -> None:
            raise ValueError("distribution mismatch")

        result = comparator.require_expected_failure(
            "sequential strict",
            expected_failure,
            (ValueError,),
            "distribution mismatch",
            checks,
        )
        self.assertEqual(result["exception"], "ValueError")
        self.assertEqual(checks.positive, 0)
        self.assertEqual(checks.negative, 1)


class ReaderOutcomeTests(unittest.TestCase):
    class StoreContractValidationError(ValueError):
        pass

    class Array:
        def __init__(self, values: object) -> None:
            self.values = tuple(values)

        def __getitem__(self, key: object) -> ReaderOutcomeTests.Array:
            if isinstance(key, list):
                return self.__class__(self.values[index] for index in key)
            selected = self.values[key]
            if isinstance(key, slice):
                return self.__class__(selected)
            return selected

        def __eq__(self, other: object) -> bool:
            return isinstance(other, self.__class__) and self.values == other.values

    class Checks:
        def __init__(self) -> None:
            self.positive = 0
            self.negative = 0

        def require(self, condition: bool, message: str) -> None:
            self.positive += 1
            if not condition:
                raise RuntimeError(message)

    @classmethod
    def _reader_patch(
        cls,
        *,
        round_robin_wrong_data: bool = False,
        round_robin_exception: Exception | None = None,
        append_global_success: bool = False,
    ) -> mock._patch:
        expected = {
            name: cls.Array(range(comparator.SYNTHETIC_EVENT_COUNT))
            for name in comparator.MEASUREMENT_ARRAYS
        }

        class Store:
            def __init__(
                self,
                root: Path,
                *,
                config: object,
                readonly: bool,
            ) -> None:
                del config
                self.mode = str(root)
                self.readonly = readonly

            def list_datasets(self) -> list[str]:
                return ["tube_1"]

            def get_meta(self, dataset_id: str) -> types.SimpleNamespace:
                if dataset_id != "tube_1":
                    raise AssertionError("unexpected dataset")
                append = self.mode == "append-sequential"
                manifests = {
                    name: [
                        types.SimpleNamespace(row_count=2, sealed=True),
                        types.SimpleNamespace(row_count=3, sealed=True),
                    ]
                    for name in comparator.MEASUREMENT_ARRAYS
                }
                return types.SimpleNamespace(
                    events_per_partition=[2, 3] if append else [3, 2],
                    write_generation=2,
                    extra={
                        "partition_sharding": (
                            comparator.APPEND_SEQUENTIAL_MODE
                            if append
                            else comparator.ROUND_ROBIN_MODE
                        )
                    },
                    partition_manifests=manifests,
                )

        def strict_validate(root: Path) -> object:
            if str(root) == "append-sequential":
                raise cls.StoreContractValidationError(
                    comparator.SEQUENTIAL_STRICT_ERROR
                )
            return object()

        def require_report(
            report: object,
            checks: ReaderOutcomeTests.Checks,
            label: str,
        ) -> None:
            del report, label
            for _ in range(4):
                checks.require(True, "strict report")

        def read_measurement(
            store: Store,
            array_name: str,
            *,
            event_slice: slice | None = None,
            event_indices: list[int] | None = None,
            partition: int | None = None,
        ) -> ReaderOutcomeTests.Array:
            if store.mode == "round-robin":
                if round_robin_exception is not None:
                    raise round_robin_exception
                if (
                    round_robin_wrong_data
                    and array_name == "pulse_features"
                    and event_slice is None
                    and event_indices is None
                    and partition is None
                ):
                    return cls.Array([-1])
                mapping = comparator.global_events_by_partition(
                    comparator.ROUND_ROBIN_MODE
                )
            else:
                if partition is None and not append_global_success:
                    raise ValueError(comparator.SEQUENTIAL_GLOBAL_ERROR)
                mapping = comparator.global_events_by_partition(
                    comparator.APPEND_SEQUENTIAL_MODE
                )
            actual = expected[array_name]
            if partition is not None:
                return actual[list(mapping[partition])]
            if event_indices is not None:
                return actual[event_indices]
            if event_slice is not None:
                return actual[event_slice]
            return actual

        return mock.patch.multiple(
            joint,
            DATASET_ID="tube_1",
            StoreContractValidationError=cls.StoreContractValidationError,
            ZarrStore=Store,
            store_config=lambda: object(),
            expected_arrays=lambda: expected,
            strict_validate=strict_validate,
            require_report=require_report,
            read_measurement=read_measurement,
            arrays_equal=lambda _name, actual, wanted: actual == wanted,
            tree_digest=lambda _root: "unchanged",
            create=True,
        )

    def test_build_summary_uses_successful_reader_outcomes(self) -> None:
        # Given: Strict and product-reader seams with accepted RR behavior and
        # expected append-sequential rejection behavior.
        checks = self.Checks()

        # When: Both candidate validators and the summary builder run.
        with self._reader_patch(), mock.patch.object(
            comparator,
            "collect_directory_metrics",
            return_value={
                "regular_files": 1,
                "total_regular_file_bytes": 1,
                "measurement_chunk_files": 1,
                "measurement_wire_bytes": 1,
                "array_partition_directories": 1,
            },
        ):
            summary = comparator.build_summary(
                Path("round-robin"),
                Path("append-sequential"),
                [10, 11, 12],
                [9, 10, 11],
                checks,
            )

        # Then: Compatibility and selection reflect the observed reader outcomes.
        self.assertEqual(summary["decision"]["selected"], "round-robin")
        self.assertEqual(
            summary["modes"]["round-robin"]["compatibility"],
            {
                "strict": "pass",
                "partition_local": "pass",
                "full": "pass",
                "slice": "pass",
                "gather": "pass",
            },
        )
        self.assertEqual(
            summary["modes"]["append-sequential"]["compatibility"]["full"],
            "expected-rejection",
        )
        self.assertEqual(summary["verification"], {
            "positive_checks": 37,
            "expected_rejections": 9,
        })

    def test_validators_return_observed_compatibility_records(self) -> None:
        # Given: Accepted RR reads and the expected append-sequential rejection.
        round_robin_checks = self.Checks()
        append_checks = self.Checks()

        # When: Each validator completes its reader matrix.
        with self._reader_patch():
            round_robin = comparator.validate_round_robin(
                Path("round-robin"),
                round_robin_checks,
            )
            append = comparator.validate_append_sequential(
                Path("append-sequential"),
                append_checks,
            )

        # Then: Each returns the compatibility record established by those reads.
        self.assertEqual(round_robin["strict"], "pass")
        self.assertEqual(round_robin["full"], "pass")
        self.assertEqual(append["strict"], "expected-rejection")
        self.assertEqual(append["full"], "expected-rejection")
        self.assertEqual(round_robin_checks.positive, 23)
        self.assertEqual(append_checks.positive, 14)
        self.assertEqual(append_checks.negative, 9)

    def test_build_summary_rejects_incompatible_validator_outcome(self) -> None:
        # Given: A completed validator record where RR is not globally compatible.
        incompatible_round_robin = {
            "strict": "pass",
            "partition_local": "pass",
            "full": "expected-rejection",
            "slice": "pass",
            "gather": "pass",
        }
        incompatible_append = {
            "strict": "expected-rejection",
            "partition_local": "pass",
            "full": "expected-rejection",
            "slice": "expected-rejection",
            "gather": "expected-rejection",
            "strict_rejection": {},
            "global_rejection": {},
        }

        # When/Then: Selection derives from the records and refuses adoption.
        with mock.patch.object(
            comparator,
            "validate_round_robin",
            return_value=incompatible_round_robin,
        ), mock.patch.object(
            comparator,
            "validate_append_sequential",
            return_value=incompatible_append,
        ), self.assertRaisesRegex(RuntimeError, "cannot adopt round-robin"):
            comparator.build_summary(
                Path("round-robin"),
                Path("append-sequential"),
                [10, 11, 12],
                [9, 10, 11],
                self.Checks(),
            )

    def test_round_robin_wrong_data_fails_loud(self) -> None:
        # Given: A RR reader that returns incorrect full pulse-feature data.
        checks = self.Checks()

        # When/Then: Validation rejects the observed data with a stable message.
        with self._reader_patch(
            round_robin_wrong_data=True
        ), self.assertRaisesRegex(RuntimeError, "pulse_features full/global drift"):
            comparator.validate_round_robin(Path("round-robin"), checks)

    def test_round_robin_reader_exception_is_not_converted_to_pass(self) -> None:
        # Given: A RR product reader that fails before returning data.
        checks = self.Checks()

        # When/Then: The original reader failure propagates without an outcome.
        with self._reader_patch(
            round_robin_exception=OSError("reader unavailable")
        ), self.assertRaisesRegex(OSError, "reader unavailable"):
            comparator.validate_round_robin(Path("round-robin"), checks)

    def test_append_global_read_success_fails_loud(self) -> None:
        # Given: Append-sequential local reads are aligned, but a global read
        # unexpectedly succeeds instead of raising the accepted incompatibility.
        checks = self.Checks()

        # When/Then: Validation refuses to report the mode as incompatible.
        with self._reader_patch(
            append_global_success=True
        ), self.assertRaisesRegex(
            RuntimeError,
            "append-sequential pulse_features full: accepted invalid input",
        ):
            comparator.validate_append_sequential(
                Path("append-sequential"),
                checks,
            )


class CliTests(unittest.TestCase):
    def test_cli_requires_both_roots_and_three_samples_per_mode(self) -> None:
        arguments = [
            "compare_sharding_modes.py",
            "--round-robin-store",
            "/stores/rr",
            "--append-sequential-store",
            "/stores/seq",
            "--round-robin-wall-ms",
            "10",
            "--round-robin-wall-ms",
            "12",
            "--round-robin-wall-ms",
            "11",
            "--append-sequential-wall-ms",
            "9",
            "--append-sequential-wall-ms",
            "8",
            "--append-sequential-wall-ms",
            "10",
            "--evidence-manifest",
            "/repository/phase0-roundtrip-manifest.json",
            "--expected-evidence",
            "/repository/sharding-comparison.json",
        ]

        with mock.patch.object(sys, "argv", arguments):
            parsed = comparator.parse_args()

        self.assertEqual(parsed.round_robin_store, Path("/stores/rr"))
        self.assertEqual(parsed.append_sequential_store, Path("/stores/seq"))
        self.assertEqual(parsed.round_robin_wall_ms, [10, 12, 11])
        self.assertEqual(parsed.append_sequential_wall_ms, [9, 8, 10])
        self.assertEqual(
            parsed.evidence_manifest,
            Path("/repository/phase0-roundtrip-manifest.json"),
        )
        self.assertEqual(
            parsed.expected_evidence,
            Path("/repository/sharding-comparison.json"),
        )

    def test_cli_requires_the_tracked_comparison_evidence(self) -> None:
        arguments = [
            "compare_sharding_modes.py",
            "--round-robin-store",
            "/stores/rr",
            "--append-sequential-store",
            "/stores/seq",
            "--round-robin-wall-ms",
            "10",
            "--round-robin-wall-ms",
            "11",
            "--round-robin-wall-ms",
            "12",
            "--append-sequential-wall-ms",
            "9",
            "--append-sequential-wall-ms",
            "10",
            "--append-sequential-wall-ms",
            "11",
            "--evidence-manifest",
            "/repository/phase0-roundtrip-manifest.json",
        ]

        with mock.patch.object(sys, "argv", arguments), self.assertRaises(SystemExit):
            comparator.parse_args()


if __name__ == "__main__":
    unittest.main()
