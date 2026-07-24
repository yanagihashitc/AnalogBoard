from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from io import StringIO
from pathlib import Path
from types import SimpleNamespace
from typing import Callable

SCRIPT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPT_ROOT))

from mapping_contract import (  # noqa: E402
    AuthorityCommitError,
    AuthorityIndexError,
    AuthorityOrderError,
    AuthoritySourceError,
    ContractOutputError,
    ContractDecisionRequiredError,
    MissingAuthoritySymbolError,
    _build_parser,
    derive_mapping_from_git,
    derive_mapping_from_source,
    generate_mapping_contract,
    read_git_blob,
    serialize_mapping_contract,
)


FL_CHANNELS = (
    ("FSC", 0),
    ("SSC", 1),
    ("FL1", 2),
    ("FL2", 3),
    ("FL3", 4),
    ("FL4", 5),
    ("FL5", 6),
    ("FL6", 7),
)
FH_CHANNELS = (
    ("fsGMI", 0),
    ("ssGMI", 1),
    ("flGMI", 2),
    ("dGMI", 3),
    ("bfGMI", 4),
)
EXPECTED_LABELS = tuple(label for label, _ in (*FL_CHANNELS, *FH_CHANNELS))
EXPECTED_MAPPING = [
    {
        "physical_channel": f"CH{position}",
        "stream": stream,
        "source_index": source_index,
        "label": label,
    }
    for position, (stream, label, source_index) in enumerate(
        (
            *(("FL", label, source_index) for label, source_index in FL_CHANNELS),
            *(("FH", label, source_index) for label, source_index in FH_CHANNELS),
        ),
        start=1,
    )
]
PROVENANCE = {
    "repository": "gcsa",
    "commit": "20689a991697217518ec2ff15aaaa2533b169eb0",
    "path": "src/gcsa/constants.py",
    "symbols": [
        "FL_CHANNEL_MAP",
        "FH_CHANNEL_MAP",
        "FL_CHANNEL_NAMES",
        "FH_CHANNEL_NAMES",
        "ALL_CHANNEL_NAMES",
    ],
}


def authority_source(
    *,
    fl_channels: tuple[tuple[str, object], ...] = FL_CHANNELS,
    fh_channels: tuple[tuple[str, object], ...] = FH_CHANNELS,
    include_fl_map: bool = True,
    include_fh_map: bool = True,
    include_fl_names: bool = True,
    include_fh_names: bool = True,
    include_all_names: bool = True,
    fl_names_expression: str = "tuple(FL_CHANNEL_MAP.keys())",
    fh_names_expression: str = "tuple(FH_CHANNEL_MAP.keys())",
    all_names_expression: str = "FL_CHANNEL_NAMES + FH_CHANNEL_NAMES",
) -> str:
    lines: list[str] = []
    if include_fl_map:
        lines.append(f"FL_CHANNEL_MAP = {dict(fl_channels)!r}")
    if include_fl_names:
        lines.append(f"FL_CHANNEL_NAMES = {fl_names_expression}")
    if include_fh_map:
        lines.append(f"FH_CHANNEL_MAP = {dict(fh_channels)!r}")
    if include_fh_names:
        lines.append(f"FH_CHANNEL_NAMES = {fh_names_expression}")
    if include_all_names:
        lines.append(f"ALL_CHANNEL_NAMES = {all_names_expression}")
    return "\n".join(lines) + "\n"


class MappingContractTests(unittest.TestCase):
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

    def test_m1_n_01_derives_ordered_mapping_from_authority(self) -> None:
        # Given: Pinned-style FL/FH maps and their composed channel-name authority.
        source = authority_source()

        # When: The physical-channel mapping is derived from source authority.
        mapping = derive_mapping_from_source(source)

        # Then: CH1-CH13 follow the authoritative FL-then-FH order exactly.
        self.assertEqual(EXPECTED_MAPPING, mapping)
        self.assertEqual(EXPECTED_LABELS, tuple(item["label"] for item in mapping))

    def test_m1_n_02_serialization_is_byte_deterministic(self) -> None:
        # Given: One authority-derived mapping and stable provenance without time fields.
        mapping = derive_mapping_from_source(authority_source())

        # When: The contract is serialized twice from identical inputs.
        first = serialize_mapping_contract(mapping, PROVENANCE)
        second = serialize_mapping_contract(mapping, PROVENANCE)

        # Then: Canonical JSON bytes match exactly and terminate with one LF.
        self.assertIs(bytes, type(first))
        self.assertEqual(first, second)
        self.assertTrue(first.endswith(b"\n"))
        self.assertFalse(first.endswith(b"\n\n"))
        contract = json.loads(first)
        self.assertEqual("analogboard.d17.channel-mapping", contract["schema"])
        self.assertEqual(1, contract["schema_version"])
        self.assertEqual(PROVENANCE, contract["provenance"])

    def test_m1_b_01_exactly_thirteen_unique_channels_are_accepted(self) -> None:
        # Given: The exact frozen boundary of eight FL and five FH channels.
        source = authority_source()

        # When: The mapping is derived at the 13-entry boundary.
        mapping = derive_mapping_from_source(source)

        # Then: All 13 physical channels and labels are unique and accepted.
        self.assertEqual(13, len(mapping))
        self.assertEqual(13, len({item["physical_channel"] for item in mapping}))
        self.assertEqual(13, len({item["label"] for item in mapping}))

    def test_m1_b_02_zero_channels_fail_closed(self) -> None:
        # Given: Present authority symbols whose FL and FH maps are both empty.
        source = authority_source(fl_channels=(), fh_channels=())

        # When: Mapping derivation encounters the zero-channel boundary.
        # Then: A typed decision-required count failure rejects a partial contract.
        self.assert_failure(
            ContractDecisionRequiredError,
            "mapping.channel_count.decision_required",
            "derived mapping must contain exactly 13 channels; found 0",
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_b_03_twelve_channels_fail_closed(self) -> None:
        # Given: Authority with exactly one channel missing from the frozen boundary.
        source = authority_source(fl_channels=FL_CHANNELS[:-1])

        # When: Mapping derivation encounters 13 - 1 entries.
        # Then: A typed decision-required count failure rejects the missing channel.
        self.assert_failure(
            ContractDecisionRequiredError,
            "mapping.channel_count.decision_required",
            "derived mapping must contain exactly 13 channels; found 12",
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_b_04_fourteen_channels_fail_closed(self) -> None:
        # Given: Authority with exactly one channel beyond the frozen boundary.
        source = authority_source(
            fh_channels=(*FH_CHANNELS, ("extraGMI", len(FH_CHANNELS)))
        )

        # When: Mapping derivation encounters 13 + 1 entries.
        # Then: A typed decision-required count failure rejects the excess channel.
        self.assert_failure(
            ContractDecisionRequiredError,
            "mapping.channel_count.decision_required",
            "derived mapping must contain exactly 13 channels; found 14",
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_a_01_null_or_non_string_source_is_rejected(self) -> None:
        # Given: NULL and representative non-string authority-source values.
        invalid_sources = (None, 0, b"FL_CHANNEL_MAP = {}")
        for invalid_source in invalid_sources:
            with self.subTest(source_type=type(invalid_source).__name__):
                # When: Mapping derivation is requested without source text.
                # Then: A typed source failure rejects implicit coercion.
                self.assert_failure(
                    AuthoritySourceError,
                    "authority.source.type",
                    "authority source must be a string",
                    lambda invalid_source=invalid_source: derive_mapping_from_source(
                        invalid_source
                    ),
                )

    def test_m1_a_02_empty_source_reports_all_missing_symbols(self) -> None:
        # Given: Empty or whitespace-only authority source text.
        for source in ("", " \t\r\n"):
            with self.subTest(source=repr(source)):
                # When: Mapping derivation searches for required symbols.
                # Then: One exact typed failure reports every missing authority symbol.
                self.assert_failure(
                    MissingAuthoritySymbolError,
                    "authority.symbol.missing",
                    (
                        "required authority symbols are missing: "
                        "FL_CHANNEL_MAP, FH_CHANNEL_MAP, FL_CHANNEL_NAMES, "
                        "FH_CHANNEL_NAMES, ALL_CHANNEL_NAMES"
                    ),
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_03_each_required_authority_symbol_is_fail_closed(self) -> None:
        # Given: Otherwise valid source with one required authority symbol absent.
        cases = (
            (
                authority_source(include_fl_map=False),
                "required authority symbol is missing: FL_CHANNEL_MAP",
            ),
            (
                authority_source(include_fh_map=False),
                "required authority symbol is missing: FH_CHANNEL_MAP",
            ),
            (
                authority_source(
                    include_fl_names=False,
                    all_names_expression=repr(EXPECTED_LABELS),
                ),
                "required authority symbol is missing: FL_CHANNEL_NAMES",
            ),
            (
                authority_source(
                    include_fh_names=False,
                    all_names_expression=repr(EXPECTED_LABELS),
                ),
                "required authority symbol is missing: FH_CHANNEL_NAMES",
            ),
            (
                authority_source(include_all_names=False),
                "required authority symbol is missing: ALL_CHANNEL_NAMES",
            ),
        )
        for source, expected_message in cases:
            with self.subTest(expected_message=expected_message):
                # When: Mapping derivation reads the incomplete authority.
                # Then: A typed missing-symbol failure prevents local inference.
                self.assert_failure(
                    MissingAuthoritySymbolError,
                    "authority.symbol.missing",
                    expected_message,
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_18_map_key_order_must_match_source_index_order(self) -> None:
        # Given: The frozen FL labels and indices with only map insertion order drift.
        drifted_fl = (FL_CHANNELS[1], FL_CHANNELS[0], *FL_CHANNELS[2:])
        source = authority_source(fl_channels=drifted_fl)

        # When: Mapping derivation validates key order against source indices.
        # Then: A typed index-order failure rejects silent index-based reordering.
        self.assert_failure(
            AuthorityIndexError,
            "authority.index.order",
            "FL_CHANNEL_MAP key order must match its contiguous source indices",
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_a_19_stream_label_only_swap_requires_decision(self) -> None:
        # Given: Thirteen correct labels with FL6 and fsGMI assigned to opposite streams.
        swapped_fl = (*FL_CHANNELS[:-1], ("fsGMI", FL_CHANNELS[-1][1]))
        swapped_fh = (("FL6", FH_CHANNELS[0][1]), *FH_CHANNELS[1:])
        source = authority_source(
            fl_channels=swapped_fl,
            fh_channels=swapped_fh,
        )

        # When: Mapping derivation validates the frozen per-stream label groups.
        # Then: A typed decision-required failure rejects the label-only seam drift.
        self.assert_failure(
            ContractDecisionRequiredError,
            "mapping.stream_labels.decision_required",
            (
                "derived FL/FH channel groups do not match the frozen "
                "D17 stream mapping"
            ),
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_a_20_fl_channel_names_must_match_fl_map_order(self) -> None:
        # Given: A valid FL map but an explicitly swapped FL_CHANNEL_NAMES tuple.
        drifted_names = (FL_CHANNELS[1][0], FL_CHANNELS[0][0]) + tuple(
            label for label, _ in FL_CHANNELS[2:]
        )
        source = authority_source(
            fl_names_expression=repr(drifted_names),
            all_names_expression=repr(EXPECTED_LABELS),
        )

        # When: Mapping derivation validates the provenance-listed FL names authority.
        # Then: A typed order failure rejects disagreement with FL_CHANNEL_MAP.
        self.assert_failure(
            AuthorityOrderError,
            "authority.order.mismatch",
            "FL_CHANNEL_NAMES order does not match FL_CHANNEL_MAP",
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_a_21_fh_channel_names_must_match_fh_map_order(self) -> None:
        # Given: A valid FH map but an explicitly swapped FH_CHANNEL_NAMES tuple.
        drifted_names = (FH_CHANNELS[1][0], FH_CHANNELS[0][0]) + tuple(
            label for label, _ in FH_CHANNELS[2:]
        )
        source = authority_source(
            fh_names_expression=repr(drifted_names),
            all_names_expression=repr(EXPECTED_LABELS),
        )

        # When: Mapping derivation validates the provenance-listed FH names authority.
        # Then: A typed order failure rejects disagreement with FH_CHANNEL_MAP.
        self.assert_failure(
            AuthorityOrderError,
            "authority.order.mismatch",
            "FH_CHANNEL_NAMES order does not match FH_CHANNEL_MAP",
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_a_04_d17_label_set_drift_requires_decision(self) -> None:
        # Given: Thirteen valid indices whose FL label set substitutes FL7 for FL6.
        drifted_fl = (*FL_CHANNELS[:-1], ("FL7", FL_CHANNELS[-1][1]))
        source = authority_source(fl_channels=drifted_fl)

        # When: Mapping derivation validates labels against frozen D17.
        # Then: A typed decision-required error reports exact missing/extra labels.
        self.assert_failure(
            ContractDecisionRequiredError,
            "mapping.labels.decision_required",
            (
                "derived labels do not match the frozen D17 label set; "
                "missing=['FL6']; extra=['FL7']"
            ),
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_a_05_invalid_map_indices_are_rejected(self) -> None:
        # Given: Duplicate, negative, gapped, non-integer, and bool FL indices.
        cases = (
            (
                (("FSC", 0), ("SSC", 0), *FL_CHANNELS[2:]),
                "authority.index.duplicate",
                "FL_CHANNEL_MAP indices must be unique; duplicate index 0",
            ),
            (
                (("FSC", -1), *FL_CHANNELS[1:]),
                "authority.index.negative",
                "FL_CHANNEL_MAP index for FSC must be non-negative; found -1",
            ),
            (
                (FL_CHANNELS[0], ("SSC", 8), *FL_CHANNELS[2:]),
                "authority.index.gap",
                (
                    "FL_CHANNEL_MAP indices must be contiguous from 0; "
                    "found [0, 2, 3, 4, 5, 6, 7, 8]"
                ),
            ),
            (
                (FL_CHANNELS[0], ("SSC", "1"), *FL_CHANNELS[2:]),
                "authority.index.type",
                "FL_CHANNEL_MAP index for SSC must be an integer; found str",
            ),
            (
                (FL_CHANNELS[0], ("SSC", True), *FL_CHANNELS[2:]),
                "authority.index.type",
                "FL_CHANNEL_MAP index for SSC must be an integer; found bool",
            ),
        )
        for fl_channels, expected_code, expected_message in cases:
            with self.subTest(expected_code=expected_code):
                source = authority_source(fl_channels=fl_channels)

                # When: Mapping derivation validates map indices.
                # Then: A stable typed index failure rejects the invalid boundary.
                self.assert_failure(
                    AuthorityIndexError,
                    expected_code,
                    expected_message,
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_06_all_channel_names_order_drift_is_rejected(self) -> None:
        # Given: Valid maps whose aggregate names authority reverses FL/FH order.
        source = authority_source(
            all_names_expression="FH_CHANNEL_NAMES + FL_CHANNEL_NAMES"
        )

        # When: Mapping derivation compares aggregate order with the two maps.
        # Then: A typed order failure rejects implicit reordering.
        self.assert_failure(
            AuthorityOrderError,
            "authority.order.mismatch",
            "ALL_CHANNEL_NAMES order does not match FL_CHANNEL_MAP + FH_CHANNEL_MAP",
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_a_07_unreadable_pinned_commit_has_no_head_fallback(self) -> None:
        # Given: A pinned gcsa blob reader that cannot resolve or read the commit.
        repository = Path("gcsa-read-only")
        commit = "a" * 40
        source_path = "src/gcsa/constants.py"
        calls: list[tuple[Path, str, str]] = []

        def unreadable_blob(
            requested_repository: Path,
            requested_commit: str,
            requested_path: str,
        ) -> str:
            calls.append(
                (requested_repository, requested_commit, requested_path)
            )
            raise OSError("synthetic unreadable commit")

        # When: Mapping derivation requests only that pinned authority blob.
        # Then: A typed commit failure is raised without falling back to HEAD.
        self.assert_failure(
            AuthorityCommitError,
            "authority.commit.unreadable",
            f"unable to read pinned authority blob {commit}:{source_path}",
            lambda: derive_mapping_from_git(
                repository,
                commit,
                source_path,
                read_blob=unreadable_blob,
            ),
        )
        self.assertEqual([(repository, commit, source_path)], calls)

    def test_m1_n_03_generator_resolves_approved_output_from_repository_root(
        self,
    ) -> None:
        # Given: An explicit repository root and a different process cwd.
        with tempfile.TemporaryDirectory() as repository_dir:
            with tempfile.TemporaryDirectory() as other_cwd:
                repository_root = Path(repository_dir)
                approved_parent = (
                    repository_root / "docs/reference/d17-golden-regression"
                )
                approved_parent.mkdir(parents=True)
                original_cwd = Path.cwd()
                try:
                    os.chdir(other_cwd)

                    # When: Generation uses the approved repository-relative target.
                    generated = generate_mapping_contract(
                        repository_root=repository_root,
                        output_path=Path(
                            "docs/reference/d17-golden-regression/"
                            "channel-mapping-v1.json"
                        ),
                        gcsa_repository=Path("gcsa-read-only"),
                        commit=PROVENANCE["commit"],
                        source_path=PROVENANCE["path"],
                        read_blob=lambda *_: authority_source(),
                    )
                finally:
                    os.chdir(original_cwd)

                # Then: The result is written under the explicit root, not cwd.
                approved_output = approved_parent / "channel-mapping-v1.json"
                self.assertEqual(approved_output, generated)
                self.assertTrue(approved_output.is_file())
                self.assertFalse(
                    (
                        Path(other_cwd)
                        / "docs/reference/d17-golden-regression/"
                        "channel-mapping-v1.json"
                    ).exists()
                )

    def test_m1_a_08_generator_rejects_every_non_approved_output_path(self) -> None:
        # Given: Absolute, traversing, and merely similar output targets.
        with tempfile.TemporaryDirectory() as repository_dir:
            repository_root = Path(repository_dir)
            approved_parent = (
                repository_root / "docs/reference/d17-golden-regression"
            )
            approved_parent.mkdir(parents=True)
            cases = (
                repository_root
                / "docs/reference/d17-golden-regression/channel-mapping-v1.json",
                Path("../docs/reference/d17-golden-regression/channel-mapping-v1.json"),
                Path("artifacts/channel-mapping-v1.json"),
                Path("docs/reference/d17-golden-regression/other.json"),
            )
            for output_path in cases:
                with self.subTest(output_path=str(output_path)):
                    # When: Generation is directed anywhere except the one contract.
                    # Then: A typed failure exposes no host locator and writes nothing.
                    with self.assertRaises(ContractOutputError) as raised:
                        generate_mapping_contract(
                            repository_root=repository_root,
                            output_path=output_path,
                            gcsa_repository=Path("gcsa-read-only"),
                            commit=PROVENANCE["commit"],
                            source_path=PROVENANCE["path"],
                            read_blob=lambda *_: authority_source(),
                        )
                    self.assertEqual("contract.output.path.invalid", raised.exception.code)
                    self.assertEqual(
                        (
                            "contract.output.path.invalid: contract output must be "
                            "docs/reference/d17-golden-regression/"
                            "channel-mapping-v1.json"
                        ),
                        str(raised.exception),
                    )
                    self.assertNotIn(str(repository_root), str(raised.exception))
            self.assertEqual([], list(approved_parent.iterdir()))

    def test_m1_a_09_generator_rejects_symlink_or_non_regular_target(self) -> None:
        # Given: The approved lexical target occupied by unsafe filesystem types.
        for target_kind in ("symlink", "directory"):
            with self.subTest(target_kind=target_kind):
                with tempfile.TemporaryDirectory() as repository_dir:
                    repository_root = Path(repository_dir)
                    approved_parent = (
                        repository_root / "docs/reference/d17-golden-regression"
                    )
                    approved_parent.mkdir(parents=True)
                    approved_output = approved_parent / "channel-mapping-v1.json"
                    if target_kind == "symlink":
                        regular_target = repository_root / "outside.json"
                        regular_target.write_bytes(b"do-not-touch")
                        approved_output.symlink_to(regular_target)
                    else:
                        approved_output.mkdir()

                    # When: Generation checks the existing approved target.
                    # Then: It fails before reading authority or replacing the target.
                    self.assert_failure(
                        ContractOutputError,
                        "contract.output.target.unsafe",
                        "approved contract output must be absent or a regular file",
                        lambda: generate_mapping_contract(
                            repository_root=repository_root,
                            output_path=Path(
                                "docs/reference/d17-golden-regression/"
                                "channel-mapping-v1.json"
                            ),
                            gcsa_repository=Path("gcsa-read-only"),
                            commit=PROVENANCE["commit"],
                            source_path=PROVENANCE["path"],
                            read_blob=lambda *_: self.fail(
                                "unsafe output must fail before authority read"
                            ),
                        ),
                    )

    def test_m1_a_10_generator_rejects_unsafe_output_parent(self) -> None:
        # Given: A symlink in the otherwise approved repository-relative parent.
        with tempfile.TemporaryDirectory() as repository_dir:
            with tempfile.TemporaryDirectory() as outside_dir:
                repository_root = Path(repository_dir)
                outside = Path(outside_dir)
                (outside / "reference/d17-golden-regression").mkdir(parents=True)
                (repository_root / "docs").symlink_to(outside, target_is_directory=True)

                # When: Generation validates the complete parent chain.
                # Then: It fails before authority read and does not escape the root.
                self.assert_failure(
                    ContractOutputError,
                    "contract.output.parent.unsafe",
                    "approved contract output parent must contain only directories",
                    lambda: generate_mapping_contract(
                        repository_root=repository_root,
                        output_path=Path(
                            "docs/reference/d17-golden-regression/"
                            "channel-mapping-v1.json"
                        ),
                        gcsa_repository=Path("gcsa-read-only"),
                        commit=PROVENANCE["commit"],
                        source_path=PROVENANCE["path"],
                        read_blob=lambda *_: self.fail(
                            "unsafe output must fail before authority read"
                        ),
                    ),
                )
                self.assertFalse(
                    (
                        outside
                        / "reference/d17-golden-regression/channel-mapping-v1.json"
                    ).exists()
                )

    def test_m1_a_11_output_write_error_is_typed_without_host_locator(self) -> None:
        # Given: A valid approved target whose injected writer fails.
        with tempfile.TemporaryDirectory() as repository_dir:
            repository_root = Path(repository_dir)
            (
                repository_root / "docs/reference/d17-golden-regression"
            ).mkdir(parents=True)

            def failing_writer(_: Path, __: bytes) -> None:
                raise OSError("synthetic host write failure")

            # When: The bounded contract write fails.
            # Then: A typed generic error does not reflect an absolute host locator.
            with self.assertRaises(ContractOutputError) as raised:
                generate_mapping_contract(
                    repository_root=repository_root,
                    output_path=Path(
                        "docs/reference/d17-golden-regression/"
                        "channel-mapping-v1.json"
                    ),
                    gcsa_repository=Path("gcsa-read-only"),
                    commit=PROVENANCE["commit"],
                    source_path=PROVENANCE["path"],
                    read_blob=lambda *_: authority_source(),
                    write_bytes=failing_writer,
                )
            self.assertEqual("contract.output.write", raised.exception.code)
            self.assertEqual(
                (
                    "contract.output.write: unable to write approved contract output"
                ),
                str(raised.exception),
            )
            self.assertNotIn(str(repository_root), str(raised.exception))

    def test_m1_a_12_git_blob_read_disables_replace_refs(self) -> None:
        # Given: An injected subprocess runner recording the exact Git command.
        calls: list[tuple[list[str], dict[str, object]]] = []

        def recording_runner(
            command: list[str], **kwargs: object
        ) -> SimpleNamespace:
            calls.append((command, kwargs))
            return SimpleNamespace(stdout=authority_source())

        # When: The default pinned-blob reader invokes Git.
        source = read_git_blob(
            Path("gcsa-read-only"),
            PROVENANCE["commit"],
            PROVENANCE["path"],
            run=recording_runner,
        )

        # Then: Replacement objects are disabled and only commit:path is shown.
        self.assertEqual(authority_source(), source)
        self.assertEqual(1, len(calls))
        command, kwargs = calls[0]
        self.assertEqual(
            [
                "git",
                "--no-replace-objects",
                "-C",
                "gcsa-read-only",
                "--no-pager",
                "show",
                f"{PROVENANCE['commit']}:{PROVENANCE['path']}",
            ],
            command,
        )
        self.assertTrue(kwargs["check"])
        self.assertIs(subprocess.PIPE, kwargs["stdout"])
        self.assertIs(subprocess.DEVNULL, kwargs["stderr"])

    def test_m1_a_13_commit_validation_is_stable_and_requires_full_sha(self) -> None:
        # Given: NULL, non-string, abbreviated, uppercase, and malformed commits.
        invalid_commits = (
            None,
            123,
            b"a" * 40,
            "a" * 39,
            "A" * 40,
            "g" * 40,
        )
        for commit in invalid_commits:
            with self.subTest(commit_type=type(commit).__name__):
                # When: The authority blob is requested with an invalid pin.
                # Then: One stable typed error rejects it before blob access.
                self.assert_failure(
                    AuthorityCommitError,
                    "authority.commit.invalid",
                    (
                        "authority commit must be a full lowercase hexadecimal "
                        "Git object ID"
                    ),
                    lambda commit=commit: derive_mapping_from_git(
                        Path("gcsa-read-only"),
                        commit,
                        PROVENANCE["path"],
                        read_blob=lambda *_: self.fail(
                            "invalid commit must fail before blob read"
                        ),
                    ),
                )

    def test_m1_a_14_source_path_validation_is_stable(self) -> None:
        # Given: NULL, non-string, absolute, traversal, and non-normal paths.
        cases = (
            (
                None,
                "authority.path.type",
                "authority source path must be a string",
            ),
            (
                123,
                "authority.path.type",
                "authority source path must be a string",
            ),
            (
                "/src/gcsa/constants.py",
                "authority.path.invalid",
                "authority source path must be a normalized repository-relative path",
            ),
            (
                "../src/gcsa/constants.py",
                "authority.path.invalid",
                "authority source path must be a normalized repository-relative path",
            ),
            (
                "src/../src/gcsa/constants.py",
                "authority.path.invalid",
                "authority source path must be a normalized repository-relative path",
            ),
            (
                "./src/gcsa/constants.py",
                "authority.path.invalid",
                "authority source path must be a normalized repository-relative path",
            ),
            (
                "src//gcsa/constants.py",
                "authority.path.invalid",
                "authority source path must be a normalized repository-relative path",
            ),
            (
                "src\\gcsa\\constants.py",
                "authority.path.invalid",
                "authority source path must be a normalized repository-relative path",
            ),
        )
        for source_path, expected_code, expected_message in cases:
            with self.subTest(source_path=repr(source_path)):
                # When: The authority path is invalid.
                # Then: Validation fails before the blob reader is invoked.
                self.assert_failure(
                    AuthoritySourceError,
                    expected_code,
                    expected_message,
                    lambda source_path=source_path: derive_mapping_from_git(
                        Path("gcsa-read-only"),
                        PROVENANCE["commit"],
                        source_path,
                        read_blob=lambda *_: self.fail(
                            "invalid path must fail before blob read"
                        ),
                    ),
                )

    def test_m1_n_04_full_sha_and_normalized_source_path_are_forwarded_exactly(
        self,
    ) -> None:
        # Given: An exact full SHA and normalized authority path.
        calls: list[tuple[Path, str, str]] = []

        def valid_blob(repository: Path, commit: str, source_path: str) -> str:
            calls.append((repository, commit, source_path))
            return authority_source()

        # When: Derivation reads the pinned authority.
        mapping = derive_mapping_from_git(
            Path("gcsa-read-only"),
            PROVENANCE["commit"],
            PROVENANCE["path"],
            read_blob=valid_blob,
        )

        # Then: Validation preserves the exact locator components.
        self.assertEqual(EXPECTED_MAPPING, mapping)
        self.assertEqual(
            [
                (
                    Path("gcsa-read-only"),
                    PROVENANCE["commit"],
                    PROVENANCE["path"],
                )
            ],
            calls,
        )

    def test_m1_a_15_unhashable_dictionary_key_is_typed(self) -> None:
        # Given: A syntactically valid authority map with an unhashable dict key.
        source = authority_source().replace(
            f"FL_CHANNEL_MAP = {dict(FL_CHANNELS)!r}",
            "FL_CHANNEL_MAP = {{}: 0}",
        )

        # When: The restricted evaluator inspects the malformed map.
        # Then: It normalizes the raw Python TypeError to a stable source failure.
        self.assert_failure(
            AuthoritySourceError,
            "authority.source.unsupported",
            "unhashable dictionary key in authority symbol: FL_CHANNEL_MAP",
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_a_16_cli_has_no_repository_root_override(self) -> None:
        # Given: A generation command that attempts to redirect the checkout root.
        parser = _build_parser()
        arguments = [
            "generate",
            "--gcsa-repo",
            "gcsa-read-only",
            "--commit",
            PROVENANCE["commit"],
            "--source-path",
            PROVENANCE["path"],
            "--output",
            "docs/reference/d17-golden-regression/channel-mapping-v1.json",
            "--repository-root",
            "redirected-root",
        ]

        # When: The production CLI parses the unauthorized override.
        # Then: argparse rejects it instead of redirecting the approved output.
        stderr = StringIO()
        with redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
            parser.parse_args(arguments)
        self.assertEqual(2, raised.exception.code)
        self.assertNotIn("/", stderr.getvalue())

    def test_m1_a_17_generator_rejects_repository_root_with_symlink_ancestor(
        self,
    ) -> None:
        # Given: A lexical root whose ancestor redirects to another directory.
        with tempfile.TemporaryDirectory() as container_dir:
            container = Path(container_dir)
            actual_root = container / "actual/checkout"
            (
                actual_root / "docs/reference/d17-golden-regression"
            ).mkdir(parents=True)
            alias = container / "alias"
            alias.symlink_to(container / "actual", target_is_directory=True)
            redirected_root = alias / "checkout"

            # When: The private generation seam receives the redirected root.
            # Then: A typed root failure occurs before authority access or writes.
            self.assert_failure(
                ContractOutputError,
                "contract.output.root.invalid",
                "repository root must be an existing canonical directory",
                lambda: generate_mapping_contract(
                    repository_root=redirected_root,
                    output_path=Path(
                        "docs/reference/d17-golden-regression/"
                        "channel-mapping-v1.json"
                    ),
                    gcsa_repository=Path("gcsa-read-only"),
                    commit=PROVENANCE["commit"],
                    source_path=PROVENANCE["path"],
                    read_blob=lambda *_: self.fail(
                        "redirected root must fail before authority read"
                    ),
                ),
            )

    def test_m1_n_05_generator_accepts_identical_existing_output_without_write(
        self,
    ) -> None:
        # Given: A generated frozen contract already exists with byte-identical content.
        with tempfile.TemporaryDirectory() as repository_dir:
            repository_root = Path(repository_dir)
            approved_parent = (
                repository_root / "docs/reference/d17-golden-regression"
            )
            approved_parent.mkdir(parents=True)
            output_path = Path(
                "docs/reference/d17-golden-regression/channel-mapping-v1.json"
            )
            approved_output = generate_mapping_contract(
                repository_root=repository_root,
                output_path=output_path,
                gcsa_repository=Path("gcsa-read-only"),
                commit=PROVENANCE["commit"],
                source_path=PROVENANCE["path"],
                read_blob=lambda *_: authority_source(),
            )
            before = approved_output.read_bytes()

            # When: The same frozen contract is generated again.
            regenerated = generate_mapping_contract(
                repository_root=repository_root,
                output_path=output_path,
                gcsa_repository=Path("gcsa-read-only"),
                commit=PROVENANCE["commit"],
                source_path=PROVENANCE["path"],
                read_blob=lambda *_: authority_source(),
                write_bytes=lambda *_: self.fail(
                    "identical frozen output must not be rewritten"
                ),
            )

            # Then: Generation succeeds as a byte-preserving no-op.
            self.assertEqual(approved_output, regenerated)
            self.assertEqual(before, regenerated.read_bytes())

    def test_m1_a_22_generator_rejects_existing_output_drift_without_write(
        self,
    ) -> None:
        # Given: The approved regular output exists with drifted frozen bytes.
        with tempfile.TemporaryDirectory() as repository_dir:
            repository_root = Path(repository_dir)
            approved_parent = (
                repository_root / "docs/reference/d17-golden-regression"
            )
            approved_parent.mkdir(parents=True)
            approved_output = approved_parent / "channel-mapping-v1.json"
            drifted = b"drifted-frozen-contract\n"
            approved_output.write_bytes(drifted)

            # When: Generation would otherwise replace the existing output.
            # Then: A typed mismatch fails closed and preserves the frozen bytes.
            self.assert_failure(
                ContractOutputError,
                "contract.output.mismatch",
                "existing contract output differs from generated content",
                lambda: generate_mapping_contract(
                    repository_root=repository_root,
                    output_path=Path(
                        "docs/reference/d17-golden-regression/"
                        "channel-mapping-v1.json"
                    ),
                    gcsa_repository=Path("gcsa-read-only"),
                    commit=PROVENANCE["commit"],
                    source_path=PROVENANCE["path"],
                    read_blob=lambda *_: authority_source(),
                    write_bytes=lambda *_: self.fail(
                        "drifted frozen output must not be rewritten"
                    ),
                ),
            )
            self.assertEqual(drifted, approved_output.read_bytes())

    def test_m1_a_23_generator_rejects_output_parent_replacement(self) -> None:
        # Given: The approved parent is replaced after validation but before write.
        with tempfile.TemporaryDirectory() as repository_dir:
            repository_root = Path(repository_dir)
            approved_parent = (
                repository_root / "docs/reference/d17-golden-regression"
            )
            approved_parent.mkdir(parents=True)
            verified_parent = approved_parent.with_name("verified-parent")
            outside = repository_root / "outside"
            outside.mkdir()

            def replace_parent(*_args) -> str:
                approved_parent.rename(verified_parent)
                approved_parent.symlink_to(outside, target_is_directory=True)
                return authority_source()

            # When/Then: The stale lexical path cannot redirect the contract write.
            self.assert_failure(
                ContractOutputError,
                "contract.output.parent.changed",
                "approved contract output parent changed during generation",
                lambda: generate_mapping_contract(
                    repository_root=repository_root,
                    output_path=Path(
                        "docs/reference/d17-golden-regression/"
                        "channel-mapping-v1.json"
                    ),
                    gcsa_repository=Path("gcsa-read-only"),
                    commit=PROVENANCE["commit"],
                    source_path=PROVENANCE["path"],
                    read_blob=replace_parent,
                ),
            )
            self.assertFalse((outside / "channel-mapping-v1.json").exists())

    def test_m1_a_24_authority_map_method_mutations_are_rejected(self) -> None:
        # Given: Valid authority followed by a direct destructive map method call.
        mutations = (
            "FL_CHANNEL_MAP.update({'FSC': 7})\n",
            "FL_CHANNEL_MAP.pop('FSC')\n",
            "FL_CHANNEL_MAP.clear()\n",
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation.strip()):
                source = authority_source() + mutation

                # When: Derivation inspects an otherwise ignored method mutation.
                # Then: It fails closed instead of extracting the stale assignment.
                self.assert_failure(
                    AuthoritySourceError,
                    "authority.source.unsupported",
                    (
                        "authority symbol has unsupported use, binding, or mutation: "
                        "FL_CHANNEL_MAP"
                    ),
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_25_alternate_assignment_forms_are_rejected(self) -> None:
        # Given: Valid authority plus alternate Store/Del forms for one symbol.
        alternate_forms = (
            "def mutate():\n    FL_CHANNEL_MAP = {}\n",
            "def mutate():\n    FL_CHANNEL_MAP: dict = {}\n",
            "FL_CHANNEL_MAP |= {}\n",
            "(FL_CHANNEL_MAP := FL_CHANNEL_MAP)\n",
            "del FL_CHANNEL_MAP\n",
            "FL_CHANNEL_MAP['FSC'] = 7\n",
        )
        for alternate in alternate_forms:
            with self.subTest(alternate=alternate.strip()):
                source = authority_source() + alternate

                # When: Derivation scans bindings beyond the supported assignment.
                # Then: A stable typed source failure rejects the ambiguity.
                self.assert_failure(
                    AuthoritySourceError,
                    "authority.source.unsupported",
                    (
                        "authority symbol has unsupported use, binding, or mutation: "
                        "FL_CHANNEL_MAP"
                    ),
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_26_non_assignment_authority_bindings_are_rejected(self) -> None:
        # Given: Valid authority plus representative non-Assign binding forms.
        bindings = (
            "import collections as FL_CHANNEL_MAP\n",
            "from collections import abc as FL_CHANNEL_MAP\n",
            "def FL_CHANNEL_MAP():\n    pass\n",
            "class FL_CHANNEL_MAP:\n    pass\n",
            "def mutate():\n    global FL_CHANNEL_MAP\n",
            "def mutate(FL_CHANNEL_MAP):\n    pass\n",
            "for FL_CHANNEL_MAP in ():\n    pass\n",
            "with context() as FL_CHANNEL_MAP:\n    pass\n",
            "try:\n    pass\nexcept Exception as FL_CHANNEL_MAP:\n    pass\n",
        )
        for binding in bindings:
            with self.subTest(binding=binding.strip()):
                source = authority_source() + binding

                # When: Derivation scans bindings not represented by ast.Name Store.
                # Then: It rejects the alternate authority definition fail-closed.
                self.assert_failure(
                    AuthoritySourceError,
                    "authority.source.unsupported",
                    (
                        "authority symbol has unsupported use, binding, or mutation: "
                        "FL_CHANNEL_MAP"
                    ),
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_27_alias_based_authority_mutations_are_rejected(self) -> None:
        # Given: Valid authority loaded through an alias before indirect mutation.
        mutations = (
            "map_alias = FL_CHANNEL_MAP\nmap_alias['FSC'] = 7\n",
            "map_alias = FL_CHANNEL_MAP\nmap_alias.update({'FSC': 7})\n",
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation.strip()):
                source = authority_source() + mutation

                # When: Derivation scans an authority Load outside a supported RHS.
                # Then: The alias seam fails closed before its mutation is ignored.
                self.assert_failure(
                    AuthoritySourceError,
                    "authority.source.unsupported",
                    (
                        "authority symbol has unsupported use, binding, or mutation: "
                        "FL_CHANNEL_MAP"
                    ),
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_28_dynamic_namespace_bindings_are_rejected(self) -> None:
        # Given: Valid authority followed by globals/vars/locals namespace assignment.
        bindings = (
            ("globals", "globals()['FL_CHANNEL_MAP'] = {}\n"),
            ("vars", "vars()['FL_CHANNEL_MAP'] = {}\n"),
            ("locals", "locals()['FL_CHANNEL_MAP'] = {}\n"),
        )
        for operation, binding in bindings:
            with self.subTest(operation=operation):
                source = authority_source() + binding

                # When: Derivation encounters a dynamic namespace access call.
                # Then: A typed dynamic-binding failure rejects string-key evasion.
                self.assert_failure(
                    AuthoritySourceError,
                    "authority.source.unsupported",
                    (
                        "authority source has unsupported dynamic binding operation: "
                        f"{operation}"
                    ),
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_29_dynamic_code_execution_calls_are_rejected(self) -> None:
        # Given: Valid authority followed by exec/eval of an authority mutation.
        calls = (
            ("exec", "exec(\"FL_CHANNEL_MAP = {}\")\n"),
            (
                "eval",
                "eval(\"globals().__setitem__('FL_CHANNEL_MAP', {})\")\n",
            ),
        )
        for operation, call in calls:
            with self.subTest(operation=operation):
                source = authority_source() + call

                # When: Derivation encounters dynamic code execution.
                # Then: A typed failure prevents opaque rebinding from being ignored.
                self.assert_failure(
                    AuthoritySourceError,
                    "authority.source.unsupported",
                    (
                        "authority source has unsupported dynamic binding operation: "
                        f"{operation}"
                    ),
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_30_dynamic_module_setattr_is_rejected(self) -> None:
        # Given: Valid authority followed by setattr on the current module object.
        source = authority_source() + (
            "setattr(sys.modules[__name__], 'FL_CHANNEL_MAP', {})\n"
        )

        # When: Derivation encounters the dynamic module binding operation.
        # Then: It rejects the binding without interpreting its string arguments.
        self.assert_failure(
            AuthoritySourceError,
            "authority.source.unsupported",
            "authority source has unsupported dynamic binding operation: setattr",
            lambda: derive_mapping_from_source(source),
        )

    def test_m1_a_31_read_only_authority_uses_obey_rhs_whitelist(self) -> None:
        # Given: Valid authority plus a read-only method outside supported RHS.
        methods = ("items", "values", "get", "copy")
        for method in methods:
            with self.subTest(method=method):
                arguments = "'FSC'" if method == "get" else ""
                source = authority_source() + (
                    f"FL_CHANNEL_MAP.{method}({arguments})\n"
                )

                # When: Derivation scans the extra authority Load.
                # Then: The message states strict unsupported use, not only mutation.
                self.assert_failure(
                    AuthoritySourceError,
                    "authority.source.unsupported",
                    (
                        "authority symbol has unsupported use, binding, or mutation: "
                        "FL_CHANNEL_MAP"
                    ),
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_32_builtins_dynamic_binding_calls_are_rejected(self) -> None:
        # Given: Valid authority plus a builtins-qualified dynamic binding form.
        cases = (
            ("exec", "builtins.exec(\"FL_CHANNEL_MAP = {}\")\n"),
            (
                "setattr",
                (
                    "builtins.setattr("
                    "sys.modules[__name__], 'FL_CHANNEL_MAP', {})\n"
                ),
            ),
            (
                "globals",
                "builtins.globals()['FL_CHANNEL_MAP'] = {}\n",
            ),
        )
        for operation, call in cases:
            with self.subTest(operation=operation):
                source = authority_source() + call

                # When: Derivation encounters the builtins-qualified operation.
                # Then: The same stable typed dynamic-binding failure rejects it.
                self.assert_failure(
                    AuthoritySourceError,
                    "authority.source.unsupported",
                    (
                        "authority source has unsupported dynamic binding operation: "
                        f"{operation}"
                    ),
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_a_33_dynamic_module_delattr_is_rejected(self) -> None:
        # Given: Valid authority plus direct and builtins-qualified module deletion.
        cases = (
            (
                "direct",
                "delattr(sys.modules[__name__], 'FL_CHANNEL_MAP')\n",
            ),
            (
                "builtins",
                (
                    "builtins.delattr("
                    "sys.modules[__name__], 'FL_CHANNEL_MAP')\n"
                ),
            ),
        )
        for form, call in cases:
            with self.subTest(form=form):
                source = authority_source() + call

                # When: Derivation encounters dynamic deletion of authority state.
                # Then: A stable typed dynamic-binding failure rejects the source.
                self.assert_failure(
                    AuthoritySourceError,
                    "authority.source.unsupported",
                    (
                        "authority source has unsupported dynamic binding operation: "
                        "delattr"
                    ),
                    lambda source=source: derive_mapping_from_source(source),
                )

    def test_m1_n_06_single_top_level_annotated_bindings_remain_supported(
        self,
    ) -> None:
        # Given: The five authority symbols use the pinned gcsa AnnAssign shape.
        source = authority_source()
        for symbol in PROVENANCE["symbols"]:
            source = source.replace(
                f"{symbol} =",
                f"{symbol}: Final[object] =",
            )

        # When: Derivation scans exactly one supported top-level binding per symbol.
        mapping = derive_mapping_from_source(source)

        # Then: The existing pinned authority shape still yields the frozen mapping.
        self.assertEqual(EXPECTED_MAPPING, mapping)


if __name__ == "__main__":
    unittest.main()
