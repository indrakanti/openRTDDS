# Architecture baseline

OpenRTDDS separates policy, protocol, transport, operating-system services,
and safety mechanisms so each boundary can be tested and analyzed.

```text
Application API
    |
DDS entities and QoS
    |
Bounded history/cache
    |
RTPS writers/readers and reliability
    |
CDR serialization
    |
UDP / shared memory transports
    |
Linux real-time abstraction
```

Cross-cutting safety services will provide end-to-end data protection,
deadline supervision, health events, metrics, and fault injection.

## Planned source ownership

| Area | Responsibility |
|---|---|
| `core` | Entity lifecycle, resource limits, histories, QoS |
| `rtps` | Wire protocol, matching, sequence state, reliability |
| `serialization` | Bounded CDR encoding and decoding |
| `transport` | UDPv4 and shared-memory I/O |
| `os/linux` | Threads, scheduling, affinity, clocks, memory, sockets |
| `safety` | E2E envelope, monitoring, fault reporting |

## Dependency direction

Higher layers may depend on lower abstractions. OS-specific types must not
escape `os/linux`, transport code must not own DDS policy, and safety policy
must consume explicit events rather than infer hidden middleware state.

## Execution model

The first implementation will use a small fixed thread set: RX, TX,
reliability/timers, and monitoring. Static discovery removes discovery from
the steady-state safety timing path. Any future thread or blocking primitive
must be added to the architecture and interference analysis.

