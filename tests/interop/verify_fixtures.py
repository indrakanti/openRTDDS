#!/usr/bin/env python3
"""Fail CI if a committed vendor packet or its provenance has changed."""

# Requirements: ORT-INT-002
# Verifies: ORT-INT-002

import hashlib
import json
import sys
from pathlib import Path


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


if __name__ == "__main__":
    try:
        verify(Path(__file__).parent / "fixtures")
    except (ValueError, KeyError, OSError, json.JSONDecodeError) as error:
        print(f"vendor fixture verification failed: {error}", file=sys.stderr)
        sys.exit(1)
