from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Callable
from unittest import mock

SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

import golden_selection as golden_selection_module  # noqa: E402
from golden_selection import (  # noqa: E402
    SelectionDecisionRequiredError,
    SelectionDensityError,
    SelectionOutputError,
    SelectionPairError,
    SelectionSchemaError,
    SelectionSourceError,
    decode_json_document,
    read_fixed_metadata_source,
    require_pinned_source_identities,
    select_golden_inputs,
    serialize_golden_selection,
    write_golden_selection,
)


DENSITY_RUNS = (
    ("low", "260717_1529"),
    ("low", "260717_1539"),
    ("mid", "260717_1532"),
    ("mid", "260717_1535"),
    ("high", "260717_1542"),
    ("high", "260717_1545"),
)
SELECTED_RUNS = (
    ("low", "260717_1529"),
    ("mid", "260717_1532"),
    ("high", "260717_1542"),
)
MANIFEST_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/manifest.json"
)
CONTRACT_PATH = (
    "docs/reference/initial-recording-corpus/2026-07-17/contract.json"
)
OUTPUT_PATH = (
    "docs/reference/d17-golden-regression/golden-inputs-v1.json"
)
EXPECTED_MANIFEST_SHA256 = (
    "f51fc2759598dba71bfc51ede5d9128e63ab90a5dd70dd31e3c02bb015e56002"
)
EXPECTED_MANIFEST_SIZE = 656_570
EXPECTED_CONTRACT_SHA256 = (
    "825503ad2cb84a6262a2b2a6c88e661973a0fba4a37f77be6d04dc451b7c51e6"
)
EXPECTED_CONTRACT_SIZE = 1_489


def manifest_entry(kind: str, path: str, size_bytes: int = 1) -> dict[str, object]:
    return {
        "kind": kind,
        "path": path,
        "sha256": hashlib.sha256(path.encode("ascii")).hexdigest(),
        "size_bytes": size_bytes,
    }


def valid_manifest() -> dict[str, object]:
    entries: list[dict[str, object]] = []
    for _, run_id in DENSITY_RUNS:
        entries.extend(
            (
                manifest_entry("bin", f"{run_id}_fl_1.bin"),
                manifest_entry("bin", f"{run_id}_fh_1.bin"),
                manifest_entry("cfg", f"{run_id}_cfg.txt"),
                manifest_entry("capture", f"{run_id}.pcapng"),
            )
        )
    for _, run_id in SELECTED_RUNS:
        entries.extend(
            (
                manifest_entry("bin", f"{run_id}_fl_2.bin"),
                manifest_entry("bin", f"{run_id}_fh_2.bin"),
            )
        )
    entries.append(manifest_entry("telemetry", "telemetry.csv"))
    entries.sort(key=lambda entry: str(entry["path"]))
    expected_counts = {
        kind: sum(entry["kind"] == kind for entry in entries)
        for kind in ("bin", "cfg", "telemetry", "capture")
    }
    total_bytes = sum(int(entry["size_bytes"]) for entry in entries)
    return {
        "schema": "analogboard.phase0.initial-recording-corpus-manifest",
        "schema_version": 1,
        "source_locator": "artifacts/field-session/2026-07-17-characterization",
        "excluded_paths": ["README.md"],
        "expected_counts": expected_counts,
        "expected_total_bytes": total_bytes,
        "actual_total_bytes": total_bytes,
        "entries": entries,
    }


def valid_contract() -> dict[str, object]:
    manifest = valid_manifest()
    return {
        "schema": "analogboard.phase0.initial-recording-corpus-contract",
        "schema_version": 1,
        "canonical_locator": (
            "artifacts/field-session/2026-07-17-characterization"
        ),
        "asset_kinds": [
            {
                "kind": "bin",
                "expected_count": manifest["expected_counts"]["bin"],
                "filename_pattern": (
                    r"(?P<run_id>[0-9]{6}_[0-9]{4})_"
                    r"(?:fl|fh)_[1-9][0-9]*[.]bin"
                ),
            },
            {
                "kind": "cfg",
                "expected_count": manifest["expected_counts"]["cfg"],
                "filename_pattern": (
                    r"(?P<run_id>[0-9]{6}_[0-9]{4})_cfg[.]txt"
                ),
            },
            {
                "kind": "telemetry",
                "expected_count": manifest["expected_counts"]["telemetry"],
                "filename_pattern": r"telemetry[.]csv",
            },
            {
                "kind": "capture",
                "expected_count": manifest["expected_counts"]["capture"],
                "filename_pattern": r"[0-9]{6}_[0-9]{4}[.]pcapng",
            },
        ],
        "expected_total_bytes": manifest["expected_total_bytes"],
        "excluded_paths": list(manifest["excluded_paths"]),
        "run_capture_mapping": [
            {
                "density": density,
                "run_id": run_id,
                "capture": f"{run_id}.pcapng",
            }
            for density, run_id in DENSITY_RUNS
        ],
        "idle_captures": [],
    }


def synchronize_contract(
    manifest: dict[str, object],
    contract: dict[str, object],
) -> None:
    entries = manifest["entries"]
    counts = {
        kind: sum(entry["kind"] == kind for entry in entries)
        for kind in ("bin", "cfg", "telemetry", "capture")
    }
    total_bytes = sum(int(entry["size_bytes"]) for entry in entries)
    manifest["expected_counts"] = counts
    manifest["expected_total_bytes"] = total_bytes
    manifest["actual_total_bytes"] = total_bytes
    for asset_kind in contract["asset_kinds"]:
        asset_kind["expected_count"] = counts[asset_kind["kind"]]
    contract["expected_total_bytes"] = total_bytes


class GoldenSelectionTests(unittest.TestCase):
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
        self.assertEqual(f"{expected_code}: {expected_message}", str(raised.exception))

    def assert_schema_failure(
        self,
        validator_code: str,
        callback: Callable[[], object],
    ) -> None:
        self.assert_failure(
            SelectionSchemaError,
            "selection.schema.invalid",
            f"P0-C4 metadata validation failed ({validator_code})",
            callback,
        )

    def test_s_n_01_selects_three_density_representatives(self) -> None:
        # Given: Two mapped runs per density and two complete pairs on first runs.
        manifest = valid_manifest()
        contract = valid_contract()

        # When: The bounded selection rule is applied.
        selection = select_golden_inputs(manifest, contract)

        # Then: The first run and lowest complete pair are selected per density.
        self.assertEqual(3, selection["pair_count"])
        self.assertEqual(6, selection["entry_count"])
        self.assertEqual(
            list(SELECTED_RUNS),
            [
                (pair["density"], pair["run_id"])
                for pair in selection["pairs"]
            ],
        )
        self.assertEqual([1, 1, 1], [pair["ordinal"] for pair in selection["pairs"]])
        self.assertEqual(
            [["FL", "FH"], ["FL", "FH"], ["FL", "FH"]],
            [
                [entry["stream"] for entry in pair["entries"]]
                for pair in selection["pairs"]
            ],
        )

    def test_s_n_02_serialization_is_byte_deterministic(self) -> None:
        # Given: One mechanically selected metadata-only input set.
        manifest_bytes = json.dumps(valid_manifest(), sort_keys=True).encode()
        contract_bytes = json.dumps(valid_contract(), sort_keys=True).encode()
        selection = select_golden_inputs(
            decode_json_document(manifest_bytes, "manifest"),
            decode_json_document(contract_bytes, "contract"),
        )

        # When: The fixture is serialized twice from identical tracked sources.
        first = serialize_golden_selection(
            selection,
            manifest_bytes=manifest_bytes,
            contract_bytes=contract_bytes,
        )
        second = serialize_golden_selection(
            selection,
            manifest_bytes=manifest_bytes,
            contract_bytes=contract_bytes,
        )

        # Then: Canonical bytes and source identity pins are exact.
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        value = json.loads(first)
        self.assertEqual("analogboard.d17.golden-input-selection", value["schema"])
        self.assertEqual(1, value["schema_version"])
        self.assertEqual(
            "artifacts/field-session/2026-07-17-characterization",
            value["asset_locator"],
        )
        self.assertEqual(MANIFEST_PATH, value["sources"]["manifest"]["path"])
        self.assertEqual(CONTRACT_PATH, value["sources"]["corpus_contract"]["path"])

    def test_s_b_01_exact_three_pairs_and_six_entries_are_accepted(self) -> None:
        # Given: Exactly one complete FL/FH pair for each selected run.
        manifest = valid_manifest()
        selected = {run_id for _, run_id in SELECTED_RUNS}
        manifest["entries"] = [
            entry
            for entry in manifest["entries"]
            if not (
                any(entry["path"].startswith(run_id) for run_id in selected)
                and entry["path"].endswith("_2.bin")
            )
        ]
        contract = valid_contract()
        synchronize_contract(manifest, contract)

        # When: Selection runs at its frozen bound.
        selection = select_golden_inputs(manifest, contract)

        # Then: The exact minimum remains one complete pair per density.
        self.assertEqual(3, selection["pair_count"])
        self.assertEqual(6, selection["entry_count"])

    def test_s_a_01_rejects_invalid_or_ambiguous_json(self) -> None:
        # Given: NULL/type, malformed, duplicate-key, and non-object documents.
        cases = (
            (
                None,
                "selection.source.type",
                "manifest source must be bytes",
            ),
            (
                b"{",
                "selection.source.json",
                "manifest source must be valid JSON",
            ),
            (
                b'{"schema":1,"schema":2}',
                "selection.source.duplicate_key",
                "manifest source must not contain duplicate JSON keys",
            ),
            (
                b"null",
                "selection.source.object",
                "manifest source must be a JSON object",
            ),
        )
        for source, code, message in cases:
            with self.subTest(code=code):
                # When/Then: Parsing fails with a stable typed error.
                self.assert_failure(
                    SelectionSourceError,
                    code,
                    message,
                    lambda source=source: decode_json_document(source, "manifest"),
                )

    def test_s_a_02_rejects_schema_or_version_drift(self) -> None:
        # Given: Each source with a wrong schema or version.
        cases = []
        for source_name, value in (
            ("manifest", valid_manifest()),
            ("contract", valid_contract()),
        ):
            wrong_schema = dict(value)
            wrong_schema["schema"] = "wrong"
            cases.append((source_name, wrong_schema, "schema"))
            wrong_version = dict(value)
            wrong_version["schema_version"] = 2
            cases.append((source_name, wrong_version, "schema_version"))

        expected_codes = {
            ("manifest", "schema"): "manifest.schema.unsupported",
            ("manifest", "schema_version"): "manifest.schema_version.unsupported",
            ("contract", "schema"): "contract.schema.unsupported",
            ("contract", "schema_version"): "contract.schema_version.unsupported",
        }
        for source_name, value, field in cases:
            with self.subTest(source=source_name, field=field):
                manifest = value if source_name == "manifest" else valid_manifest()
                contract = value if source_name == "contract" else valid_contract()
                # When/Then: Frozen P0-C4 schema identity is required.
                self.assert_schema_failure(
                    expected_codes[(source_name, field)],
                    lambda manifest=manifest, contract=contract: select_golden_inputs(
                        manifest, contract
                    ),
                )

    def test_s_a_20_rejects_invalid_validator_exception_surface(self) -> None:
        # Given: Validator modules with a missing or invalid exception class.
        cases = (
            ("missing", ""),
            ("non_class", "CorpusIndexError = object()\n"),
            ("non_exception_class", "class CorpusIndexError:\n    pass\n"),
        )
        validator_body = (
            "def load_contract_data(value):\n"
            "    return value\n"
            "\n"
            "def validate_manifest_metadata(contract, manifest):\n"
            "    return {}\n"
        )

        for case_name, declaration in cases:
            with self.subTest(case=case_name):
                source = (declaration + validator_body).encode("utf-8")

                # When/Then: Loading fails through the stable schema error path.
                with (
                    mock.patch.object(
                        golden_selection_module,
                        "_CORPUS_INDEX_MODULE",
                        None,
                    ),
                    mock.patch.object(
                        golden_selection_module,
                        "_read_regular_beneath_root",
                        return_value=source,
                    ),
                ):
                    self.assert_failure(
                        SelectionSchemaError,
                        "selection.schema.validator",
                        "unable to load the canonical P0-C4 metadata validator",
                        golden_selection_module._load_corpus_index_module,
                    )

    def test_s_a_03_requires_exactly_low_mid_high(self) -> None:
        # Given: A contract missing one required density.
        contract = valid_contract()
        contract["run_capture_mapping"] = [
            row
            for row in contract["run_capture_mapping"]
            if row["density"] != "high"
        ]
        manifest = valid_manifest()
        removed_runs = {
            run_id for density, run_id in DENSITY_RUNS if density == "high"
        }
        manifest["entries"] = [
            entry
            for entry in manifest["entries"]
            if not any(
                str(entry["path"]).startswith(run_id)
                for run_id in removed_runs
            )
        ]
        synchronize_contract(manifest, contract)

        # When/Then: Missing density fails rather than shrinking the selection.
        self.assert_failure(
            SelectionDensityError,
            "selection.density.mismatch",
            "density mapping must contain exactly low, mid, high; missing=['high']; extra=[]",
            lambda: select_golden_inputs(manifest, contract),
        )

    def test_s_a_04_rejects_duplicate_run_or_manifest_path(self) -> None:
        # Given: Duplicate run mapping and duplicate manifest path variants.
        contract = valid_contract()
        contract["run_capture_mapping"].append(
            dict(contract["run_capture_mapping"][0])
        )
        manifest = valid_manifest()
        manifest["entries"].append(dict(manifest["entries"][0]))
        cases = (
            (
                valid_manifest(),
                contract,
                "contract.run_capture_mapping.duplicate",
            ),
            (
                manifest,
                valid_contract(),
                "manifest.path.duplicate",
            ),
        )
        for manifest_value, contract_value, validator_code in cases:
            with self.subTest(code=validator_code):
                # When/Then: Ambiguous authority is a typed failure.
                self.assert_schema_failure(
                    validator_code,
                    lambda manifest_value=manifest_value, contract_value=contract_value:
                    select_golden_inputs(manifest_value, contract_value),
                )

    def test_s_a_05_rejects_selected_run_without_complete_pair(self) -> None:
        # Given: The first low run has FL entries but no FH entry.
        manifest = valid_manifest()
        manifest["entries"] = [
            entry
            for entry in manifest["entries"]
            if not (
                entry["path"].startswith("260717_1529_fh_")
            )
        ]
        contract = valid_contract()
        synchronize_contract(manifest, contract)

        # When/Then: The selector does not skip to another run or infer a pair.
        self.assert_failure(
            SelectionPairError,
            "selection.pair.missing",
            "selected run has no complete FL/FH pair for density low",
            lambda: select_golden_inputs(manifest, contract),
        )

    def test_s_a_06_rejects_invalid_path_or_kind_name_disagreement(self) -> None:
        # Given: An absolute path and an FH filename mislabeled as another kind.
        cases = (
            (
                "/260717_1529_fl_1.bin",
                "bin",
                "manifest.path.invalid",
            ),
            (
                "260717_1529_fh_1.bin",
                "cfg",
                "manifest.kind.mismatch",
            ),
        )
        for path, kind, validator_code in cases:
            manifest = valid_manifest()
            manifest["entries"][0] = {
                **manifest["entries"][0],
                "path": path,
                "kind": kind,
            }
            with self.subTest(code=validator_code):
                # When/Then: Locator or kind drift is rejected before selection.
                self.assert_schema_failure(
                    validator_code,
                    lambda manifest=manifest: select_golden_inputs(
                        manifest, valid_contract()
                    ),
                )

    def test_s_a_07_rejects_malformed_sha_or_size(self) -> None:
        # Given: A non-SHA digest and NULL/non-positive byte sizes.
        cases = (
            (
                {"sha256": "not-a-sha"},
                "manifest.sha256.invalid",
            ),
            (
                {"size_bytes": None},
                "manifest.size.invalid",
            ),
            (
                {"size_bytes": -1},
                "manifest.size.invalid",
            ),
        )
        for mutation, validator_code in cases:
            manifest = valid_manifest()
            manifest["entries"][0] = {**manifest["entries"][0], **mutation}
            with self.subTest(mutation=mutation):
                # When/Then: Identity drift fails closed.
                self.assert_schema_failure(
                    validator_code,
                    lambda manifest=manifest: select_golden_inputs(
                        manifest, valid_contract()
                    ),
                )

    def test_s_a_08_rejects_extra_density_as_decision_required(self) -> None:
        # Given: An additional density not authorized by the frozen rule.
        contract = valid_contract()
        contract["run_capture_mapping"].append(
            {
                "density": "ultra",
                "run_id": "260717_1600",
                "capture": "260717_1600.pcapng",
            }
        )
        manifest = valid_manifest()
        manifest["entries"].extend(
            (
                manifest_entry("cfg", "260717_1600_cfg.txt"),
                manifest_entry("capture", "260717_1600.pcapng"),
            )
        )
        manifest["entries"].sort(key=lambda entry: str(entry["path"]))
        synchronize_contract(manifest, contract)

        # When/Then: The scope cannot silently expand beyond three pairs.
        self.assert_failure(
            SelectionDecisionRequiredError,
            "selection.density.decision_required",
            "density mapping contains an unauthorized density; extra=['ultra']",
            lambda: select_golden_inputs(manifest, contract),
        )

    def test_s_a_10_rejects_missing_required_metadata_fields(self) -> None:
        # Given: Canonical contract and manifest documents with required fields absent.
        contract = valid_contract()
        del contract["asset_kinds"]
        manifest = valid_manifest()
        del manifest["expected_counts"]
        cases = (
            (
                valid_manifest(),
                contract,
                "contract.asset_kinds.type",
            ),
            (
                manifest,
                valid_contract(),
                "manifest.expected_counts.invalid",
            ),
        )

        for manifest_value, contract_value, validator_code in cases:
            with self.subTest(code=validator_code):
                # When/Then: The canonical P0-C4 validator fails closed.
                self.assert_schema_failure(
                    validator_code,
                    lambda manifest_value=manifest_value, contract_value=contract_value:
                    select_golden_inputs(manifest_value, contract_value),
                )

    def test_s_a_11_rejects_unknown_top_entry_and_run_fields(self) -> None:
        # Given: Unknown fields at every schema layer consumed by the selector.
        contract_top = valid_contract()
        contract_top["unexpected"] = True
        manifest_top = valid_manifest()
        manifest_top["unexpected"] = True
        manifest_entry_value = valid_manifest()
        manifest_entry_value["entries"][0]["unexpected"] = True
        contract_run = valid_contract()
        contract_run["run_capture_mapping"][0]["unexpected"] = True
        cases = (
            (
                valid_manifest(),
                contract_top,
                "contract.fields.unknown",
            ),
            (
                manifest_top,
                valid_contract(),
                "manifest.fields.unknown",
            ),
            (
                manifest_entry_value,
                valid_contract(),
                "manifest.entry.fields.unknown",
            ),
            (
                valid_manifest(),
                contract_run,
                "contract.run_capture_mapping.fields.unknown",
            ),
        )

        for manifest_value, contract_value, validator_code in cases:
            with self.subTest(code=validator_code):
                # When/Then: Additive drift cannot bypass the canonical schema.
                self.assert_schema_failure(
                    validator_code,
                    lambda manifest_value=manifest_value, contract_value=contract_value:
                    select_golden_inputs(manifest_value, contract_value),
                )

    def test_s_a_12_rejects_unknown_kind_and_count_capture_drift(self) -> None:
        # Given: Unknown kind, cfg/run count drift, and capture-set count drift.
        unknown_kind = valid_contract()
        unknown_kind["asset_kinds"][2]["kind"] = "unknown"
        run_count = valid_contract()
        run_count["asset_kinds"][1]["expected_count"] = 5
        capture_count = valid_contract()
        capture_count["asset_kinds"][3]["expected_count"] = 7
        cases = (
            (unknown_kind, "contract.asset_kind.unknown"),
            (run_count, "contract.run_capture_mapping.count"),
            (capture_count, "contract.capture_set.count"),
        )

        for contract_value, validator_code in cases:
            with self.subTest(code=validator_code):
                # When/Then: Canonical linkage/count validation fails typed.
                self.assert_schema_failure(
                    validator_code,
                    lambda contract_value=contract_value: select_golden_inputs(
                        valid_manifest(), contract_value
                    ),
                )

    def test_s_a_13_rejects_boolean_schema_versions(self) -> None:
        # Given: bool values, which compare equal to integer one in Python.
        manifest = valid_manifest()
        manifest["schema_version"] = True
        contract = valid_contract()
        contract["schema_version"] = True
        cases = (
            (
                manifest,
                valid_contract(),
                "manifest.schema_version.unsupported",
            ),
            (
                valid_manifest(),
                contract,
                "contract.schema_version.type",
            ),
        )

        for manifest_value, contract_value, validator_code in cases:
            with self.subTest(code=validator_code):
                # When/Then: bool is rejected by the canonical integer validator.
                self.assert_schema_failure(
                    validator_code,
                    lambda manifest_value=manifest_value, contract_value=contract_value:
                    select_golden_inputs(manifest_value, contract_value),
                )

    def test_s_a_14_pins_exact_current_metadata_source_identities(self) -> None:
        # Given: The two fixed, tracked P0-C4 metadata files in this checkout.
        repository_root = SCRIPT_ROOT.parents[1]
        manifest_bytes = read_fixed_metadata_source(
            repository_root,
            Path(MANIFEST_PATH),
        )
        contract_bytes = read_fixed_metadata_source(
            repository_root,
            Path(CONTRACT_PATH),
        )

        # When: Their immutable identities are checked.
        require_pinned_source_identities(manifest_bytes, contract_bytes)

        # Then: The exact checked-in pins match and any byte drift fails first.
        self.assertEqual(EXPECTED_MANIFEST_SIZE, len(manifest_bytes))
        self.assertEqual(
            EXPECTED_MANIFEST_SHA256,
            hashlib.sha256(manifest_bytes).hexdigest(),
        )
        self.assertEqual(EXPECTED_CONTRACT_SIZE, len(contract_bytes))
        self.assertEqual(
            EXPECTED_CONTRACT_SHA256,
            hashlib.sha256(contract_bytes).hexdigest(),
        )
        self.assert_failure(
            SelectionSourceError,
            "selection.source.identity_mismatch",
            "fixed tracked P0-C4 metadata source identity does not match its pin",
            lambda: require_pinned_source_identities(
                manifest_bytes + b"\n",
                contract_bytes,
            ),
        )

    def test_s_a_15_rejects_unsafe_fixed_metadata_sources(self) -> None:
        # Given: Symlink ancestor/leaf, non-regular leaf, and cross-root source cases.
        cases = ("ancestor_symlink", "source_symlink", "nonregular", "cross_root")
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                outside = root / "outside"
                outside.mkdir()
                if case == "ancestor_symlink":
                    (root / "docs").symlink_to(outside, target_is_directory=True)
                    (outside / "reference/initial-recording-corpus/2026-07-17").mkdir(
                        parents=True
                    )
                    (
                        outside
                        / "reference/initial-recording-corpus/2026-07-17/manifest.json"
                    ).write_bytes(b"{}")
                    source = Path(MANIFEST_PATH)
                else:
                    parent = (
                        root
                        / "docs/reference/initial-recording-corpus/2026-07-17"
                    )
                    parent.mkdir(parents=True)
                    source = Path(MANIFEST_PATH)
                    target = root / source
                    if case == "source_symlink":
                        real_source = outside / "manifest.json"
                        real_source.write_bytes(b"{}")
                        target.symlink_to(real_source)
                    elif case == "nonregular":
                        target.mkdir()
                    else:
                        (root / "outside.json").write_bytes(b"{}")
                        source = Path("../outside.json")

                # When/Then: No source is read through ambiguity or outside scope.
                self.assert_failure(
                    SelectionSourceError,
                    "selection.source.unsafe",
                    (
                        "fixed tracked metadata source must be a regular file "
                        "beneath the canonical repository root"
                    ),
                    lambda root=root, source=source: read_fixed_metadata_source(
                        root, source
                    ),
                )

    def test_s_a_09_rejects_wrong_or_symlinked_output(self) -> None:
        # Given: A canonical temp checkout and invalid lexical/filesystem targets.
        content = b"{}\n"
        with tempfile.TemporaryDirectory() as repository_dir:
            root = Path(repository_dir)
            parent = root / "docs/reference/d17-golden-regression"
            parent.mkdir(parents=True)
            outside = root / "outside.json"
            outside.write_bytes(b"do-not-touch")
            approved = parent / "golden-inputs-v1.json"
            approved.symlink_to(outside)

            # When/Then: Wrong lexical path and symlink target fail typed.
            self.assert_failure(
                SelectionOutputError,
                "selection.output.path",
                f"selection output must be {OUTPUT_PATH}",
                lambda: write_golden_selection(
                    root,
                    Path("artifacts/golden-inputs-v1.json"),
                    content,
                ),
            )
            self.assert_failure(
                SelectionOutputError,
                "selection.output.target",
                "selection output must be absent or a regular file",
                lambda: write_golden_selection(
                    root,
                    Path(OUTPUT_PATH),
                    content,
                ),
            )
            self.assertEqual(b"do-not-touch", outside.read_bytes())

    def test_s_a_16_refuses_to_overwrite_output_with_unpinned_sources(self) -> None:
        # Given: An existing approved-path output whose source pins have drifted.
        existing = {
            "sources": {
                "manifest": {
                    "path": MANIFEST_PATH,
                    "sha256": "0" * 64,
                    "size_bytes": EXPECTED_MANIFEST_SIZE,
                },
                "corpus_contract": {
                    "path": CONTRACT_PATH,
                    "sha256": EXPECTED_CONTRACT_SHA256,
                    "size_bytes": EXPECTED_CONTRACT_SIZE,
                },
            }
        }
        with tempfile.TemporaryDirectory() as repository_dir:
            root = Path(repository_dir)
            target = root / OUTPUT_PATH
            target.parent.mkdir(parents=True)
            before = (json.dumps(existing, sort_keys=True) + "\n").encode()
            target.write_bytes(before)

            # When/Then: Existing unpinned provenance blocks overwrite unchanged.
            self.assert_failure(
                SelectionOutputError,
                "selection.output.identity_mismatch",
                (
                    "existing selection source identities do not match "
                    "the immutable P0-C4 pins"
                ),
                lambda: write_golden_selection(
                    root,
                    Path(OUTPUT_PATH),
                    b'{"replacement":true}\n',
                ),
            )
            self.assertEqual(before, target.read_bytes())


if __name__ == "__main__":
    unittest.main()
