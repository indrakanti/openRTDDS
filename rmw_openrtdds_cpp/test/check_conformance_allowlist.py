#!/usr/bin/env python3
"""Validate the reviewed Jazzy conformance-to-local-evidence mapping."""

from __future__ import annotations

import argparse
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("allowlist", type=Path)
    parser.add_argument("cmake", type=Path)
    args = parser.parse_args()

    cmake = args.cmake.read_text(encoding="utf-8")
    entries: set[tuple[str, str]] = set()
    for number, raw in enumerate(
        args.allowlist.read_text(encoding="utf-8").splitlines(), start=1
    ):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = tuple(item.strip() for item in line.split("|"))
        if len(parts) != 2 or not parts[0].endswith(".cpp"):
            raise ValueError(f"invalid allowlist entry on line {number}: {raw}")
        if parts in entries:
            raise ValueError(f"duplicate allowlist entry on line {number}: {raw}")
        upstream, local_test = parts
        if f"add_test(NAME {local_test} " not in cmake:
            raise ValueError(
                f"{upstream} maps to unregistered local test {local_test}"
            )
        entries.add(parts)

    required = {"test_event.cpp", "test_wait_set.cpp"}
    present = {upstream for upstream, _ in entries}
    missing = sorted(required - present)
    if missing:
        raise ValueError(f"required Jazzy suites absent from allowlist: {missing}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
