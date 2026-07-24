from __future__ import annotations

import copy
import hashlib
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

SCRIPT_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = SCRIPT_ROOT.parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from corpus_closeout import (  # noqa: E402
    CLOSEOUT_SCHEMA,
    DEFAULT_CLOSEOUT_PATH,
    MAXIMUM_METADATA_BYTES,
    CloseoutDependencyError,
    CloseoutIntegrityError,
    CloseoutPathError,
    CloseoutSourceError,
    CloseoutValidationError,
    load_closeout_data,
    main,
    serialize_closeout,
    verify_closeout,
)
from corpus_index import CorpusIndexError  # noqa: E402


PLAN_PATH = "docs/plans/260710-analogboard-rebuild-plan.html"
INITIAL_ROOT = "docs/reference/initial-recording-corpus/2026-07-17"
USB_ROOT = "docs/reference/usb-recording-corpus/2026-07-17"
SOURCE_PATHS = {
    "plan": PLAN_PATH,
    "corpus_contract": f"{INITIAL_ROOT}/contract.json",
    "corpus_manifest": f"{INITIAL_ROOT}/manifest.json",
    "relationship_contract": f"{INITIAL_ROOT}/relationship-contract.json",
    "relationships": f"{INITIAL_ROOT}/relationships.json",
    "custody": f"{INITIAL_ROOT}/custody.json",
    "recovery_procedure": (
        "docs/operations/initial-recording-corpus/"
        "restore-and-reacquisition.md"
    ),
    "usb_index_readme": f"{USB_ROOT}/README.md",
    "usb_manifest": f"{USB_ROOT}/manifest.json",
    "usb_scenarios": f"{USB_ROOT}/scenarios.json",
    "manifest_tool": "scripts/corpus-index/corpus_index.py",
    "relationship_tool": "scripts/corpus-index/corpus_relationships.py",
    "custody_tool": "scripts/corpus-index/corpus_custody.py",
}
SOURCE_SCHEMAS = {
    "corpus_contract": (
        "analogboard.phase0.initial-recording-corpus-contract",
        1,
    ),
    "corpus_manifest": (
        "analogboard.phase0.initial-recording-corpus-manifest",
        1,
    ),
    "relationship_contract": (
        "analogboard.phase0.initial-recording-corpus-relationship-contract",
        1,
    ),
    "relationships": (
        "analogboard.phase0.initial-recording-corpus-relationships",
        1,
    ),
    "custody": (
        "analogboard.phase0.initial-recording-corpus-custody",
        1,
    ),
    "usb_manifest": ("analogboard.phase0.usbpcap-corpus-index", 1),
    "usb_scenarios": ("analogboard.phase0.usbpcap-scenarios", 1),
}
SOURCE_ROLES = tuple(SOURCE_PATHS)
AUTHORITY_DAG = [
    {"consumer": "corpus_manifest", "inputs": ["corpus_contract"]},
    {
        "consumer": "relationship_contract",
        "inputs": [
            "plan",
            "corpus_contract",
            "corpus_manifest",
            "usb_manifest",
        ],
    },
    {"consumer": "relationships", "inputs": ["relationship_contract"]},
    {
        "consumer": "custody",
        "inputs": [
            "plan",
            "corpus_contract",
            "corpus_manifest",
            "manifest_tool",
            "recovery_procedure",
        ],
    },
    {
        "consumer": "closeout",
        "inputs": [
            "plan",
            "corpus_contract",
            "corpus_manifest",
            "relationship_contract",
            "relationships",
            "custody",
            "recovery_procedure",
            "usb_index_readme",
            "usb_manifest",
            "usb_scenarios",
            "manifest_tool",
            "relationship_tool",
            "custody_tool",
        ],
    },
]
ACCEPTANCE_CONDITIONS = [
    {
        "condition_id": "P0-C4-1",
        "evidence_roles": ["custody"],
        "verdict": "verified",
    },
    {
        "condition_id": "P0-C4-2",
        "evidence_roles": ["corpus_contract", "custody"],
        "verdict": "verified",
    },
    {
        "condition_id": "P0-C4-3",
        "evidence_roles": [
            "corpus_manifest",
            "manifest_tool",
            "custody",
        ],
        "verdict": "verified",
    },
    {
        "condition_id": "P0-C4-4",
        "evidence_roles": [
            "corpus_contract",
            "corpus_manifest",
            "manifest_tool",
        ],
        "verdict": "verified",
    },
    {
        "condition_id": "P0-C4-5",
        "evidence_roles": ["custody", "recovery_procedure"],
        "verdict": "verified",
    },
    {
        "condition_id": "P0-C4-6",
        "evidence_roles": [
            "relationship_contract",
            "relationships",
            "usb_manifest",
            "relationship_tool",
        ],
        "verdict": "verified",
    },
]
OPEN_ITEM_IDS = [
    "P0-C4-ASSET-OWNER",
    "P0-C4-RESTORE-SOURCE",
    "P0-C4-RETENTION",
]


def _canonical_json(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _write(repo_root: Path, relative_path: str, payload: bytes) -> None:
    destination = repo_root / relative_path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload)


def _source_payloads() -> dict[str, bytes]:
    return {
        role: (REPOSITORY_ROOT / path).read_bytes()
        for role, path in SOURCE_PATHS.items()
    }


def _source_reference(role: str, payload: bytes) -> dict[str, object]:
    reference: dict[str, object] = {
        "path": SOURCE_PATHS[role],
        "sha256": _sha256(payload),
    }
    if role == "plan":
        reference["revision"] = "Draft 4.7"
    if role in SOURCE_SCHEMAS:
        schema, version = SOURCE_SCHEMAS[role]
        reference["schema"] = schema
        reference["schema_version"] = version
    return reference


def _closeout(payloads: dict[str, bytes]) -> dict[str, object]:
    return {
        "acceptance_conditions": copy.deepcopy(ACCEPTANCE_CONDITIONS),
        "authority_dag": copy.deepcopy(AUTHORITY_DAG),
        "manual_gate": {
            "base_branch": "main",
            "central_handoff": "not_published",
            "head_branch": "analysis/phase0-corpus-index",
            "initial_corpus_gate": "not_closed",
            "next_scope_transition": "not_authorized",
            "pre_merge_action": "create_pr_then_stop",
            "required_action": "human_merge",
            "scope_completion": "not_declared",
        },
        "open_item_references": {
            "ids": list(OPEN_ITEM_IDS),
            "source_role": "custody",
        },
        "p0_c1_c3_overlap": {
            "binding_role": "relationship_contract",
            "evidence_role": "relationships",
            "historical_index_roles": [
                "usb_index_readme",
                "usb_manifest",
                "usb_scenarios",
            ],
            "shared_capture_identity_roles": [
                "corpus_manifest",
                "usb_manifest",
            ],
            "verdict": "verified",
        },
        "schema": CLOSEOUT_SCHEMA,
        "schema_version": 1,
        "scope": {
            "status": "gate_ready",
            "step_id": "P0-C4",
            "transition": "pending_human_merge",
        },
        "source_references": {
            role: _source_reference(role, payloads[role])
            for role in SOURCE_ROLES
        },
    }


def _fixture(repo_root: Path) -> dict[str, object]:
    payloads = _source_payloads()
    for role, payload in payloads.items():
        _write(repo_root, SOURCE_PATHS[role], payload)
    closeout = _closeout(payloads)
    _write(repo_root, DEFAULT_CLOSEOUT_PATH, serialize_closeout(closeout))
    return closeout


def _parsed_source(role: str) -> dict[str, object]:
    return json.loads((REPOSITORY_ROOT / SOURCE_PATHS[role]).read_text("utf-8"))


class CloseoutTests(unittest.TestCase):
    def assert_failure(
        self,
        expected_type: type[Exception],
        expected_code: str,
        callback: object,
    ) -> None:
        with self.assertRaises(expected_type) as raised:
            callback()  # type: ignore[operator]
        self.assertIs(expected_type, type(raised.exception))
        self.assertEqual(expected_code, getattr(raised.exception, "code", None))
        message = getattr(raised.exception, "message", None)
        self.assertIsInstance(message, str)
        self.assertTrue(message)
        self.assertEqual(f"{expected_code}: {message}", str(raised.exception))
        self.assertLessEqual(len(str(raised.exception)), 512)

    def assert_exact_failure(
        self,
        expected_type: type[Exception],
        expected_code: str,
        expected_message: str,
        callback: object,
    ) -> None:
        with self.assertRaises(expected_type) as raised:
            callback()  # type: ignore[operator]
        self.assertIs(expected_type, type(raised.exception))
        self.assertEqual(expected_code, getattr(raised.exception, "code", None))
        self.assertEqual(expected_message, getattr(raised.exception, "message", None))
        self.assertEqual(
            f"{expected_code}: {expected_message}",
            str(raised.exception),
        )

    def _align_embedded_source_references(
        self,
        repo_root: Path,
        closeout: dict[str, object],
    ) -> None:
        references = closeout["source_references"]
        assert isinstance(references, dict)
        source_mappings = {
            "relationship_contract": {
                "plan": "plan",
                "primary_contract": "corpus_contract",
                "primary_manifest": "corpus_manifest",
                "usb_manifest": "usb_manifest",
            },
            "custody": {
                "plan": "plan",
                "corpus_contract": "corpus_contract",
                "corpus_manifest": "corpus_manifest",
                "manifest_tool": "manifest_tool",
                "recovery_procedure": "recovery_procedure",
            },
        }
        for source_role, mappings in source_mappings.items():
            source = _parsed_source(source_role)
            source["source_references"] = {
                embedded_role: copy.deepcopy(references[closeout_role])
                for embedded_role, closeout_role in mappings.items()
            }
            payload = _canonical_json(source)
            _write(repo_root, SOURCE_PATHS[source_role], payload)
            source_reference = references[source_role]
            assert isinstance(source_reference, dict)
            source_reference["sha256"] = _sha256(payload)
        _write(
            repo_root,
            DEFAULT_CLOSEOUT_PATH,
            serialize_closeout(closeout),
        )

    def _rewrite_embedded_source(
        self,
        repo_root: Path,
        closeout: dict[str, object],
        source_role: str,
        *,
        embedded_role: str | None,
    ) -> None:
        source_path = repo_root / SOURCE_PATHS[source_role]
        source = json.loads(source_path.read_text(encoding="utf-8"))
        if embedded_role is None:
            source.pop("source_references")
        else:
            source["source_references"][embedded_role]["sha256"] = "0" * 64
        payload = _canonical_json(source)
        _write(repo_root, SOURCE_PATHS[source_role], payload)
        references = closeout["source_references"]
        assert isinstance(references, dict)
        source_reference = references[source_role]
        assert isinstance(source_reference, dict)
        source_reference["sha256"] = _sha256(payload)
        _write(
            repo_root,
            DEFAULT_CLOSEOUT_PATH,
            serialize_closeout(closeout),
        )

    def _live_patches(self) -> tuple[object, object, object]:
        return (
            patch(
                "corpus_closeout.build_manifest",
                return_value=_parsed_source("corpus_manifest"),
            ),
            patch(
                "corpus_closeout.build_relationship_evidence",
                return_value=_parsed_source("relationships"),
            ),
            patch(
                "corpus_closeout.verify_custody",
                return_value=_parsed_source("custody"),
            ),
        )

    def test_c5_n_01_exact_current_closeout_schema_validates(self) -> None:
        # Given: The exact 13-source, six-condition, gate-ready closeout state.
        payloads = _source_payloads()
        closeout = _closeout(payloads)

        # When: The closed current-state schema is validated.
        validated = load_closeout_data(closeout)

        # Then: It remains pending human merge with three referenced open items.
        self.assertEqual(closeout, validated)
        self.assertEqual("gate_ready", validated["scope"]["status"])
        self.assertEqual(
            OPEN_ITEM_IDS,
            validated["open_item_references"]["ids"],
        )

    def test_c5_n_02_live_composer_invokes_each_existing_seam_once(self) -> None:
        # Given: Tracked metadata plus payload-free synthetic dependency results.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            closeout = _fixture(repo_root)
            manifest_patch, relationship_patch, custody_patch = self._live_patches()

            # When: The verify-only live composer runs.
            with manifest_patch as manifest, relationship_patch as relationship:
                with custody_patch as custody:
                    verified = verify_closeout(repo_root)

            # Then: Each accepted live seam runs once and the exact index returns.
            self.assertEqual(closeout, verified)
            manifest.assert_called_once()
            relationship.assert_called_once()
            custody.assert_called_once()

    def test_c5_n_03_canonical_serialization_is_order_independent(self) -> None:
        # Given: The same closeout semantics with reversed object-key order.
        closeout = _closeout(_source_payloads())
        reordered = dict(reversed(list(closeout.items())))

        # When: Both values are serialized.
        first = serialize_closeout(closeout)
        second = serialize_closeout(reordered)

        # Then: UTF-8 canonical JSON is byte-identical with one terminal LF.
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        self.assertFalse(first.endswith(b"\n\n"))

    def test_c5_n_04_p0_c1_c3_overlap_uses_frozen_historical_roles(self) -> None:
        # Given: The accepted P0-C1-C3 README, manifest, and scenarios sources.
        closeout = _closeout(_source_payloads())

        # When: Their P0-C4 overlap declaration is inspected.
        overlap = closeout["p0_c1_c3_overlap"]

        # Then: The historical trio stays frozen while identities bind elsewhere.
        self.assertEqual(
            ["usb_index_readme", "usb_manifest", "usb_scenarios"],
            overlap["historical_index_roles"],
        )
        self.assertEqual("relationship_contract", overlap["binding_role"])
        self.assertEqual("relationships", overlap["evidence_role"])

    def test_c5_n_05_two_unchanged_runs_have_fixed_success_output(self) -> None:
        # Given: One unchanged metadata snapshot and deterministic dependency seams.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            stdout = io.StringIO()
            stderr = io.StringIO()
            manifest_patch, relationship_patch, custody_patch = self._live_patches()

            # When: The exact live CLI is run twice.
            with manifest_patch, relationship_patch, custody_patch:
                with redirect_stdout(stdout), redirect_stderr(stderr):
                    first = main(
                        [
                            "--repo-root",
                            str(repo_root),
                            "verify",
                            "--index",
                            DEFAULT_CLOSEOUT_PATH,
                        ]
                    )
                    second = main(
                        [
                            "--repo-root",
                            str(repo_root),
                            "verify",
                            "--index",
                            DEFAULT_CLOSEOUT_PATH,
                        ]
                    )

            # Then: Both exits and bounded success messages are identical.
            self.assertEqual((0, 0), (first, second))
            self.assertEqual("", stderr.getvalue())
            self.assertEqual(
                2,
                stdout.getvalue().count(
                    "P0-C4 closeout verified: gate_ready; "
                    "pending_human_merge"
                ),
            )
            self.assertNotIn(str(repo_root.resolve()), stdout.getvalue())

    def test_c5_b_01_schema_version_null_type_and_unit_boundaries(self) -> None:
        # Given: NULL/empty/schema-drift and version 0/1/2/type candidates.
        closeout = _closeout(_source_payloads())
        invalid: list[tuple[object, str]] = [
            (None, "closeout.type"),
            ({}, "closeout.fields"),
        ]
        for version in (None, True, "1", 0, 2):
            candidate = copy.deepcopy(closeout)
            candidate["schema_version"] = version
            invalid.append((candidate, "closeout.schema_version"))
        wrong_schema = copy.deepcopy(closeout)
        wrong_schema["schema"] = "wrong"
        invalid.append((wrong_schema, "closeout.schema"))

        # When/Then: Only integer schema version 1 is accepted.
        self.assertEqual(closeout, load_closeout_data(closeout))
        for value, code in invalid:
            with self.subTest(value=value, code=code):
                self.assert_failure(
                    CloseoutValidationError,
                    code,
                    lambda value=value: load_closeout_data(value),
                )

    def test_c5_b_02_acceptance_exact_six_min_plus_minus_one(self) -> None:
        # Given: Exact six conditions and five/seven/duplicate/reordered variants.
        closeout = _closeout(_source_payloads())
        mutations = []
        for conditions in (
            ACCEPTANCE_CONDITIONS[:-1],
            ACCEPTANCE_CONDITIONS + [copy.deepcopy(ACCEPTANCE_CONDITIONS[-1])],
            ACCEPTANCE_CONDITIONS[:-1] + [copy.deepcopy(ACCEPTANCE_CONDITIONS[0])],
            list(reversed(ACCEPTANCE_CONDITIONS)),
        ):
            candidate = copy.deepcopy(closeout)
            candidate["acceptance_conditions"] = conditions
            mutations.append(candidate)

        # When/Then: Six fixed ordered mappings are the sole accepted boundary.
        for candidate in mutations:
            self.assert_failure(
                CloseoutValidationError,
                "closeout.acceptance",
                lambda candidate=candidate: load_closeout_data(candidate),
            )

    def test_c5_b_03_source_roles_exact_thirteen_min_plus_minus_one(self) -> None:
        # Given: Exact 13 roles and 12/14/renamed variants.
        closeout = _closeout(_source_payloads())
        missing = copy.deepcopy(closeout)
        missing["source_references"].pop("custody_tool")
        extra = copy.deepcopy(closeout)
        extra["source_references"]["extra"] = copy.deepcopy(
            extra["source_references"]["custody_tool"]
        )
        renamed = copy.deepcopy(closeout)
        renamed["source_references"]["alternate_tool"] = renamed[
            "source_references"
        ].pop("custody_tool")

        # When/Then: The exact source role set is fail-closed.
        for candidate in (missing, extra, renamed):
            self.assert_failure(
                CloseoutSourceError,
                "closeout.source.roles",
                lambda candidate=candidate: load_closeout_data(candidate),
            )

    def test_c5_b_04_open_references_exact_three_min_plus_minus_one(self) -> None:
        # Given: Exact three open refs and two/four/reordered/duplicate variants.
        closeout = _closeout(_source_payloads())
        values = (
            OPEN_ITEM_IDS[:-1],
            OPEN_ITEM_IDS + ["P0-C4-EXTRA"],
            list(reversed(OPEN_ITEM_IDS)),
            [OPEN_ITEM_IDS[0], OPEN_ITEM_IDS[0], OPEN_ITEM_IDS[2]],
        )

        # When/Then: The exact unresolved custody reference set is required.
        for ids in values:
            candidate = copy.deepcopy(closeout)
            candidate["open_item_references"]["ids"] = ids
            self.assert_failure(
                CloseoutValidationError,
                "closeout.open_items",
                lambda candidate=candidate: load_closeout_data(candidate),
            )

    def test_c5_a_01_closed_fields_duplicate_keys_and_noncanonical_bytes_fail(
        self,
    ) -> None:
        # Given: Missing/extra fields, duplicate JSON, and extra-newline bytes.
        closeout = _closeout(_source_payloads())
        missing = copy.deepcopy(closeout)
        missing.pop("manual_gate")
        extra = copy.deepcopy(closeout)
        extra["unknown"] = None

        # When/Then: The exact nine-field schema rejects every structural drift.
        for candidate in (missing, extra):
            self.assert_failure(
                CloseoutValidationError,
                "closeout.fields",
                lambda candidate=candidate: load_closeout_data(candidate),
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            index = repo_root / DEFAULT_CLOSEOUT_PATH
            duplicate = index.read_bytes().replace(
                b'  "schema":',
                b'  "schema": "duplicate",\n  "schema":',
                1,
            )
            index.write_bytes(duplicate)
            self.assert_failure(
                CloseoutValidationError,
                "closeout.json.duplicate_key",
                lambda: verify_closeout(repo_root),
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            index = repo_root / DEFAULT_CLOSEOUT_PATH
            index.write_bytes(index.read_bytes() + b"\n")
            self.assert_failure(
                CloseoutValidationError,
                "closeout.bytes",
                lambda: verify_closeout(repo_root),
            )

    def test_c5_a_02_path_confinement_symlinks_and_nonregular_files_fail(
        self,
    ) -> None:
        # Given: Alternate/absolute source paths and symlinked metadata.
        closeout = _closeout(_source_payloads())
        for unsafe in (
            "../plan.html",
            "/absolute/plan.html",
            r"docs\\plans\\plan.html",
            "docs/plans/alternate.html",
        ):
            candidate = copy.deepcopy(closeout)
            candidate["source_references"]["plan"]["path"] = unsafe

            # When/Then: Source paths must be exact normalized repo-relative paths.
            with self.subTest(path=unsafe):
                self.assert_failure(
                    CloseoutSourceError,
                    "closeout.source.path",
                    lambda candidate=candidate: load_closeout_data(candidate),
                )

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            source = repo_root / SOURCE_PATHS["plan"]
            target = source.with_name("plan-real.html")
            source.rename(target)
            source.symlink_to(target.name)
            self.assert_failure(
                CloseoutSourceError,
                "closeout.source.symlink",
                lambda: verify_closeout(repo_root),
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            index = repo_root / DEFAULT_CLOSEOUT_PATH
            index.unlink()
            index.mkdir()
            self.assert_failure(
                CloseoutPathError,
                "closeout.index.unreadable",
                lambda: verify_closeout(repo_root),
            )

    def test_c5_a_03_source_hash_schema_revision_and_tool_drift_fail(self) -> None:
        # Given: Recorded SHA, schema, revision, and tool identity drift.
        closeout = _closeout(_source_payloads())
        invalid_schema = copy.deepcopy(closeout)
        invalid_schema["source_references"]["usb_manifest"][
            "schema_version"
        ] = 2
        invalid_revision = copy.deepcopy(closeout)
        invalid_revision["source_references"]["plan"]["revision"] = "Draft 4.8"

        # When/Then: Unsupported declarations fail before source reads.
        for candidate in (invalid_schema, invalid_revision):
            self.assert_failure(
                CloseoutSourceError,
                "closeout.source.declaration",
                lambda candidate=candidate: load_closeout_data(candidate),
            )

        for role in ("plan", "manifest_tool"):
            with self.subTest(role=role):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    repo_root = Path(temporary_directory)
                    _fixture(repo_root)
                    source = repo_root / SOURCE_PATHS[role]
                    source.write_bytes(source.read_bytes() + b"\ndrift\n")
                    self.assert_failure(
                        CloseoutSourceError,
                        "closeout.source.sha256_mismatch",
                        lambda: verify_closeout(repo_root),
                    )

    def test_c5_a_04_authority_dag_missing_extra_reorder_and_cycle_fail(self) -> None:
        # Given: Missing/extra/reordered edges and a direct authority cycle.
        closeout = _closeout(_source_payloads())
        dags = [
            AUTHORITY_DAG[:-1],
            AUTHORITY_DAG
            + [{"consumer": "plan", "inputs": ["closeout"]}],
            list(reversed(AUTHORITY_DAG)),
        ]
        cyclic = copy.deepcopy(AUTHORITY_DAG)
        cyclic[0]["inputs"] = ["closeout"]
        dags.append(cyclic)

        # When/Then: The exact acyclic authority graph is required.
        for dag in dags:
            candidate = copy.deepcopy(closeout)
            candidate["authority_dag"] = dag
            self.assert_failure(
                CloseoutValidationError,
                "closeout.authority_dag",
                lambda candidate=candidate: load_closeout_data(candidate),
            )

    def test_c5_a_05_duplicate_authority_payload_and_host_metadata_fail(self) -> None:
        # Given: Count/byte/entry/run/pair/locator/payload authority additions.
        closeout = _closeout(_source_payloads())
        prohibited = (
            ("expected_counts", {}),
            ("expected_total_bytes", 0),
            ("actual_total_bytes", 0),
            ("entries", []),
            ("runs", []),
            ("pair_count", 0),
            ("canonical_locator", "artifacts/local"),
            ("payload", ""),
            ("host_locator", "/host/path"),
        )

        # When/Then: The closeout sink cannot become another fact authority.
        for field, value in prohibited:
            candidate = copy.deepcopy(closeout)
            candidate[field] = value
            with self.subTest(field=field):
                self.assert_failure(
                    CloseoutValidationError,
                    "closeout.metadata.prohibited",
                    lambda candidate=candidate: load_closeout_data(candidate),
                )

    def test_c5_a_06_completed_handoff_gate_and_transition_claims_fail(self) -> None:
        # Given: Positive completion, merge, handoff, gate, or next-scope claims.
        closeout = _closeout(_source_payloads())
        mutations = []
        for section, field, value in (
            ("scope", "status", "completed"),
            ("scope", "status", "planned"),
            ("scope", "transition", "merged"),
            ("manual_gate", "scope_completion", "completed"),
            ("manual_gate", "initial_corpus_gate", "closed"),
            ("manual_gate", "central_handoff", "published"),
            ("manual_gate", "next_scope_transition", "authorized"),
            ("manual_gate", "pre_merge_action", "merge"),
        ):
            candidate = copy.deepcopy(closeout)
            candidate[section][field] = value
            mutations.append(candidate)

        # When/Then: Only gate-ready/pending-human-merge current state is accepted.
        for candidate in mutations:
            self.assert_failure(
                CloseoutValidationError,
                "closeout.status",
                lambda candidate=candidate: load_closeout_data(candidate),
            )

    def test_c5_a_07_acceptance_role_verdict_id_and_order_drift_fail(self) -> None:
        # Given: Wrong ID, verdict, role, and object-field mutations.
        closeout = _closeout(_source_payloads())
        mutations = []
        for field, value in (
            ("condition_id", "P0-C4-7"),
            ("verdict", "partial"),
            ("evidence_roles", ["plan"]),
        ):
            candidate = copy.deepcopy(closeout)
            candidate["acceptance_conditions"][0][field] = value
            mutations.append(candidate)
        unknown = copy.deepcopy(closeout)
        unknown["acceptance_conditions"][0]["criterion"] = "duplicated prose"
        mutations.append(unknown)

        # When/Then: Six plan-referenced mappings remain exact and prose-free.
        for candidate in mutations:
            self.assert_failure(
                CloseoutValidationError,
                "closeout.acceptance",
                lambda candidate=candidate: load_closeout_data(candidate),
            )

    def test_c5_a_08_custody_open_items_must_match_held_closeout_refs(self) -> None:
        # Given: A custody validator result with a different open-item set.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            custody = _parsed_source("custody")
            custody["open_items"] = custody["open_items"][:-1]
            manifest_patch, relationship_patch, custody_patch = self._live_patches()

            # When/Then: Cross-authority mismatch fails despite a successful seam.
            with manifest_patch, relationship_patch:
                with patch(
                    "corpus_closeout.verify_custody",
                    return_value=custody,
                ):
                    self.assert_failure(
                        CloseoutIntegrityError,
                        "closeout.custody.result_mismatch",
                        lambda: verify_closeout(repo_root),
                    )

    def test_pr9_cl_a_03_embedded_reference_drift_fails_at_binding(self) -> None:
        # Given: A relationship/custody embedded pin drifts while its outer SHA is repinned.
        cases = (
            ("relationship_contract", "primary_manifest"),
            ("custody", "manifest_tool"),
        )
        for source_role, embedded_role in cases:
            with self.subTest(
                source_role=source_role,
                embedded_role=embedded_role,
            ), tempfile.TemporaryDirectory() as temporary_directory:
                repo_root = Path(temporary_directory)
                closeout = _fixture(repo_root)
                self._align_embedded_source_references(repo_root, closeout)
                self._rewrite_embedded_source(
                    repo_root,
                    closeout,
                    source_role,
                    embedded_role=embedded_role,
                )

                # When: Closeout verification checks the independently valid source.
                # Then: The inner/outer authority mismatch has one exact typed error.
                self.assert_exact_failure(
                    CloseoutSourceError,
                    "closeout.source.binding",
                    "embedded source reference differs from the closeout pin",
                    lambda: verify_closeout(repo_root),
                )

    def test_pr9_cl_a_03_missing_embedded_references_fail_at_binding(self) -> None:
        # Given: Relationship/custody source references are removed and outer SHA repinned.
        cases = (
            (
                "relationship_contract",
                "relationship source references are missing",
            ),
            ("custody", "custody source references are missing"),
        )
        for source_role, message in cases:
            with self.subTest(
                source_role=source_role
            ), tempfile.TemporaryDirectory() as temporary_directory:
                repo_root = Path(temporary_directory)
                closeout = _fixture(repo_root)
                self._align_embedded_source_references(repo_root, closeout)
                self._rewrite_embedded_source(
                    repo_root,
                    closeout,
                    source_role,
                    embedded_role=None,
                )

                # When: Closeout verification inspects the missing inner authority.
                # Then: Binding reports the exact source-specific missing-reference error.
                self.assert_exact_failure(
                    CloseoutSourceError,
                    "closeout.source.binding",
                    message,
                    lambda: verify_closeout(repo_root),
                )

    def test_c5_a_09_usb_overlap_and_historical_source_drift_fail(self) -> None:
        # Given: Drift in each frozen P0-C1-C3 historical source.
        for role in ("usb_index_readme", "usb_manifest", "usb_scenarios"):
            with self.subTest(role=role):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    repo_root = Path(temporary_directory)
                    _fixture(repo_root)
                    source = repo_root / SOURCE_PATHS[role]
                    source.write_bytes(source.read_bytes() + b"\ndrift\n")

                    # When/Then: No historical source can be silently substituted.
                    self.assert_failure(
                        CloseoutSourceError,
                        "closeout.source.sha256_mismatch",
                        lambda: verify_closeout(repo_root),
                    )

        # Given: The relationship validator reports an overlap/failure-trace error.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            manifest_patch, _, custody_patch = self._live_patches()
            dependency = CorpusIndexError(
                "relationship.capture.identity",
                "untrusted capture mismatch",
            )

            # When/Then: The closeout fails as a bounded dependency error.
            with manifest_patch, custody_patch:
                with patch(
                    "corpus_closeout.build_relationship_evidence",
                    side_effect=dependency,
                ):
                    self.assert_failure(
                        CloseoutDependencyError,
                        "closeout.dependency.relationship",
                        lambda: verify_closeout(repo_root),
                    )

    def test_c5_a_10_manifest_regeneration_byte_drift_fails(self) -> None:
        # Given: A regenerated manifest differing by one canonical metadata byte.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            manifest = _parsed_source("corpus_manifest")
            manifest["entries"] = list(reversed(manifest["entries"]))
            _, relationship_patch, custody_patch = self._live_patches()

            # When/Then: Full-sweep regeneration must byte-match the tracked manifest.
            with relationship_patch, custody_patch:
                with patch(
                    "corpus_closeout.build_manifest",
                    return_value=manifest,
                ):
                    self.assert_failure(
                        CloseoutIntegrityError,
                        "closeout.manifest.bytes",
                        lambda: verify_closeout(repo_root),
                    )

    def test_c5_a_11_relationship_regeneration_byte_drift_fails(self) -> None:
        # Given: Relationship evidence differing in deterministic run order.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            relationships = _parsed_source("relationships")
            relationships["runs"] = list(reversed(relationships["runs"]))
            manifest_patch, _, custody_patch = self._live_patches()

            # When/Then: Relationship regeneration must byte-match tracked evidence.
            with manifest_patch, custody_patch:
                with patch(
                    "corpus_closeout.build_relationship_evidence",
                    return_value=relationships,
                ):
                    self.assert_failure(
                        CloseoutIntegrityError,
                        "closeout.relationships.bytes",
                        lambda: verify_closeout(repo_root),
                    )

    def test_c5_a_12_dependency_errors_are_bounded_and_never_partial(self) -> None:
        # Given: A dependency failure carrying secret-like untrusted text.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            secret = "token=" + ("s" * 4096)
            stderr = io.StringIO()

            # When: The CLI catches the dependency failure.
            with patch(
                "corpus_closeout.build_manifest",
                side_effect=CorpusIndexError("source.failure", secret),
            ):
                with redirect_stderr(stderr):
                    result = main(
                        [
                            "--repo-root",
                            str(repo_root),
                            "verify",
                            "--index",
                            DEFAULT_CLOSEOUT_PATH,
                        ]
                    )

            # Then: It fails without echo, traceback, or a partial success line.
            output = stderr.getvalue()
            self.assertEqual(2, result)
            self.assertIn("ERROR closeout.dependency.manifest:", output)
            self.assertNotIn(secret, output)
            self.assertNotIn("token=", output.lower())
            self.assertNotIn("gate_ready", output)
            self.assertLessEqual(len(output), 512)

    def test_c5_a_13_snapshot_reread_detects_toctou_replacement(self) -> None:
        # Given: A source that changes only at the final snapshot re-read.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            manifest_patch, relationship_patch, custody_patch = self._live_patches()
            from corpus_closeout import _read_metadata_file

            plan = repo_root / SOURCE_PATHS["plan"]
            original = _read_metadata_file(plan, "plan")
            reads = 0

            def racing_reader(path: Path, label: str) -> bytes:
                nonlocal reads
                payload = _read_metadata_file(path, label)
                if path == plan:
                    reads += 1
                    if reads == 2:
                        return payload + b"replacement"
                return payload

            # When/Then: End-of-run snapshot comparison fails closed.
            with manifest_patch, relationship_patch, custody_patch:
                with patch(
                    "corpus_closeout._read_metadata_file",
                    side_effect=racing_reader,
                ):
                    self.assert_failure(
                        CloseoutIntegrityError,
                        "closeout.snapshot.changed",
                        lambda: verify_closeout(repo_root),
                    )
            self.assertEqual(original, plan.read_bytes())

    def test_c5_a_14_duplicate_key_echo_and_oversize_sources_are_bounded(self) -> None:
        # Given: A duplicate 4 KiB secret-like key in the closeout JSON.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            secret = "token=" + ("s" * 4096)
            index = repo_root / DEFAULT_CLOSEOUT_PATH
            duplicate = index.read_bytes().replace(
                b"{\n",
                (
                    "{\n"
                    f'  "{secret}": null,\n'
                    f'  "{secret}": null,\n'
                ).encode("utf-8"),
                1,
            )
            index.write_bytes(duplicate)
            stderr = io.StringIO()

            # When: Strict JSON parsing rejects the duplicate.
            with redirect_stderr(stderr):
                result = main(
                    [
                        "--repo-root",
                        str(repo_root),
                        "verify",
                        "--index",
                        DEFAULT_CLOSEOUT_PATH,
                    ]
                )

            # Then: The error is bounded and never reflects the key.
            output = stderr.getvalue()
            self.assertEqual(2, result)
            self.assertNotIn(secret, output)
            self.assertNotIn("token=", output.lower())
            self.assertLessEqual(len(output), 512)

        # Given: A source one byte beyond the metadata limit with a matching pin.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            closeout = _fixture(repo_root)
            oversized = b"x" * (MAXIMUM_METADATA_BYTES + 1)
            _write(repo_root, SOURCE_PATHS["plan"], oversized)
            closeout["source_references"]["plan"]["sha256"] = _sha256(oversized)
            _write(repo_root, DEFAULT_CLOSEOUT_PATH, serialize_closeout(closeout))

            # When/Then: Bounded reading rejects it without consuming more.
            self.assert_failure(
                CloseoutSourceError,
                "closeout.source.oversize",
                lambda: verify_closeout(repo_root),
            )

        # Given: Adversarially deep JSON and an over-limit integer literal.
        malformed_payloads = (
            b"[" * 1200 + b"]" * 1200,
            b'{"schema_version":' + (b"9" * 5000) + b"}",
        )
        for payload in malformed_payloads:
            with self.subTest(payload_length=len(payload)):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    repo_root = Path(temporary_directory)
                    _fixture(repo_root)
                    _write(repo_root, DEFAULT_CLOSEOUT_PATH, payload)
                    stderr = io.StringIO()

                    # When: The strict JSON boundary receives pathological input.
                    with redirect_stderr(stderr):
                        result = main(
                            [
                                "--repo-root",
                                str(repo_root),
                                "verify",
                                "--index",
                                DEFAULT_CLOSEOUT_PATH,
                            ]
                        )

                    # Then: Parsing fails as a fixed, bounded typed error.
                    output = stderr.getvalue()
                    self.assertEqual(2, result)
                    self.assertIn("ERROR closeout.json.invalid:", output)
                    self.assertNotIn(str(repo_root.resolve()), output)
                    self.assertLessEqual(len(output), 512)

    def test_c5_a_15_cli_has_no_build_output_metadata_only_or_write_path(self) -> None:
        # Given: The exact verify command and spies on publication primitives.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            manifest_patch, relationship_patch, custody_patch = self._live_patches()

            # When: The live verifier runs under write-failing spies.
            with manifest_patch, relationship_patch, custody_patch:
                with patch.object(
                    Path,
                    "write_bytes",
                    side_effect=AssertionError("write forbidden"),
                ), patch(
                    "tempfile.mkstemp",
                    side_effect=AssertionError("temporary output forbidden"),
                ), patch(
                    "os.replace",
                    side_effect=AssertionError("replace forbidden"),
                ):
                    verified = verify_closeout(repo_root)

            # Then: Verification succeeds without any tracked or temporary write.
            self.assertEqual("gate_ready", verified["scope"]["status"])

        # Given/When/Then: Build/output/metadata-only CLI shapes are absent and
        # parse failures are bounded without reflecting untrusted arguments.
        for arguments in (
            ["build"],
            ["metadata-only"],
            ["verify", "--index", DEFAULT_CLOSEOUT_PATH, "--output", "x"],
            ["verify", "--index", "token=" + ("s" * 4096)],
        ):
            with self.subTest(arguments=arguments):
                stderr = io.StringIO()
                with redirect_stderr(stderr):
                    result = main(arguments)
                output = stderr.getvalue()
                self.assertEqual(2, result)
                self.assertIn("ERROR closeout.cli.arguments:", output)
                self.assertNotIn("token=", output.lower())
                self.assertLessEqual(len(output), 512)

        # Given: A repository-root argument containing an embedded NUL.
        stderr = io.StringIO()

        # When: Filesystem traversal receives the invalid host path.
        with redirect_stderr(stderr):
            result = main(
                [
                    "--repo-root",
                    "bad\x00root",
                    "verify",
                    "--index",
                    DEFAULT_CLOSEOUT_PATH,
                ]
            )

        # Then: The filesystem exception is normalized without echo or traceback.
        output = stderr.getvalue()
        self.assertEqual(2, result)
        self.assertIn("ERROR closeout.index.unreadable:", output)
        self.assertNotIn("bad", output)
        self.assertLessEqual(len(output), 512)


if __name__ == "__main__":
    unittest.main()
