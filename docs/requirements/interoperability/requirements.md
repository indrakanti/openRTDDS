# Vendor packet interoperability evidence

This feature verifies received SPDP and publications SEDP wire subsets against
actual vendor implementations. It does not establish live DDS interoperability.

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

### ORT-INT-005 — Vendor SEDP publication receive gate

**Status:** Verified  
**Verification:** Test

The CI interoperability job shall create a user writer and a second
participant with each pinned vendor, capture a publications SEDP DATA
datagram within a fixed timeout, and pass the unchanged UDP payload to
`parse_sedp_message`. It shall validate the endpoint identity against an SPDP
packet captured from the same participant and require usable participant
default UDPv4 locators when the endpoint omits them. It shall reject invalid
topic, type, or explicitly supplied UDPv4 locators. The job shall preserve
both packets and a manifest of vendor version, commands, domain, byte counts,
and SHA-256 digests.

**Rationale:** Discovery announcements produced by vendors expose gaps that
internally generated endpoint messages cannot exercise.
