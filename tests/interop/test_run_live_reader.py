#!/usr/bin/env python3
"""Tests for the bounded live-reader process harness."""

# Verifies: ORT-INT-009, ORT-INT-011

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
HARNESS = HERE / "run_live_reader.py"


class LiveReaderHarnessTests(unittest.TestCase):
    def make_program(self, root: Path, name: str, body: str) -> Path:
        path = root / name
        path.write_text("#!/bin/sh\nset -eu\n" + body, encoding="utf-8")
        path.chmod(path.stat().st_mode | 0o111)
        return path

    def run_harness(
            self, root: Path, writer: Path, reader: Path,
            qos: str = "best_effort"):
        evidence = root / "evidence.json"
        command = [
            sys.executable, str(HARNESS),
            "--vendor", "stubdds",
            "--version", "1.2.3",
            "--evidence", str(evidence),
            "--writer", str(writer),
            "--reader", str(reader),
            "--reader-address", "127.0.0.1",
            "--qos", qos,
        ]
        result = subprocess.run(
            command, text=True, capture_output=True, check=False)
        return result, json.loads(evidence.read_text(encoding="utf-8"))

    def test_success_records_reverse_direction_and_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            writer = self.make_program(
                root, "writer", '[ "$1" = "publish-openrtdds" ]\n'
                'echo "vendor wrote sample"\n')
            reader = self.make_program(
                root, "reader", '[ "$1" = "127.0.0.1" ]\n'
                'echo "OpenRTDDS received sample"\n')
            result, evidence = self.run_harness(root, writer, reader)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(
                evidence["direction"],
                "vendor_writer_to_openrtdds_reader")
            self.assertEqual(evidence["sample_uint32"], 0x4F525444)
            self.assertEqual(evidence["writer_exit"], 0)
            self.assertEqual(evidence["reader_exit"], 0)
            self.assertFalse(evidence["writer_timeout"])
            self.assertFalse(evidence["reader_timeout"])
            self.assertEqual(
                evidence["writer_command"][-1], "publish-openrtdds")
            self.assertEqual(evidence["reader_command"][-1], "127.0.0.1")

    def test_reliable_mode_records_bounded_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            writer = self.make_program(
                root, "writer",
                '[ "$1" = "publish-openrtdds-reliable" ]\n'
                'echo "vendor wrote reliable sample"\n')
            reader = self.make_program(
                root, "reader",
                '[ "$1" = "--reliable" ]\n'
                '[ "$2" = "127.0.0.1" ]\n'
                'echo "OpenRTDDS acknowledged reliable sample"\n')
            result, evidence = self.run_harness(
                root, writer, reader, "reliable")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(evidence["qos"], "reliable")
            self.assertEqual(evidence["timeout_seconds"], 20)
            self.assertEqual(
                evidence["writer_command"][-1],
                "publish-openrtdds-reliable")
            self.assertEqual(evidence["reader_command"][-2], "--reliable")
            self.assertEqual(evidence["writer_exit"], 0)
            self.assertEqual(evidence["reader_exit"], 0)
            self.assertFalse(evidence["writer_timeout"])
            self.assertFalse(evidence["reader_timeout"])

    def test_reader_failure_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            writer = self.make_program(root, "writer", "exit 0\n")
            reader = self.make_program(
                root, "reader", 'echo "reader failed" >&2\nexit 7\n')
            result, evidence = self.run_harness(root, writer, reader)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(evidence["reader_exit"], 7)
            self.assertEqual(evidence["reader_stderr"], "reader failed")

    def test_launch_failure_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reader = self.make_program(root, "reader", "exit 0\n")
            missing_writer = root / "missing-writer"
            result, evidence = self.run_harness(
                root, missing_writer, reader)
            self.assertEqual(result.returncode, 1)
            self.assertNotEqual(evidence["launch_error"], "")
            self.assertIsNone(evidence["writer_exit"])


if __name__ == "__main__":
    unittest.main()
