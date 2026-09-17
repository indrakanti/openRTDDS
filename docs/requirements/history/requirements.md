# Bounded history requirements

### ORT-HIST-001 — Compile-time capacity

**Status:** Verified  
**Verification:** Test

KEEP_LAST history shall have compile-time sample-depth and payload-size bounds
and shall perform no heap allocation.

**Rationale:** Worst-case history memory must be knowable at build time.

### ORT-HIST-002 — KEEP_LAST replacement

**Status:** Verified  
**Verification:** Test

When full, a successful push shall replace the oldest sample while preserving
the configured depth.

**Rationale:** This is the bounded DDS KEEP_LAST policy.

### ORT-HIST-003 — Failed push preserves state

**Status:** Verified  
**Verification:** Test

Null non-empty input and payloads exceeding the configured bound shall be
rejected without changing history contents, size, or ordering.

**Rationale:** Invalid input must not corrupt previously accepted evidence.

### ORT-HIST-004 — Metadata and ordering

**Status:** Verified  
**Verification:** Test

Each stored sample shall retain its sequence number and source timestamp, and
oldest/newest access shall reflect successful insertion order.

**Rationale:** Reliability and freshness checks depend on stable ordering and
metadata.

