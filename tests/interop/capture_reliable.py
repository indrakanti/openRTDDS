#!/usr/bin/env python3
"""Capture a vendor reliable DATA and its discovery/control chain."""

# Requirements: ORT-INT-007

import argparse
import hashlib
import json
import socket
import subprocess
import sys
import time
from pathlib import Path

from capture_data import (data_writer_ids, routed_submessages,
                          source_for_writer, terminate, user_writer_id)
from capture_sedp import is_sedp_publication, udp_payload
from capture_spdp import contains_spdp


HEARTBEAT = 0x07
ACKNACK = 0x06
PUBLICATIONS_WRITER = b"\x00\x00\x03\xc2"
SUBSCRIPTIONS_WRITER = b"\x00\x00\x04\xc2"
SPDP_WRITER = b"\x00\x01\x00\xc2"
USER_READER_KINDS = (0x04, 0x07)


def reliability_controls(message: bytes):
    """Return bounded HEARTBEAT/ACKNACK identities and effective sources."""
    controls = []
    for kind, flags, content, source in routed_submessages(message):
        if kind == HEARTBEAT and len(content) == 28:
            order = "little" if flags & 1 else "big"
            first = ((int.from_bytes(content[8:12], order, signed=True) << 32) |
                     int.from_bytes(content[12:16], order))
            last = ((int.from_bytes(content[16:20], order, signed=True) << 32) |
                    int.from_bytes(content[20:24], order))
            if first > 0 and last >= first - 1:
                controls.append((kind, content[0:4], content[4:8], source,
                                 flags, first, last))
        elif kind == ACKNACK and len(content) >= 24:
            order = "little" if flags & 1 else "big"
            high = int.from_bytes(content[8:12], order, signed=True)
            base = (high << 32) | int.from_bytes(content[12:16], order)
            bits = int.from_bytes(content[16:20], order)
            words = (bits + 31) // 32
            if base > 0 and bits <= 256 and len(content) == 24 + words * 4:
                controls.append((kind, content[0:4], content[4:8], source,
                                 flags, base, bits))
    return controls


def is_sedp_subscription(message: bytes) -> bool:
    return source_for_writer(message, SUBSCRIPTIONS_WRITER) is not None


def data_sequence(message: bytes, expected_writer: bytes):
    """Return a positive user DATA sequence number for one writer."""
    for kind, flags, content, _ in routed_submessages(message):
        if kind != 0x15 or len(content) < 20 or content[8:12] != expected_writer:
            continue
        order = "little" if flags & 1 else "big"
        high = int.from_bytes(content[12:16], order, signed=True)
        low = int.from_bytes(content[16:20], order)
        sequence = (high << 32) | low
        return sequence if sequence > 0 else None
    return None


def digest(path: Path, payload: bytes, manifest: dict, prefix: str) -> None:
    path.write_bytes(payload)
    manifest[prefix + "byte_count"] = len(payload)
    manifest[prefix + "sha256"] = hashlib.sha256(payload).hexdigest()


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
        publisher = subprocess.Popen(command + ["publish-reliable"])
        time.sleep(0.3)
        subscriber = subprocess.Popen(command + ["subscribe-reliable"])
        spdp = {}
        publications = {}
        subscriptions = {}
        heartbeats = []
        acknacks = []
        data_packet = None
        data_source = None
        data_writer = None
        sequence = None
        chain = None
        debug_packets = []
        deadline = time.monotonic() + 16.0
        try:
            while time.monotonic() < deadline:
                try:
                    ip, _ = sniffer.recvfrom(65_536)
                except socket.timeout:
                    continue
                candidate = udp_payload(ip)
                if candidate[:4] != b"RTPS":
                    continue
                writers = data_writer_ids(candidate)
                controls = reliability_controls(candidate)
                if (writers or controls) and len(debug_packets) < 48:
                    debug_packets.append(candidate)
                if contains_spdp(candidate):
                    prefix = source_for_writer(candidate, SPDP_WRITER)
                    if prefix is not None:
                        spdp[prefix] = candidate
                if is_sedp_publication(candidate):
                    prefix = source_for_writer(candidate, PUBLICATIONS_WRITER)
                    if prefix is not None:
                        publications[prefix] = candidate
                if is_sedp_subscription(candidate):
                    prefix = source_for_writer(candidate, SUBSCRIPTIONS_WRITER)
                    if prefix is not None:
                        subscriptions[prefix] = candidate
                user_writer = user_writer_id(candidate)
                if user_writer is not None:
                    data_packet = candidate
                    data_writer = user_writer
                    data_source = source_for_writer(candidate, user_writer)
                    sequence = data_sequence(candidate, user_writer)
                for control in controls:
                    if control[0] == HEARTBEAT:
                        heartbeats.append((candidate, control))
                    else:
                        acknacks.append((candidate, control))
                if data_packet is not None and sequence is not None:
                    heartbeat_packet = next((packet for packet, control in heartbeats
                        if control[2] == data_writer and
                           control[3] == data_source and
                           control[5] <= sequence <= control[6]), None)
                    for ack_packet, control in acknacks:
                        _, reader, writer, subscriber_source, _, _, _ = control
                        if writer != data_writer or \
                                reader[3] not in USER_READER_KINDS:
                            continue
                        candidate_chain = (
                            data_packet, publications.get(data_source),
                            spdp.get(data_source), heartbeat_packet, ack_packet,
                            subscriptions.get(subscriber_source),
                            spdp.get(subscriber_source))
                        if all(item is not None for item in candidate_chain):
                            chain = candidate_chain
                            break
                if chain is not None:
                    break
            for process in (publisher, subscriber):
                process.wait(timeout=max(1.0, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            print("vendor reliable peer timed out", file=sys.stderr)
            return 1
        finally:
            terminate(publisher)
            terminate(subscriber)

    if publisher.returncode != 0 or subscriber.returncode != 0:
        print("vendor reliable peer failed", file=sys.stderr)
        return 1
    if chain is None:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        for index, packet in enumerate(debug_packets):
            (args.output.parent / f"debug-reliable-{index:02d}.rtps").write_bytes(packet)
        print("incomplete reliable DATA/discovery/control chain", file=sys.stderr)
        print(f"SPDP={len(spdp)} publications={len(publications)} "
              f"subscriptions={len(subscriptions)} HEARTBEAT={len(heartbeats)} "
              f"ACKNACK={len(acknacks)}", file=sys.stderr)
        return 1

    (data_packet, publisher_endpoint, publisher_participant,
     heartbeat_packet, acknack_packet, subscriber_endpoint,
     subscriber_participant) = chain
    args.output.parent.mkdir(parents=True, exist_ok=True)
    stem = args.output.stem
    manifest = {
        "vendor": args.vendor,
        "package_version": args.version,
        "domain_id": 43,
        "qos": "reliable",
        "sample_uint32": 0x4F525444,
        "publisher": command + ["publish-reliable"],
        "subscriber": command + ["subscribe-reliable"],
        "capture_format": "raw IPv4 UDP payload (.rtps)",
    }
    digest(args.output, data_packet, manifest, "")
    for suffix, payload, prefix in (
            ("-publisher-endpoint.rtps", publisher_endpoint,
             "publisher_endpoint_"),
            ("-publisher-participant.rtps", publisher_participant,
             "publisher_participant_"),
            ("-heartbeat.rtps", heartbeat_packet, "heartbeat_"),
            ("-acknack.rtps", acknack_packet, "acknack_"),
            ("-subscriber-endpoint.rtps", subscriber_endpoint,
             "subscriber_endpoint_"),
            ("-subscriber-participant.rtps", subscriber_participant,
             "subscriber_participant_")):
        digest(args.output.with_name(stem + suffix), payload, manifest, prefix)
    args.output.with_suffix(".json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
