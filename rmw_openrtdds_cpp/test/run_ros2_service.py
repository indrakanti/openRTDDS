#!/usr/bin/env python3
"""Run the bounded two-process rclcpp service qualification case."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    arguments = parser.parse_args()
    environment = os.environ.copy()
    environment["RMW_IMPLEMENTATION"] = "rmw_openrtdds_cpp"
    server = subprocess.Popen(
        [arguments.binary, "server"],
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    client: subprocess.Popen[str] | None = None
    try:
        time.sleep(0.3)
        client = subprocess.Popen(
            [arguments.binary, "client"],
            env=environment,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        client_output, _ = client.communicate(timeout=12.0)
        if client.returncode != 0 or "OPENRTDDS_CLIENT_RESPONSE 42" not in client_output:
            raise RuntimeError(
                f"client failed: {client.returncode}: {client_output}"
            )
        print("OPENRTDDS_ROS2_SERVICE_OK")
        return 0
    finally:
        for process in (client, server):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2.0)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2.0)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (RuntimeError, subprocess.TimeoutExpired) as error:
        print(error, file=sys.stderr)
        raise SystemExit(1)
