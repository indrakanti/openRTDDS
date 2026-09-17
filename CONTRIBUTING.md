# Contributing

OpenRTDDS is being developed in small, reviewable increments. Every change
should preserve explicit resource bounds and make timing or failure behavior
no harder to explain.

## Pull-request expectations

- define or update requirements before implementing feature behavior
- update the matching `docs/design/<feature>/detailed-design.md` with code
- document behavior, classes, APIs, interfaces, sequences, errors, faults,
  ownership, bounds, concurrency, and timing impacts
- use stable `ORT-<FEATURE>-<NNN>` IDs from `docs/requirements/`
- put `Requirements:` IDs beside the implementing code
- put `Verifies:` IDs in tests and `Demonstrates:` IDs in examples
- update `docs/requirements/traceability.md` when traces change
- update `docs/design/traceability.md` when design ownership changes
- include tests for observable behavior
- avoid new runtime heap allocation on Safety Profile paths
- document new threads, locks, queues, timers, and retry loops
- return errors explicitly; do not silently reduce real-time guarantees
- keep warnings clean under GCC and Clang
- separate standards interoperability from proprietary extensions

Run the local gate before submitting:

```bash
python3 tools/check_requirement_traces.py
cmake -S . -B build -DOPENRTDDS_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

See [requirements governance](docs/requirements/README.md) for the lifecycle,
required evidence, and change rules. See
[detailed-design governance](docs/design/README.md) for the design lifecycle
and required content.
