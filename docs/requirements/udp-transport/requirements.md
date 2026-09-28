# UDPv4 transport requirements

### ORT-UDP-001 — Nonblocking close-on-exec socket

**Status:** Verified  
**Verification:** Test, Inspection

Opening a UDPv4 transport socket shall create a nonblocking descriptor with
close-on-exec enabled, or return an explicit failure.

**Rationale:** Transport calls must not introduce hidden waits or leak into an
executed child process.

### ORT-UDP-002 — Explicit endpoint operations

**Status:** Verified  
**Verification:** Test

The transport shall provide explicit open, bind, local-endpoint, send-to, and
receive-from operations for IPv4 endpoints.

**Rationale:** Static deployments need a small auditable socket contract.

### ORT-UDP-003 — Observable receive conditions

**Status:** Verified  
**Verification:** Test

Receive shall distinguish no available datagram, truncated datagram, invalid
endpoint, and operating-system failure without retrying or waiting internally.

**Rationale:** Scheduling and recovery policy belongs to the caller.

### ORT-UDP-004 — Datagram size bound

**Status:** Verified  
**Verification:** Test

Send shall reject payloads larger than 65,507 bytes and both send and receive
shall report the relevant byte count.

**Rationale:** IPv4 UDP payload limits must be enforced before system calls.

### ORT-UDP-005 — Allocation-free ownership

**Status:** Verified  
**Verification:** Test, Inspection

The socket wrapper shall own only its descriptor, perform no heap allocation,
be non-copyable, support ownership transfer by move, and close exactly once.

**Rationale:** Descriptor lifetime and runtime allocation behavior must be
unambiguous.

### ORT-UDP-006 — Explicit IPv4 multicast membership

**Status:** Verified
**Verification:** Test, Inspection

The transport shall expose allocation-free operations to enable address reuse
and join a caller-selected IPv4 multicast group on a caller-selected interface.
It shall reject non-multicast group addresses, preserve native `errno` on
option failure, and shall not hide binding, polling, retry, or interface
selection policy.

**Rationale:** DDS participant discovery requires an explicit and auditable
SPDP multicast receive path without bypassing the production UDP abstraction.
