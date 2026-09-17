# Linux real-time requirements

### ORT-LNX-001 — Process memory locking

**Status:** Verified  
**Verification:** Demonstration

The Linux abstraction shall provide an explicit operation to lock current and
future process mappings and shall return the native failure cause when the
operation cannot be completed.

**Rationale:** Demand paging can introduce unbounded latency; deployment
permissions and limits must remain observable.

### ORT-LNX-002 — Thread CPU affinity

**Status:** Verified  
**Verification:** Test

The Linux abstraction shall provide an operation to pin the calling thread to
a validated logical CPU index and shall reject an invalid index explicitly.

**Rationale:** CPU placement is required to analyze scheduler and interrupt
interference.

### ORT-LNX-003 — FIFO scheduling

**Status:** Verified  
**Verification:** Test

The Linux abstraction shall provide an operation to select `SCHED_FIFO` for
the calling thread, validate the priority range, and distinguish permission,
resource-limit, and operating-system failures.

**Rationale:** Real-time scheduling failures must not silently degrade timing
guarantees.

### ORT-LNX-004 — Monotonic time source

**Status:** Verified  
**Verification:** Test, Demonstration

The Linux abstraction shall expose a nanosecond timestamp derived from
`CLOCK_MONOTONIC` for interval and deadline measurement.

**Rationale:** Wall-clock changes must not invalidate relative timing logic.

