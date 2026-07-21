from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT_PATH = (
    Path(__file__).parents[1] / "scripts" / "verify_windows_artifacts.py"
)
SPEC = importlib.util.spec_from_file_location("verify_windows_artifacts", SCRIPT_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load verifier: {SCRIPT_PATH}")
verifier = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(verifier)


class WindowsArtifactVerifierTests(unittest.TestCase):
    def test_release_library_requires_every_x64_object_and_release_crt(self) -> None:
        headers = "\n".join(["8664 machine (x64)"] * 12)
        directives = "/DEFAULTLIB:MSVCRT\n/DEFAULTLIB:OLDNAMES\n"

        result = verifier.verify_library_reports(
            headers, directives, configuration="Release"
        )

        self.assertEqual(result["object_count"], 12)
        self.assertEqual(result["crt"], "MSVCRT")

    def test_debug_library_rejects_release_crt(self) -> None:
        headers = "\n".join(["8664 machine (x64)"] * 12)

        with self.assertRaisesRegex(verifier.VerificationError, "MSVCRTD"):
            verifier.verify_library_reports(
                headers,
                "/DEFAULTLIB:MSVCRT\n",
                configuration="Debug",
            )

    def test_library_rejects_non_x64_object(self) -> None:
        headers = "\n".join(
            ["8664 machine (x64)"] * 11 + ["14C machine (x86)"]
        )

        with self.assertRaisesRegex(verifier.VerificationError, "x64"):
            verifier.verify_library_reports(
                headers,
                "/DEFAULTLIB:MSVCRT\n",
                configuration="Release",
            )

    def test_release_smoke_allows_only_pinned_system_runtime_dependencies(self) -> None:
        report = """
8664 machine (x64)
Image has the following dependencies:
  bcrypt.dll
  KERNEL32.dll
  MSVCP140.dll
  VCRUNTIME140.dll
  VCRUNTIME140_1.dll
  api-ms-win-crt-runtime-l1-1-0.dll

SECTION HEADER #3
"""

        result = verifier.verify_smoke_report(report, configuration="Release")

        self.assertEqual(result["architecture"], "x64")
        self.assertIn("bcrypt.dll", result["dependencies"])

    def test_debug_smoke_rejects_unknown_or_forbidden_dependency(self) -> None:
        report = """
8664 machine (x64)
Image has the following dependencies:
  bcrypt.dll
  KERNEL32.dll
  MSVCP140D.dll
  VCRUNTIME140D.dll
  VCRUNTIME140_1D.dll
  ucrtbased.dll
  lz4.dll

SECTION HEADER #3
"""

        with self.assertRaisesRegex(verifier.VerificationError, "lz4.dll"):
            verifier.verify_smoke_report(report, configuration="Debug")

    def test_smoke_requires_bcrypt_and_configuration_specific_runtime(self) -> None:
        report = """
8664 machine (x64)
Image has the following dependencies:
  KERNEL32.dll
  MSVCP140.dll
  VCRUNTIME140.dll
  VCRUNTIME140_1.dll

SECTION HEADER #3
"""

        with self.assertRaisesRegex(verifier.VerificationError, "bcrypt"):
            verifier.verify_smoke_report(report, configuration="Release")

    def test_atomic_evidence_write_commits_json_without_a_temp_file(self) -> None:
        # Given: A valid evidence document and a writable destination.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = root / "evidence.json"

            # When: Atomic evidence publication completes normally.
            verifier._write_json_atomic(evidence, {"status": "pass"})

            # Then: Only the deterministic committed JSON remains.
            self.assertEqual(
                evidence.read_text(encoding="utf-8"),
                '{\n  "status": "pass"\n}\n',
            )
            self.assertEqual(list(root.glob(".evidence.json.*.tmp")), [])

    def test_atomic_evidence_write_removes_temp_when_fsync_fails(self) -> None:
        # Given: Evidence publication reaches a synthetic fsync failure.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = root / "evidence.json"

            # When: The durable write step raises an operating-system error.
            with mock.patch.object(
                verifier.os,
                "fsync",
                side_effect=OSError("synthetic fsync failure"),
            ), self.assertRaisesRegex(OSError, "synthetic fsync failure"):
                verifier._write_json_atomic(evidence, {"status": "pass"})

            # Then: Neither committed evidence nor its named temp file remains.
            self.assertFalse(evidence.exists())
            self.assertEqual(list(root.glob(".evidence.json.*.tmp")), [])

    def test_atomic_evidence_write_removes_temp_when_stream_io_fails(self) -> None:
        # Given: A named temporary stream that fails during write or flush.
        real_named_temporary_file = verifier.tempfile.NamedTemporaryFile

        for operation in ("write", "flush"):
            with self.subTest(
                operation=operation
            ), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                evidence = root / "evidence.json"

                class StreamFailureContext:
                    def __init__(self, *args: object, **kwargs: object) -> None:
                        self._stream = real_named_temporary_file(*args, **kwargs)

                    def __enter__(self) -> object:
                        setattr(
                            self._stream,
                            operation,
                            mock.Mock(
                                side_effect=OSError(
                                    f"synthetic {operation} failure"
                                )
                            ),
                        )
                        return self._stream

                    def __exit__(self, *args: object) -> None:
                        self._stream.close()

                # When: The selected stream operation raises before rename.
                with mock.patch.object(
                    verifier.tempfile,
                    "NamedTemporaryFile",
                    StreamFailureContext,
                ), self.assertRaisesRegex(
                    OSError, f"synthetic {operation} failure"
                ):
                    verifier._write_json_atomic(evidence, {"status": "pass"})

                # Then: Cleanup removes the uncommitted temp for either failure.
                self.assertFalse(evidence.exists())
                self.assertEqual(list(root.glob(".evidence.json.*.tmp")), [])

    def test_atomic_evidence_write_removes_temp_when_close_fails(self) -> None:
        # Given: A named temporary stream that closes its file and then fails.
        real_named_temporary_file = verifier.tempfile.NamedTemporaryFile

        class CloseFailureContext:
            def __init__(self, *args: object, **kwargs: object) -> None:
                self._stream = real_named_temporary_file(*args, **kwargs)

            def __enter__(self) -> object:
                return self._stream

            def __exit__(self, *args: object) -> None:
                self._stream.close()
                raise OSError("synthetic close failure")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            evidence = root / "evidence.json"

            # When: Context-manager close raises before rename.
            with mock.patch.object(
                verifier.tempfile,
                "NamedTemporaryFile",
                CloseFailureContext,
            ), self.assertRaisesRegex(OSError, "synthetic close failure"):
                verifier._write_json_atomic(evidence, {"status": "pass"})

            # Then: Cleanup still removes the uncommitted temp file.
            self.assertFalse(evidence.exists())
            self.assertEqual(list(root.glob(".evidence.json.*.tmp")), [])


if __name__ == "__main__":
    unittest.main()
