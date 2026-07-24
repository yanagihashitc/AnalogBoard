from __future__ import annotations

import copy
import hashlib
import json
import os
from dataclasses import replace
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock


SCRIPT_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(SCRIPT_DIR))

from closeout import (  # noqa: E402
    ACCEPTANCE_CONDITIONS,
    DEFAULT_CLOSEOUT_PATH,
    FIXED_SOURCE_DECLARATIONS,
    MANDATORY_MUTATIONS,
    CloseoutFailure,
    build_closeout,
    generate_closeout,
    load_closeout_bytes,
    load_closeout_data,
    serialize_closeout,
    verify_closeout,
)


def _canonical(value: object) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=True,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def _copy_sources(destination: Path) -> None:
    for declaration in FIXED_SOURCE_DECLARATIONS.values():
        source = REPO_ROOT / declaration.path
        target = destination / declaration.path
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)


def _fixture() -> tempfile.TemporaryDirectory[str]:
    temporary = tempfile.TemporaryDirectory()
    _copy_sources(Path(temporary.name))
    return temporary


def _updated_declarations(
    role: str,
    payload: bytes,
) -> dict[str, object]:
    declarations = dict(FIXED_SOURCE_DECLARATIONS)
    declarations[role] = replace(
        declarations[role],
        sha256=hashlib.sha256(payload).hexdigest(),
        size_bytes=len(payload),
    )
    return declarations


def _rewrite_json(
    repo_root: Path,
    role: str,
    mutate,
) -> dict[str, object]:
    declaration = FIXED_SOURCE_DECLARATIONS[role]
    path = repo_root / declaration.path
    value = json.loads(path.read_bytes())
    mutate(value)
    payload = _canonical(value)
    path.write_bytes(payload)
    return _updated_declarations(role, payload)


class CloseoutPositiveTests(unittest.TestCase):
    def test_c_n_01_exact_current_sources_build_six_verified_conditions(self) -> None:
        closeout = build_closeout(REPO_ROOT)

        self.assertEqual(
            [condition["condition_id"] for condition in closeout["acceptance_conditions"]],
            [condition.condition_id for condition in ACCEPTANCE_CONDITIONS],
        )
        self.assertEqual(
            ["verified"] * 6,
            [condition["verdict"] for condition in closeout["acceptance_conditions"]],
        )
        self.assertEqual("gate_ready", closeout["scope"]["status"])

    def test_c_n_02_serialization_is_canonical_and_deterministic(self) -> None:
        closeout = build_closeout(REPO_ROOT)
        reversed_root = dict(reversed(list(closeout.items())))

        first = serialize_closeout(closeout)
        second = serialize_closeout(reversed_root)

        self.assertEqual(first, second)
        self.assertLessEqual(len(first), 32_768)

    def test_c_n_03_generate_twice_and_verify_are_byte_identical(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)

            first = generate_closeout(repo_root)
            second = generate_closeout(repo_root)
            verified = verify_closeout(repo_root)

            self.assertEqual(first, second)
            self.assertEqual(first, (repo_root / DEFAULT_CLOSEOUT_PATH).read_bytes())
            self.assertEqual(load_closeout_bytes(first), verified)

    def test_c_b_01_exact_sources_and_mutation_classes_are_closed(self) -> None:
        closeout = build_closeout(REPO_ROOT)

        self.assertEqual(
            list(FIXED_SOURCE_DECLARATIONS),
            list(closeout["source_references"]),
        )
        self.assertEqual(
            list(MANDATORY_MUTATIONS),
            closeout["regression_evidence"]["mandatory_mutations"],
        )
        self.assertEqual(39, closeout["regression_evidence"]["channel_records"])
        self.assertEqual(3, closeout["regression_evidence"]["pair_count"])

    def test_c_n_04_closeout_is_payload_free_and_locator_free(self) -> None:
        payload = serialize_closeout(build_closeout(REPO_ROOT)).decode("ascii")

        self.assertNotIn("samples", payload.lower())
        self.assertNotIn("waveform", payload.lower())
        self.assertNotIn("file://", payload.lower())
        self.assertNotIn(str(REPO_ROOT), payload)


class CloseoutSchemaFailureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.closeout = build_closeout(REPO_ROOT)

    def assert_code(self, expected: str, operation) -> None:
        with self.assertRaises(CloseoutFailure) as caught:
            operation()
        self.assertEqual(expected, caught.exception.code)
        self.assertNotIn(str(REPO_ROOT), str(caught.exception))

    def test_c_a_03_root_fields_are_exact(self) -> None:
        missing = copy.deepcopy(self.closeout)
        missing.pop("scope")
        extra = copy.deepcopy(self.closeout)
        extra["note"] = "not authorized"

        self.assert_code("closeout.fields", lambda: load_closeout_data(missing))
        self.assert_code("closeout.fields", lambda: load_closeout_data(extra))

    def test_c_a_03_acceptance_conditions_reject_missing_extra_and_order(self) -> None:
        missing = copy.deepcopy(self.closeout)
        missing["acceptance_conditions"].pop()
        extra = copy.deepcopy(self.closeout)
        extra["acceptance_conditions"].append(
            copy.deepcopy(extra["acceptance_conditions"][-1])
        )
        reordered = copy.deepcopy(self.closeout)
        reordered["acceptance_conditions"][0:2] = reversed(
            reordered["acceptance_conditions"][0:2]
        )

        for candidate in (missing, extra, reordered):
            self.assert_code(
                "closeout.acceptance",
                lambda candidate=candidate: load_closeout_data(candidate),
            )

    def test_c_a_06_payload_and_absolute_locator_are_rejected(self) -> None:
        payload = copy.deepcopy(self.closeout)
        payload["source_references"]["plan"]["samples"] = [1, 2]
        locator = copy.deepcopy(self.closeout)
        locator["source_references"]["plan"]["path"] = "/host/plan.html"

        self.assert_code(
            "closeout.payload",
            lambda: load_closeout_data(payload),
        )
        self.assert_code(
            "closeout.locator",
            lambda: load_closeout_data(locator),
        )

    def test_c_a_06_duplicate_keys_and_nonfinite_json_are_rejected(self) -> None:
        duplicate = b'{"schema":"a","schema":"b"}'
        nonfinite = b'{"value":NaN}'

        self.assert_code(
            "closeout.json.duplicate_key",
            lambda: load_closeout_bytes(duplicate),
        )
        self.assert_code(
            "closeout.json.nonfinite",
            lambda: load_closeout_bytes(nonfinite),
        )

    def test_c_a_06_document_size_is_bounded(self) -> None:
        oversized = b" " * 32_769

        self.assert_code(
            "closeout.json.oversize",
            lambda: load_closeout_bytes(oversized),
        )


class CloseoutSourceFailureTests(unittest.TestCase):
    def assert_code(self, expected: str, operation) -> None:
        with self.assertRaises(CloseoutFailure) as caught:
            operation()
        self.assertEqual(expected, caught.exception.code)
        self.assertNotIn("tmp", str(caught.exception).lower())

    def test_c_a_01_missing_source_fails_typed(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            declaration = FIXED_SOURCE_DECLARATIONS["channel_mapping"]
            (repo_root / declaration.path).unlink()

            self.assert_code(
                "closeout.source.missing",
                lambda: build_closeout(repo_root),
            )

    def test_c_a_01_source_file_symlink_fails_typed(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            declaration = FIXED_SOURCE_DECLARATIONS["channel_mapping"]
            target = repo_root / declaration.path
            moved = target.with_name("mapping-target.json")
            target.rename(moved)
            target.symlink_to(moved.name)

            self.assert_code(
                "closeout.source.symlink",
                lambda: build_closeout(repo_root),
            )

    def test_c_a_01_source_ancestor_symlink_fails_typed(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            source_dir = repo_root / "docs/reference/d17-golden-regression"
            moved = repo_root / "docs/reference/d17-golden-regression-real"
            source_dir.rename(moved)
            source_dir.symlink_to(moved.name, target_is_directory=True)

            self.assert_code(
                "closeout.source.symlink",
                lambda: build_closeout(repo_root),
            )

    def test_c_a_02_source_content_identity_drift_fails_typed(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            declaration = FIXED_SOURCE_DECLARATIONS["channel_mapping"]
            path = repo_root / declaration.path
            path.write_bytes(path.read_bytes() + b" ")

            self.assert_code(
                "closeout.source.size_mismatch",
                lambda: build_closeout(repo_root),
            )

    def test_c_a_05_profile_git_blob_drift_fails_even_if_sha_is_redeclared(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            declaration = FIXED_SOURCE_DECLARATIONS["refactor_profile"]
            path = repo_root / declaration.path
            payload = path.read_bytes() + b"\n"
            path.write_bytes(payload)
            declarations = _updated_declarations("refactor_profile", payload)

            self.assert_code(
                "closeout.source.git_blob_mismatch",
                lambda: build_closeout(repo_root, declarations),
            )


class CloseoutSemanticFailureTests(unittest.TestCase):
    def assert_code(self, expected: str, operation) -> None:
        with self.assertRaises(CloseoutFailure) as caught:
            operation()
        self.assertEqual(expected, caught.exception.code)

    def test_c_a_04_mapping_must_have_exact_thirteen_ordered_channels(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            declarations = _rewrite_json(
                repo_root,
                "channel_mapping",
                lambda value: value["mapping"].pop(),
            )

            self.assert_code(
                "closeout.mapping",
                lambda: build_closeout(repo_root, declarations),
            )

    def test_c_a_04_selection_must_bind_three_pairs_and_six_entries(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            declarations = _rewrite_json(
                repo_root,
                "golden_inputs",
                lambda value: value.update({"pair_count": 2}),
            )

            self.assert_code(
                "closeout.selection",
                lambda: build_closeout(repo_root, declarations),
            )

    def test_c_a_04_reference_requires_thirty_nine_u2_channel_records(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            declarations = _rewrite_json(
                repo_root,
                "golden_reference",
                lambda value: value["pairs"][0]["channels"][0].update(
                    {"dtype": "<u4"}
                ),
            )

            self.assert_code(
                "closeout.reference",
                lambda: build_closeout(repo_root, declarations),
            )

    def test_c_a_05_selection_manifest_pin_must_match_p0_c4_manifest(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            declarations = _rewrite_json(
                repo_root,
                "golden_inputs",
                lambda value: value["sources"]["manifest"].update(
                    {"sha256": "0" * 64}
                ),
            )

            self.assert_code(
                "closeout.corpus_binding",
                lambda: build_closeout(repo_root, declarations),
            )

    def test_c_a_05_p0_c4_closeout_manifest_pin_must_match(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            declarations = _rewrite_json(
                repo_root,
                "p0_c4_closeout",
                lambda value: value["source_references"]["corpus_manifest"].update(
                    {"sha256": "0" * 64}
                ),
            )

            self.assert_code(
                "closeout.corpus_binding",
                lambda: build_closeout(repo_root, declarations),
            )

    def test_c_a_05_each_selected_identity_must_exist_in_p0_c4_manifest(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            selection_path = (
                repo_root / FIXED_SOURCE_DECLARATIONS["golden_inputs"].path
            )
            selection = json.loads(selection_path.read_bytes())
            selection["pairs"][0]["entries"][0]["sha256"] = "0" * 64
            selection_payload = _canonical(selection)
            selection_path.write_bytes(selection_payload)

            declarations = dict(FIXED_SOURCE_DECLARATIONS)
            declarations["golden_inputs"] = replace(
                declarations["golden_inputs"],
                sha256=hashlib.sha256(selection_payload).hexdigest(),
                size_bytes=len(selection_payload),
            )

            reference_path = (
                repo_root / FIXED_SOURCE_DECLARATIONS["golden_reference"].path
            )
            reference = json.loads(reference_path.read_bytes())
            reference["sources"]["golden_inputs"] = {
                "path": declarations["golden_inputs"].path,
                "sha256": declarations["golden_inputs"].sha256,
                "size_bytes": declarations["golden_inputs"].size_bytes,
            }
            reference_payload = _canonical(reference)
            reference_path.write_bytes(reference_payload)
            declarations["golden_reference"] = replace(
                declarations["golden_reference"],
                sha256=hashlib.sha256(reference_payload).hexdigest(),
                size_bytes=len(reference_payload),
            )

            self.assert_code(
                "closeout.corpus_binding",
                lambda: build_closeout(repo_root, declarations),
            )


class CloseoutOutputFailureTests(unittest.TestCase):
    def assert_code(self, expected: str, operation) -> None:
        with self.assertRaises(CloseoutFailure) as caught:
            operation()
        self.assertEqual(expected, caught.exception.code)

    def test_c_a_07_existing_drifted_closeout_is_not_overwritten(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            output = repo_root / DEFAULT_CLOSEOUT_PATH
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(b"{}\n")

            self.assert_code(
                "closeout.output.mismatch",
                lambda: generate_closeout(repo_root),
            )
            self.assertEqual(b"{}\n", output.read_bytes())

    def test_c_a_07_output_symlink_is_not_followed(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            output = repo_root / DEFAULT_CLOSEOUT_PATH
            output.parent.mkdir(parents=True, exist_ok=True)
            target = output.with_name("target.json")
            target.write_bytes(b"unchanged")
            output.symlink_to(target.name)

            self.assert_code(
                "closeout.output.symlink",
                lambda: generate_closeout(repo_root),
            )
            self.assertEqual(b"unchanged", target.read_bytes())

    def test_c_a_07_noncanonical_closeout_fails_verification(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            generated = generate_closeout(repo_root)
            output = repo_root / DEFAULT_CLOSEOUT_PATH
            output.write_bytes(b" " + generated)

            self.assert_code(
                "closeout.bytes",
                lambda: verify_closeout(repo_root),
            )

    def test_c_a_07_existing_output_is_read_through_safe_descriptor(self) -> None:
        with _fixture() as temporary:
            repo_root = Path(temporary)
            expected = generate_closeout(repo_root)

            with mock.patch.object(
                Path,
                "read_bytes",
                side_effect=AssertionError("path-based output read"),
            ):
                self.assertEqual(expected, generate_closeout(repo_root))
                self.assertEqual(
                    load_closeout_bytes(expected),
                    verify_closeout(repo_root),
                )


if __name__ == "__main__":
    unittest.main()
