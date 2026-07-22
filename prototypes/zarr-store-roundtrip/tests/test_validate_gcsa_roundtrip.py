from __future__ import annotations

import importlib.util
import sys
import tempfile
import types
import unittest
from copy import deepcopy
from pathlib import Path
from unittest import mock


SCRIPT_PATH = (
    Path(__file__).parents[1] / "scripts" / "validate_gcsa_roundtrip.py"
)


def _module(name: str, **attributes: object) -> types.ModuleType:
    module = types.ModuleType(name)
    for attribute, value in attributes.items():
        setattr(module, attribute, value)
    return module


class _Candidate:
    def __init__(self, rank: int, trailing_shape: tuple[int, ...]) -> None:
        self.rank = rank
        self.trailing_shape = trailing_shape


gcsa = _module("gcsa", __file__="/unused/gcsa/__init__.py")
visibility = _module(
    "gcsa.data_model.visibility",
    DatasetNotFinalizedError=RuntimeError,
)
aead = _module(
    "gcsa.store._zarr_aead",
    AEAD_HEADER_SIZE=16,
    AEAD_MAGIC=b"GCSA",
    AEAD_NONCE_SIZE=12,
    AeadChunkContext=object,
    decrypt_chunk=lambda *args, **kwargs: b"",
    encrypt_chunk=lambda *args, **kwargs: b"",
)
schema = _module(
    "gcsa.store.schema",
    ARRAY_WIRE_CANDIDATES={
        "pulse_features": _Candidate(2, (24,)),
        "gmi_waveform": _Candidate(3, (5, 2400)),
        "fl_waveform": _Candidate(3, (8, 2400)),
    },
    FL_CHANNEL_ORDER=("FSC", "SSC", "FL1", "FL2", "FL3", "FL4", "FL5", "FL6"),
    GMI_CHANNEL_ORDER=("fsGMI", "ssGMI", "flGMI", "dGMI", "bfGMI"),
    PULSE_FEATURE_COLUMNS=tuple(f"feature_{index}" for index in range(24)),
    STORE_FORMAT_MARKER=".gcsa_store.json",
    StoreContractValidationError=RuntimeError,
    validate_analogboard_store=lambda *args, **kwargs: object(),
)
zarr_store = _module(
    "gcsa.store.zarr_store",
    ZarrStore=object,
    ZarrStoreConfig=object,
)
stubs = {
    "numpy": _module("numpy"),
    "gcsa": gcsa,
    "gcsa.data_model": _module("gcsa.data_model"),
    "gcsa.data_model.visibility": visibility,
    "gcsa.store": _module("gcsa.store"),
    "gcsa.store._zarr_aead": aead,
    "gcsa.store.schema": schema,
    "gcsa.store.zarr_store": zarr_store,
}

SPEC = importlib.util.spec_from_file_location("validate_gcsa_roundtrip", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load validator: {SCRIPT_PATH}")
validator = importlib.util.module_from_spec(SPEC)
with mock.patch.dict(sys.modules, stubs):
    SPEC.loader.exec_module(validator)


class FiveEventFixtureTests(unittest.TestCase):
    def test_fixture_uses_five_global_events_and_round_robin_rows(self) -> None:
        # Given: The joint fixture shared with the C++ writer.
        # When: Its event count, shapes, and partition mapping are inspected.
        shapes = validator.EXPECTED_SHAPES

        # Then: Five events map to uneven [3, 2] round-robin partitions.
        self.assertEqual(validator.SYNTHETIC_EVENT_COUNT, 5)
        self.assertEqual(shapes["pulse_features"], (5, 24))
        self.assertEqual(shapes["gmi_waveform"], (5, 5, 2400))
        self.assertEqual(shapes["fl_waveform"], (5, 8, 2400))
        self.assertEqual(validator.partition_global_events(0), (0, 2, 4))
        self.assertEqual(validator.partition_global_events(1), (1, 3))

    def test_fixture_preserves_float_boundaries_and_fourteen_bit_values(
        self,
    ) -> None:
        # Given: Boundary feature coordinates and the largest waveform coordinate.
        # When: Their synthetic values are calculated without NumPy.
        negative_zero = validator.synthetic_feature_bits(0, 0)
        positive_zero = validator.synthetic_feature_bits(1, 0)
        minimum_subnormal = validator.synthetic_feature_bits(0, 1)
        maximum_waveform = validator.synthetic_waveform_value(
            "fl_waveform",
            4,
            7,
            2399,
        )

        # Then: Exact float bits and the 14-bit ADC mask match the C++ fixture.
        self.assertEqual(negative_zero, 0x8000000000000000)
        self.assertEqual(positive_zero, 0)
        self.assertEqual(minimum_subnormal, 1)
        self.assertEqual(maximum_waveform, (30000 + 40000 + 16800 + 2399) & 16383)
        self.assertLessEqual(maximum_waveform, 16383)


class LifecycleMetadataTests(unittest.TestCase):
    @staticmethod
    def _base_meta() -> dict[str, object]:
        return {
            "dataset_id": "tube_1",
            "n_events": 5,
            "events_per_partition": [3, 2],
            "status": "finalized",
            "finalized_at": "2026-07-22T00:00:00+00:00",
            "write_generation": 3,
            "features": [
                {
                    "name": f"feature_{index}",
                    "range_min": -1.0,
                    "range_max": 1.0,
                }
                for index in range(24)
            ],
            "partition_manifests": {
                name: [
                    {"partition": 0, "row_count": 3, "sealed": True},
                    {"partition": 1, "row_count": 2, "sealed": True},
                ]
                for name in ("pulse_features", "gmi_waveform", "fl_waveform")
            },
            "future_nested": {"preserve": [1, 2]},
        }

    def test_lifecycle_metadata_covers_gen0_through_gen3_open(self) -> None:
        # Given: Finalized five-event metadata used only as a disposable template.
        base = self._base_meta()

        # When: Every round-robin open lifecycle state is materialized.
        snapshots = [
            validator.lifecycle_metadata(base, generation)
            for generation in range(4)
        ]

        # Then: Rows, seal state, generation, and NULL ranges follow D21.
        self.assertEqual(
            [snapshot["events_per_partition"] for snapshot in snapshots],
            [[0, 0], [1, 1], [3, 2], [3, 2]],
        )
        self.assertEqual(
            [
                snapshot["partition_manifests"]["pulse_features"][0]["sealed"]
                for snapshot in snapshots
            ],
            [False, False, False, True],
        )
        self.assertEqual(
            [snapshot["write_generation"] for snapshot in snapshots],
            [0, 1, 2, 3],
        )
        self.assertTrue(all(snapshot["status"] == "open" for snapshot in snapshots))
        self.assertTrue(
            all(snapshot["finalized_at"] is None for snapshot in snapshots)
        )
        self.assertIsNone(snapshots[0]["features"][0]["range_min"])
        self.assertIsNone(snapshots[0]["features"][0]["range_max"])
        self.assertEqual(snapshots[3]["future_nested"], {"preserve": [1, 2]})
        self.assertEqual(base, self._base_meta())

    def test_lifecycle_metadata_rejects_unknown_generation(self) -> None:
        # Given: A generation outside the fixed round-robin state table.
        # When: A disposable lifecycle snapshot is requested.
        with self.assertRaisesRegex(ValueError, "unsupported lifecycle generation: 4"):
            validator.lifecycle_metadata(self._base_meta(), 4)

        # Then: No implicit or clamped state is produced.

    def test_ordered_lifecycle_uses_every_consecutive_transition(self) -> None:
        # Given: The four synthetic open snapshots and finalized terminal state.
        base = self._base_meta()
        observed: list[tuple[object, object]] = []
        kinds = iter(
            ("create", "content", "content", "content", "open_to_finalized")
        )

        def validate_transition(previous: object, current: object) -> object:
            observed.append((previous, current))
            return types.SimpleNamespace(kind=next(kinds))

        checks = validator.Checks()

        # When: The ordered Contract RC transition sequence is validated.
        validator.validate_ordered_lifecycle_transitions(
            base,
            checks,
            validate_transition,
        )

        # Then: Creation, every consecutive open state, and finalization are used.
        snapshots = [
            validator.lifecycle_metadata(base, generation)
            for generation in range(4)
        ]
        self.assertEqual(
            observed,
            [
                (None, snapshots[0]),
                (snapshots[0], snapshots[1]),
                (snapshots[1], snapshots[2]),
                (snapshots[2], snapshots[3]),
                (snapshots[3], base),
            ],
        )
        self.assertEqual(checks.positive, 5)


class CanonicalEvidenceTests(unittest.TestCase):
    @staticmethod
    def _inputs() -> tuple[dict[str, object], dict[str, object], dict[str, str]]:
        metadata: dict[str, object] = {
            "dataset_id": "tube_1",
            "status": "finalized",
            "events_per_partition": [3, 2],
        }
        zarrays: dict[str, object] = {
            "pulse_features/partition_0.zarr": {
                "shape": [3, 24],
                "dtype": "<f8",
            }
        }
        decoded = {"pulse_features/global": "a" * 64}
        return metadata, zarrays, decoded

    def test_canonical_identity_excludes_nonce_and_wire_variability(self) -> None:
        # Given: Two evidence sets with identical decoded/meta content.
        metadata, zarrays, decoded = self._inputs()

        # When: Random nonce and encrypted-wire observations differ between runs.
        first = validator.canonical_evidence_digest(
            metadata,
            zarrays,
            decoded,
            nonce_keys=((1, b"a" * 12),),
            wire_digests=("1" * 64,),
        )
        second = validator.canonical_evidence_digest(
            metadata,
            zarrays,
            decoded,
            nonce_keys=((1, b"b" * 12),),
            wire_digests=("2" * 64,),
        )

        # Then: Only canonical decoded and metadata content controls identity.
        self.assertEqual(first, second)

    def test_canonical_identity_detects_decoded_and_metadata_drift(self) -> None:
        # Given: One accepted canonical evidence payload.
        metadata, zarrays, decoded = self._inputs()
        accepted = validator.canonical_evidence_digest(metadata, zarrays, decoded)

        # When: Decoded content or metadata changes independently.
        decoded_drift = deepcopy(decoded)
        decoded_drift["pulse_features/global"] = "b" * 64
        metadata_drift = deepcopy(metadata)
        metadata_drift["events_per_partition"] = [2, 3]

        # Then: Both semantic changes produce a different canonical identity.
        self.assertNotEqual(
            accepted,
            validator.canonical_evidence_digest(metadata, zarrays, decoded_drift),
        )
        self.assertNotEqual(
            accepted,
            validator.canonical_evidence_digest(metadata_drift, zarrays, decoded),
        )

    def test_joint_summary_is_bounded_payload_free_evidence(self) -> None:
        # Given: A completed two-run semantic identity and final check counts.
        evidence = validator.StoreEvidence(
            canonical_digest="a" * 64,
            wire_digest="b" * 64,
            nonce_keys=frozenset({(1, b"n" * 12)}),
        )
        checks = validator.Checks()
        checks.positive = 264
        checks.negative = 35

        # When: The tracked-evidence summary is built.
        summary = validator.build_joint_evidence_summary(evidence, checks)
        serialized = validator.json.dumps(summary, sort_keys=True)

        # Then: It records the contract and semantic SHA without wire/nonce bytes.
        self.assertEqual(
            summary["schema"],
            "analogboard-p0-s-joint-evidence-v1",
        )
        self.assertEqual(summary["contract_id"], "gcsa-store-a4a-rc1")
        self.assertEqual(
            summary["gcsa_snapshot_commit"],
            "20689a991697217518ec2ff15aaaa2533b169eb0",
        )
        self.assertEqual(summary["fixture"]["events_per_partition"], [3, 2])
        self.assertEqual(
            summary["fixture"]["global_events_by_partition"],
            [[0, 2, 4], [1, 3]],
        )
        self.assertEqual(
            summary["verification"],
            {
                "canonical_evidence_sha256": "a" * 64,
                "encrypted_chunk_count": 6,
                "encrypted_wires_differ": True,
                "gcsa_recompute_mismatch": True,
                "negative_checks": 35,
                "nonce_sets_disjoint": True,
                "positive_checks": 264,
                "raw_zarr_reads_rejected": 6,
            },
        )
        self.assertNotIn("b" * 64, serialized)
        self.assertNotIn((b"n" * 12).hex(), serialized)
        self.assertEqual(
            summary["content_policy"],
            {
                "encrypted_wire_bytes_included": False,
                "measurement_payload_included": False,
                "nonce_bytes_included": False,
                "public_kat_only": True,
            },
        )


class CliTests(unittest.TestCase):
    def test_cli_requires_open_and_two_finalized_stores(self) -> None:
        # Given: Explicit paths for one open and two independent finalized stores.
        arguments = [
            "validate_gcsa_roundtrip.py",
            "--open-store",
            "/stores/open",
            "--finalized-store-a",
            "/stores/final-a",
            "--finalized-store-b",
            "/stores/final-b",
        ]

        # When: The joint CLI arguments are parsed.
        with mock.patch.object(sys, "argv", arguments):
            parsed = validator.parse_args()

        # Then: All three roots remain distinct, required Path values.
        self.assertEqual(parsed.open_store, Path("/stores/open"))
        self.assertEqual(parsed.finalized_store_a, Path("/stores/final-a"))
        self.assertEqual(parsed.finalized_store_b, Path("/stores/final-b"))

    def test_cli_accepts_an_optional_tracked_evidence_golden(self) -> None:
        # Given: The three stores plus a tracked, payload-free golden manifest.
        arguments = [
            "validate_gcsa_roundtrip.py",
            "--open-store",
            "/stores/open",
            "--finalized-store-a",
            "/stores/final-a",
            "--finalized-store-b",
            "/stores/final-b",
            "--expected-evidence",
            "/repository/joint-roundtrip-golden.json",
        ]

        # When: The joint CLI arguments are parsed.
        with mock.patch.object(sys, "argv", arguments):
            parsed = validator.parse_args()

        # Then: The golden path remains explicit and is not inferred from CWD.
        self.assertEqual(
            parsed.expected_evidence,
            Path("/repository/joint-roundtrip-golden.json"),
        )


class TrackedEvidenceTests(unittest.TestCase):
    @staticmethod
    def _summary() -> dict[str, object]:
        return {
            "schema": "analogboard-p0-s-joint-evidence-v1",
            "contract_id": "gcsa-store-a4a-rc1",
            "verification": {"positive_checks": 264},
        }

    @staticmethod
    def _committed_source(
        repository: Path,
        commit: str,
        relative: str,
    ) -> bytes:
        del commit
        return (repository / relative).read_bytes()

    @staticmethod
    def _manifest(
        root: Path,
        summary: dict[str, object],
    ) -> tuple[Path, dict[str, object]]:
        source = root / "source.txt"
        source.write_text("accepted source\n", encoding="utf-8")
        source_sha = validator.hashlib.sha256(source.read_bytes()).hexdigest()
        document: dict[str, object] = {
            "schema": "analogboard-p0-s-joint-golden-v1",
            "generated_at": "2026-07-22",
            "result": "pass",
            "generator": {
                "base_commit": "8fdcd747e0d6bf760fd6e674f620f8c97b356235",
                "command": "scripts/zarr-roundtrip/run-focused-verification.sh joint",
                "source_sha256": {"source.txt": source_sha},
            },
            "gcsa": {
                "commit": "20689a991697217518ec2ff15aaaa2533b169eb0",
                "contract_id": "gcsa-store-a4a-rc1",
                "package_tree_sha256": (
                    "c63c79c4add3a8034cd1486921470818ad71d024ace1e8e356ae4f8dbf396d14"
                ),
                "container_id": (
                    "d141d00e5edb0bd17ee37836340a4315343019d32db4f9197322e9a3a5c9e1d8"
                ),
                "image_id": (
                    "sha256:e65e9f8b0ffafef5b5d2b9711c9a3411"
                    "649ae80fd036cc79f0febb80b4c0b06e"
                ),
            },
            "public_kat": {
                "classification": "public-packaged-kat",
                "sha256": (
                    "cd0ee69428b483ddff4a10a84d15732ed9a7aabd2b85c99adbb97168f8fe60aa"
                ),
            },
            "joint_evidence": summary,
        }
        golden = (
            root
            / "docs/reference/zarr-store-contract/phase0-roundtrip"
            / "joint-roundtrip-golden.json"
        )
        golden.parent.mkdir(parents=True, exist_ok=True)
        golden.write_text(validator.json.dumps(document), encoding="utf-8")
        return golden, document

    def test_tracked_evidence_accepts_an_exact_joint_summary(self) -> None:
        # Given: A bounded manifest whose source is available at the pinned commit.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            summary = self._summary()
            golden, _ = self._manifest(root, summary)

            # When: Immutable commit lookup returns the recorded source bytes.
            with mock.patch.object(
                validator,
                "EXPECTED_GOLDEN_SOURCE_PATHS",
                ("source.txt",),
            ), mock.patch.object(
                validator,
                "require_immutable_commit",
            ), mock.patch.object(
                validator,
                "read_committed_source",
                side_effect=self._committed_source,
            ):
                # Then: Exact semantic evidence is accepted.
                validator.require_expected_joint_evidence(golden, summary)

    def test_tracked_evidence_rejects_missing_or_malformed_summary(self) -> None:
        # Given: A manifest without an object-valued joint evidence record.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for invalid in (None, []):
                golden, document = self._manifest(root, self._summary())
                if invalid is None:
                    del document["joint_evidence"]
                else:
                    document["joint_evidence"] = invalid
                golden.write_text(validator.json.dumps(document), encoding="utf-8")
                with self.subTest(document=document):
                    # When/Then: The malformed golden fails closed.
                    with mock.patch.object(
                        validator,
                        "EXPECTED_GOLDEN_SOURCE_PATHS",
                        ("source.txt",),
                    ), mock.patch.object(
                        validator,
                        "require_immutable_commit",
                    ), mock.patch.object(
                        validator,
                        "read_committed_source",
                        side_effect=self._committed_source,
                    ), self.assertRaisesRegex(
                        validator.CheckFailure,
                        "top-level structure drift|no joint_evidence object",
                    ):
                        validator.require_expected_joint_evidence(
                            golden,
                            self._summary(),
                        )

    def test_tracked_evidence_rejects_runtime_drift(self) -> None:
        # Given: A tracked summary with a different positive check count.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            drifted = deepcopy(self._summary())
            drifted["verification"]["positive_checks"] = 263
            golden, _ = self._manifest(root, drifted)

            # When/Then: Any exact-summary drift fails loudly.
            with mock.patch.object(
                validator,
                "EXPECTED_GOLDEN_SOURCE_PATHS",
                ("source.txt",),
            ), mock.patch.object(
                validator,
                "require_immutable_commit",
            ), mock.patch.object(
                validator,
                "read_committed_source",
                side_effect=self._committed_source,
            ), self.assertRaisesRegex(
                validator.CheckFailure,
                "joint evidence differs from tracked golden",
            ):
                validator.require_expected_joint_evidence(golden, self._summary())

    def test_tracked_evidence_rejects_outer_provenance_drift(self) -> None:
        # Given: Exact joint evidence with a falsified outer identity or source hash.
        mutations = {
            "schema": lambda document: document.update({"schema": "wrong"}),
            "gcsa": lambda document: document["gcsa"].update({"commit": "0" * 40}),
            "public KAT": lambda document: document["public_kat"].update(
                {"sha256": "0" * 64}
            ),
            "source": lambda document: document["generator"][
                "source_sha256"
            ].update({"source.txt": "0" * 64}),
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for label, mutate in mutations.items():
                golden, document = self._manifest(root, self._summary())
                mutate(document)
                golden.write_text(validator.json.dumps(document), encoding="utf-8")

                # When/Then: Outer provenance cannot drift independently.
                with self.subTest(label=label), mock.patch.object(
                    validator,
                    "EXPECTED_GOLDEN_SOURCE_PATHS",
                    ("source.txt",),
                ), mock.patch.object(
                    validator,
                    "require_immutable_commit",
                ), mock.patch.object(
                    validator,
                    "read_committed_source",
                    side_effect=self._committed_source,
                ), self.assertRaises(validator.CheckFailure):
                    validator.require_expected_joint_evidence(
                        golden,
                        self._summary(),
                    )

    def test_tracked_evidence_rejects_committed_source_drift(self) -> None:
        # Given: The manifest and worktree agree but the pinned commit blob differs.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            summary = self._summary()
            golden, _ = self._manifest(root, summary)

            # When: Immutable source lookup returns bytes not named by the manifest.
            with mock.patch.object(
                validator,
                "EXPECTED_GOLDEN_SOURCE_PATHS",
                ("source.txt",),
            ), mock.patch.object(
                validator,
                "require_immutable_commit",
                return_value="8fdcd747e0d6bf760fd6e674f620f8c97b356235",
            ), mock.patch.object(
                validator,
                "read_committed_source",
                return_value=b"different committed source\n",
            ), self.assertRaisesRegex(
                validator.CheckFailure,
                "committed source SHA-256 drift",
            ):
                # Then: The producer revision mismatch fails before acceptance.
                validator.require_expected_joint_evidence(golden, summary)

    def test_tracked_evidence_rejects_worktree_source_drift(self) -> None:
        # Given: The manifest and pinned commit agree on accepted source bytes.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            summary = self._summary()
            golden, _ = self._manifest(root, summary)
            (root / "source.txt").write_text(
                "uncommitted shipped source\n",
                encoding="utf-8",
            )

            # When: The shipped worktree differs from the attested commit.
            with mock.patch.object(
                validator,
                "EXPECTED_GOLDEN_SOURCE_PATHS",
                ("source.txt",),
            ), mock.patch.object(
                validator,
                "require_immutable_commit",
                return_value="8fdcd747e0d6bf760fd6e674f620f8c97b356235",
            ), mock.patch.object(
                validator,
                "read_committed_source",
                return_value=b"accepted source\n",
            ), self.assertRaisesRegex(
                validator.CheckFailure,
                "worktree source SHA-256 drift",
            ):
                # Then: A green runtime result cannot mask shipped-source drift.
                validator.require_expected_joint_evidence(golden, summary)

    def test_tracked_evidence_rejects_absent_or_symlinked_worktree_source(
        self,
    ) -> None:
        # Given: A manifest whose committed source bytes are still available.
        for variant in ("absent", "symlink"):
            with self.subTest(
                variant=variant
            ), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                summary = self._summary()
                golden, _ = self._manifest(root, summary)
                source = root / "source.txt"
                source.unlink()
                if variant == "symlink":
                    target = root / "target.txt"
                    target.write_text("accepted source\n", encoding="utf-8")
                    source.symlink_to(target.name)

                # When: The shipped source is absent or redirects through a symlink.
                with mock.patch.object(
                    validator,
                    "EXPECTED_GOLDEN_SOURCE_PATHS",
                    ("source.txt",),
                ), mock.patch.object(
                    validator,
                    "require_immutable_commit",
                    return_value=(
                        "8fdcd747e0d6bf760fd6e674f620f8c97b356235"
                    ),
                ), mock.patch.object(
                    validator,
                    "read_committed_source",
                    return_value=b"accepted source\n",
                ), self.assertRaisesRegex(
                    validator.CheckFailure,
                    "worktree source is absent",
                ):
                    # Then: Neither variant can satisfy shipped-source provenance.
                    validator.require_expected_joint_evidence(golden, summary)

    def test_tracked_evidence_rejects_duplicate_keys_and_nonfinite_values(
        self,
    ) -> None:
        # Given: Ambiguous or non-standard JSON before provenance validation.
        invalid_json = {
            "duplicate": '{"joint_evidence":{},"joint_evidence":{}}',
            "nonfinite": '{"poison":NaN}',
        }
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            golden = (
                root
                / "docs/reference/zarr-store-contract/phase0-roundtrip"
                / "joint-roundtrip-golden.json"
            )
            golden.parent.mkdir(parents=True)
            for label, payload in invalid_json.items():
                golden.write_text(payload, encoding="utf-8")

                # When/Then: Parsing itself fails closed with a stable category.
                with self.subTest(label=label), self.assertRaisesRegex(
                    validator.CheckFailure,
                    "duplicate key|non-finite constant",
                ):
                    validator.require_expected_joint_evidence(
                        golden,
                        self._summary(),
                    )


class ImmutableSourceProvenanceTests(unittest.TestCase):
    COMMIT = "8fdcd747e0d6bf760fd6e674f620f8c97b356235"

    @staticmethod
    def _git_result(
        *,
        returncode: int = 0,
        stdout: bytes = b"",
    ) -> object:
        return validator.subprocess.CompletedProcess(
            args=[],
            returncode=returncode,
            stdout=stdout,
            stderr=b"",
        )

    def test_accepts_a_canonical_commit_in_current_head_history(self) -> None:
        # Given: The full object ID names a commit that is an ancestor of HEAD.
        with mock.patch.object(
            validator.subprocess,
            "run",
            side_effect=(
                self._git_result(stdout=b"commit\n"),
                self._git_result(),
            ),
        ) as run:
            # When: The immutable generator revision is validated.
            actual = validator.require_immutable_commit(
                Path("/repository"),
                self.COMMIT,
            )

        # Then: The canonical SHA is returned after type and ancestry checks.
        self.assertEqual(actual, self.COMMIT)
        self.assertEqual(
            [call.args[0][2:] for call in run.call_args_list],
            [
                ["cat-file", "-t", self.COMMIT],
                ["merge-base", "--is-ancestor", self.COMMIT, "HEAD"],
            ],
        )

    def test_reads_a_source_blob_from_the_pinned_commit(self) -> None:
        # Given: Git returns one blob from the exact immutable revision.
        completed = validator.subprocess.CompletedProcess(
            args=[],
            returncode=0,
            stdout=b"committed source\n",
            stderr=b"",
        )
        with mock.patch.object(
            validator.subprocess,
            "run",
            return_value=completed,
        ) as run:
            # When: The source blob is requested by repository-relative path.
            actual = validator.read_committed_source(
                Path("/repository"),
                self.COMMIT,
                "source/file.py",
            )

        # Then: The exact committed bytes are returned without a tree fallback.
        self.assertEqual(actual, b"committed source\n")
        run.assert_called_once_with(
            [
                "git",
                "--git-dir=/repository/.git",
                "cat-file",
                "blob",
                f"{self.COMMIT}:source/file.py",
            ],
            check=False,
            stdout=validator.subprocess.PIPE,
            stderr=validator.subprocess.PIPE,
            timeout=10,
        )

    def test_rejects_noncanonical_commit_identifiers_before_git(self) -> None:
        # Given: Symbolic, short, empty, uppercase, and non-string identifiers.
        malformed_identifiers = (
            "HEAD",
            self.COMMIT[:12],
            "",
            self.COMMIT.upper(),
            None,
        )

        # When/Then: Every noncanonical boundary fails before Git execution.
        with mock.patch.object(validator.subprocess, "run") as run:
            for commit in malformed_identifiers:
                with self.subTest(commit=commit), self.assertRaises(
                    validator.CheckFailure
                ) as malformed:
                    validator.require_immutable_commit(
                        Path("/repository"),
                        commit,
                    )
                self.assertEqual(
                    str(malformed.exception),
                    "expected evidence base commit is not a full SHA-1",
                )
        run.assert_not_called()

    def test_rejects_unavailable_or_noncommit_objects(self) -> None:
        # Given: One absent object and one blob object at canonical full SHAs.
        cases = {
            "unavailable": (
                self._git_result(returncode=1),
                "expected evidence base commit is unavailable",
            ),
            "blob": (
                self._git_result(stdout=b"blob\n"),
                "expected evidence base object is not a commit",
            ),
        }

        # When/Then: Existence and exact object type are both enforced.
        for label, (result, message) in cases.items():
            with self.subTest(label=label), mock.patch.object(
                validator.subprocess,
                "run",
                return_value=result,
            ), self.assertRaises(validator.CheckFailure) as raised:
                validator.require_immutable_commit(
                    Path("/repository"),
                    self.COMMIT,
                )
            self.assertEqual(str(raised.exception), message)

    def test_rejects_a_commit_outside_current_head_history(self) -> None:
        # Given: A valid commit object that is not an ancestor of current HEAD.
        with mock.patch.object(
            validator.subprocess,
            "run",
            side_effect=(
                self._git_result(stdout=b"commit\n"),
                self._git_result(returncode=1),
            ),
        ):
            # When: The unrelated revision is used as generator provenance.
            with self.assertRaises(validator.CheckFailure) as raised:
                validator.require_immutable_commit(
                    Path("/repository"),
                    self.COMMIT,
                )

        # Then: Evidence outside the shipped history fails closed.
        self.assertEqual(
            str(raised.exception),
            "expected evidence base commit is not an ancestor of HEAD",
        )

    def test_rejects_unsafe_or_missing_blob_paths(self) -> None:
        # Given: Parent traversal, an absolute path, and a missing committed blob.
        invalid_paths = ("../source.py", "/source.py")
        missing = validator.subprocess.CompletedProcess(
            args=[],
            returncode=1,
            stdout=b"",
            stderr=b"missing",
        )

        # When/Then: Unsafe paths fail before Git can resolve them.
        for relative in invalid_paths:
            with self.subTest(relative=relative), self.assertRaises(
                validator.CheckFailure
            ) as invalid:
                validator.read_committed_source(
                    Path("/repository"),
                    self.COMMIT,
                    relative,
                )
            self.assertEqual(
                str(invalid.exception),
                "expected evidence source path is unsafe",
            )

        # When: A safe path has no blob in the pinned commit.
        with mock.patch.object(
            validator.subprocess,
            "run",
            return_value=missing,
        ), self.assertRaises(validator.CheckFailure) as unavailable:
            validator.read_committed_source(
                Path("/repository"),
                self.COMMIT,
                "missing.py",
            )

        # Then: Blob absence fails closed without reading the working tree.
        self.assertEqual(
            str(unavailable.exception),
            "expected evidence source blob is unavailable: missing.py",
        )

    def test_rejects_git_execution_failure(self) -> None:
        # Given: Git execution fails to start or exceeds its fixed timeout.
        failures = (
            OSError("git unavailable"),
            validator.subprocess.TimeoutExpired(cmd="git", timeout=10),
        )

        # When/Then: External failures are normalized without partial acceptance.
        for failure in failures:
            with self.subTest(failure=type(failure).__name__), mock.patch.object(
                validator.subprocess,
                "run",
                side_effect=failure,
            ), self.assertRaises(validator.CheckFailure) as raised:
                validator.require_immutable_commit(
                    Path("/repository"),
                    self.COMMIT,
                )
            self.assertEqual(
                str(raised.exception),
                "expected evidence git command failed",
            )
            self.assertIs(raised.exception.__cause__, failure)


class ChecksTests(unittest.TestCase):
    def test_require_failure_accepts_only_the_expected_exception(self) -> None:
        # Given: A negative check whose action raises the expected failure type.
        checks = validator.Checks()

        def raise_expected() -> None:
            raise ValueError("authentication failed")

        # When: The expected failure is required.
        checks.require_failure(
            "wrong key",
            raise_expected,
            (ValueError,),
        )

        # Then: The negative case is counted exactly once.
        self.assertEqual(checks.negative, 1)

    def test_require_failure_rejects_an_action_that_returns(self) -> None:
        # Given: A negative check whose action accepts invalid input.
        checks = validator.Checks()

        # When: The action returns instead of raising.
        with self.assertRaises(validator.CheckFailure) as raised:
            checks.require_failure("wrong key", lambda: None, (ValueError,))

        # Then: The exact fail-loud error is raised without counting the case.
        self.assertIs(type(raised.exception), validator.CheckFailure)
        self.assertEqual(str(raised.exception), "wrong key: accepted invalid input")
        self.assertEqual(checks.negative, 0)

    def test_require_failure_reports_and_preserves_an_unexpected_exception(
        self,
    ) -> None:
        # Given: A negative check whose action raises a different failure type.
        checks = validator.Checks()
        unexpected = TypeError("malformed chunk")

        def raise_unexpected() -> None:
            raise unexpected

        # When: The mismatched failure is required.
        with self.assertRaises(validator.CheckFailure) as raised:
            checks.require_failure("wrong key", raise_unexpected, (ValueError,))

        # Then: The exact type/message is reported and the cause is retained.
        self.assertIs(type(raised.exception), validator.CheckFailure)
        self.assertEqual(
            str(raised.exception),
            "wrong key: unexpected failure type TypeError: malformed chunk",
        )
        self.assertIs(raised.exception.__cause__, unexpected)
        self.assertEqual(checks.negative, 0)


class GcsaSnapshotDigestTests(unittest.TestCase):
    def test_snapshot_root_fails_without_a_package_file(self) -> None:
        # Given: A gcsa module without a filesystem-backed package location.
        with mock.patch.object(validator.gcsa, "__file__", None):
            # When: The bounded snapshot root is requested.
            with self.assertRaisesRegex(
                validator.CheckFailure,
                "gcsa package has no filesystem location",
            ):
                # Then: Validation fails loudly instead of widening the root.
                validator.gcsa_snapshot_root()

    def test_snapshot_root_is_the_gcsa_package_and_ignores_siblings(self) -> None:
        # Given: A gcsa package nested in an interpreter environment.
        with tempfile.TemporaryDirectory() as directory:
            site_packages = Path(directory) / "lib" / "site-packages"
            package = site_packages / "gcsa"
            package.mkdir(parents=True)
            package_file = package / "__init__.py"
            package_file.write_text("accepted = True\n", encoding="utf-8")
            unrelated = site_packages / "unrelated.txt"
            unrelated.write_text("before\n", encoding="utf-8")

            # When: The package root is resolved and an unrelated sibling changes.
            with mock.patch.object(validator.gcsa, "__file__", str(package_file)):
                snapshot_root = validator.gcsa_snapshot_root()
                before = validator.gcsa_snapshot_digest(snapshot_root)
                unrelated.write_text("after\n", encoding="utf-8")
                after = validator.gcsa_snapshot_digest(snapshot_root)

            # Then: Only the explicit gcsa package is covered by the snapshot.
            self.assertEqual(snapshot_root, package.resolve())
            self.assertEqual(after, before)

    def test_snapshot_digest_excludes_caches_and_vcs_metadata(self) -> None:
        # Given: A package containing source, interpreter caches, and VCS metadata.
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "gcsa"
            package.mkdir()
            (package / "module.py").write_text("accepted = True\n", encoding="utf-8")
            excluded_files = [
                package / "__pycache__" / "module.cpython.pyc",
                package / "legacy.pyc",
                package / "legacy.pyo",
                package / ".git" / "index",
                package / ".hg" / "dirstate",
                package / ".svn" / "entries",
            ]
            for path in excluded_files:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("before\n", encoding="utf-8")
            before = validator.gcsa_snapshot_digest(package)

            # When: Every excluded cache and metadata file is changed.
            for path in excluded_files:
                path.write_text("after\n", encoding="utf-8")
            after = validator.gcsa_snapshot_digest(package)

            # Then: Transient state does not affect the immutable-source digest.
            self.assertEqual(after, before)

    def test_snapshot_digest_still_detects_package_source_mutation(self) -> None:
        # Given: A package with one accepted source file.
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "gcsa"
            package.mkdir()
            source = package / "module.py"
            source.write_text("accepted = True\n", encoding="utf-8")
            before = validator.gcsa_snapshot_digest(package)

            # When: Package source is mutated.
            source.write_text("accepted = False\n", encoding="utf-8")
            after = validator.gcsa_snapshot_digest(package)

            # Then: The existing before/after mutation check remains effective.
            self.assertNotEqual(after, before)

    def test_snapshot_identity_accepts_the_exact_pinned_package_tree(self) -> None:
        # Given: A package tree whose digest is the explicitly accepted identity.
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "gcsa"
            package.mkdir()
            (package / "module.py").write_text(
                "accepted = True\n", encoding="utf-8"
            )
            accepted_digest = validator.gcsa_snapshot_digest(package)

            # When: Runtime identity validation checks that exact tree.
            with mock.patch.object(
                validator,
                "EXPECTED_GCSA_PACKAGE_TREE_SHA256",
                accepted_digest,
            ):
                actual = validator.require_accepted_gcsa_snapshot(package)

            # Then: The verified digest is returned for the mutation guard.
            self.assertEqual(actual, accepted_digest)

    def test_snapshot_identity_rejects_an_unapproved_package_tree(self) -> None:
        # Given: A package tree whose source differs from the accepted snapshot.
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / "gcsa"
            package.mkdir()
            (package / "module.py").write_text(
                "accepted = False\n", encoding="utf-8"
            )
            actual_digest = validator.gcsa_snapshot_digest(package)

            # When: Runtime identity validation compares it with the fixed pin.
            with mock.patch.object(
                validator,
                "EXPECTED_GCSA_PACKAGE_TREE_SHA256",
                "0" * 64,
            ), self.assertRaises(validator.CheckFailure) as raised:
                validator.require_accepted_gcsa_snapshot(package)

            # Then: The failure identifies both the accepted commit and actual tree.
            self.assertIs(type(raised.exception), validator.CheckFailure)
            self.assertIn("gcsa snapshot identity mismatch", str(raised.exception))
            self.assertIn(
                "20689a991697217518ec2ff15aaaa2533b169eb0",
                str(raised.exception),
            )
            self.assertIn(actual_digest, str(raised.exception))

    def test_positive_validation_rejects_snapshot_before_reading_store_trees(
        self,
    ) -> None:
        # Given: Runtime gcsa identity validation rejects the imported package.
        identity_failure = validator.CheckFailure("snapshot mismatch")
        with mock.patch.object(
            validator,
            "gcsa_snapshot_root",
            return_value=Path("/unapproved/gcsa"),
        ), mock.patch.object(
            validator,
            "require_accepted_gcsa_snapshot",
            side_effect=identity_failure,
            create=True,
        ) as require_identity, mock.patch.object(
            validator,
            "tree_digest",
            side_effect=AssertionError("store tree was inspected"),
        ) as tree_digest:
            # When: Positive roundtrip validation begins.
            with self.assertRaises(validator.CheckFailure) as raised:
                validator.validate_positive(
                    Path("/ignored/open"),
                    Path("/ignored/finalized-a"),
                    Path("/ignored/finalized-b"),
                    validator.Checks(),
                )

        # Then: It stops on identity before either store tree is inspected.
        self.assertIs(raised.exception, identity_failure)
        require_identity.assert_called_once_with(Path("/unapproved/gcsa"))
        tree_digest.assert_not_called()


if __name__ == "__main__":
    unittest.main()
