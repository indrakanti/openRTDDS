#!/usr/bin/env python3
"""Fail CI if a committed vendor packet or its provenance has changed."""

# Requirements: ORT-INT-002, ORT-INT-004, ORT-INT-005, ORT-INT-006, ORT-INT-007
# Verifies: ORT-INT-002, ORT-INT-004, ORT-INT-005, ORT-INT-006, ORT-INT-007

import hashlib
import json
import sys
from pathlib import Path

from capture_data import source_for_writer, user_writer_id


def verify(root: Path) -> None:
    manifests = sorted(root.glob("*/*/spdp.json"))
    if len(manifests) != 2:
        raise ValueError("expected exactly one Fast DDS and one Cyclone DDS fixture")
    if {path.parent.parent.name for path in manifests} != {"fastdds", "cyclonedds"}:
        raise ValueError("missing one of the pinned vendor fixtures")

    for manifest_path in manifests:
        meta = json.loads(manifest_path.read_text(encoding="utf-8"))
        packet = manifest_path.with_suffix(".rtps").read_bytes()
        if not 20 <= len(packet) <= 65_507 or packet[:4] != b"RTPS":
            raise ValueError(f"invalid RTPS packet: {manifest_path}")
        if meta["vendor"] != manifest_path.parent.parent.name:
            raise ValueError(f"vendor mismatch: {manifest_path}")
        if meta["package_version"] != manifest_path.parent.name:
            raise ValueError(f"package version mismatch: {manifest_path}")
        if meta["domain_id"] != 43 or meta["multicast_port"] != 18150:
            raise ValueError(f"domain/port mismatch: {manifest_path}")
        if meta["capture_format"] != "raw IPv4 UDP payload (.rtps)":
            raise ValueError(f"capture format mismatch: {manifest_path}")
        if meta["byte_count"] != len(packet):
            raise ValueError(f"byte count mismatch: {manifest_path}")
        if meta["sha256"] != hashlib.sha256(packet).hexdigest():
            raise ValueError(f"SHA-256 mismatch: {manifest_path}")
        if not meta.get("generator_source") or not meta.get("capture_run"):
            raise ValueError(f"missing provenance: {manifest_path}")
        print(f"verified {meta['vendor']} {meta['package_version']}: "
              f"{len(packet)} bytes sha256:{meta['sha256']}")

        sedp_manifest = manifest_path.parent / "sedp.json"
        sedp_meta = json.loads(sedp_manifest.read_text(encoding="utf-8"))
        endpoint_packet = sedp_manifest.with_suffix(".rtps").read_bytes()
        participant_packet = (manifest_path.parent /
                              "sedp-participant.rtps").read_bytes()
        if (sedp_meta["vendor"] != meta["vendor"] or \
                sedp_meta["package_version"] != meta["package_version"] or \
                sedp_meta["domain_id"] != 43 or \
                sedp_meta["capture_format"] != meta["capture_format"]):
            raise ValueError(f"SEDP provenance mismatch: {sedp_manifest}")
        for label, payload, size_key, hash_key in (
                ("endpoint", endpoint_packet, "byte_count", "sha256"),
                ("participant", participant_packet, "participant_byte_count",
                 "participant_sha256")):
            if not 20 <= len(payload) <= 65_507 or \
                    payload[:4] != b"RTPS" or \
                    sedp_meta[size_key] != len(payload) or \
                    sedp_meta[hash_key] != hashlib.sha256(payload).hexdigest():
                raise ValueError(f"invalid {label} SEDP fixture: {sedp_manifest}")
        if endpoint_packet[8:20] != participant_packet[8:20]:
            raise ValueError(f"SEDP participant prefix mismatch: {sedp_manifest}")
        if not sedp_meta.get("generator_source") or \
                not sedp_meta.get("capture_run") or \
                not sedp_meta.get("capture_commit"):
            raise ValueError(f"missing SEDP provenance: {sedp_manifest}")
        print(f"verified {meta['vendor']} SEDP participant pair")

        data_manifest = manifest_path.parent / "data-best-effort.json"
        data_meta = json.loads(data_manifest.read_text(encoding="utf-8"))
        data_packet = data_manifest.with_suffix(".rtps").read_bytes()
        data_endpoint = (manifest_path.parent /
                         "data-best-effort-endpoint.rtps").read_bytes()
        data_participant = (manifest_path.parent /
                            "data-best-effort-participant.rtps").read_bytes()
        if (data_meta["vendor"] != meta["vendor"] or
                data_meta["package_version"] != meta["package_version"] or
                data_meta["domain_id"] != 43 or
                data_meta["qos"] != "best_effort" or
                data_meta["sample_uint32"] != 0x4F525444 or
                data_meta["capture_format"] != meta["capture_format"]):
            raise ValueError(f"DATA provenance mismatch: {data_manifest}")
        for label, payload, size_key, hash_key in (
                ("DATA", data_packet, "byte_count", "sha256"),
                ("DATA endpoint", data_endpoint, "endpoint_byte_count",
                 "endpoint_sha256"),
                ("DATA participant", data_participant,
                 "participant_byte_count", "participant_sha256")):
            if not 20 <= len(payload) <= 65_507 or \
                    payload[:4] != b"RTPS" or \
                    data_meta[size_key] != len(payload) or \
                    data_meta[hash_key] != hashlib.sha256(payload).hexdigest():
                raise ValueError(f"invalid {label} fixture: {data_manifest}")
        writer = user_writer_id(data_packet)
        data_source = source_for_writer(data_packet, writer) if writer else None
        endpoint_source = source_for_writer(
            data_endpoint, b"\x00\x00\x03\xc2")
        participant_source = source_for_writer(
            data_participant, b"\x00\x01\x00\xc2")
        if data_source is None or data_source != endpoint_source or \
                data_source != participant_source:
            raise ValueError(f"DATA discovery chain mismatch: {data_manifest}")
        if not data_meta.get("generator_source") or \
                not data_meta.get("capture_run") or \
                not data_meta.get("capture_commit"):
            raise ValueError(f"missing DATA provenance: {data_manifest}")
        print(f"verified {meta['vendor']} best-effort DATA chain")

        reliable_manifest = manifest_path.parent / "data-reliable.json"
        reliable_meta = json.loads(
            reliable_manifest.read_text(encoding="utf-8"))
        if (reliable_meta["vendor"] != meta["vendor"] or
                reliable_meta["package_version"] != meta["package_version"] or
                reliable_meta["domain_id"] != 43 or
                reliable_meta["qos"] != "reliable" or
                reliable_meta["sample_uint32"] != 0x4F525444 or
                reliable_meta["capture_format"] != meta["capture_format"]):
            raise ValueError(
                f"reliable DATA provenance mismatch: {reliable_manifest}")
        reliable_files = (
            ("DATA", "data-reliable.rtps", "byte_count", "sha256"),
            ("publisher endpoint", "data-reliable-publisher-endpoint.rtps",
             "publisher_endpoint_byte_count", "publisher_endpoint_sha256"),
            ("publisher participant",
             "data-reliable-publisher-participant.rtps",
             "publisher_participant_byte_count",
             "publisher_participant_sha256"),
            ("HEARTBEAT", "data-reliable-heartbeat.rtps",
             "heartbeat_byte_count", "heartbeat_sha256"),
            ("ACKNACK", "data-reliable-acknack.rtps",
             "acknack_byte_count", "acknack_sha256"),
            ("subscriber endpoint", "data-reliable-subscriber-endpoint.rtps",
             "subscriber_endpoint_byte_count",
             "subscriber_endpoint_sha256"),
            ("subscriber participant",
             "data-reliable-subscriber-participant.rtps",
             "subscriber_participant_byte_count",
             "subscriber_participant_sha256"),
        )
        for label, filename, size_key, hash_key in reliable_files:
            payload = (manifest_path.parent / filename).read_bytes()
            if not 20 <= len(payload) <= 65_507 or \
                    payload[:4] != b"RTPS" or \
                    reliable_meta[size_key] != len(payload) or \
                    reliable_meta[hash_key] != hashlib.sha256(payload).hexdigest():
                raise ValueError(
                    f"invalid reliable {label} fixture: {reliable_manifest}")
        if not reliable_meta.get("generator_source") or \
                not reliable_meta.get("capture_run") or \
                not reliable_meta.get("capture_commit"):
            raise ValueError(
                f"missing reliable DATA provenance: {reliable_manifest}")
        print(f"verified {meta['vendor']} reliable DATA control chain")


if __name__ == "__main__":
    try:
        verify(Path(__file__).parent / "fixtures")
    except (ValueError, KeyError, OSError, json.JSONDecodeError) as error:
        print(f"vendor fixture verification failed: {error}", file=sys.stderr)
        sys.exit(1)
