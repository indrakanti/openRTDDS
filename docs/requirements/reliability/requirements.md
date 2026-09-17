# Bounded reliability requirements

This feature set is delivered incrementally. The control-message wire subset is
implemented and verified; writer/reader repair state remains Draft.

### ORT-REL-001 — Bounded writer history window

**Status:** Draft  
**Verification:** Test

A reliable writer shall retain at most a configured fixed number of sequenced
DATA samples for repair and shall report exhaustion or replacement explicitly.

**Rationale:** Repair state must have a known memory bound.

### ORT-REL-002 — HEARTBEAT wire subset

**Status:** Verified  
**Verification:** Test

The reliability component shall build and parse a full RTPS message containing
exactly one bounded HEARTBEAT submessage with reader and writer identity,
first and last available sequence numbers, signed 32-bit count, Final flag,
and Liveliness flag in either RTPS byte order without runtime allocation.

**Rationale:** Readers need a bounded declaration of writer availability.

### ORT-REL-003 — ACKNACK wire subset

**Status:** Verified  
**Verification:** Test

The reliability component shall build and parse a full RTPS message containing
exactly one ACKNACK submessage with reader and writer identity, signed 32-bit
count, Final flag, and an MSB-first SequenceNumberSet of no more than 256
positions in either RTPS byte order without runtime allocation.

**Rationale:** Repair requests must not allocate or carry unbounded bitmaps.

### ORT-REL-004 — Bounded repair policy

**Status:** Draft  
**Verification:** Test

The writer shall enforce configured maximum repair attempts and repair-window
duration for each sample and shall emit an explicit terminal failure when
either bound is exceeded.

**Rationale:** Reliable delivery must not become unbounded recovery.

### ORT-REL-005 — Duplicate and stale control traffic

**Status:** Draft  
**Verification:** Test

The reliability state machine shall ignore duplicate or stale HEARTBEAT and
ACKNACK counts without changing acknowledged state or repair-attempt limits.

**Rationale:** Reordered control traffic must not trigger extra work.

### ORT-REL-006 — Deterministic state machine

**Status:** Draft  
**Verification:** Test, Analysis

Reliability processing shall use fixed storage, no runtime heap allocation, no
internal blocking waits, and a documented upper bound on work per event.

**Rationale:** Reliability must preserve the deterministic profile.
