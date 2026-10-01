#!/usr/bin/env python3
"""Check the explicitly implemented RMW ABI slice in a shared library."""

from __future__ import annotations

import argparse
import subprocess
from pathlib import Path


def expected_symbols(path: Path) -> set[str]:
    return {
        line.strip()
        for line in path.read_text(encoding="utf-8").splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    }


def exported_symbols(path: Path) -> set[str]:
    result = subprocess.run(
        ["nm", "-D", "--defined-only", str(path)],
        check=True,
        capture_output=True,
        text=True,
    )
    return {line.split()[-1] for line in result.stdout.splitlines() if line.split()}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("library", type=Path)
    parser.add_argument("expected", type=Path)
    args = parser.parse_args()

    missing = sorted(expected_symbols(args.expected) - exported_symbols(args.library))
    if missing:
        print("missing RMW symbols:")
        for symbol in missing:
            print(f"  {symbol}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
