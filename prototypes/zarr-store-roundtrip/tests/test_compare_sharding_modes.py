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
joint.EXPECTED_GCSA_CONTAINER_ID = (
    "d141d00e5edb0bd17ee37836340a4315343019d32db4f9197322e9a3a5c9e1d8"
)
joint.EXPECTED_GCSA_IMAGE_ID = (
    "sha256:e65e9f8b0ffafef5b5d2b9711c9a3411649ae80fd036cc79f0febb80b4c0b06e"
)
joint.CONTRACT_ID = "gcsa-store-a4a-rc1"
joint.EXPECTED_PUBLIC_KAT_SHA256 = (
    "cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa"
)
joint.load_strict_json_object = lambda path: json.loads(path.read_text())

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
                "gcsa_container_id": joint.EXPECTED_GCSA_CONTAINER_ID,
                "gcsa_image_id": joint.EXPECTED_GCSA_IMAGE_ID,
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
