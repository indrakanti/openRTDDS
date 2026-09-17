# RTPS DATA requirements

### ORT-RTPS-001 — RTPS message header

**Status:** Verified  
**Verification:** Test, Demonstration

The DATA builder shall emit the RTPS magic, protocol version, caller-supplied
vendor identifier, and GUID prefix in the fixed 20-byte message header.

**Rationale:** A conforming header is required for RTPS peer recognition.

### ORT-RTPS-002 — Unfragmented DATA layout

**Status:** Verified  
**Verification:** Test, Demonstration

The builder shall produce exactly one unfragmented DATA submessage without
inline QoS, with correct flags, offsets, length, entity IDs, sequence number,
and serialized payload placement.

**Rationale:** This is the minimum static interoperable user-data path.

### ORT-RTPS-003 — Static endpoint identity

**Status:** Verified  
**Verification:** Test, Demonstration

The DATA path shall carry caller-configured reader and writer EntityIds and
shall not depend on dynamic endpoint discovery.

**Rationale:** Static configuration keeps discovery outside the safety timing
path.

### ORT-RTPS-004 — Positive sequence number

**Status:** Verified  
**Verification:** Test, Demonstration

The builder and parser shall encode and validate positive 64-bit writer
sequence numbers as RTPS high and low 32-bit words in submessage byte order.

**Rationale:** Sequence identity is the basis for ordering and reliability.

### ORT-RTPS-005 — Bounded datagram construction

**Status:** Verified  
**Verification:** Test, Demonstration

DATA construction shall use caller-owned storage, perform no allocation, and
reject messages larger than the maximum UDP payload of 65,507 bytes.

**Rationale:** Message memory and transport size must have explicit bounds.

### ORT-RTPS-006 — Defensive parsing

**Status:** Verified  
**Verification:** Test

The parser shall validate protocol identity, supported version, submessage
kind, flags, lengths, offsets, sequence numbers, and serialized-payload
encapsulation before publishing a view.

**Rationale:** Malformed network input must not escape validation.

### ORT-RTPS-007 — Explicit subset and atomic view

**Status:** Verified  
**Verification:** Test

Unsupported keys, inline QoS, fragmentation, or submessage forms shall return
an explicit error, and any parse failure shall leave the caller's view
unchanged.

**Rationale:** A deliberately small wire subset must fail closed.

