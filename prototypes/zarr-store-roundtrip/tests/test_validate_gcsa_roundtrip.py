from __future__ import annotations

import importlib.util
import sys
import tempfile
import types
import unittest
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
    AeadChunkContext=object,
    decrypt_chunk=lambda *args, **kwargs: b"",
)
schema = _module(
    "gcsa.store.schema",
    ARRAY_WIRE_CANDIDATES={
        "pulse_features": _Candidate(2, (24,)),
        "gmi_waveform": _Candidate(3, (5, 2400)),
        "fl_waveform": _Candidate(3, (8, 2400)),
    },
    FL_CHANNEL_ORDER=(),
    GMI_CHANNEL_ORDER=(),
    PULSE_FEATURE_COLUMNS=(),
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
                    Path("/ignored/finalized"),
                    validator.Checks(),
                )

        # Then: It stops on identity before either store tree is inspected.
        self.assertIs(raised.exception, identity_failure)
        require_identity.assert_called_once_with(Path("/unapproved/gcsa"))
        tree_digest.assert_not_called()


if __name__ == "__main__":
    unittest.main()
