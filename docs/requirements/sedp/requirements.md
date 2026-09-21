# SEDP endpoint discovery requirements

This feature implements bounded Simple Endpoint Discovery Protocol records,
remote endpoint storage, and deterministic request/offered matching for the
General Profile. Reliable delivery scheduling of the built-in SEDP traffic is
provided by the existing caller-driven reliability layer.

### ORT-SEDP-001 — Standard endpoint announcement encoding

**Status:** Verified  
**Verification:** Test, Demonstration

The SEDP component shall encode publication and subscription endpoint
announcements as DDSI-RTPS PL_CDR DATA using the standard built-in entity
identities, endpoint and participant GUIDs, bounded topic/type names, UDPv4
locators, and supported reliability and durability policies.

**Rationale:** Standards-based endpoint records are required for dynamic DDS
interoperability and later ROS 2 middleware integration.

### ORT-SEDP-002 — Defensive endpoint announcement parsing

**Status:** Verified  
**Verification:** Test

The SEDP parser shall validate the RTPS envelope, SEDP built-in identities,
expected SPDP participant ownership, PL_CDR representation, parameter bounds,
required fields, endpoint kind, QoS values, locators, unknown
must-understand parameters, and sentinel before publishing a parsed result.

**Rationale:** A remote endpoint shall not enter matching state unless its
identity and bounded representation are fully validated.

### ORT-SEDP-003 — Bounded discovered-endpoint cache

**Status:** Verified  
**Verification:** Test, Demonstration

The discovered-endpoint cache shall add and update endpoints in compile-time
fixed storage, reject stale and excess announcements explicitly, and expose
bounded lookup without dynamic allocation.

**Rationale:** Dynamic endpoint discovery must retain analyzable memory and
capacity behavior.

### ORT-SEDP-004 — Participant-driven endpoint removal

**Status:** Verified  
**Verification:** Test, Demonstration

The discovered-endpoint cache shall remove all endpoints owned by an expired
SPDP participant only through an explicit operation with caller-owned bounded
output, and shall leave the table unchanged when output capacity is
insufficient.

**Rationale:** Participant lease expiry must produce complete, observable,
and atomic endpoint teardown decisions.

### ORT-SEDP-005 — Deterministic endpoint matching

**Status:** Verified  
**Verification:** Test, Demonstration

The matching function shall compare opposite endpoint kinds, exact bounded
topic and type names, and request/offered reliability and durability without
allocation, state mutation, or callbacks, returning one explicit match or
incompatibility status.

**Rationale:** Match decisions must be reproducible and diagnosable before
transport or reliability state is created.

### ORT-SEDP-006 — Deterministic discovery boundary

**Status:** Verified  
**Verification:** Test, Analysis, Demonstration

SEDP construction, parsing, cache operations, removal, and matching shall
perform no runtime heap allocation, socket ownership, internal thread
creation, blocking wait, callback, or hidden retry, and every variable
collection shall have a compile-time or caller-provided bound.

**Rationale:** General-profile discovery shall preserve the explicit
ownership and bounded-execution architecture.
