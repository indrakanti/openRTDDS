# Vendor packet interoperability evidence

This feature verifies the received SPDP wire subset against actual vendor
implementations. It does not establish live DDS interoperability.

### ORT-INT-001 — Vendor packet generation

**Status:** Verified  
**Verification:** Test

The CI interoperability job shall create participants using pinned Fast DDS
and Cyclone DDS package versions and capture each vendor's emitted SPDP UDP
payload within a fixed timeout.

**Rationale:** A local builder output cannot substitute for a vendor packet.

### ORT-INT-002 — Reproducible capture evidence

**Status:** Verified  
**Verification:** Test

Each captured packet shall be stored with a machine-readable record of vendor,
exact package version, generator command, domain, transport endpoint, byte
count, SHA-256 digest, and raw UDP-payload format.

**Rationale:** Binary evidence must be identifiable and reproducible.

### ORT-INT-003 — Vendor SPDP receive gate

**Status:** Verified  
**Verification:** Test

The CI interoperability job shall pass each captured SPDP datagram unchanged
to `parse_spdp_message` and fail when parsing, participant identity, or
required unicast locators are invalid.

**Rationale:** The gate tests the OpenRTDDS receive path using bytes produced
by two independent vendor implementations.

### ORT-INT-004 — Full vendor packet corpus

**Status:** Approved  
**Verification:** Test

The vendor corpus shall include SEDP endpoint announcements, best-effort user
DATA, and reliable user DATA for both vendors before G2 is marked complete.

**Rationale:** SPDP coverage alone does not qualify discovery, matching, or
application communication.
