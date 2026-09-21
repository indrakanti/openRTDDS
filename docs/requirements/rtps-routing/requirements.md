# Compound RTPS message-routing requirements

### ORT-ROUTE-001 — Complete bounded submessage walk

**Status:** Verified  
**Verification:** Test

The message router shall walk every RTPS submessage within the supplied
message bound using each submessage's byte order and `octetsToNextHeader`
value, including the standard zero-length final-submessage rule.

**Rationale:** Interoperable implementations commonly combine several RTPS
submessages in one UDP datagram.

### ORT-ROUTE-002 — Interpreter routing context

**Status:** Verified  
**Verification:** Test

The message router shall apply valid `INFO_SRC`, `INFO_DST`, and `INFO_TS`
submessages to the effective source, destination, protocol, vendor, and
timestamp context returned with each selected submessage.

**Rationale:** RTPS interpreter submessages modify the meaning of subsequent
submessages rather than representing independent application data.

### ORT-ROUTE-003 — Compound DATA dispatch

**Status:** Verified  
**Verification:** Test, Demonstration

The DATA parser shall locate and parse the first supported DATA submessage in
a compound RTPS message while preserving its effective routing context.

**Rationale:** DATA is not guaranteed to be the first submessage emitted by an
independent DDS implementation.

### ORT-ROUTE-004 — Compound reliability dispatch

**Status:** Verified  
**Verification:** Test

The HEARTBEAT and ACKNACK parsers shall locate their first supported
submessage in a compound RTPS message instead of requiring it at byte 20.

**Rationale:** Reliability traffic may be preceded by interpreter or
implementation-specific length-delimited submessages.

### ORT-ROUTE-005 — Defensive and atomic routing

**Status:** Verified  
**Verification:** Test

The message router shall reject truncated headers, out-of-bound lengths,
invalid interpreter submessages, invalid protocol identity, and unsupported
protocol versions without modifying the caller's output view.

**Rationale:** Network input is untrusted and partial observations must not be
published after validation fails.

### ORT-ROUTE-006 — Deterministic resource bound

**Status:** Verified  
**Verification:** Test, Demonstration

The message router shall use caller-owned input storage, perform no dynamic
allocation, and complete in time linear in the supplied message size.

**Rationale:** The routing path must remain analyzable and bounded in the
deterministic profile.
