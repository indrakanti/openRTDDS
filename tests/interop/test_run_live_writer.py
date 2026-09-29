#!/usr/bin/env python3
"""Tests for the bounded live-writer process harness."""

# Verifies: ORT-INT-008, ORT-INT-010

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


HERE = Path(__file__).resolve().parent
HARNESS = HERE / "run_live_writer.py"


class LiveWriterHarnessTests(unittest.TestCase):
    def make_program(self, root: Path, name: str, body: str) -> Path:
        path = root / name
        path.write_text("#!/bin/sh\nset -eu\n" + body, encoding="utf-8")
        path.chmod(path.stat().st_mode | 0o111)
        return path

    def run_harness(
            self, root: Path, reader: Path, writer: Path,
            qos: str = "best_effort"):
        evidence = root / "evidence.json"
        command = [
            sys.executable, str(HARNESS),
            "--vendor", "stubdds",
            "--version", "1.2.3",
            "--evidence", str(evidence),
            "--reader", str(reader),
            "--writer", str(writer),
            "--writer-address", "127.0.0.1",
            "--qos", qos,
        ]
        result = subprocess.run(
            command, text=True, capture_output=True, check=False)
        return result, json.loads(evidence.read_text(encoding="utf-8"))

    def test_reliable_mode_records_bounded_commands(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reader = self.make_program(
                root, "reader",
                '[ "$1" = "receive-openrtdds-reliable" ]\n'
                'echo "vendor received reliable sample"\n')
            writer = self.make_program(
                root, "writer",
                '[ "$1" = "--reliable" ]\n'
                '[ "$2" = "127.0.0.1" ]\n'
                'echo "OpenRTDDS reliable delivery acknowledged"\n')
            result, evidence = self.run_harness(
                root, reader, writer, "reliable")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(evidence["qos"], "reliable")
            self.assertEqual(evidence["timeout_seconds"], 20)
            self.assertEqual(
                evidence["reader_command"][-1],
                "receive-openrtdds-reliable")
            self.assertEqual(evidence["writer_command"][-2], "--reliable")
            self.assertEqual(evidence["reader_exit"], 0)
            self.assertEqual(evidence["writer_exit"], 0)
            self.assertFalse(evidence["reader_timeout"])
            self.assertFalse(evidence["writer_timeout"])

    def test_best_effort_mode_remains_default(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reader = self.make_program(
                root, "reader",
                '[ "$1" = "receive-openrtdds" ]\n')
            writer = self.make_program(
                root, "writer",
                '[ "$1" = "127.0.0.1" ]\n')
            result, evidence = self.run_harness(root, reader, writer)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(evidence["qos"], "best_effort")
            self.assertEqual(evidence["timeout_seconds"], 16)
            self.assertNotIn("--reliable", evidence["writer_command"])

    def test_writer_failure_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            reader = self.make_program(root, "reader", "exit 0\n")
            writer = self.make_program(
                root, "writer", 'echo "writer failed" >&2\nexit 15\n')
            result, evidence = self.run_harness(
                root, reader, writer, "reliable")
            self.assertEqual(result.returncode, 1)
            self.assertEqual(evidence["writer_exit"], 15)
            self.assertEqual(evidence["writer_stderr"], "writer failed")

    def test_launch_failure_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            missing_reader = root / "missing-reader"
            writer = self.make_program(root, "writer", "exit 0\n")
            result, evidence = self.run_harness(
                root, missing_reader, writer, "reliable")
            self.assertEqual(result.returncode, 1)
            self.assertNotEqual(evidence["launch_error"], "")
            self.assertIsNone(evidence["reader_exit"])


if __name__ == "__main__":
    unittest.main()
