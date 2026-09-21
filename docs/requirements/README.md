# Requirements governance

OpenRTDDS uses requirements-first development. Feature behavior is described
and reviewed here before implementation starts, then traced to code and
verification evidence.

## Catalog

| Feature | Prefix | Requirements |
|---|---|---|
| Core resource management | `ORT-CORE` | [core](core/requirements.md) |
| Linux real-time primitives | `ORT-LNX` | [linux-realtime](linux-realtime/requirements.md) |
| XCDR1 serialization | `ORT-SER` | [serialization](serialization/requirements.md) |
| Bounded history | `ORT-HIST` | [history](history/requirements.md) |
| RTPS DATA path | `ORT-RTPS` | [rtps-data](rtps-data/requirements.md) |
| UDPv4 transport | `ORT-UDP` | [udp-transport](udp-transport/requirements.md) |
| Bounded reliability | `ORT-REL` | [reliability](reliability/requirements.md) |
| Static DDS API | `ORT-DDS` | [static-dds](static-dds/requirements.md) |
| SPDP participant discovery | `ORT-SPDP` | [spdp](spdp/requirements.md) |
| SEDP endpoint discovery | `ORT-SEDP` | [sedp](sedp/requirements.md) |

The consolidated code and evidence mapping is the
[traceability matrix](traceability.md).

## Identifier and lifecycle

Requirement IDs use `ORT-<FEATURE>-<NNN>`. IDs are permanent: a retired
requirement is marked Deprecated rather than renumbered or reused.

Allowed states are:

- **Draft** — proposed and open to change; implementation is not claimed.
- **Approved** — accepted as a design obligation but not yet implemented.
- **Implemented** — code exists; required verification may still be pending.
- **Verified** — implementation and declared verification evidence exist.
- **Deprecated** — retained for history and no longer applicable.

## Requirement record

Each requirement has one normative “shall” statement plus its rationale,
status, and verification method. Supported verification methods are Test,
Demonstration, Analysis, and Inspection. Multiple methods may be declared.

## Trace tags

Use searchable comments next to evidence:

```cpp
// Requirements: ORT-SER-001
// Verifies: ORT-SER-001
// Demonstrates: ORT-SER-001
```

- `Requirements:` belongs beside implementing code in `include/` or `src/`.
- `Verifies:` belongs in automated tests in `tests/`.
- `Demonstrates:` belongs in runnable examples in `examples/`.

`tools/check_requirement_traces.py` rejects duplicate definitions, unknown
IDs, and missing evidence for Implemented or Verified requirements. Verified
requirements must have implementation evidence and the evidence declared by
their verification method.

## Change policy

1. Add or update the feature requirement and assign its stable ID.
2. Review bounds, failure behavior, timing implications, and verification.
3. Add implementation tags next to the relevant public contract or code.
4. Add test and example tags matching the declared verification method.
5. Update `traceability.md` and run the trace checker.
6. Only mark a requirement Verified after all declared evidence passes CI.
