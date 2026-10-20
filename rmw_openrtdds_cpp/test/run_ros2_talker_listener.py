#!/usr/bin/env python3
"""Run the bounded ROS 2 best-effort talker/listener qualification pair."""

from __future__ import annotations

import os
import subprocess
import sys
import time


def stop(process: subprocess.Popen[str]) -> None:
    if process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=3)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: run_ros2_talker_listener.py EXECUTABLE", file=sys.stderr)
        return 2
    environment = dict(os.environ)
    environment["RMW_IMPLEMENTATION"] = "rmw_openrtdds_cpp"
    environment["ROS_DOMAIN_ID"] = "38"
    listener = subprocess.Popen(
        [sys.argv[1], "listener"],
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    talker: subprocess.Popen[str] | None = None
    try:
        time.sleep(0.5)
        talker = subprocess.Popen(
            [sys.argv[1], "talker"],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            listener_output, _ = listener.communicate(timeout=12)
        except subprocess.TimeoutExpired:
            stop(listener)
            listener_output = listener.stdout.read() if listener.stdout else ""
            print(listener_output, file=sys.stderr)
            print("listener timed out", file=sys.stderr)
            return 1
        if listener.returncode != 0 or "OPENRTDDS_ROS2_RECEIVED 42" not in listener_output:
            print(listener_output, file=sys.stderr)
            print(f"listener exited with {listener.returncode}", file=sys.stderr)
            return 1
        print(listener_output, end="")
        return 0
    finally:
        stop(listener)
        if talker is not None:
            stop(talker)


if __name__ == "__main__":
    raise SystemExit(main())
