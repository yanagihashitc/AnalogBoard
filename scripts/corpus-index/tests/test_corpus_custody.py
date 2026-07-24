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
from typing import Any

SCRIPT_ROOT = Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = SCRIPT_ROOT.parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

import corpus_custody  # noqa: E402
from corpus_custody import (  # noqa: E402
    CUSTODY_SCHEMA,
    DEFAULT_CUSTODY_PATH,
    CustodyPathError,
    CustodyProcedureError,
    CustodySourceError,
    CustodyValidationError,
    load_custody_data,
    main,
    serialize_custody,
    verify_custody,
)


CONTRACT_SCHEMA = "analogboard.phase0.initial-recording-corpus-contract"
MANIFEST_SCHEMA = "analogboard.phase0.initial-recording-corpus-manifest"
LOCATOR = "artifacts/field-session/2026-07-17-characterization"
PLAN_PATH = "docs/plans/260710-analogboard-rebuild-plan.html"
CONTRACT_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/contract.json"
)
MANIFEST_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/manifest.json"
)
TOOL_PATH = "scripts/corpus-index/corpus_index.py"
PROCEDURE_PATH = (
    "docs/operations/initial-recording-corpus/restore-and-reacquisition.md"
)
AVAILABILITY_CHECKS = [
    "source_set_exact",
    "present",
    "regular_file",
    "readable",
    "size_bytes",
    "sha256",
]
OPEN_ITEMS = [
    {
        "id": "P0-C4-ASSET-OWNER",
        "reason": "asset_owner_decision_required",
        "status": "open",
    },
    {
        "id": "P0-C4-RESTORE-SOURCE",
        "reason": "restore_source_not_identified",
        "status": "open",
    },
    {
        "id": "P0-C4-RETENTION",
        "reason": "retention_policy_decision_required",
        "status": "open",
    },
]
ALLOWED_COMMAND = """PYTHONDONTWRITEBYTECODE=1 python3 scripts/corpus-index/corpus_index.py \\
  verify \\
  --manifest docs/reference/initial-recording-corpus/2026-07-17/manifest.json"""
PROCEDURE_TEXT = (REPOSITORY_ROOT / PROCEDURE_PATH).read_text(encoding="utf-8")


def _canonical_json(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")


def _sha256(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _contract() -> dict[str, Any]:
    return {
        "asset_kinds": [
            {
                "expected_count": 1,
                "filename_pattern": (
                    "(?P<run_id>[0-9]{6}_[0-9]{4})_"
                    "(?:fl|fh)_[1-9][0-9]*[.]bin"
                ),
                "kind": "bin",
            },
            {
                "expected_count": 1,
                "filename_pattern": (
                    "(?P<run_id>[0-9]{6}_[0-9]{4})_cfg[.]txt"
                ),
                "kind": "cfg",
            },
            {
                "expected_count": 1,
                "filename_pattern": (
                    "[0-9]{6}_[0-9]{6}_rearm_telemetry[.]csv"
                ),
                "kind": "telemetry",
            },
            {
                "expected_count": 1,
                "filename_pattern": "[a-z0-9_]+[.]pcapng",
                "kind": "capture",
            },
        ],
        "canonical_locator": LOCATOR,
        "excluded_paths": ["analysis"],
        "expected_total_bytes": 4,
        "idle_captures": [],
        "run_capture_mapping": [
            {
                "capture": "capture.pcapng",
                "density": "synthetic",
                "run_id": "260717_0001",
            }
        ],
        "schema": CONTRACT_SCHEMA,
        "schema_version": 1,
    }


def _manifest() -> dict[str, Any]:
    entries = [
        {
            "kind": "telemetry",
            "path": "260717_000100_rearm_telemetry.csv",
            "sha256": "1" * 64,
            "size_bytes": 1,
        },
        {
            "kind": "cfg",
            "path": "260717_0001_cfg.txt",
            "sha256": "2" * 64,
            "size_bytes": 1,
        },
        {
            "kind": "bin",
            "path": "260717_0001_fl_1.bin",
            "sha256": "3" * 64,
            "size_bytes": 1,
        },
        {
            "kind": "capture",
            "path": "capture.pcapng",
            "sha256": "4" * 64,
            "size_bytes": 1,
        },
    ]
    return {
        "actual_total_bytes": 4,
        "entries": sorted(entries, key=lambda item: item["path"]),
        "excluded_paths": ["analysis"],
        "expected_counts": {
            "bin": 1,
            "capture": 1,
            "cfg": 1,
            "telemetry": 1,
        },
        "expected_total_bytes": 4,
        "schema": MANIFEST_SCHEMA,
        "schema_version": 1,
        "source_locator": LOCATOR,
    }


def _write(repo_root: Path, relative_path: str, payload: bytes) -> None:
    destination = repo_root / relative_path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(payload)


def _policy(source_payloads: dict[str, bytes]) -> dict[str, Any]:
    return {
        "asset_owner": {
            "identity": None,
            "open_item": "P0-C4-ASSET-OWNER",
            "verdict": "owner_decision_required",
        },
        "at_rest": {
            "authorized_location": "asset-retaining-machine",
            "d19_protection": "not_applied",
            "decision_date": "2026-07-20",
            "decision_source": "plan",
            "export": "prohibited",
            "git_exclusion": "not_at_rest_protection",
            "reacquisition_performed": False,
            "relocation_performed": False,
            "reprotection_performed": False,
            "state": "pre_d19_plaintext_local_only",
        },
        "availability": {
            "checks": AVAILABILITY_CHECKS,
            "result": "all_verified",
            "restore_inferred": False,
            "scope": "all_manifest_entries",
            "source": "corpus_manifest",
            "verdict": "verified",
        },
        "canonical_locator": LOCATOR,
        "open_items": copy.deepcopy(OPEN_ITEMS),
        "reacquisition": {
            "current_corpus_replaced": False,
            "result_scope": "new_corpus_version",
            "same_sha256_required": False,
            "verdict": "out_of_scope_not_performed",
        },
        "restore": {
            "open_item": "P0-C4-RESTORE-SOURCE",
            "source_identity": None,
            "source_status": "not_identified",
            "verification": None,
            "verdict": "not_performed",
        },
        "retention": {
            "open_item": "P0-C4-RETENTION",
            "policy": None,
            "verdict": "owner_decision_required",
        },
        "schema": CUSTODY_SCHEMA,
        "schema_version": 1,
        "source_references": {
            "corpus_contract": {
                "path": CONTRACT_PATH,
                "schema": CONTRACT_SCHEMA,
                "schema_version": 1,
                "sha256": _sha256(source_payloads[CONTRACT_PATH]),
            },
            "corpus_manifest": {
                "path": MANIFEST_PATH,
                "schema": MANIFEST_SCHEMA,
                "schema_version": 1,
                "sha256": _sha256(source_payloads[MANIFEST_PATH]),
            },
            "manifest_tool": {
                "path": TOOL_PATH,
                "sha256": _sha256(source_payloads[TOOL_PATH]),
            },
            "plan": {
                "path": PLAN_PATH,
                "revision": "Draft 4.7",
                "sha256": _sha256(source_payloads[PLAN_PATH]),
            },
            "recovery_procedure": {
                "path": PROCEDURE_PATH,
                "sha256": _sha256(source_payloads[PROCEDURE_PATH]),
            },
        },
    }


def _fixture(repo_root: Path) -> dict[str, Any]:
    source_payloads = {
        PLAN_PATH: b"<html>Draft 4.7 synthetic plan</html>\n",
        CONTRACT_PATH: _canonical_json(_contract()),
        MANIFEST_PATH: _canonical_json(_manifest()),
        TOOL_PATH: b"# synthetic manifest verifier\n",
        PROCEDURE_PATH: PROCEDURE_TEXT.encode("utf-8"),
    }
    for path, payload in source_payloads.items():
        _write(repo_root, path, payload)
    policy = _policy(source_payloads)
    _write(repo_root, DEFAULT_CUSTODY_PATH, serialize_custody(policy))
    return policy


class CustodyPolicyTests(unittest.TestCase):
    def assert_failure(
        self,
        expected_type: type[Exception],
        expected_code: str,
        callback: Any,
    ) -> None:
        with self.assertRaises(expected_type) as raised:
            callback()
        self.assertIs(expected_type, type(raised.exception))
        self.assertEqual(expected_code, getattr(raised.exception, "code", None))
        message = getattr(raised.exception, "message", None)
        self.assertIsInstance(message, str)
        self.assertTrue(message)
        self.assertEqual(f"{expected_code}: {message}", str(raised.exception))

    def assert_exact_failure(
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
        self.assertEqual(expected_message, getattr(raised.exception, "message", None))
        self.assertEqual(
            f"{expected_code}: {expected_message}",
            str(raised.exception),
        )

    def test_c4_n_01_current_state_validates_without_resolving_authority(self) -> None:
        # Given: The exact authorized current custody state and metadata sources.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            policy = _fixture(repo_root)

            # When: The static policy and its pinned metadata are verified.
            verified = verify_custody(repo_root)

            # Then: Missing owner, retention, and restore-source authority stays open.
            self.assertEqual(policy, verified)
            self.assertEqual(
                [item["id"] for item in OPEN_ITEMS],
                [item["id"] for item in verified["open_items"]],
            )
            self.assertFalse(verified["availability"]["restore_inferred"])

    def test_c4_n_02_canonical_serialization_is_order_independent(self) -> None:
        # Given: The same valid semantic policy with reversed top-level key order.
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy = _fixture(Path(temporary_directory))
            reordered = dict(reversed(list(policy.items())))

            # When: Both policies are serialized.
            first = serialize_custody(policy)
            second = serialize_custody(reordered)

            # Then: Canonical UTF-8 JSON is byte-identical with one terminal LF.
            self.assertEqual(first, second)
            self.assertTrue(first.endswith(b"\n"))
            self.assertFalse(first.endswith(b"\n\n"))
            self.assertEqual(policy, load_custody_data(json.loads(first)))

    def test_c4_n_03_procedure_and_cli_are_bounded_and_read_only(self) -> None:
        # Given: The complete procedure and a valid exact-path custody policy.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            stdout = io.StringIO()
            stderr = io.StringIO()

            # When: The verify-only CLI is invoked twice.
            with redirect_stdout(stdout), redirect_stderr(stderr):
                first = main(
                    [
                        "--repo-root",
                        str(repo_root),
                        "verify",
                        "--policy",
                        DEFAULT_CUSTODY_PATH,
                    ]
                )
                second = main(
                    [
                        "--repo-root",
                        str(repo_root),
                        "verify",
                        "--policy",
                        DEFAULT_CUSTODY_PATH,
                    ]
                )

            # Then: It succeeds deterministically without exposing a host locator.
            self.assertEqual(0, first)
            self.assertEqual(0, second)
            self.assertEqual("", stderr.getvalue())
            self.assertNotIn(str(repo_root.resolve()), stdout.getvalue())
            self.assertEqual(
                2,
                stdout.getvalue().count(
                    f"custody verified: {DEFAULT_CUSTODY_PATH}"
                ),
            )

    def test_c4_a_01_schema_null_empty_type_and_boundaries_fail(self) -> None:
        # Given: NULL/empty policies and schema/version type or boundary mutations.
        invalid_values: list[tuple[object, str]] = [
            (None, "custody.type"),
            ({}, "custody.fields"),
        ]
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy = _fixture(Path(temporary_directory))
            for schema in ("wrong", ""):
                candidate = copy.deepcopy(policy)
                candidate["schema"] = schema
                invalid_values.append((candidate, "custody.schema"))
            for version in (None, True, "1", 0, 2):
                candidate = copy.deepcopy(policy)
                candidate["schema_version"] = version
                invalid_values.append((candidate, "custody.schema_version"))

            # When/Then: Every unsupported state fails with a typed schema error.
            for value, code in invalid_values:
                with self.subTest(value=value, code=code):
                    self.assert_failure(
                        CustodyValidationError,
                        code,
                        lambda value=value: load_custody_data(value),
                    )

    def test_c4_a_02_closed_fields_duplicate_json_and_empty_values_fail(self) -> None:
        # Given: Missing, unknown, duplicate-key, and empty open-item mutations.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            policy = _fixture(repo_root)
            missing = copy.deepcopy(policy)
            missing.pop("restore")
            unknown = copy.deepcopy(policy)
            unknown["unexpected"] = False
            empty = copy.deepcopy(policy)
            empty["open_items"][0]["reason"] = ""

            # When/Then: Closed-schema and required-string checks reject each case.
            self.assert_failure(
                CustodyValidationError,
                "custody.fields",
                lambda: load_custody_data(missing),
            )
            self.assert_failure(
                CustodyValidationError,
                "custody.fields",
                lambda: load_custody_data(unknown),
            )
            self.assert_failure(
                CustodyValidationError,
                "custody.open_items",
                lambda: load_custody_data(empty),
            )
            duplicate = serialize_custody(policy).replace(
                b'  "schema":',
                b'  "schema": "duplicate",\n  "schema":',
                1,
            )
            _write(repo_root, DEFAULT_CUSTODY_PATH, duplicate)
            self.assert_failure(
                CustodyValidationError,
                "custody.json.duplicate_key",
                lambda: verify_custody(repo_root),
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            policy_path = repo_root / DEFAULT_CUSTODY_PATH
            policy_path.write_bytes(policy_path.read_bytes() + b"\n")
            self.assert_failure(
                CustodyValidationError,
                "custody.bytes",
                lambda: verify_custody(repo_root),
            )

    def test_c4_a_03_source_path_confinement_symlinks_and_hashes_fail(self) -> None:
        # Given: Alternate/unsafe paths, symlinked sources, and SHA drift.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            policy = _fixture(repo_root)
            for path in (
                "/absolute/plan.html",
                r"docs\\plans\\plan.html",
                "../plan.html",
                "docs/plans/alternate.html",
            ):
                candidate = copy.deepcopy(policy)
                candidate["source_references"]["plan"]["path"] = path

                # When/Then: Path validation fails before any alternate target read.
                with self.subTest(path=path):
                    self.assert_failure(
                        CustodySourceError,
                        "custody.source.path",
                        lambda candidate=candidate: load_custody_data(candidate),
                    )

            drift = copy.deepcopy(policy)
            drift["source_references"]["plan"]["sha256"] = "0" * 64
            _write(repo_root, DEFAULT_CUSTODY_PATH, serialize_custody(drift))
            self.assert_failure(
                CustodySourceError,
                "custody.source.sha256_mismatch",
                lambda: verify_custody(repo_root),
            )

            _write(repo_root, DEFAULT_CUSTODY_PATH, serialize_custody(policy))
            plan = repo_root / PLAN_PATH
            target = plan.with_name("real-plan.html")
            plan.rename(target)
            plan.symlink_to(target.name)
            self.assert_failure(
                CustodySourceError,
                "custody.source.symlink",
                lambda: verify_custody(repo_root),
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            plans = repo_root / "docs/plans"
            real_plans = repo_root / "docs/plans-real"
            plans.rename(real_plans)
            plans.symlink_to(real_plans.name, target_is_directory=True)
            self.assert_failure(
                CustodySourceError,
                "custody.source.symlink",
                lambda: verify_custody(repo_root),
            )

    def test_c4_a_04_source_schema_and_locator_authority_mismatch_fail(self) -> None:
        # Given: Source declaration/content schema drift and locator contradictions.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            policy = _fixture(repo_root)
            declared = copy.deepcopy(policy)
            declared["source_references"]["corpus_contract"]["schema_version"] = 2
            contract = _contract()
            contract["schema_version"] = 2
            _write(repo_root, CONTRACT_PATH, _canonical_json(contract))
            declared["source_references"]["corpus_contract"]["sha256"] = _sha256(
                _canonical_json(contract)
            )

            # When/Then: Future source schema declarations fail closed.
            self.assert_failure(
                CustodySourceError,
                "custody.source.schema",
                lambda: load_custody_data(declared),
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            policy = _fixture(repo_root)
            policy["canonical_locator"] = "artifacts/alternate"
            _write(repo_root, DEFAULT_CUSTODY_PATH, serialize_custody(policy))
            self.assert_failure(
                CustodySourceError,
                "custody.locator.mismatch",
                lambda: verify_custody(repo_root),
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            policy = _fixture(repo_root)
            contract_payload = _canonical_json(_contract()).replace(
                b'  "schema":',
                b'  "schema": "duplicate",\n  "schema":',
                1,
            )
            _write(repo_root, CONTRACT_PATH, contract_payload)
            policy["source_references"]["corpus_contract"]["sha256"] = _sha256(
                contract_payload
            )
            _write(repo_root, DEFAULT_CUSTODY_PATH, serialize_custody(policy))
            self.assert_failure(
                CustodySourceError,
                "custody.source.json",
                lambda: verify_custody(repo_root),
            )

    def test_c4_a_05_authority_duplication_and_payload_metadata_fail(self) -> None:
        # Given: Count, byte, payload, row, packet, and host metadata additions.
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy = _fixture(Path(temporary_directory))

            # When/Then: Every prohibited authority/payload key fails explicitly.
            for field, value in (
                ("expected_counts", {}),
                ("expected_count", 0),
                ("entry_count", 0),
                ("total_bytes", 0),
                ("payload", ""),
                ("raw_rows", []),
                ("packet_body", ""),
                ("host_locator", "/host/path"),
            ):
                candidate = copy.deepcopy(policy)
                candidate[field] = value
                with self.subTest(field=field):
                    self.assert_failure(
                        CustodyValidationError,
                        "custody.metadata.prohibited",
                        lambda candidate=candidate: load_custody_data(candidate),
                    )

    def test_c4_a_06_owner_claims_without_authority_fail(self) -> None:
        # Given: Resolved/named owner claims without an owner decision.
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy = _fixture(Path(temporary_directory))
            resolved = copy.deepcopy(policy)
            resolved["asset_owner"]["verdict"] = "resolved"
            named = copy.deepcopy(policy)
            named["asset_owner"]["identity"] = "repository-owner"

            # When/Then: Both unsupported ownership claims fail closed.
            for candidate in (resolved, named):
                self.assert_failure(
                    CustodyValidationError,
                    "custody.asset_owner",
                    lambda candidate=candidate: load_custody_data(candidate),
                )

    def test_c4_a_07_retention_claims_or_missing_open_item_fail(self) -> None:
        # Given: Inferred retention policy and a missing retention open-item link.
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy = _fixture(Path(temporary_directory))
            for field, value in (
                ("policy", "delete-after-30-days"),
                ("duration_days", 30),
                ("disposition", "delete"),
            ):
                candidate = copy.deepcopy(policy)
                candidate["retention"][field] = value

                # When/Then: No expiry, disposition, or policy may be invented.
                with self.subTest(field=field):
                    self.assert_failure(
                        CustodyValidationError,
                        "custody.retention",
                        lambda candidate=candidate: load_custody_data(candidate),
                    )
            missing_link = copy.deepcopy(policy)
            missing_link["retention"]["open_item"] = ""
            self.assert_failure(
                CustodyValidationError,
                "custody.retention",
                lambda: load_custody_data(missing_link),
            )

    def test_c4_a_08_availability_order_state_and_restore_inference_fail(self) -> None:
        # Given: Non-verified, missing/reordered checks, and restore inference states.
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy = _fixture(Path(temporary_directory))
            mutations = []
            for field, value in (
                ("verdict", "partial"),
                ("result", "some_verified"),
                ("restore_inferred", True),
            ):
                candidate = copy.deepcopy(policy)
                candidate["availability"][field] = value
                mutations.append(candidate)
            missing = copy.deepcopy(policy)
            missing["availability"]["checks"] = AVAILABILITY_CHECKS[:-1]
            mutations.append(missing)
            reordered = copy.deepcopy(policy)
            reordered["availability"]["checks"] = list(
                reversed(AVAILABILITY_CHECKS)
            )
            mutations.append(reordered)

            # When/Then: Availability remains exact and cannot imply restore.
            for candidate in mutations:
                self.assert_failure(
                    CustodyValidationError,
                    "custody.availability",
                    lambda candidate=candidate: load_custody_data(candidate),
                )

    def test_c4_a_09_restore_claims_and_availability_reuse_fail(self) -> None:
        # Given: Verified/failed restore, identified source, or non-NULL verification.
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy = _fixture(Path(temporary_directory))
            mutations = []
            for field, value in (
                ("verdict", "verified"),
                ("verdict", "failed"),
                ("source_status", "identified"),
                ("source_identity", "corpus_manifest"),
                ("verification", "availability"),
            ):
                candidate = copy.deepcopy(policy)
                candidate["restore"][field] = value
                mutations.append(candidate)

            # When/Then: Current restore remains strictly not performed.
            for candidate in mutations:
                self.assert_failure(
                    CustodyValidationError,
                    "custody.restore",
                    lambda candidate=candidate: load_custody_data(candidate),
                )

    def test_c4_a_10_reacquisition_contradictions_fail(self) -> None:
        # Given: In-scope, replacing, or same-SHA reacquisition mutations.
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy = _fixture(Path(temporary_directory))
            mutations = []
            for field, value in (
                ("verdict", "in_scope"),
                ("result_scope", "current_corpus"),
                ("current_corpus_replaced", True),
                ("same_sha256_required", True),
            ):
                candidate = copy.deepcopy(policy)
                candidate["reacquisition"][field] = value
                mutations.append(candidate)

            # When/Then: Reacquisition stays a separate, future corpus version.
            for candidate in mutations:
                self.assert_failure(
                    CustodyValidationError,
                    "custody.reacquisition",
                    lambda candidate=candidate: load_custody_data(candidate),
                )

    def test_c4_a_11_at_rest_boundary_contradictions_fail(self) -> None:
        # Given: Applied D19, protective Git, export, or material-action states.
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy = _fixture(Path(temporary_directory))
            mutations = []
            for field, value in (
                ("state", "d19_protected"),
                ("d19_protection", "applied"),
                ("git_exclusion", "at_rest_protection"),
                ("export", "allowed"),
                ("authorized_location", "any-machine"),
                ("relocation_performed", True),
                ("reprotection_performed", True),
                ("reacquisition_performed", True),
                ("decision_date", "2026-07-21"),
                ("decision_source", "repository"),
            ):
                candidate = copy.deepcopy(policy)
                candidate["at_rest"][field] = value
                mutations.append(candidate)

            # When/Then: The 2026-07-20 local-only boundary cannot be expanded.
            for candidate in mutations:
                self.assert_failure(
                    CustodyValidationError,
                    "custody.at_rest",
                    lambda candidate=candidate: load_custody_data(candidate),
                )

    def test_c4_a_12_open_item_set_order_state_and_links_fail(self) -> None:
        # Given: Missing, extra, duplicate, unsorted, resolved, and unlinked items.
        with tempfile.TemporaryDirectory() as temporary_directory:
            policy = _fixture(Path(temporary_directory))
            mutations = []
            missing = copy.deepcopy(policy)
            missing["open_items"].pop()
            mutations.append(missing)
            extra = copy.deepcopy(policy)
            extra["open_items"].append(
                {"id": "P0-C4-EXTRA", "reason": "unknown", "status": "open"}
            )
            mutations.append(extra)
            duplicate = copy.deepcopy(policy)
            duplicate["open_items"][1] = copy.deepcopy(
                duplicate["open_items"][0]
            )
            mutations.append(duplicate)
            unsorted = copy.deepcopy(policy)
            unsorted["open_items"] = list(reversed(unsorted["open_items"]))
            mutations.append(unsorted)
            resolved = copy.deepcopy(policy)
            resolved["open_items"][0]["status"] = "resolved"
            mutations.append(resolved)
            unlinked = copy.deepcopy(policy)
            unlinked["asset_owner"]["open_item"] = "P0-C4-RETENTION"
            mutations.append(unlinked)

            # When/Then: The exact three sorted open items and links are required.
            for candidate in mutations:
                self.assert_failure(
                    CustodyValidationError,
                    "custody.open_items",
                    lambda candidate=candidate: load_custody_data(candidate),
                )

    def test_pr9_cl_a_01_procedure_semantic_lint_reports_missing_authority(self) -> None:
        # Given: Approved procedure text missing one required heading or policy token.
        cases = (
            (
                PROCEDURE_TEXT.replace("## Stop conditions", "## Other conditions"),
                "recovery procedure is missing a required section",
            ),
            (
                PROCEDURE_TEXT.replace(
                    "availability is not restore verification",
                    "availability cannot establish a restored copy",
                ),
                "recovery procedure is missing a required policy statement",
            ),
        )

        # When: The independently callable semantic lint checks each mutation.
        # Then: Each authority omission has its exact stable typed failure.
        for text, message in cases:
            with self.subTest(message=message):
                self.assert_exact_failure(
                    CustodyProcedureError,
                    "custody.procedure",
                    message,
                    lambda text=text: corpus_custody._lint_procedure_semantics(
                        text
                    ),
                )

    def test_pr9_cl_a_02_procedure_semantic_lint_rejects_command_and_secret(self) -> None:
        # Given: Approved text with an altered fenced command or secret-like content.
        cases = (
            (
                PROCEDURE_TEXT.replace(
                    ALLOWED_COMMAND,
                    f"{ALLOWED_COMMAND} --unexpected",
                ),
                (
                    "recovery procedure may contain only the canonical "
                    "read-only verifier command"
                ),
            ),
            (
                f"{PROCEDURE_TEXT}\ntoken=synthetic-placeholder\n",
                "recovery procedure must not contain secret-like content",
            ),
        )

        # When: The independently callable semantic lint checks each mutation.
        # Then: Command allowlisting and secret detection report exact failures.
        for text, message in cases:
            with self.subTest(message=message):
                self.assert_exact_failure(
                    CustodyProcedureError,
                    "custody.procedure",
                    message,
                    lambda text=text: corpus_custody._lint_procedure_semantics(
                        text
                    ),
                )

    def test_pr9_gh_a02_policy_decoder_bounds_recursion_and_integer_limits(self) -> None:
        # Given: Pathological JSON at the exact custody policy path.
        cases = (
            (
                "deep",
                ("[" * 2_000 + "0" + "]" * 2_000).encode("utf-8"),
            ),
            (
                "huge-integer",
                ('{"schema":' + "9" * 5_000 + "}").encode("utf-8"),
            ),
        )
        for name, payload in cases:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary_directory:
                repo_root = Path(temporary_directory)
                policy_path = repo_root / DEFAULT_CUSTODY_PATH
                policy_path.parent.mkdir(parents=True)
                policy_path.write_bytes(payload)

                # When/Then: Decoder limits stay behind one bounded typed error.
                self.assert_exact_failure(
                    CustodyValidationError,
                    "custody.json.invalid",
                    "custody policy must contain strict UTF-8 JSON",
                    lambda repo_root=repo_root: verify_custody(repo_root),
                )

    def test_c4_a_13_procedure_identity_sections_tokens_and_commands_fail(self) -> None:
        # Given: Missing/hash-drifted/incomplete or operational procedure variants.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            policy = _fixture(repo_root)
            procedure = repo_root / PROCEDURE_PATH
            procedure.unlink()

            # When/Then: Missing procedure fails as a typed source error.
            self.assert_failure(
                CustodySourceError,
                "custody.source.unreadable",
                lambda: verify_custody(repo_root),
            )

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            _write(
                repo_root,
                PROCEDURE_PATH,
                (PROCEDURE_TEXT + "\nidentity drift\n").encode("utf-8"),
            )
            self.assert_failure(
                CustodySourceError,
                "custody.source.sha256_mismatch",
                lambda: verify_custody(repo_root),
            )

        for replacement in (
            ("## Stop conditions", "## Missing stop section"),
            ("availability is not restore verification", ""),
            (ALLOWED_COMMAND, "cp source destination"),
            (ALLOWED_COMMAND, "curl https://example.invalid/upload"),
            (ALLOWED_COMMAND, "python reacquire.py --run"),
        ):
            with tempfile.TemporaryDirectory() as temporary_directory:
                repo_root = Path(temporary_directory)
                policy = _fixture(repo_root)
                self.assertIn(replacement[0], PROCEDURE_TEXT)
                procedure_payload = PROCEDURE_TEXT.replace(*replacement).encode(
                    "utf-8"
                )
                _write(repo_root, PROCEDURE_PATH, procedure_payload)
                policy["source_references"]["recovery_procedure"][
                    "sha256"
                ] = _sha256(procedure_payload)
                _write(
                    repo_root,
                    DEFAULT_CUSTODY_PATH,
                    serialize_custody(policy),
                )

                # When/Then: The approved whole-document identity rejects the
                # mutation even though the outer source reference was repinned.
                self.assert_failure(
                    CustodyProcedureError,
                    "custody.procedure",
                    lambda: verify_custody(repo_root),
                )

        for appended_text in (
            "Run `cp /home/source/corpus /home/destination/corpus`.",
            "Run cp source destination.",
            "Use the candidate at file:///home/source/corpus.",
            "rsync source destination",
            "Run sh -c 'cat source > destination'.",
            "Run Get-Content source | Set-Content destination.",
            "Run install source destination.",
        ):
            with self.subTest(appended_text=appended_text):
                with tempfile.TemporaryDirectory() as temporary_directory:
                    repo_root = Path(temporary_directory)
                    policy = _fixture(repo_root)
                    procedure_payload = (
                        f"{PROCEDURE_TEXT}\n{appended_text}\n".encode("utf-8")
                    )
                    _write(repo_root, PROCEDURE_PATH, procedure_payload)
                    policy["source_references"]["recovery_procedure"][
                        "sha256"
                    ] = _sha256(procedure_payload)
                    _write(
                        repo_root,
                        DEFAULT_CUSTODY_PATH,
                        serialize_custody(policy),
                    )

                    # When/Then: The approved whole-document identity rejects
                    # appended operations after the outer source SHA is repinned.
                    self.assert_failure(
                        CustodyProcedureError,
                        "custody.procedure",
                        lambda: verify_custody(repo_root),
                    )

    def test_c4_a_14_exact_policy_path_and_bounded_error_output(self) -> None:
        # Given: A valid policy at the canonical path and an alternate path request.
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            _fixture(repo_root)
            stderr = io.StringIO()

            # When: The CLI is directed to an alternate or absolute policy path.
            with redirect_stderr(stderr):
                alternate = main(
                    [
                        "--repo-root",
                        str(repo_root),
                        "verify",
                        "--policy",
                        "docs/reference/alternate-custody.json",
                    ]
                )
                absolute = main(
                    [
                        "--repo-root",
                        str(repo_root),
                        "verify",
                        "--policy",
                        str((repo_root / DEFAULT_CUSTODY_PATH).resolve()),
                    ]
                )

            # Then: Both fail without printing the host locator or secret-like data.
            self.assertEqual(2, alternate)
            self.assertEqual(2, absolute)
            output = stderr.getvalue()
            self.assertNotIn(str(repo_root.resolve()), output)
            self.assertNotIn("token=", output.lower())
            self.assertIn("ERROR custody.policy.path:", output)

        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory)
            policy = _fixture(repo_root)
            secret_key = "token=" + ("s" * 4096)
            duplicate = serialize_custody(policy).replace(
                b"{\n",
                (
                    "{\n"
                    f'  "{secret_key}": null,\n'
                    f'  "{secret_key}": null,\n'
                ).encode("utf-8"),
                1,
            )
            _write(repo_root, DEFAULT_CUSTODY_PATH, duplicate)
            stderr = io.StringIO()

            # When: The CLI parses duplicate untrusted keys with secret-like text.
            with redirect_stderr(stderr):
                result = main(
                    [
                        "--repo-root",
                        str(repo_root),
                        "verify",
                        "--policy",
                        DEFAULT_CUSTODY_PATH,
                    ]
                )

            # Then: The existing error code is retained, while stderr is bounded
            # and never reflects the untrusted key or its secret-like prefix.
            output = stderr.getvalue()
            self.assertEqual(2, result)
            self.assertIn("ERROR custody.json.duplicate_key:", output)
            self.assertNotIn(secret_key, output)
            self.assertNotIn("token=", output.lower())
            self.assertLessEqual(len(output), 256)


if __name__ == "__main__":
    unittest.main()
