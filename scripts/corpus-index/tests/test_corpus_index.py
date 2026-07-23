from __future__ import annotations

import hashlib
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest import mock

SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from corpus_index import (  # noqa: E402
    ChunkSizeError,
    ContractValidationError,
    CountMismatchError,
    DEFAULT_MANIFEST_PATH,
    IntegrityMismatchError,
    ManifestValidationError,
    PathValidationError,
    SourceUnreadableError,
    TotalBytesMismatchError,
    UnexpectedFileError,
    _secure_binary_opener,
    _write_manifest,
    build_manifest,
    hash_file,
    load_contract,
    load_contract_data,
    serialize_manifest,
    verify_manifest,
)


CONTRACT_SCHEMA = "analogboard.phase0.initial-recording-corpus-contract"
MANIFEST_SCHEMA = "analogboard.phase0.initial-recording-corpus-manifest"


def small_contract(
    *,
    locator: str = "fixtures/corpus",
    counts: dict[str, int] | None = None,
    total_bytes: int = 4,
) -> dict[str, Any]:
    expected = counts or {"bin": 1, "cfg": 1, "telemetry": 1, "capture": 1}
    filename_patterns = {
        "bin": r"(?P<run_id>[0-9]{6}_[0-9]{4})_(?:fl|fh)_[1-9][0-9]*[.]bin",
        "cfg": r"(?P<run_id>[0-9]{6}_[0-9]{4})_cfg[.]txt",
        "telemetry": r"[0-9]{6}_[0-9]{6}_rearm_telemetry[.]csv",
        "capture": r"[a-z0-9_]+[.]pcapng",
    }
    return {
        "schema": CONTRACT_SCHEMA,
        "schema_version": 1,
        "canonical_locator": locator,
        "asset_kinds": [
            {
                "kind": kind,
                "expected_count": expected[kind],
                "filename_pattern": filename_patterns[kind],
            }
            for kind in ("bin", "cfg", "telemetry", "capture")
        ],
        "expected_total_bytes": total_bytes,
        "excluded_paths": ["analysis"],
        "run_capture_mapping": [
            {
                "run_id": "260717_0001",
                "density": "synthetic",
                "capture": "capture.pcapng",
            }
        ],
        "idle_captures": [],
    }


def write_small_corpus(repo_root: Path, *, creation_order: tuple[str, ...] | None = None) -> None:
    files = {
        "260717_0001_fl_1.bin": b"b",
        "260717_0001_cfg.txt": b"c",
        "260717_000100_rearm_telemetry.csv": b"t",
        "capture.pcapng": b"p",
    }
    corpus_root = repo_root / "fixtures/corpus"
    corpus_root.mkdir(parents=True)
    for relative_path in creation_order or tuple(files):
        destination = corpus_root / relative_path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(files[relative_path])


class CorpusIndexContractTests(unittest.TestCase):
    def assert_failure(
        self,
        expected_type: type[Exception],
        expected_code: str,
        expected_message: str,
        callback: Any,
    ) -> None:
        with self.assertRaises(expected_type) as raised:
            callback()
        self.assertIs(expected_type, type(raised.exception))
        self.assertEqual(expected_code, getattr(raised.exception, "code", None))
        self.assertEqual(f"{expected_code}: {expected_message}", str(raised.exception))

    def test_valid_small_contract_builds_deterministic_manifest(self) -> None:
        # Given: A valid small contract and normalized repository-relative fixture paths.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract())

            # When: The same corpus is indexed twice.
            first = serialize_manifest(build_manifest(repo_root, contract))
            second = serialize_manifest(build_manifest(repo_root, contract))

            # Then: Both manifests are byte-identical canonical JSON and verify cleanly.
            self.assertEqual(first, second)
            self.assertTrue(first.endswith(b"\n"))
            parsed = json.loads(first)
            self.assertEqual(MANIFEST_SCHEMA, parsed["schema"])
            self.assertNotIn(str(repo_root.resolve()), first.decode("utf-8"))
            verify_manifest(repo_root, contract, parsed)

    def test_null_contract_is_rejected(self) -> None:
        # Given: A JSON null contract.
        # When: Contract validation is requested.
        # Then: A stable typed contract error rejects the implicit default.
        self.assert_failure(
            ContractValidationError,
            "contract.type",
            "contract must be an object",
            lambda: load_contract_data(None),
        )

    def test_empty_contract_is_rejected(self) -> None:
        # Given: An empty contract object.
        # When: Contract validation is requested.
        # Then: The missing schema is reported exactly.
        self.assert_failure(
            ContractValidationError,
            "contract.schema.missing",
            "schema is required",
            lambda: load_contract_data({}),
        )

    def test_wrong_schema_is_rejected(self) -> None:
        # Given: A contract with an unknown schema name.
        contract_data = small_contract()
        contract_data["schema"] = "unknown"

        # When: Contract validation is requested.
        # Then: The unsupported schema is reported exactly.
        self.assert_failure(
            ContractValidationError,
            "contract.schema.unsupported",
            f"schema must be '{CONTRACT_SCHEMA}'",
            lambda: load_contract_data(contract_data),
        )

    def test_future_schema_version_is_rejected(self) -> None:
        # Given: A contract with a future integer schema version.
        contract_data = small_contract()
        contract_data["schema_version"] = 2

        # When: Contract validation is requested.
        # Then: The version is rejected without forward guessing.
        self.assert_failure(
            ContractValidationError,
            "contract.schema_version.unsupported",
            "schema_version must be 1",
            lambda: load_contract_data(contract_data),
        )

    def test_boolean_schema_version_is_rejected(self) -> None:
        # Given: A boolean masquerading as an integer schema version.
        contract_data = small_contract()
        contract_data["schema_version"] = True

        # When: Contract validation is requested.
        # Then: The invalid type is reported exactly.
        self.assert_failure(
            ContractValidationError,
            "contract.schema_version.type",
            "schema_version must be an integer",
            lambda: load_contract_data(contract_data),
        )

    def test_expected_count_one_is_accepted(self) -> None:
        # Given: Every required kind has the minimum valid expected count of one.
        contract_data = small_contract()

        # When: The contract is loaded.
        contract = load_contract_data(contract_data)

        # Then: All four typed kinds retain that boundary value.
        self.assertEqual(
            {"bin": 1, "cfg": 1, "telemetry": 1, "capture": 1},
            contract.expected_counts,
        )

    def test_expected_count_zero_is_rejected(self) -> None:
        # Given: A required kind has a zero expected count.
        contract_data = small_contract(counts={"bin": 0, "cfg": 1, "telemetry": 1, "capture": 1})

        # When: Contract validation is requested.
        # Then: Zero fails at the lower boundary.
        self.assert_failure(
            ContractValidationError,
            "contract.expected_count.invalid",
            "asset_kinds.bin.expected_count must be a positive integer",
            lambda: load_contract_data(contract_data),
        )

    def test_negative_and_boolean_expected_counts_are_rejected(self) -> None:
        # Given: Negative and boolean values that are not valid positive counts.
        for invalid_count in (-1, True):
            with self.subTest(invalid_count=invalid_count):
                contract_data = small_contract(
                    counts={"bin": invalid_count, "cfg": 1, "telemetry": 1, "capture": 1}
                )

                # When: Contract validation is requested.
                # Then: Each invalid value has the same stable typed failure.
                self.assert_failure(
                    ContractValidationError,
                    "contract.expected_count.invalid",
                    "asset_kinds.bin.expected_count must be a positive integer",
                    lambda: load_contract_data(contract_data),
                )

    def test_capture_set_must_cover_expected_count_including_idle(self) -> None:
        # Given: Two expected captures but only one run-mapped capture and no idle capture.
        contract_data = small_contract(
            counts={"bin": 1, "cfg": 1, "telemetry": 1, "capture": 2}
        )

        # When: Contract validation derives the fixed capture set.
        # Then: The missing idle capture fails closed at the contract boundary.
        self.assert_failure(
            ContractValidationError,
            "contract.capture_set.count",
            "capture set expected 2 unique file(s), found 1",
            lambda: load_contract_data(contract_data),
        )

    def test_idle_capture_must_not_overlap_run_mapped_capture(self) -> None:
        # Given: The same capture is declared as both run-mapped and idle evidence.
        contract_data = small_contract()
        contract_data["idle_captures"] = ["capture.pcapng"]

        # When: Contract validation derives the fixed capture set.
        # Then: The ambiguous role is rejected with a stable typed failure.
        self.assert_failure(
            ContractValidationError,
            "contract.capture_set.overlap",
            "capture cannot be both run-mapped and idle: capture.pcapng",
            lambda: load_contract_data(contract_data),
        )

    def test_invalid_locator_forms_are_rejected(self) -> None:
        # Given: Empty, POSIX/Windows absolute, escaping, backslash, and non-normalized locators.
        invalid_locators = (
            "",
            "/tmp/corpus",
            "C:/corpus",
            "../corpus",
            "fixtures\\corpus",
            "fixtures/./corpus",
        )
        for locator in invalid_locators:
            with self.subTest(locator=locator):
                # When: Contract validation is requested.
                # Then: Every unsafe form is rejected with the same exact failure.
                self.assert_failure(
                    ContractValidationError,
                    "contract.path.invalid",
                    "canonical_locator must be a normalized repository-relative path",
                    lambda: load_contract_data(small_contract(locator=locator)),
                )

    def test_locator_symlink_is_rejected_without_following(self) -> None:
        # Given: The canonical locator crosses a symlink inside the repository root.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            real_corpus = repo_root / "real-corpus"
            real_corpus.mkdir()
            (repo_root / "fixtures").mkdir()
            (repo_root / "fixtures/corpus").symlink_to(real_corpus, target_is_directory=True)
            contract = load_contract_data(small_contract())

            # When: Manifest discovery reaches the locator.
            # Then: The symlink is rejected and never followed.
            self.assert_failure(
                PathValidationError,
                "path.symlink",
                "symlink is not allowed: fixtures/corpus",
                lambda: build_manifest(repo_root, contract),
            )

    def test_exact_expected_counts_discover_all_required_kinds(self) -> None:
        # Given: One synthetic file for each required asset kind.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract())

            # When: A manifest is built.
            manifest = build_manifest(repo_root, contract)

            # Then: Each kind has exactly the contract-authoritative count.
            actual_counts = {
                kind: sum(entry["kind"] == kind for entry in manifest["entries"])
                for kind in contract.expected_counts
            }
            self.assertEqual(contract.expected_counts, actual_counts)

    def test_suffix_only_replacement_filename_is_rejected(self) -> None:
        # Given: A required bin is replaced by a same-suffix name outside the exact grammar.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            source = repo_root / "fixtures/corpus/260717_0001_fl_1.bin"
            source.rename(repo_root / "fixtures/corpus/replacement.bin")
            contract = load_contract_data(small_contract())

            # When: Discovery classifies the root-level source set.
            # Then: A suffix-only replacement cannot satisfy the required bin count.
            self.assert_failure(
                UnexpectedFileError,
                "discovery.unexpected_file",
                "unexpected file: replacement.bin",
                lambda: build_manifest(repo_root, contract),
            )

    def test_unmapped_run_filename_is_rejected(self) -> None:
        # Given: A grammar-valid bin whose run ID is absent from run_capture_mapping.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            source = repo_root / "fixtures/corpus/260717_0001_fl_1.bin"
            source.rename(repo_root / "fixtures/corpus/260717_9999_fl_1.bin")
            contract = load_contract_data(small_contract())

            # When: Discovery classifies the root-level source set.
            # Then: The unregistered run cannot replace the contract-authorized file.
            self.assert_failure(
                UnexpectedFileError,
                "discovery.unexpected_file",
                "unexpected file: 260717_9999_fl_1.bin",
                lambda: build_manifest(repo_root, contract),
            )

    def test_unlisted_capture_cannot_replace_fixed_idle_capture(self) -> None:
        # Given: A contract with one mapped and one explicit idle capture.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            corpus_root = repo_root / "fixtures/corpus"
            (corpus_root / "replacement.pcapng").write_bytes(b"i")
            contract_data = small_contract(
                counts={"bin": 1, "cfg": 1, "telemetry": 1, "capture": 2},
                total_bytes=5,
            )
            contract_data["idle_captures"] = ["idle.pcapng"]
            contract = load_contract_data(contract_data)

            # When: Discovery sees a grammar-valid capture absent from the fixed set.
            # Then: The replacement cannot satisfy the idle capture identity.
            self.assert_failure(
                UnexpectedFileError,
                "discovery.unexpected_file",
                "unexpected file: replacement.pcapng",
                lambda: build_manifest(repo_root, contract),
            )

    def test_nested_source_directory_is_rejected(self) -> None:
        # Given: A valid source file is silently moved below an unapproved directory.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            nested = repo_root / "fixtures/corpus/moved"
            nested.mkdir()
            source = repo_root / "fixtures/corpus/260717_0001_fl_1.bin"
            source.rename(nested / source.name)
            contract = load_contract_data(small_contract())

            # When: Discovery reaches the unexpected directory.
            # Then: Root-level layout drift fails before recursive classification.
            self.assert_failure(
                PathValidationError,
                "path.layout.invalid",
                "nested source directory is not allowed: moved",
                lambda: build_manifest(repo_root, contract),
            )

    def test_missing_required_file_fails_closed(self) -> None:
        # Given: A corpus that is one required bin file below its exact count.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            (repo_root / "fixtures/corpus/260717_0001_fl_1.bin").unlink()
            contract = load_contract_data(small_contract(total_bytes=3))

            # When: A manifest build is requested.
            # Then: Discovery fails with an exact typed count mismatch.
            self.assert_failure(
                CountMismatchError,
                "count.mismatch",
                "bin expected 1 file(s), found 0",
                lambda: build_manifest(repo_root, contract),
            )

    def test_extra_recognized_file_fails_closed(self) -> None:
        # Given: A corpus that is one bin file above its exact expected count.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            (repo_root / "fixtures/corpus/260717_0001_fh_2.bin").write_bytes(b"x")
            contract = load_contract_data(small_contract(total_bytes=5))

            # When: A manifest build is requested.
            # Then: Discovery fails with an exact typed count mismatch.
            self.assert_failure(
                CountMismatchError,
                "count.mismatch",
                "bin expected 1 file(s), found 2",
                lambda: build_manifest(repo_root, contract),
            )

    def test_unexpected_file_fails_closed(self) -> None:
        # Given: A regular file that matches no contract asset kind.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            (repo_root / "fixtures/corpus/surprise.dat").write_bytes(b"x")
            contract = load_contract_data(small_contract())

            # When: A manifest build is requested.
            # Then: The file is rejected instead of silently ignored.
            self.assert_failure(
                UnexpectedFileError,
                "discovery.unexpected_file",
                "unexpected file: surprise.dat",
                lambda: build_manifest(repo_root, contract),
            )

    def test_regular_analysis_directory_is_excluded(self) -> None:
        # Given: A regular excluded analysis directory containing arbitrary prior output.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            analysis = repo_root / "fixtures/corpus/analysis"
            analysis.mkdir()
            (analysis / "old-summary.json").write_bytes(b"ignored")
            contract = load_contract_data(small_contract())

            # When: A manifest is built.
            manifest = build_manifest(repo_root, contract)

            # Then: Analysis files are ignored and the exact source set remains valid.
            self.assertEqual(4, len(manifest["entries"]))
            self.assertNotIn("analysis/old-summary.json", serialize_manifest(manifest).decode("utf-8"))

    def test_excluded_analysis_symlink_is_rejected(self) -> None:
        # Given: The excluded analysis path is a symlink rather than a regular directory.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            outside = repo_root / "outside"
            outside.mkdir()
            (repo_root / "fixtures/corpus/analysis").symlink_to(outside, target_is_directory=True)
            contract = load_contract_data(small_contract())

            # When: Discovery reaches the excluded path.
            # Then: Its unsafe type fails before exclusion and is never followed.
            self.assert_failure(
                PathValidationError,
                "path.symlink",
                "symlink is not allowed: analysis",
                lambda: build_manifest(repo_root, contract),
            )

    def test_one_byte_sha_chunk_is_correct_and_bounded(self) -> None:
        # Given: A source seam that records every requested streaming read size.
        class BoundedStream:
            def __init__(self) -> None:
                self.remaining = b"abc"
                self.requested_sizes: list[int] = []

            def __enter__(self) -> "BoundedStream":
                return self

            def __exit__(self, *_args: Any) -> None:
                return None

            def read(self, size: int) -> bytes:
                self.requested_sizes.append(size)
                chunk, self.remaining = self.remaining[:size], self.remaining[size:]
                return chunk

        stream = BoundedStream()

        # When: The source is hashed with the minimum valid one-byte chunk.
        result = hash_file(
            Path("unused"),
            chunk_size=1,
            display_path="source.bin",
            opener=lambda _path, _mode: stream,
        )

        # Then: Digest, byte count, and every bounded read request are exact.
        self.assertEqual(3, result.size_bytes)
        self.assertEqual(hashlib.sha256(b"abc").hexdigest(), result.sha256)
        self.assertEqual([1, 1, 1, 1], stream.requested_sizes)

    def test_non_positive_and_boolean_sha_chunks_are_rejected(self) -> None:
        # Given: Zero, negative, and boolean chunk sizes.
        with tempfile.TemporaryDirectory() as temporary_directory:
            source = Path(temporary_directory) / "source.bin"
            source.write_bytes(b"abc")
            for chunk_size in (0, -1, True):
                with self.subTest(chunk_size=chunk_size):
                    # When: Streaming SHA-256 is requested.
                    # Then: The invalid boundary has an exact typed failure.
                    self.assert_failure(
                        ChunkSizeError,
                        "hash.chunk_size.invalid",
                        "chunk_size must be a positive integer",
                        lambda: hash_file(source, chunk_size=chunk_size, display_path="source.bin"),
                    )

    def test_empty_source_has_standard_sha256(self) -> None:
        # Given: A zero-byte regular source file.
        with tempfile.TemporaryDirectory() as temporary_directory:
            source = Path(temporary_directory) / "empty.bin"
            source.write_bytes(b"")

            # When: The source is hashed by the streaming seam.
            result = hash_file(source, chunk_size=8, display_path="empty.bin")

            # Then: Its size is zero and its digest is the standard empty SHA-256.
            self.assertEqual(0, result.size_bytes)
            self.assertEqual(hashlib.sha256(b"").hexdigest(), result.sha256)

    def test_source_open_failure_is_typed(self) -> None:
        # Given: An opener that fails before exposing any source bytes.
        def failing_opener(_path: Path, _mode: str) -> Any:
            raise OSError("synthetic open failure")

        # When: Streaming SHA-256 attempts to open the source.
        # Then: The dependency error is normalized without leaking its host path.
        self.assert_failure(
            SourceUnreadableError,
            "source.unreadable",
            "file is not readable: bin/event.bin",
            lambda: hash_file(
                Path("/not-used"),
                chunk_size=8,
                display_path="bin/event.bin",
                opener=failing_opener,
            ),
        )

    def test_source_read_failure_is_typed(self) -> None:
        # Given: A readable-looking stream whose first bounded read fails.
        class FailingStream:
            def __enter__(self) -> "FailingStream":
                return self

            def __exit__(self, *_args: Any) -> None:
                return None

            def read(self, _size: int) -> bytes:
                raise OSError("synthetic read failure")

        # When: Streaming SHA-256 attempts to read the source.
        # Then: The dependency error is normalized without payload or host details.
        self.assert_failure(
            SourceUnreadableError,
            "source.unreadable",
            "file is not readable: bin/event.bin",
            lambda: hash_file(
                Path("/not-used"),
                chunk_size=8,
                display_path="bin/event.bin",
                opener=lambda _path, _mode: FailingStream(),
            ),
        )

    def test_total_byte_drift_fails_closed(self) -> None:
        # Given: A valid source set whose aggregate bytes differ from the contract.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract(total_bytes=5))

            # When: A manifest is built.
            # Then: Aggregate byte drift is a stable typed failure.
            self.assert_failure(
                TotalBytesMismatchError,
                "total_bytes.mismatch",
                "total bytes expected 5, found 4",
                lambda: build_manifest(repo_root, contract),
            )

    def test_recorded_size_mismatch_is_recomputed_and_rejected(self) -> None:
        # Given: A valid manifest whose recorded size is altered.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract())
            manifest = build_manifest(repo_root, contract)
            entry = next(item for item in manifest["entries"] if item["kind"] == "bin")
            entry["size_bytes"] += 1
            path = entry["path"]

            # When: The manifest is verified against readable source bytes.
            # Then: Recomputed size rejects the recorded value exactly.
            self.assert_failure(
                IntegrityMismatchError,
                "integrity.size_mismatch",
                f"size mismatch for {path}: recorded 2, actual 1",
                lambda: verify_manifest(repo_root, contract, manifest),
            )

    def test_recorded_sha_mismatch_is_recomputed_and_rejected(self) -> None:
        # Given: A valid manifest whose recorded digest is altered.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract())
            manifest = build_manifest(repo_root, contract)
            entry = next(item for item in manifest["entries"] if item["kind"] == "bin")
            entry["sha256"] = "0" * 64
            path = entry["path"]
            actual = hashlib.sha256(b"b").hexdigest()

            # When: The manifest is verified against readable source bytes.
            # Then: Recomputed SHA-256 rejects the recorded digest exactly.
            self.assert_failure(
                IntegrityMismatchError,
                "integrity.sha256_mismatch",
                f"SHA-256 mismatch for {path}: recorded {'0' * 64}, actual {actual}",
                lambda: verify_manifest(repo_root, contract, manifest),
            )

    def test_creation_order_does_not_change_serialized_manifest(self) -> None:
        # Given: Equivalent corpora created in opposing input enumeration orders.
        with tempfile.TemporaryDirectory() as first_directory, tempfile.TemporaryDirectory() as second_directory:
            first_root = Path(first_directory)
            second_root = Path(second_directory)
            paths = (
                "260717_0001_fl_1.bin",
                "260717_0001_cfg.txt",
                "260717_000100_rearm_telemetry.csv",
                "capture.pcapng",
            )
            write_small_corpus(first_root, creation_order=paths)
            write_small_corpus(second_root, creation_order=tuple(reversed(paths)))
            contract = load_contract_data(small_contract())

            # When: Both corpora are serialized independently.
            first = serialize_manifest(build_manifest(first_root, contract))
            second = serialize_manifest(build_manifest(second_root, contract))

            # Then: UTF-8 sorted-key JSON with a terminal LF is byte-identical.
            self.assertEqual(first, second)
            self.assertTrue(first.endswith(b"\n"))

    def test_missing_manifest_path_is_rejected(self) -> None:
        # Given: A manifest missing one source path from the exact discovered set.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract())
            manifest = build_manifest(repo_root, contract)
            manifest["entries"].pop()

            # When: The incomplete manifest is verified.
            # Then: The missing path fails closed with an exact typed message.
            self.assert_failure(
                ManifestValidationError,
                "manifest.count.mismatch",
                "capture expected 1 manifest entry(s), found 0",
                lambda: verify_manifest(repo_root, contract, manifest),
            )

    def test_manifest_kind_must_match_exact_filename_grammar(self) -> None:
        # Given: A manifest entry relabeled to a valid but incorrect asset kind.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract())
            manifest = build_manifest(repo_root, contract)
            entry = next(item for item in manifest["entries"] if item["kind"] == "bin")
            entry["kind"] = "cfg"

            # When: Manifest validation inspects metadata before source hashing.
            # Then: Kind/filename disagreement fails closed.
            self.assert_failure(
                ManifestValidationError,
                "manifest.kind.mismatch",
                f"kind does not match filename grammar: {entry['path']}",
                lambda: verify_manifest(repo_root, contract, manifest),
            )

    def test_manifest_expected_counts_reject_boolean_values(self) -> None:
        # Given: A manifest header where boolean true masquerades as integer one.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract())
            manifest = build_manifest(repo_root, contract)
            manifest["expected_counts"]["bin"] = True

            # When: Manifest header validation compares contract authority.
            # Then: Strict integer typing rejects Python equality coercion.
            self.assert_failure(
                ManifestValidationError,
                "manifest.expected_counts.invalid",
                "expected_counts must contain positive integer values for all required kinds",
                lambda: verify_manifest(repo_root, contract, manifest),
            )

    def test_duplicate_manifest_path_is_rejected(self) -> None:
        # Given: A manifest containing the same normalized path twice.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract())
            manifest = build_manifest(repo_root, contract)
            duplicate_path = manifest["entries"][0]["path"]
            manifest["entries"].append(dict(manifest["entries"][0]))

            # When: The duplicated manifest is verified.
            # Then: Duplicate identity is rejected before any partial pass.
            self.assert_failure(
                ManifestValidationError,
                "manifest.path.duplicate",
                f"duplicate manifest path: {duplicate_path}",
                lambda: verify_manifest(repo_root, contract, manifest),
            )

    def test_escaping_manifest_path_is_rejected(self) -> None:
        # Given: A manifest entry whose path attempts to escape the corpus root.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract())
            manifest = build_manifest(repo_root, contract)
            manifest["entries"][0]["path"] = "../outside.bin"

            # When: The unsafe manifest is verified.
            # Then: Normalized relative path validation fails before filesystem access.
            self.assert_failure(
                ManifestValidationError,
                "manifest.path.invalid",
                "manifest entry path must be a normalized relative path",
                lambda: verify_manifest(repo_root, contract, manifest),
            )

    def test_contract_loader_normalizes_invalid_json_failure(self) -> None:
        # Given: A tracked contract file containing invalid JSON bytes.
        with tempfile.TemporaryDirectory() as temporary_directory:
            contract_path = Path(temporary_directory) / "contract.json"
            contract_path.write_text("{", encoding="utf-8")

            # When: The contract file is loaded.
            # Then: Parser details are bounded behind a stable typed failure.
            self.assert_failure(
                ContractValidationError,
                "contract.json.invalid",
                "contract must contain valid UTF-8 JSON",
                lambda: load_contract(contract_path),
            )

    def test_unknown_contract_field_is_rejected(self) -> None:
        # Given: A versioned contract with an unrecognized payload-bearing field.
        contract_data = small_contract()
        contract_data["payload"] = "must-not-be-ignored"

        # When: Contract validation is requested.
        # Then: The unknown field fails closed behind a stable typed error.
        self.assert_failure(
            ContractValidationError,
            "contract.fields.unknown",
            "unknown contract field(s): payload",
            lambda: load_contract_data(contract_data),
        )

    def test_unknown_manifest_fields_are_rejected(self) -> None:
        # Given: Valid synthetic manifests with an extra header or entry field.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            write_small_corpus(repo_root)
            contract = load_contract_data(small_contract())
            cases = (
                (
                    "header",
                    "manifest.fields.unknown",
                    "unknown manifest field(s): payload",
                ),
                (
                    "entry",
                    "manifest.entry.fields.unknown",
                    "unknown manifest entry field(s): payload",
                ),
            )
            for location, code, message in cases:
                with self.subTest(location=location):
                    manifest = build_manifest(repo_root, contract)
                    if location == "header":
                        manifest["payload"] = "must-not-be-ignored"
                    else:
                        manifest["entries"][0]["payload"] = "must-not-be-ignored"

                    # When: The manifest is verified.
                    # Then: Unknown metadata cannot bypass the payload-free schema.
                    self.assert_failure(
                        ManifestValidationError,
                        code,
                        message,
                        lambda: verify_manifest(repo_root, contract, manifest),
                    )

    def test_source_identity_change_during_open_is_typed(self) -> None:
        # Given: A regular file whose post-open identity differs from its lstat identity.
        with tempfile.TemporaryDirectory() as temporary_directory:
            source = Path(temporary_directory) / "source.bin"
            source.write_bytes(b"abc")
            real_fstat = os.fstat

            def changed_fstat(descriptor: int) -> Any:
                metadata = list(real_fstat(descriptor))
                metadata[1] += 1
                return os.stat_result(metadata)

            # When: The secure opener detects the identity race.
            # Then: Hashing fails closed without exposing the host path.
            with mock.patch("corpus_index.os.fstat", side_effect=changed_fstat):
                self.assert_failure(
                    SourceUnreadableError,
                    "source.unreadable",
                    "file is not readable: source.bin",
                    lambda: hash_file(
                        source,
                        display_path="source.bin",
                        opener=_secure_binary_opener,
                    ),
                )

    def test_source_mutation_during_read_is_typed(self) -> None:
        # Given: A regular file whose opened metadata changes after streaming begins.
        with tempfile.TemporaryDirectory() as temporary_directory:
            source = Path(temporary_directory) / "source.bin"
            source.write_bytes(b"abc")
            metadata = source.stat()
            changed_values = list(metadata)
            changed_values[6] += 1
            changed_metadata = os.stat_result(changed_values)

            # When: The post-read snapshot differs from the pre-read snapshot.
            # Then: Hashing fails closed rather than publishing a raced digest.
            with mock.patch(
                "corpus_index.os.fstat",
                side_effect=(metadata, metadata, changed_metadata),
            ):
                self.assert_failure(
                    SourceUnreadableError,
                    "source.unreadable",
                    "file is not readable: source.bin",
                    lambda: hash_file(
                        source,
                        display_path="source.bin",
                        opener=_secure_binary_opener,
                    ),
                )

    def test_output_scope_rejects_contract_and_unrelated_files(self) -> None:
        # Given: Normalized repository-relative output paths outside the one manifest target.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            for output_path in (
                "docs/reference/initial-recording-corpus/2026-07-17/contract.json",
                "README.md",
            ):
                with self.subTest(output_path=output_path):
                    # When: Atomic publication is requested for an unauthorized target.
                    # Then: The overwrite scope fails closed before any parent lookup or write.
                    self.assert_failure(
                        PathValidationError,
                        "path.output.scope",
                        f"output must be {DEFAULT_MANIFEST_PATH}",
                        lambda: _write_manifest(repo_root, output_path, b"replacement\n"),
                    )

    def test_output_symlink_is_rejected_without_touching_target(self) -> None:
        # Given: The authorized manifest path is a symlink to an unrelated file.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            output_directory = (
                repo_root / "docs/reference/initial-recording-corpus/2026-07-17"
            )
            output_directory.mkdir(parents=True)
            unrelated = repo_root / "unrelated.json"
            unrelated.write_bytes(b"preserved\n")
            (output_directory / "manifest.json").symlink_to(unrelated)

            # When: Atomic publication inspects the destination.
            # Then: It rejects the symlink and preserves the target bytes.
            self.assert_failure(
                PathValidationError,
                "path.symlink",
                f"symlink is not allowed: {DEFAULT_MANIFEST_PATH}",
                lambda: _write_manifest(
                    repo_root,
                    DEFAULT_MANIFEST_PATH,
                    b"replacement\n",
                ),
            )
            self.assertEqual(b"preserved\n", unrelated.read_bytes())

    def test_atomic_output_failure_preserves_existing_manifest(self) -> None:
        # Given: An existing manifest and a synthetic atomic-replace failure.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            output_directory = (
                repo_root / "docs/reference/initial-recording-corpus/2026-07-17"
            )
            output_directory.mkdir(parents=True)
            destination = output_directory / "manifest.json"
            destination.write_bytes(b"previous\n")

            # When: Publishing the completed temporary file fails.
            # Then: The prior bytes remain and no temporary output is retained.
            with mock.patch(
                "corpus_index.os.replace",
                side_effect=OSError("synthetic replace failure"),
            ):
                self.assert_failure(
                    PathValidationError,
                    "path.output.unwritable",
                    f"output is not writable: {DEFAULT_MANIFEST_PATH}",
                    lambda: _write_manifest(
                        repo_root,
                        DEFAULT_MANIFEST_PATH,
                        b"replacement\n",
                    ),
                )
            self.assertEqual(b"previous\n", destination.read_bytes())
            self.assertEqual([], list(output_directory.glob(".manifest.json.*.tmp")))


if __name__ == "__main__":
    unittest.main()
