# Detailed design governance

This directory is the maintained software detailed-design baseline for
OpenRTDDS. Requirements define **what** must be achieved; these documents
define **how** the software behaves, how its types collaborate, which APIs and
interfaces are exposed, and how errors and faults propagate.

## Design catalog

| Feature | Status | Detailed design |
|---|---|---|
| System integration | Current | [System design](system/detailed-design.md) |
| Core resources | Current | [Core design](core/detailed-design.md) |
| Linux real-time primitives | Current | [Linux real-time design](linux-realtime/detailed-design.md) |
| XCDR1 serialization | Current | [Serialization design](serialization/detailed-design.md) |
| Bounded history | Current | [History design](history/detailed-design.md) |
| RTPS DATA | Current | [RTPS DATA design](rtps-data/detailed-design.md) |
| UDPv4 transport | Current | [UDP transport design](udp-transport/detailed-design.md) |
| Bounded reliability | Planned | [Reliability design](reliability/detailed-design.md) |

Cross-cutting contracts are documented in:

- [API and interface catalog](interfaces.md)
- [Error and fault model](faults-and-errors.md)
- [Requirement-to-design traceability](traceability.md)

## Required content

Every feature detailed-design document shall contain, where applicable:

1. status, scope, requirements, and source ownership;
2. behavior and invariants;
3. class/type definitions and ownership;
4. public API contracts, preconditions, postconditions, and lifetimes;
5. internal and external interfaces, including wire formats;
6. normal and failure sequence diagrams;
7. state transitions;
8. error codes, fault mapping, and recovery ownership;
9. memory, concurrency, timing, and determinism properties;
10. deliberate limitations and verification evidence.

## Lifecycle

- **Planned** — design is approved or under review; implementation is not
  claimed.
- **Current** — design describes the code on `main` and is covered by the
  referenced evidence.
- **Deprecated** — retained for history but not used for new implementation.

The status applies to the document as a whole. Any forward-looking section in
a Current document must be explicitly labeled Planned.

## Update policy

Design is versioned with code. A feature PR shall update its requirements and
detailed design in the same PR whenever it changes:

- externally visible behavior or API;
- class responsibility, ownership, or lifetime;
- wire or process interface;
- state machine or sequence;
- error, fault, recovery, or retry behavior;
- memory bound, allocation behavior, concurrency, timing, or scheduling;
- supported feature set or deliberate limitation.

Documentation-only corrections that do not change behavior may be submitted
separately. Design documents describe the code merged with them; they are not
release promises.

## Trace convention

Each feature document contains one or more metadata lines such as:

```text
Requirements: ORT-SER-001, ORT-SER-002
```

`tools/check_requirement_traces.py` requires every Implemented or Verified
requirement to have a design reference. The human-reviewed mapping is in
`traceability.md`.

## Definition of done

A feature is not ready to merge until:

- requirements are Approved, Implemented, or Verified as appropriate;
- detailed design matches the proposed code;
- code carries `Requirements:` tags;
- tests carry `Verifies:` tags;
- examples carry `Demonstrates:` tags where required;
- both traceability matrices are updated;
- trace checks, GCC, Clang, tests, and examples pass.

