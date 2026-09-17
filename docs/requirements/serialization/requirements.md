# XCDR1 serialization requirements

### ORT-SER-001 — Caller-owned bounded storage

**Status:** Verified  
**Verification:** Test, Demonstration

The serializer and deserializer shall operate on caller-owned buffers with
explicit sizes and shall perform no heap allocation.

**Rationale:** Memory consumption and failure points must be bounded.

### ORT-SER-002 — XCDR1 encapsulation

**Status:** Verified  
**Verification:** Test, Demonstration

The serializer shall emit, and the deserializer shall accept, the four-byte
XCDR1 `CDR_BE` and `CDR_LE` encapsulation headers.

**Rationale:** DDSI-RTPS interoperability requires an identified serialized
payload representation.

### ORT-SER-003 — Alignment and padding

**Status:** Verified  
**Verification:** Test, Demonstration

The serialization origin shall reset after the encapsulation header; primitive
values shall use XCDR1 alignment and writer-inserted padding shall be zero.

**Rationale:** Deterministic golden bytes prevent ambiguous wire layouts and
information leakage through padding.

### ORT-SER-004 — Primitive round trip

**Status:** Verified  
**Verification:** Test, Demonstration

The serialization API shall support bounded round trips for boolean, integer,
floating-point, and raw byte primitive values in both byte orders.

**Rationale:** These primitives form the minimum useful static DDS type set.

### ORT-SER-005 — Bounded strings

**Status:** Verified  
**Verification:** Test

The serialization API shall encode and decode NUL-terminated XCDR1 strings,
enforce the declared character bound, and enforce destination capacity.

**Rationale:** Variable-length fields require explicit limits.

### ORT-SER-006 — Atomic failure and unsupported encoding

**Status:** Verified  
**Verification:** Test

A failed field operation shall not advance the cursor or partially update an
output value; errors shall remain sticky until reinitialization, and unsupported
encapsulation identifiers shall be rejected explicitly.

**Rationale:** Callers must never mistake partial or unsupported data for a
valid sample.

