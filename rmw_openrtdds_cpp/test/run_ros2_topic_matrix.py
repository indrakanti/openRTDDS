#!/usr/bin/env python3
"""Qualify C/C++ best-effort and reliable ROS topic exchange."""

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


def run_pair(listener_executable: str, listener_language: str,
             talker_executable: str, qos: str, domain_id: int) -> bool:
    environment = dict(os.environ)
    environment["RMW_IMPLEMENTATION"] = "rmw_openrtdds_cpp"
    environment["ROS_DOMAIN_ID"] = str(domain_id)
    listener = subprocess.Popen(
        [listener_executable, "listener", qos], env=environment,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    talker: subprocess.Popen[str] | None = None
    try:
        time.sleep(0.4)
        talker = subprocess.Popen(
            [talker_executable, "talker", qos], env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            output, _ = listener.communicate(timeout=12)
        except subprocess.TimeoutExpired:
            stop(listener)
            output = listener.stdout.read() if listener.stdout else ""
            print(output, file=sys.stderr)
            print(f"{listener_language} listener timed out for {qos}",
                  file=sys.stderr)
            return False
        expected = f"OPENRTDDS_ROS2_RECEIVED {listener_language} {qos} 42"
        if listener.returncode != 0 or expected not in output:
            print(output, file=sys.stderr)
            print(f"expected {expected!r}; exit={listener.returncode}",
                  file=sys.stderr)
            return False
        print(output, end="")
        return True
    finally:
        stop(listener)
        if talker is not None:
            stop(talker)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: run_ros2_topic_matrix.py CPP_EXECUTABLE C_EXECUTABLE",
              file=sys.stderr)
        return 2
    cpp_executable, c_executable = sys.argv[1:]
    executables = ((cpp_executable, "cpp"), (c_executable, "c"))
    domain_id = 38
    for qos in ("best_effort", "reliable"):
        for listener_executable, listener_language in executables:
            for talker_executable, _ in executables:
                if not run_pair(listener_executable, listener_language,
                                talker_executable, qos, domain_id):
                    return 1
                domain_id += 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
