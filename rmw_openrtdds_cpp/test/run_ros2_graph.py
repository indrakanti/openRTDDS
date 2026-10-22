#!/usr/bin/env python3
"""Qualify bounded ROS graph propagation and lease-expiry removal."""

from __future__ import annotations

import argparse
import os
import selectors
import subprocess
import sys
import time


OBSERVED = "OPENRTDDS_GRAPH_OBSERVED"
REMOVED = "OPENRTDDS_GRAPH_REMOVED"


def wait_for_line(
    process: subprocess.Popen[str], expected: str, timeout: float
) -> list[str]:
    selector = selectors.DefaultSelector()
    assert process.stdout is not None
    selector.register(process.stdout, selectors.EVENT_READ)
    lines: list[str] = []
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            remainder = process.stdout.read()
            if remainder:
                lines.extend(remainder.splitlines())
            break
        for key, _ in selector.select(timeout=0.1):
            line = key.fileobj.readline()
            if not line:
                continue
            value = line.rstrip("\n")
            lines.append(value)
            if expected in value:
                selector.close()
                return lines
    selector.close()
    raise RuntimeError(
        f"timed out waiting for {expected}; output={lines!r}; "
        f"returncode={process.poll()}"
    )


def finish(process: subprocess.Popen[str], timeout: float) -> tuple[int, str]:
    stdout, _ = process.communicate(timeout=timeout)
    return process.returncode, stdout


def run_case(binary: str, crash: bool) -> None:
    environment = os.environ.copy()
    environment["RMW_IMPLEMENTATION"] = "rmw_openrtdds_cpp"
    observer = subprocess.Popen(
        [binary, "observer", "14000"],
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    subject = subprocess.Popen(
        [binary, "subject", "30000" if crash else "3000"],
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
    )
    try:
        observed_output = wait_for_line(observer, OBSERVED, 6.0)
        if crash:
            subject.kill()
            subject.wait(timeout=3.0)
        else:
            subject_code, subject_output = finish(subject, 6.0)
            if subject_code != 0:
                raise RuntimeError(
                    f"subject failed: {subject_code}: {subject_output}"
                )
        removed_output = wait_for_line(observer, REMOVED, 8.0)
        observer_code, remainder = finish(observer, 2.0)
        if observer_code != 0:
            raise RuntimeError(
                f"observer failed: {observer_code}: "
                f"{observed_output + removed_output!r} {remainder}"
            )
    finally:
        for process in (subject, observer):
            if process.poll() is None:
                process.kill()
                process.wait(timeout=3.0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    arguments = parser.parse_args()
    run_case(arguments.binary, crash=False)
    run_case(arguments.binary, crash=True)
    print("OPENRTDDS_ROS2_GRAPH_MATRIX_OK graceful lease_expiry")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
