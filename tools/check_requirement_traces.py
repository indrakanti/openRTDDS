#!/usr/bin/env python3
"""Validate OpenRTDDS requirement definitions and source evidence."""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
REQUIREMENTS_ROOT = ROOT / "docs" / "requirements"
DEFINITION = re.compile(r"^### (ORT-[A-Z]+-\d{3})\b", re.MULTILINE)
REFERENCE = re.compile(r"\bORT-[A-Z]+-\d{3}\b")
STATUS = re.compile(r"^\*\*Status:\*\*\s+([A-Za-z]+)", re.MULTILINE)
VERIFICATION = re.compile(
    r"^\*\*Verification:\*\*\s+([^\n]+)", re.MULTILINE
)
ALLOWED_STATUSES = {"Draft", "Approved", "Implemented", "Verified", "Deprecated"}
ALLOWED_METHODS = {"Test", "Demonstration", "Analysis", "Inspection"}


@dataclass(frozen=True)
class Requirement:
    identifier: str
    status: str
    verification: frozenset[str]
    source: Path


def relative(path: Path) -> str:
    return path.relative_to(ROOT).as_posix()


def load_requirements() -> tuple[dict[str, Requirement], list[str]]:
    requirements: dict[str, Requirement] = {}
    errors: list[str] = []
    for path in sorted(REQUIREMENTS_ROOT.glob("*/requirements.md")):
        text = path.read_text(encoding="utf-8")
        matches = list(DEFINITION.finditer(text))
        for index, match in enumerate(matches):
            identifier = match.group(1)
            end = matches[index + 1].start() if index + 1 < len(matches) else len(text)
            record = text[match.end() : end]
            status_match = STATUS.search(record)
            verification_match = VERIFICATION.search(record)
            if identifier in requirements:
                errors.append(
                    f"duplicate definition {identifier}: {relative(path)} and "
                    f"{relative(requirements[identifier].source)}"
                )
                continue
            if status_match is None:
                errors.append(f"{identifier} has no Status in {relative(path)}")
                continue
            if verification_match is None:
                errors.append(
                    f"{identifier} has no Verification method in {relative(path)}"
                )
                continue
            methods = frozenset(
                item.strip()
                for item in verification_match.group(1).split(",")
                if item.strip()
            )
            status = status_match.group(1)
            if status not in ALLOWED_STATUSES:
                errors.append(f"{identifier} has unknown status {status}")
            unknown_methods = methods - ALLOWED_METHODS
            if unknown_methods:
                errors.append(
                    f"{identifier} has unknown verification method(s): "
                    f"{', '.join(sorted(unknown_methods))}"
                )
            if re.search(r"\bshall\b", record) is None:
                errors.append(f"{identifier} has no normative shall statement")
            requirements[identifier] = Requirement(
                identifier, status, methods, path
            )
    if not requirements:
        errors.append("no requirements found")
    return requirements, errors


def collect_references(directory: str, tag: str) -> dict[str, set[str]]:
    references: dict[str, set[str]] = {}
    root = ROOT / directory
    if not root.exists():
        return references
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        tagged_line = re.compile(rf"{re.escape(tag)}:[^\n]*")
        for line in tagged_line.findall(text):
            for identifier in REFERENCE.findall(line):
                references.setdefault(identifier, set()).add(relative(path))
    return references


def main() -> int:
    requirements, errors = load_requirements()
    code = collect_references("include", "Requirements")
    for identifier, paths in collect_references("src", "Requirements").items():
        code.setdefault(identifier, set()).update(paths)
    tests = collect_references("tests", "Verifies")
    examples = collect_references("examples", "Demonstrates")

    known = set(requirements)
    matrix_path = REQUIREMENTS_ROOT / "traceability.md"
    matrix_ids = set(REFERENCE.findall(matrix_path.read_text(encoding="utf-8")))
    for identifier in sorted(known - matrix_ids):
        errors.append(f"{identifier} is missing from the traceability matrix")
    for identifier in sorted(matrix_ids - known):
        errors.append(f"unknown requirement {identifier} in traceability matrix")
    for evidence_name, evidence in (
        ("code", code),
        ("test", tests),
        ("example", examples),
    ):
        for identifier, paths in evidence.items():
            if identifier not in known:
                errors.append(
                    f"unknown requirement {identifier} in {evidence_name}: "
                    f"{', '.join(sorted(paths))}"
                )

    enforced = {"Implemented", "Verified"}
    for identifier, requirement in sorted(requirements.items()):
        if requirement.status not in enforced:
            continue
        if identifier not in code:
            errors.append(f"{identifier} has no implementation trace")
        if "Test" in requirement.verification and identifier not in tests:
            errors.append(f"{identifier} declares Test but has no test trace")
        if (
            "Demonstration" in requirement.verification
            and identifier not in examples
        ):
            errors.append(
                f"{identifier} declares Demonstration but has no example trace"
            )

    if errors:
        print("requirement trace check failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1

    status_counts: dict[str, int] = {}
    for requirement in requirements.values():
        status_counts[requirement.status] = (
            status_counts.get(requirement.status, 0) + 1
        )
    counts = ", ".join(
        f"{status}={count}" for status, count in sorted(status_counts.items())
    )
    print(f"requirement traces valid: total={len(requirements)} ({counts})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
