#!/usr/bin/env python3
"""Run a bounded vendor-writer to OpenRTDDS-reader exchange."""

# Requirements: ORT-INT-009
# Verifies: ORT-INT-009

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
    parser.add_argument("--writer", required=True)
    parser.add_argument("--reader", required=True)
    parser.add_argument("--reader-address")
    args = parser.parse_args()

    writer_command = [args.writer, "publish-openrtdds"]
    reader_command = [args.reader]
    if args.reader_address:
        reader_command.append(args.reader_address)
    deadline = time.monotonic() + 16.0
    writer = None
    reader = None
    writer_code = None
    reader_code = None
    writer_stdout = ""
    writer_stderr = ""
    reader_stdout = ""
    reader_stderr = ""
    writer_timeout = False
    reader_timeout = False
    launch_error = ""
    try:
        reader = subprocess.Popen(
            reader_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True)
        time.sleep(0.3)
        writer = subprocess.Popen(
            writer_command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            text=True)
        reader_code, reader_stdout, reader_stderr, reader_timeout = \
            communicate(reader, deadline)
        writer_code, writer_stdout, writer_stderr, writer_timeout = \
            communicate(writer, deadline)
    except OSError as error:
        launch_error = str(error)
    finally:
        if reader is not None:
            terminate(reader)
        if writer is not None:
            terminate(writer)

    evidence = {
        "vendor": args.vendor,
        "package_version": args.version,
        "domain_id": 43,
        "direction": "vendor_writer_to_openrtdds_reader",
        "qos": "best_effort",
        "sample_uint32": 0x4F525444,
        "timeout_seconds": 16,
        "writer_command": writer_command,
        "reader_command": reader_command,
        "writer_exit": writer_code,
        "reader_exit": reader_code,
        "writer_timeout": writer_timeout,
        "reader_timeout": reader_timeout,
        "launch_error": launch_error,
        "writer_stdout": writer_stdout.strip(),
        "writer_stderr": writer_stderr.strip(),
        "reader_stdout": reader_stdout.strip(),
        "reader_stderr": reader_stderr.strip(),
    }
    args.evidence.parent.mkdir(parents=True, exist_ok=True)
    args.evidence.write_text(
        json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(evidence, sort_keys=True))
    if launch_error or writer_code != 0 or reader_code != 0 or \
            writer_timeout or reader_timeout:
        print("live vendor-to-OpenRTDDS exchange failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
