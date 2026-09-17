# API and interface catalog

This catalog identifies the implemented public C++ API, operating-system
interfaces, and wire interfaces. Detailed contracts remain in the linked
feature designs.

## Public C++ headers

| Header | Namespace | Public contract |
|---|---|---|
| `dds/static_entities.hpp` | `openrtdds::dds` | bounded static participant, typed topic, writer, and reader composition |
| `core/runtime_limits.hpp` | `openrtdds::core` | startup bounds and validation |
| `core/static_pool.hpp` | `openrtdds::core` | fixed-capacity object ownership |
| `core/keep_last_history.hpp` | `openrtdds::core` | bounded sample and KEEP_LAST history |
| `os/linux/realtime.hpp` | `openrtdds::os::linux_rt` | memory lock, affinity, FIFO scheduling, monotonic time |
| `serialization/cdr.hpp` | `openrtdds::serialization` | bounded XCDR1 reader and writer |
| `rtps/types.hpp` | `openrtdds::rtps` | RTPS identity value types |
| `rtps/data_message.hpp` | `openrtdds::rtps` | DATA message construction and parsing |
| `rtps/reliability_messages.hpp` | `openrtdds::rtps` | bounded HEARTBEAT/ACKNACK construction and parsing |
| `rtps/reliability_state.hpp` | `openrtdds::rtps` | fixed writer/reader reliability state and caller-owned actions |
| `transport/udp_socket.hpp` | `openrtdds::transport` | nonblocking UDPv4 ownership and I/O |
| `version.hpp` | `openrtdds` | library semantic version string |

All implemented API functions are `noexcept`. Failure is represented by a
boolean plus an object error accessor, a feature error enum, or a result
structure. Exceptions are neither thrown nor translated.

## Wire interface

The current network interface is one IPv4 UDP datagram containing:

1. 20-byte RTPS message header;
2. four-byte RTPS submessage header;
3. 20-byte fixed DATA content;
4. one XCDR1 `CDR_BE` or `CDR_LE` serialized payload.

The DATA path has a maximum datagram size of 65,507 bytes. The current
reliability-control path contains exactly one HEARTBEAT (52 bytes) or ACKNACK
(48 through 80 bytes) submessage. See the
[RTPS DATA design](rtps-data/detailed-design.md) and
[reliability design](reliability/detailed-design.md) for offsets and
validation.

## Linux interfaces

| Library operation | Linux interface |
|---|---|
| Lock mappings | `mlockall(MCL_CURRENT | MCL_FUTURE)` |
| Set calling-thread CPU | `pthread_setaffinity_np` |
| Read FIFO priority range | `sched_get_priority_min/max(SCHED_FIFO)` |
| Set calling-thread policy | `pthread_setschedparam(..., SCHED_FIFO, ...)` |
| Read monotonic time | `clock_gettime(CLOCK_MONOTONIC, ...)` |
| Create UDP socket | `socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)` |
| Bind/query endpoint | `bind`, `getsockname` |
| Datagram I/O | `sendto`, `recvfrom` with nonblocking flags |
| Release socket | `close` |

## Interface stability

The project is pre-1.0. Source compatibility is preferred but not guaranteed.
Wire-format changes must remain compliant with the supported DDSI-RTPS subset
and require interoperability tests. Error enum names are API contracts;
implicit numeric enum values are not persistent diagnostic identifiers and
must not be stored or transmitted.

## Static typed DDS interface

`dds/static_entities.hpp` composes the serialization, DATA, and reliability
interfaces without taking ownership of transport or scheduling. The
application provides a generated type-support adapter, static participant and
endpoint identities, fixed datagram buffers, monotonic timestamps, and
caller-owned action buffers. Successful writer and reader calls respectively
produce or consume exactly one complete RTPS datagram.

The typed API validates remote GUID prefixes and endpoint entity IDs before
committing receive state or a decoded sample. `DdsResult` preserves the
relevant `CdrError`, `RtpsError`, `ReliabilityMessageError`, or
`ReliabilityError` rather than collapsing lower-layer evidence. See the
[static DDS design](static-dds/detailed-design.md) for the type-support
contract, ordering, lifetime, and failure guarantees.

## Reliability state interface

The caller-driven writer/reader state machines and HEARTBEAT/ACKNACK wire
codec are Current. They remain separate interfaces: applications may use the
codec without state ownership, and state actions must be explicitly translated
to transport operations by the caller. Neither interface owns threads, timers,
sockets, or callbacks.
