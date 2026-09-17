# Core resource requirements

### ORT-CORE-001 — Startup resource limits

**Status:** Verified  
**Verification:** Test

The runtime limits component shall reject zero or inconsistent participant,
topic, endpoint, sample, payload, and history-depth bounds before operation.

**Rationale:** Invalid bounds must fail during initialization rather than
creating an unbounded or unusable runtime configuration.

### ORT-CORE-002 — Fixed-capacity object storage

**Status:** Verified  
**Verification:** Test

The core shall provide object storage with compile-time capacity that performs
no heap allocation, reports exhaustion explicitly, and destroys each acquired
object exactly once.

**Rationale:** Fixed storage makes maximum ownership and cleanup behavior
auditable for deterministic profiles.

