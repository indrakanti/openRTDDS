# Vendor packet interoperability evidence

This feature verifies received RTPS wire subsets and bounded live best-effort
exchanges against pinned vendor implementations. It does not establish
drop-in DDS compatibility outside the documented feature and version scope.

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

**Status:** Verified
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

**Status:** Verified
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

### ORT-INT-008 — Live OpenRTDDS writer to vendor reader gate

**Status:** Verified
**Verification:** Test

The CI interoperability job shall run one OpenRTDDS best-effort writer against
readers created by each pinned vendor in domain 43. OpenRTDDS shall announce
the CI-selected local IPv4 address in its participant and publication, receive
and parse the vendor's participant
and subscription announcements, request missing bounded subscription SEDP
samples with ACKNACK, require topic, type, reliability, durability, and
endpoint identity compatibility, observe the vendor publications reader's
ACKNACK and repair its SEDP sample, and transmit `VendorProbe.value =
0x4F525444` to the discovered reader locator. The subscription ACKNACK shall
carry `INFO_DST` for the discovered participant, and a subscription DATA
submessage shall be processed when compounded with its HEARTBEAT. The vendor
API shall take and validate the sample within a 16-second process deadline. The
job shall preserve the vendor version, commands, exit results, timeout results,
and both process
outputs as JSON evidence.

**Rationale:** Inbound packet parsing alone does not demonstrate that a vendor
can discover an OpenRTDDS writer and consume an OpenRTDDS application sample.

### ORT-INT-009 — Live vendor writer to OpenRTDDS reader gate

**Status:** Verified
**Verification:** Test

The CI interoperability job shall run one OpenRTDDS best-effort reader against
writers created by each pinned vendor in domain 43. OpenRTDDS shall announce
the CI-selected local IPv4 address in its participant and subscription,
receive and parse the vendor's participant and publication announcements,
request missing bounded publication SEDP samples with an `INFO_DST`-directed
ACKNACK, require topic, type, reliability, durability, source participant, and
writer identity compatibility, observe the vendor subscriptions reader's
ACKNACK and repair its SEDP sample, and accept only a DATA submessage addressed
to its reader or the unknown reader. The reader shall decode standard CDR and
accept only the exact `VendorProbe.value = 0x4F525444` sample with no trailing
payload. The exchange shall complete within one 16-second process deadline and
preserve the vendor version, commands, exit results, timeout results, and both
process outputs as JSON evidence.

**Rationale:** Frozen inbound packets do not demonstrate that a vendor can
discover a live OpenRTDDS reader and deliver an application sample through the
production UDP, discovery, matching, DATA, and CDR receive path.
