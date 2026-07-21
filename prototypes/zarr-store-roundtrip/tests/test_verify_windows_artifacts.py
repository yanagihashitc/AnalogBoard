from __future__ import annotations

import importlib.util
from pathlib import Path
import unittest


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


if __name__ == "__main__":
    unittest.main()
