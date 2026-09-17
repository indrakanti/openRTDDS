# Bounded reliability requirements

The bounded control-message and single-pair reliability state-machine profile
is implemented and verified. Multi-reader aggregation remains future scope.

### ORT-REL-001 — Bounded writer history window

**Status:** Verified  
**Verification:** Test

A reliable-writer state-machine instance shall retain at most its compile-time
history depth of sequenced DATA samples for one statically matched reader and
shall reject a new sample with `history_full` rather than replace an
unacknowledged sample.

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

**Status:** Verified  
**Verification:** Test

The reliable writer shall enforce configured maximum repair attempts and
repair-window duration for each retained sample and shall emit exactly one
terminal `sample_failed` action before releasing a sample when either bound is
reached.

**Rationale:** Reliable delivery must not become unbounded recovery.

### ORT-REL-005 — Duplicate and stale control traffic

**Status:** Verified  
**Verification:** Test

The reliability state machines shall identify duplicate or stale HEARTBEAT and
ACKNACK counts using wrap-aware 32-bit ordering and shall return
`stale_control` without changing state, producing actions, or consuming repair
attempts.

**Rationale:** Reordered control traffic must not trigger extra work.

### ORT-REL-006 — Deterministic state machine

**Status:** Verified  
**Verification:** Test, Analysis

Reliability processing shall use compile-time fixed state and caller-owned
fixed action storage, perform no runtime heap allocation or internal blocking
wait, and examine at most the configured history depth or receive-window size
per event.

**Rationale:** Reliability must preserve the deterministic profile.
