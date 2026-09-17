# System detailed design

**Design status:** Current  
**Scope:** PR1–PR3 implementation baseline  
**Requirements:** This document is architectural context; normative feature
requirements are traced in their feature designs.

## Responsibility and boundary

The current library is a synchronous, caller-driven C++17 component. It
provides bounded storage, XCDR1 serialization, one unfragmented RTPS DATA
message, and nonblocking UDPv4 transport. It does not create threads, schedule
callbacks, perform discovery, or implement reliability.

```mermaid
flowchart TD
    App[Application] --> CDR[XCDR1 writer]
    CDR --> RTPS[RTPS DATA builder]
    RTPS --> UDP[Nonblocking UDPv4]
    UDP --> Peer[Static peer]
    Peer --> Parser[RTPS DATA parser]
    Parser --> Reader[XCDR1 reader]
    Reader --> App
    History[KEEP_LAST history] -. bounded storage .-> App
    Linux[Linux RT primitives] -. process/thread setup .-> App
```

The application owns orchestration. It selects static identifiers and
endpoints, provides all message buffers, calls APIs, handles errors, and
decides retry, deadline, and degradation policies.

## End-to-end transmit and receive behavior

```mermaid
sequenceDiagram
    participant A as Application
    participant C as CdrWriter
    participant R as DataMessageBuilder
    participant U as UdpSocket
    participant P as Peer
    A->>C: begin(byte_order)
    A->>C: write fields
    C-->>A: payload pointer + size
    A->>R: build(config, payload, size)
    R-->>A: RTPS datagram pointer + size
    A->>U: send_to(endpoint, datagram)
    U-->>P: one UDP datagram
    P->>U: one UDP datagram
    A->>U: receive_from(buffer)
    A->>R: parse_data_message(buffer)
    R-->>A: validated DataMessageView
    A->>C: begin() + read fields
```

Each step is explicit and synchronous. There are no internal queues between
these components. Failure terminates the current call and is returned to the
application.

## Execution and concurrency model

| Property | Current design |
|---|---|
| Internal threads | None |
| Internal blocking waits | None |
| Hidden retries | None |
| Callbacks | None |
| Runtime heap allocation | None in implemented components |
| Synchronization | None |
| Thread safety | Instances are not internally synchronized |

Distinct instances may be used by distinct threads. Concurrent access to the
same mutable object requires caller-provided synchronization. The caller must
analyze any synchronization for priority inversion and bounded blocking.

## Ownership model

- Writers and builders borrow mutable caller buffers for their entire object
  lifetime.
- Readers and parsed views borrow immutable input buffers.
- A `DataMessageView` becomes invalid when its source datagram storage is
  modified or destroyed.
- `BoundedSample`, `KeepLastHistory`, and `StaticPool` own inline storage.
- `UdpSocket` exclusively owns one file descriptor and transfers ownership on
  move.

## Initialization order

Recommended application order:

1. validate `RuntimeLimits` and static configuration;
2. create fixed storage and communication objects;
3. open and bind UDP sockets;
4. lock process memory and pre-fault application stacks;
5. set thread affinity and scheduling policy;
6. enter the caller's cyclic/event loop;
7. treat any setup failure according to the deployment safety concept.

The library does not enforce this order because the application owns process
startup and thread creation.

## Failure propagation

Local API errors are returned using feature-specific enums. `RealtimeResult`
and `UdpResult` preserve native OS error information. No component currently
publishes a diagnostic event; the application maps errors into the stable
design-level fault codes defined in [faults-and-errors](../faults-and-errors.md).

## Current limitations

- Linux and UDPv4 only.
- Static endpoints; no SPDP or SEDP.
- One unfragmented DATA submessage per datagram.
- No inline QoS, keys, DATA_FRAG, security, or dynamic types.
- No HEARTBEAT, ACKNACK, retransmission, deadlines, or health monitor yet.
- No internal application-level end-to-end safety envelope yet.

