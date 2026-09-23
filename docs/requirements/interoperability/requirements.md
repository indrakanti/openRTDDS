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

### ORT-INT-006 — Vendor best-effort DATA receive gate

**Status:** Verified  
**Verification:** Test

The CI interoperability job shall configure an explicitly best-effort writer
and reader for `OpenRTDDSProbe`, transmit the fixed `VendorProbe` value
`0x4F525444`, and capture the vendor's unmodified user DATA UDP payload within
a fixed timeout. The gate shall parse the sample with `parse_data_message`,
match its source and writer entity to same-run SPDP and SEDP announcements,
verify the endpoint topic, type, and best-effort QoS, and decode the fixed
32-bit value from standard CDR. Each packet in the discovery-to-data chain
shall have versioned provenance and a SHA-256 digest.

**Rationale:** A discovery-only corpus does not demonstrate that the existing
production DATA receive path accepts vendor-produced application samples.

### ORT-INT-007 — Vendor reliable DATA control-chain gate

**Status:** Implemented
**Verification:** Test

The CI interoperability job shall configure reliable `OpenRTDDSProbe`
endpoints for each pinned vendor and capture one fixed `VendorProbe` DATA
sample together with its publisher HEARTBEAT and subscriber ACKNACK. The gate
shall correlate publisher DATA, publications SEDP, SPDP, and HEARTBEAT by
effective source GUID prefix and writer entity. It shall correlate subscriber
ACKNACK, subscriptions SEDP, and SPDP by source GUID prefix and reader entity.
Production parsers shall validate both control messages, the DATA sequence
shall fall within the HEARTBEAT range, both endpoints shall advertise reliable
QoS, and all control bitmaps shall remain within the 256-bit bound. The
capture shall preserve every packet, exact vendor version, commands, byte
counts, and SHA-256 digests.

**Rationale:** Reliable DATA acceptance is incomplete without evidence that
the associated writer and reader control identities and bounded sequence state
are understood by the production RTPS receive path.
