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

## Source ownership

| Area | Responsibility |
|---|---|
| `dds` | Static typed entity composition and endpoint configuration |
| `core` | Resource limits, histories, and fixed storage |
| `rtps` | Wire protocol, matching, sequence state, reliability |
| `rtps/spdp` | Bounded General Profile participant discovery and leases |
| `rtps/sedp` | Bounded General Profile endpoint discovery and matching |
| `serialization` | Bounded CDR encoding and decoding |
| `transport` | UDPv4 and shared-memory I/O |
| `os/linux` | Threads, scheduling, affinity, clocks, memory, sockets |
| `safety` | E2E envelope, monitoring, fault reporting |

## Dependency direction

Higher layers may depend on lower abstractions. OS-specific types must not
escape `os/linux`, transport code must not own DDS policy, and safety policy
must consume explicit events rather than infer hidden middleware state.

## Execution model

The current library owns no threads, locks, timers, or blocking waits. The
application drives receive, transmit, reliability timer events, and fault
policy using its deployment-specific scheduling model. A future managed
runtime may define a small fixed thread set, but any such thread or blocking
primitive must first be added to the architecture and interference analysis.
