#!/usr/bin/env python3
"""Capture a vendor best-effort user DATA packet and its discovery chain."""

# Requirements: ORT-INT-006

import argparse
import hashlib
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

from capture_sedp import is_sedp_publication, udp_payload
from capture_spdp import contains_spdp


USER_WRITER_KINDS = (0x02, 0x03)


def data_writer_ids(message: bytes):
    """Return writer IDs from bounded unfragmented DATA submessages."""
    if len(message) < 24 or message[:4] != b"RTPS":
        return []
    writers = []
    offset = 20
    while offset + 4 <= len(message):
        kind, flags = message[offset : offset + 2]
        length = int.from_bytes(message[offset + 2 : offset + 4],
                                "little" if flags & 1 else "big")
        start = offset + 4
        end = start + length if length else (
            start if kind in (0x01, 0x09) else len(message))
        if end > len(message) or end <= offset:
            return []
        if kind == 0x15 and end - start >= 24 and flags & 0x04:
            writers.append(message[start + 8 : start + 12])
        offset = end
    return writers


def user_writer_id(message: bytes):
    """Return the first keyed or unkeyed user DATA writer ID, or None."""
    for writer in data_writer_ids(message):
        if writer[3] in USER_WRITER_KINDS:
            return writer
    return None


def terminate(process: subprocess.Popen) -> None:
    if process.poll() is None:
        process.kill()
    process.wait()


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

    with socket.socket(socket.AF_PACKET, socket.SOCK_DGRAM,
                       socket.htons(0x0800)) as sniffer:
        sniffer.settimeout(0.5)
        subscriber = subprocess.Popen(command + ["subscribe-data"])
        time.sleep(0.3)
        publisher = subprocess.Popen(command + ["publish-data"])
        data_packet = None
        endpoint_packet = None
        participant_packet = None
        sedp_by_prefix = {}
        spdp_by_prefix = {}
        debug_data = []
        deadline = time.monotonic() + 12.0
        try:
            while time.monotonic() < deadline:
                try:
                    ip, _ = sniffer.recvfrom(65_536)
                except socket.timeout:
                    continue
                candidate = udp_payload(ip)
                writers = data_writer_ids(candidate)
                if writers and len(debug_data) < 32:
                    debug_data.append((candidate, writers))
                prefix = candidate[8:20] if len(candidate) >= 20 else None
                if prefix is not None and contains_spdp(candidate):
                    spdp_by_prefix[prefix] = candidate
                if prefix is not None and is_sedp_publication(candidate):
                    sedp_by_prefix[prefix] = candidate
                if user_writer_id(candidate) is not None:
                    data_packet = candidate
                if data_packet is not None:
                    source = data_packet[8:20]
                    endpoint_packet = sedp_by_prefix.get(source)
                    participant_packet = spdp_by_prefix.get(source)
                if endpoint_packet is not None and participant_packet is not None:
                    break
            for process in (publisher, subscriber):
                process.wait(timeout=max(1.0, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            print("vendor data peer timed out", file=sys.stderr)
            return 1
        finally:
            terminate(publisher)
            terminate(subscriber)

    if publisher.returncode != 0 or subscriber.returncode != 0:
        print("vendor data peer failed", file=sys.stderr)
        return 1
    if data_packet is None or endpoint_packet is None or \
            participant_packet is None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        for index, (packet, writers) in enumerate(debug_data):
            writer_names = "-".join(writer.hex() for writer in writers)
            (args.output.parent /
             f"debug-data-{index:02d}-{writer_names}.rtps").write_bytes(packet)
        print("DATA writers seen: " + ", ".join(
            writer.hex() for _, writers in debug_data for writer in writers),
            file=sys.stderr)
        print("SPDP prefixes seen: " + str(len(spdp_by_prefix)) +
              "; SEDP prefixes seen: " + str(len(sedp_by_prefix)),
              file=sys.stderr)
        print("no matched DATA/SEDP/SPDP chain captured", file=sys.stderr)
        return 1

    args.output.parent.mkdir(parents=True, exist_ok=True)
    endpoint_path = args.output.with_name(args.output.stem + "-endpoint.rtps")
    participant_path = args.output.with_name(
        args.output.stem + "-participant.rtps")
    args.output.write_bytes(data_packet)
    endpoint_path.write_bytes(endpoint_packet)
    participant_path.write_bytes(participant_packet)
    manifest = {
        "vendor": args.vendor,
        "package_version": args.version,
        "domain_id": 43,
        "qos": "best_effort",
        "sample_uint32": 0x4F525444,
        "publisher": command + ["publish-data"],
        "subscriber": command + ["subscribe-data"],
        "capture_format": "raw IPv4 UDP payload (.rtps)",
        "byte_count": len(data_packet),
        "sha256": hashlib.sha256(data_packet).hexdigest(),
        "endpoint_byte_count": len(endpoint_packet),
        "endpoint_sha256": hashlib.sha256(endpoint_packet).hexdigest(),
        "participant_byte_count": len(participant_packet),
        "participant_sha256": hashlib.sha256(participant_packet).hexdigest(),
    }
    args.output.with_suffix(".json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
