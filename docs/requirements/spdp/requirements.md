# SPDP participant discovery requirements

This feature implements bounded Simple Participant Discovery Protocol support
for the General Profile. It discovers participants and their locators; SEDP
endpoint discovery remains outside this feature.

### ORT-SPDP-001 — Standard UDPv4 discovery mapping

**Status:** Verified  
**Verification:** Test, Demonstration

The SPDP component shall calculate multicast and unicast discovery ports from
caller-configurable DDSI-RTPS port parameters and shall represent validated,
bounded UDPv4 locators without dynamic allocation.

**Rationale:** Standard defaults enable interoperable discovery while
deployment-specific port plans remain explicit and testable.

### ORT-SPDP-002 — Participant announcement encoding

**Status:** Verified  
**Verification:** Test, Demonstration

The SPDP builder shall produce one best-effort SPDP participant DATA
announcement using the standard built-in writer identity, PL_CDR parameter
list, participant identity, locators, endpoint set, domain, and lease data in
caller-owned storage.

**Rationale:** A standards-based participant announcement is the entry point
for dynamic DDS interoperability.

### ORT-SPDP-003 — Defensive participant parsing

**Status:** Verified  
**Verification:** Test

The SPDP parser shall validate the RTPS envelope, built-in endpoint identity,
PL_CDR representation, parameter lengths and bounds, required fields, GUID,
protocol/vendor consistency, domain, locators, lease, unknown
must-understand parameters, and sentinel before publishing a parsed result.

**Rationale:** Discovery input is unauthenticated network data until a future
security profile is added and must fail closed without partial output.

### ORT-SPDP-004 — Bounded discovered-participant cache

**Status:** Verified  
**Verification:** Test, Demonstration

The discovery cache shall add and refresh participants in compile-time fixed
storage, reject self, stale, and excess participants explicitly, and expose
lookup and occupancy without allocation.

**Rationale:** Dynamic discovery must still have analyzable memory and
capacity behavior.

### ORT-SPDP-005 — Lease-driven participant removal

**Status:** Verified  
**Verification:** Test, Demonstration

The discovery cache shall refresh a participant lease on an accepted
announcement and shall remove expired participants only through an explicit
caller-driven expiration operation with bounded caller-owned output.

**Rationale:** Participant loss must be observable and independent of hidden
threads or timers.

### ORT-SPDP-006 — Deterministic discovery boundary

**Status:** Verified  
**Verification:** Test, Analysis, Demonstration

SPDP construction, parsing, cache update, and expiration shall perform no
runtime heap allocation, socket ownership, internal thread creation, blocking
wait, callback, or hidden retry, and all variable collections shall have
compile-time or caller-provided bounds.

**Rationale:** General-profile discovery should not weaken the library's
explicit ownership and bounded-execution architecture.
