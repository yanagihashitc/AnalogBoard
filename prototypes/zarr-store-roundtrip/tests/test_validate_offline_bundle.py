from __future__ import annotations

from contextlib import redirect_stderr
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest import mock
import warnings
import zipfile


SCRIPT_PATH = (
    Path(__file__).parents[1] / "scripts" / "validate_offline_bundle.py"
)
SPEC = importlib.util.spec_from_file_location("validate_offline_bundle", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load validator: {SCRIPT_PATH}")
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)


class BundleValidatorTests(unittest.TestCase):
    def test_destination_is_fixed_to_the_ignored_repository_cache(self) -> None:
        expected = Path(__file__).parents[3] / ".deps" / "p0-s"

        self.assertEqual(
            validator.validate_repository_destination(expected),
            expected.resolve(),
        )
        if hasattr(os, "symlink"):
            with tempfile.TemporaryDirectory() as directory:
                repository_link = Path(directory) / "repository"
                repository_link.symlink_to(
                    Path(__file__).parents[3], target_is_directory=True
                )
                linked_destination = repository_link / ".deps" / "p0-s"
                self.assertEqual(
                    validator.validate_repository_destination(linked_destination),
                    expected.resolve(),
                )
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(
                validator.ValidationError,
                "repository-local",
            ):
                validator.validate_repository_destination(Path(directory))

    def test_cli_normalizes_os_errors_to_the_stable_exit_surface(self) -> None:
        stderr = io.StringIO()
        archive = Path(validator.EXPECTED_ARCHIVE_NAME)

        with mock.patch.object(
            validator,
            "validate_and_extract",
            side_effect=OSError("synthetic read failure"),
        ), redirect_stderr(stderr):
            exit_code = validator.cli(["--archive", str(archive)])

        self.assertEqual(exit_code, 2)
        self.assertEqual(
            stderr.getvalue(),
            "bundle validation failed: synthetic read failure\n",
        )

    def test_missing_bundle_names_expected_archive_and_digest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / validator.EXPECTED_ARCHIVE_NAME

            with self.assertRaisesRegex(
                validator.ValidationError,
                validator.EXPECTED_ARCHIVE_SHA256,
            ) as context:
                validator.validate_archive_identity(missing)

        self.assertIn(validator.EXPECTED_ARCHIVE_NAME, str(context.exception))

    def test_archive_name_is_not_allowed_to_float(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / "renamed.tar.gz"
            archive.write_bytes(b"not the approved archive")

            with self.assertRaisesRegex(
                validator.ValidationError,
                "unexpected dependency archive name",
            ):
                validator.validate_archive_identity(archive)

    def test_archive_digest_mismatch_fails_loud(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            archive = Path(directory) / validator.EXPECTED_ARCHIVE_NAME
            archive.write_bytes(b"wrong")

            with self.assertRaisesRegex(
                validator.ValidationError,
                "dependency archive SHA-256 mismatch",
            ):
                validator.validate_archive_identity(archive)

    def test_member_path_rejects_traversal_absolute_and_backslash(self) -> None:
        for name in ("../escape", "/absolute", "root/../../escape", "root\\file"):
            with self.subTest(name=name):
                member = tarfile.TarInfo(name)
                with self.assertRaisesRegex(
                    validator.ValidationError,
                    "unsafe archive member path",
                ):
                    validator.validate_member(member)

    def test_member_type_rejects_links_and_special_files(self) -> None:
        for member_type in (
            tarfile.SYMTYPE,
            tarfile.LNKTYPE,
            tarfile.CHRTYPE,
            tarfile.BLKTYPE,
            tarfile.FIFOTYPE,
        ):
            with self.subTest(member_type=member_type):
                member = tarfile.TarInfo("root/item")
                member.type = member_type
                with self.assertRaisesRegex(
                    validator.ValidationError,
                    "unsupported archive member type",
                ):
                    validator.validate_member(member)

    def test_manifest_parser_rejects_duplicate_and_noncanonical_paths(self) -> None:
        digest = "0" * 64
        for text in (
            f"{digest}  file.txt\n{digest}  file.txt\n",
            f"{digest}  ../escape\n",
            f"{digest}  ./file.txt\n",
        ):
            with self.subTest(text=text):
                with self.assertRaises(validator.ValidationError):
                    validator.parse_manifest(text)

    def test_tree_verification_rejects_extra_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = root / "payload.txt"
            payload.write_bytes(b"approved")
            (root / "MANIFEST.sha256").write_text("manifest", encoding="utf-8")
            manifest = {
                "payload.txt": hashlib.sha256(payload.read_bytes()).hexdigest()
            }
            (root / "extra.txt").write_text("unexpected", encoding="utf-8")

            with self.assertRaisesRegex(
                validator.ValidationError,
                "unexpected extracted file",
            ):
                validator.verify_extracted_tree(root, manifest)

    def test_tree_verification_rejects_symlinks(self) -> None:
        if not hasattr(os, "symlink"):
            self.skipTest("symlink is unavailable")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "payload.txt"
            target.write_bytes(b"approved")
            (root / "MANIFEST.sha256").write_text("manifest", encoding="utf-8")
            link = root / "link.txt"
            link.symlink_to(target.name)
            manifest = {
                "payload.txt": hashlib.sha256(target.read_bytes()).hexdigest(),
                "link.txt": hashlib.sha256(target.read_bytes()).hexdigest(),
            }

            with self.assertRaisesRegex(
                validator.ValidationError,
                "symlink is forbidden",
            ):
                validator.verify_extracted_tree(root, manifest)

    def test_safe_extract_rejects_a_traversal_archive_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "input.tar.gz"
            destination = root / "extract"
            with tarfile.open(archive, "w:gz") as stream:
                member = tarfile.TarInfo("../escape.txt")
                data = b"escape"
                member.size = len(data)
                stream.addfile(member, io.BytesIO(data))

            with self.assertRaisesRegex(
                validator.ValidationError,
                "unsafe archive member path",
            ):
                validator.safe_extract(archive, destination)

            self.assertFalse((root / "escape.txt").exists())

    def test_safe_extract_rejects_duplicate_tar_members_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "input.tar.gz"
            destination = root / "extract"
            with tarfile.open(archive, "w:gz") as stream:
                for data in (b"first", b"second"):
                    member = tarfile.TarInfo("root/payload.txt")
                    member.size = len(data)
                    stream.addfile(member, io.BytesIO(data))

            with self.assertRaisesRegex(
                validator.ValidationError,
                "duplicate archive member",
            ):
                validator.safe_extract(archive, destination)

            self.assertFalse(destination.exists())

    def test_source_extract_rejects_duplicate_zip_members_before_writing(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = root / "input.zip"
            destination = root / "extract"
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", UserWarning)
                with zipfile.ZipFile(archive, "w") as stream:
                    stream.writestr("root/payload.txt", b"first")
                    stream.writestr("root/payload.txt", b"second")

            with self.assertRaisesRegex(
                validator.ValidationError,
                "duplicate source ZIP entry",
            ):
                validator._safe_extract_zip(archive, destination)

            self.assertFalse(destination.exists())


if __name__ == "__main__":
    unittest.main()
