#!/usr/bin/env python3
"""Capture outbound vendor SEDP DATA without binding a vendor-owned UDP port."""

# Requirements: ORT-INT-005

import argparse
import hashlib
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

from capture_spdp import contains_spdp


def udp_payload(ip: bytes) -> bytes:
    if len(ip) < 28 or ip[0] >> 4 != 4 or ip[9] != 17:
        return b""
    ihl = (ip[0] & 0x0F) * 4
    total = int.from_bytes(ip[2:4], "big")
    if ihl < 20 or total > len(ip) or total < ihl + 8 or \
            int.from_bytes(ip[6:8], "big") & 0x1FFF:
        return b""
    udp_length = int.from_bytes(ip[ihl + 4 : ihl + 6], "big")
    if udp_length < 8 or ihl + udp_length > total:
        return b""
    return ip[ihl + 8 : ihl + udp_length]


def is_sedp_publication(message: bytes) -> bool:
    if len(message) < 24 or message[:4] != b"RTPS":
        return False
    offset = 20
    while offset + 4 <= len(message):
        kind, flags = message[offset : offset + 2]
        length = int.from_bytes(message[offset + 2 : offset + 4],
                                "little" if flags & 1 else "big")
        start = offset + 4
        end = start + length if length else (
            start if kind in (0x01, 0x09) else len(message))
        if end > len(message):
            return False
        if kind == 0x15 and end - start >= 20 and \
                message[start + 8 : start + 12] == b"\x00\x00\x03\xc2":
            return True
        if end <= offset:
            return False
        offset = end
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vendor", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("vendor command required")
    # AF_PACKET/SOCK_DGRAM returns an IPv4 packet without a link header.
    # CAP_NET_RAW is granted by sudo on the CI runner. Capture does not
    # compete with the vendor's UDP sockets for unicast SEDP packets.
    with socket.socket(socket.AF_PACKET, socket.SOCK_DGRAM,
                       socket.htons(0x0800)) as sniffer:
        sniffer.settimeout(0.5)
        with subprocess.Popen(command + ["publish"]) as publisher:
            time.sleep(0.3)
            with subprocess.Popen(command) as peer:
                packet = None
                participant_packet = None
                spdp_by_prefix = {}
                deadline = time.monotonic() + 10.0
                while time.monotonic() < deadline:
                    try:
                        ip, _ = sniffer.recvfrom(65536)
                    except socket.timeout:
                        continue
                    candidate = udp_payload(ip)
                    if contains_spdp(candidate):
                        spdp_by_prefix[candidate[8:20]] = candidate
                    if is_sedp_publication(candidate):
                        packet = candidate
                    if packet is not None:
                        participant_packet = spdp_by_prefix.get(packet[8:20])
                    if packet is not None and participant_packet is not None:
                        break
                if packet is None or participant_packet is None:
                    print("no matched SEDP/SPDP publication pair captured",
                          file=sys.stderr)
                try:
                    publisher.wait(timeout=10)
                    peer.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    publisher.kill()
                    peer.kill()
                    print("vendor peer timed out", file=sys.stderr)
                    return 1
                if publisher.returncode != 0 or peer.returncode != 0:
                    print("vendor peer failed", file=sys.stderr)
                    return 1
    if packet is None or participant_packet is None:
        return 1
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(packet)
    participant_path = args.output.with_name(
        args.output.stem + "-participant.rtps")
    participant_path.write_bytes(participant_packet)
    manifest = {
        "vendor": args.vendor,
        "package_version": args.version,
        "domain_id": 43,
        "generator": command + ["publish"],
        "peer": command,
        "capture_format": "raw IPv4 UDP payload (.rtps)",
        "byte_count": len(packet),
        "sha256": hashlib.sha256(packet).hexdigest(),
        "participant_byte_count": len(participant_packet),
        "participant_sha256": hashlib.sha256(participant_packet).hexdigest(),
    }
    args.output.with_suffix(".json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
