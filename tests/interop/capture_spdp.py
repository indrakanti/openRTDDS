#!/usr/bin/env python3
"""Capture a vendor's actual RTPS SPDP multicast datagram for CI evidence."""

# Requirements: ORT-INT-001, ORT-INT-002

import argparse
import hashlib
import json
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path

SPDP_WRITER = b"\x00\x01\x00\xc2"
MULTICAST_GROUP = "239.255.0.1"
DOMAIN = 43
PORT = 7400 + DOMAIN * 250


def contains_spdp(datagram: bytes) -> bool:
    if len(datagram) < 24 or datagram[:4] != b"RTPS":
        return False
    offset = 20
    while offset + 4 <= len(datagram):
        kind = datagram[offset]
        flags = datagram[offset + 1]
        size = int.from_bytes(
            datagram[offset + 2 : offset + 4],
            "little" if flags & 1 else "big",
        )
        begin = offset + 4
        if size == 0 and kind not in (0x01, 0x09):
            end = len(datagram)
        else:
            end = begin + size
        if end > len(datagram):
            return False
        if kind == 0x15 and end - begin >= 20:
            if datagram[begin + 8 : begin + 12] == SPDP_WRITER:
                return True
        offset = end
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("vendor participant command required after --")

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
        receiver.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        receiver.bind(("", PORT))
        receiver.setsockopt(
            socket.IPPROTO_IP,
            socket.IP_ADD_MEMBERSHIP,
            struct.pack("4s4s", socket.inet_aton(MULTICAST_GROUP),
                        socket.inet_aton("0.0.0.0")),
        )
        receiver.settimeout(0.5)
        with subprocess.Popen(command) as process:
            datagram = None
            deadline = time.monotonic() + 10.0
            while time.monotonic() < deadline:
                if process.poll() not in (None, 0):
                    break
                try:
                    candidate, _ = receiver.recvfrom(65536)
                except socket.timeout:
                    continue
                if contains_spdp(candidate):
                    datagram = candidate
                    break
            try:
                process.wait(timeout=max(1.0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                print("vendor participant timed out", file=sys.stderr)
                return 1
            if process.returncode != 0:
                print(f"vendor participant exited {process.returncode}",
                      file=sys.stderr)
                return 1

    if datagram is None:
        print(f"no {args.vendor} SPDP DATA captured on "
              f"{MULTICAST_GROUP}:{PORT}", file=sys.stderr)
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(datagram)
    manifest = {
        "vendor": args.vendor,
        "package_version": args.version,
        "domain_id": DOMAIN,
        "multicast_group": MULTICAST_GROUP,
        "multicast_port": PORT,
        "generator": command,
        "capture_format": "raw IPv4 UDP payload (.rtps)",
        "byte_count": len(datagram),
        "sha256": hashlib.sha256(datagram).hexdigest(),
    }
    args.output.with_suffix(".json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(manifest, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
