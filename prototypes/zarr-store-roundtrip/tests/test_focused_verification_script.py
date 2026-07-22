from __future__ import annotations

import subprocess
import unittest
from pathlib import Path


SCRIPT_PATH = (
    Path(__file__).parents[3]
    / "scripts/zarr-roundtrip/run-focused-verification.sh"
)
EXPECTED_IMAGE = (
    "sha256:e65e9f8b0ffafef5b5d2b9711c9a3411649ae80fd036cc79f0febb80b4c0b06e"
)


class FreshGcsaRunnerTests(unittest.TestCase):
    def test_python_runs_in_a_fresh_immutable_reader_container(self) -> None:
        # Given: A fake Docker command that exposes every argument from the
        # focused verification wrapper without creating a container.
        command = r'''
source "$1"
docker() { printf '<%s>\n' "$@"; }
run_gcsa_python -c 'print("accepted")'
'''

        # When: The wrapper prepares one accepted-reader Python invocation.
        completed = subprocess.run(
            ["bash", "-c", command, "bash", str(SCRIPT_PATH)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=10,
        )

        # Then: It creates a disposable, networkless, read-only container from
        # the exact image and exposes only a read-only repository plus /tmp.
        self.assertEqual(completed.returncode, 0, completed.stderr)
        arguments = [
            line[1:-1]
            for line in completed.stdout.splitlines()
            if line.startswith("<") and line.endswith(">")
        ]
        repository_root = SCRIPT_PATH.parents[2]
        self.assertEqual(arguments[0], "run")
        self.assertIn("--rm", arguments)
        self.assertIn("--pull", arguments)
        self.assertEqual(arguments[arguments.index("--pull") + 1], "never")
        self.assertIn("--read-only", arguments)
        self.assertIn("--network", arguments)
        self.assertEqual(arguments[arguments.index("--network") + 1], "none")
        self.assertIn("--security-opt", arguments)
        self.assertEqual(
            arguments[arguments.index("--security-opt") + 1],
            "no-new-privileges",
        )
        self.assertIn("--tmpfs", arguments)
        self.assertEqual(arguments.count("--tmpfs"), 1)
        self.assertEqual(
            arguments[arguments.index("--tmpfs") + 1],
            "/tmp:rw,nosuid,nodev,size=64m",
        )
        self.assertIn("--volume", arguments)
        self.assertEqual(arguments.count("--volume"), 1)
        self.assertEqual(
            arguments[arguments.index("--volume") + 1],
            f"{repository_root}:/home/jupyter/AnalogBoard:ro",
        )
        self.assertEqual(arguments[arguments.index("--entrypoint") + 1], "python")
        self.assertIn(EXPECTED_IMAGE, arguments)
        self.assertEqual(arguments[-2:], ["-c", 'print("accepted")'])


if __name__ == "__main__":
    unittest.main()
