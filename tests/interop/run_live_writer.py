#!/usr/bin/env python3
"""Run a bounded OpenRTDDS-writer to vendor-reader exchange."""

# Requirements: ORT-INT-008
# Verifies: ORT-INT-008

import argparse
import json
import subprocess
import sys
import time
from pathlib import Path


def terminate(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.kill()
    process.wait()


def communicate(process: subprocess.Popen, deadline: float):
    remaining = max(0.0, deadline - time.monotonic())
    try:
        stdout, stderr = process.communicate(timeout=remaining)
        return process.returncode, stdout, stderr, False
    except subprocess.TimeoutExpired:
        process.kill()
        stdout, stderr = process.communicate()
        return process.returncode, stdout, stderr, True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--reader", required=True)
    parser.add_argument("--writer", required=True)
    args = parser.parse_args()

    reader_command = [args.reader, "receive-openrtdds"]
    writer_command = [args.writer]
    deadline = time.monotonic() + 16.0
    reader = None
    writer = None
    reader_code = None
    writer_code = None
    reader_stdout = ""
    reader_stderr = ""
    writer_stdout = ""
    writer_stderr = ""
    reader_timeout = False
    writer_timeout = False
    launch_error = ""
    try:
        reader = subprocess.Popen(
            reader_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True)
        time.sleep(0.3)
        writer = subprocess.Popen(
            writer_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True)
        writer_code, writer_stdout, writer_stderr, writer_timeout = \
            communicate(writer, deadline)
        reader_code, reader_stdout, reader_stderr, reader_timeout = \
            communicate(reader, deadline)
    except OSError as error:
        launch_error = str(error)
    finally:
        if writer is not None:
            terminate(writer)
        if reader is not None:
            terminate(reader)

    evidence = {
        "vendor": args.vendor,
        "package_version": args.version,
        "domain_id": 43,
        "direction": "openrtdds_writer_to_vendor_reader",
        "qos": "best_effort",
        "sample_uint32": 0x4F525444,
        "timeout_seconds": 16,
        "reader_command": reader_command,
        "writer_command": writer_command,
        "reader_exit": reader_code,
        "writer_exit": writer_code,
        "reader_timeout": reader_timeout,
        "writer_timeout": writer_timeout,
        "launch_error": launch_error,
        "reader_stdout": reader_stdout.strip(),
        "reader_stderr": reader_stderr.strip(),
        "writer_stdout": writer_stdout.strip(),
        "writer_stderr": writer_stderr.strip(),
    }
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(
        json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(evidence, sort_keys=True))
    if launch_error or writer_code != 0 or reader_code != 0 or \
            writer_timeout or reader_timeout:
        print("live OpenRTDDS-to-vendor exchange failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
