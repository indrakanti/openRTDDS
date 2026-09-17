# Static DDS API requirements

This feature defines the first usable DDS-style API over the existing bounded
serialization, RTPS, reliability, and UDP components. Discovery remains
static and application configured.

### ORT-DDS-001 — Static DDS entity identity

**Status:** Verified  
**Verification:** Test

The static DDS API shall provide bounded `DomainParticipant`, `Publisher`,
`Subscriber`, and typed `Topic` entities and shall reject unsupported RTPS
versions, zero participant GUID prefixes, zero topic identifiers, or
topic/type-support mismatches.

**Rationale:** Applications need a coherent DDS-facing identity model without
dynamic discovery or allocation.

### ORT-DDS-002 — Typed bounded publication

**Status:** Verified  
**Verification:** Test

A static `DataWriter` shall serialize a typed sample into fixed XCDR1 storage,
assign a strictly increasing sequence number, retain it in bounded reliable
history, and construct one RTPS DATA datagram in caller-owned storage.

**Rationale:** Publication must compose the existing bounded layers without
hidden ownership or partial state updates.

### ORT-DDS-003 — Typed bounded subscription

**Status:** Verified  
**Verification:** Test

A static `DataReader` shall validate the configured remote participant and
endpoint identities, parse one RTPS DATA datagram, deserialize the configured
type, and update its bounded receive state before publishing the sample to the
caller.

**Rationale:** Incorrectly routed or malformed traffic must not enter the
application as a valid typed sample.

### ORT-DDS-004 — Integrated reliability control path

**Status:** Verified  
**Verification:** Test

The static writer and reader shall build and process HEARTBEAT and ACKNACK
datagrams through their bounded reliability state and shall expose repair,
delivery, and terminal failure actions explicitly to the caller.

**Rationale:** The DDS API must integrate reliability rather than bypass the
state machines implemented beneath it.

### ORT-DDS-005 — Explicit datagram transport boundary

**Status:** Verified  
**Verification:** Test, Demonstration

The static DDS API shall produce and consume complete caller-owned RTPS
datagrams that can be transmitted unchanged through the nonblocking UDPv4
transport, without owning a socket or performing a hidden retry.

**Rationale:** Transport scheduling and error policy remain under application
control in the deterministic profile.

### ORT-DDS-006 — Deterministic typed path

**Status:** Verified  
**Verification:** Test, Analysis

The static DDS entity and endpoint path shall use compile-time fixed internal
storage, caller-owned datagram and action storage, no runtime heap allocation,
no internal threads or blocking waits, and bounded work per invocation.

**Rationale:** A convenient typed API must preserve the guarantees of the
underlying deterministic components.
