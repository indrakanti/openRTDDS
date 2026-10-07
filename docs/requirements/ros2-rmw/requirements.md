# ROS 2 RMW adapter readiness

This feature defines the obligations for a separate `rmw_openrtdds` adapter
that allows ROS 2 client libraries to use OpenRTDDS. The initial compatibility
baseline is ROS 2 Jazzy on Linux. Before implementation begins, the adapter
build shall pin the exact Jazzy `rmw` package version and ABI used by CI.

These requirements do not claim that OpenRTDDS currently supports ROS 2. They
define the staged contract and the missing middleware capabilities that must be
implemented and verified before interoperability gate G4 can pass.

### ORT-RMW-001 — Adapter identity and package registration

**Status:** Draft

**Verification:** Test, Inspection

The adapter shall build as a shared library named `rmw_openrtdds_cpp`, export
the complete C entry-point set required by the pinned ROS 2 Jazzy `rmw` ABI,
register as an `rmw_implementation_packages` member, and report one stable
implementation identifier from every created handle.

**Rationale:** ROS 2 discovers middleware implementations through package and
symbol contracts; partial symbol coverage otherwise fails only at load time.

### ORT-RMW-002 — Initialization and context lifecycle

**Status:** Draft

**Verification:** Test

The adapter shall validate init options, domain configuration, allocator, and
implementation identity; create one bounded context; support shutdown before
finalization; reject invalid lifecycle order; and release every context-owned
resource during finalization without invalidating caller-owned ROS storage.

**Rationale:** Context lifetime is the ownership root for graph, discovery,
transport, wait, and entity resources.

### ORT-RMW-003 — Node lifecycle

**Status:** Draft

**Verification:** Test

The adapter shall create and destroy bounded node handles after validating
their context, namespace, name, and implementation identity, and shall add and
remove each node atomically from the local ROS graph.

**Rationale:** DDS has no native ROS node object, so the adapter must provide a
consistent logical node and graph representation.

### ORT-RMW-004 — Publisher lifecycle and publish path

**Status:** Draft

**Verification:** Test, Demonstration

The adapter shall create and destroy bounded publisher handles, map the ROS
topic, type, and supported QoS to one OpenRTDDS writer, serialize a caller
message through the selected type support, publish one sample with stable GID,
timestamp, and sequence metadata, and propagate failures without partial
entity or history ownership.

**Rationale:** A ROS publisher requires more than a raw RTPS writer: it also
requires type-erased serialization, metadata, lifecycle, and graph behavior.

### ORT-RMW-005 — Subscription lifecycle and take path

**Status:** Draft

**Verification:** Test, Demonstration

The adapter shall create and destroy bounded subscription handles, map the ROS
topic, type, and supported QoS to one OpenRTDDS reader, report readiness only
for a complete accepted sample, deserialize into caller-provided storage, and
return source timestamp, receive timestamp, publisher GID, and sequence
metadata without exposing a partially decoded message.

**Rationale:** The ROS executor depends on accurate readiness and take
semantics, not merely arrival of a UDP datagram.

### ORT-RMW-006 — ROS type-support boundary

**Status:** Draft

**Verification:** Test, Analysis

The first adapter profile shall accept the pinned Jazzy C and C++ introspection
type-support identifiers, calculate a bounded maximum serialized size before
endpoint activation, reject unsupported or unbounded types under the Safety
Profile, and serialize or deserialize XCDR1 without runtime schema mutation.

**Rationale:** `rmw` passes type-erased messages; a deterministic adapter must
resolve that type information into explicit storage and wire bounds.

### ORT-RMW-007 — ROS-to-DDS name and type mapping

**Status:** Draft

**Verification:** Test, Inspection

The adapter shall apply the ROS 2 DDS topic and type-name mapping selected for
the pinned Jazzy baseline, distinguish topic, service request, and service
response channels, preserve `avoid_ros_namespace_conventions` behavior, and
reject names or mapped identifiers that exceed configured bounds.

**Rationale:** Compatible bytes cannot communicate when ROS and DDS endpoint
names or type identities differ.

### ORT-RMW-008 — QoS mapping and compatibility

**Status:** Draft

**Verification:** Test, Analysis

The adapter shall map supported ROS reliability, durability, history, depth,
deadline, lifespan, liveliness, and lease-duration policies to explicit
OpenRTDDS values; resolve system-default and unknown policies predictably;
report unsupported policies; and return the same compatibility result used by
endpoint matching.

**Rationale:** Silent QoS reduction can produce communication loss and invalid
real-time assumptions.

### ORT-RMW-009 — Wait sets and guard conditions

**Status:** Draft

**Verification:** Test, Analysis

The adapter shall provide bounded wait sets and guard conditions that can wait
for subscriptions, services, clients, events, and explicit wakeups; use a
monotonic timeout; tolerate spurious wakeups; update only readiness entries
owned by the call; and unblock all waiters during context shutdown.

**Rationale:** ROS executors are driven by `rmw_wait`; polling or lost wakeups
would break latency and shutdown behavior.

### ORT-RMW-010 — ROS graph cache and discovery propagation

**Status:** Draft

**Verification:** Test, Demonstration

Each context shall maintain a bounded graph cache for local and remote nodes,
publish local graph changes, consume remote graph changes, remove expired
participants and their entities, provide graph query/count APIs from a
consistent snapshot, and trigger the graph guard condition after a committed
change.

**Rationale:** ROS graph introspection includes logical nodes and endpoint
relationships that are not fully represented by SPDP and SEDP alone.

### ORT-RMW-011 — Service and client request/reply

**Status:** Draft

**Verification:** Test, Demonstration

The adapter shall create bounded service and client pairs over mapped request
and response topics, assign a stable client GID and monotonically increasing
request sequence, correlate each response with its request, reject unrelated
or duplicate responses, and expose request/response readiness through wait
sets.

**Rationale:** ROS services are an `rmw` obligation and are composed over DDS
publish/subscribe rather than supplied by the current OpenRTDDS API.

### ORT-RMW-012 — Message metadata and globally unique identifiers

**Status:** Draft

**Verification:** Test

The adapter shall derive stable `rmw_gid_t` values from OpenRTDDS endpoint
identity, preserve publisher identity across discovery and take paths, report
source and receive timestamps from monotonic or declared clock domains, and
detect sequence values that cannot be represented by the selected ROS
metadata contract.

**Rationale:** ROS uses this metadata for tracing, message information, and
request/response correlation.

### ORT-RMW-013 — Events and status conditions

**Status:** Draft

**Verification:** Test

The adapter shall implement the supported `rmw_event_type_t` set with bounded
counters and wait-set readiness, including incompatible QoS and liveliness or
deadline events once their underlying OpenRTDDS policies exist, and shall
return `RMW_RET_UNSUPPORTED` for every event that lacks truthful middleware
evidence.

**Rationale:** Reporting success for an unimplemented status hides faults from
ROS applications.

### ORT-RMW-014 — Error and unsupported-feature contract

**Status:** Draft

**Verification:** Test, Inspection

Every adapter entry point shall validate null pointers, implementation
identity, handle state, and required arguments; map OpenRTDDS failures to the
most specific stable `rmw_ret_t`; set one bounded thread-local diagnostic; and
return `RMW_RET_UNSUPPORTED` rather than silently emulating an unsupported
semantic contract.

**Rationale:** The C ABI cannot propagate C++ exceptions and callers require a
consistent error contract.

### ORT-RMW-015 — Bounded ownership and allocation

**Status:** Draft

**Verification:** Test, Analysis

The context shall receive startup limits for nodes, publishers, subscriptions,
services, clients, wait sets, guard conditions, graph records, samples, and
serialized bytes; reject limit exhaustion atomically; and, in the Safety
Profile, perform no heap allocation after context activation.

**Rationale:** Existing OpenRTDDS bounds must extend across the adapter rather
than ending at its ABI boundary.

### ORT-RMW-016 — Concurrency and shutdown

**Status:** Draft

**Verification:** Test, Analysis

The adapter shall document a fixed worker-thread topology, lock ownership and
ordering, callback prohibition, and executor interaction; allow concurrent
operations only where declared; and complete shutdown without deadlock while
waking every blocked wait set within a configured bound.

**Rationale:** Hidden threads and ambiguous lock ordering make latency and
termination behavior unanalyzable.

### ORT-RMW-017 — Feature reporting and deliberate limitations

**Status:** Draft

**Verification:** Test, Inspection

The adapter shall report supported `rmw_feature_t` values truthfully and shall
document the initial exclusions, including DDS Security/SROS 2, content
filtering, network flow endpoints, dynamic types, loaned messages, and
zero-copy, until separate requirements and verification evidence exist.

**Rationale:** A restricted adapter can be useful, but callers must be able to
distinguish deliberate limitations from working capabilities.

### ORT-RMW-018 — Build and load qualification gate

**Status:** Draft

**Verification:** Test

Gate G4.1 shall require that a clean Jazzy workspace builds and installs
`rmw_openrtdds_cpp`, ROS 2 discovers it, runtime selection through
`RMW_IMPLEMENTATION=rmw_openrtdds_cpp` loads it, and an initialization/node
lifecycle smoke test exits without leaks or invalid accesses.

**Rationale:** Adapter behavior cannot be qualified before packaging, symbol,
and runtime-selection contracts are proven.

### ORT-RMW-019 — ROS topic communication qualification gate

**Status:** Draft

**Verification:** Test, Demonstration

Gate G4.2 shall require bounded C and C++ ROS talker/listener processes to
exchange fixed best-effort and reliable messages in both directions through
`rmw_openrtdds_cpp`, validate exact content and metadata, exercise wait/take,
and finish within declared process and retry deadlines.

**Rationale:** Direct DDS interoperability does not prove the ROS client-library
and executor path.

### ORT-RMW-020 — ROS graph qualification gate

**Status:** Draft

**Verification:** Test, Demonstration

Gate G4.3 shall require two processes to observe bounded node, topic, type,
publisher, subscription, and matched-endpoint graph changes and their removal
after orderly shutdown and participant lease expiry.

**Rationale:** ROS tools and application discovery depend on graph semantics
beyond user-data delivery.

### ORT-RMW-021 — ROS service qualification gate

**Status:** Draft

**Verification:** Test, Demonstration

Gate G4.4 shall require a bounded client and service to exchange concurrent
requests and correctly correlated responses, expose readiness through wait
sets, reject a mismatched response identity, and terminate within declared
deadlines.

**Rationale:** Topic-only success does not establish an operational ROS 2 RMW
implementation.

### ORT-RMW-022 — Selected conformance qualification gate

**Status:** Draft

**Verification:** Test, Analysis

Gate G4.5 shall define and run a version-controlled allowlist of upstream Jazzy
`rmw` and `rcl` conformance tests, record every exclusion with a linked
unsupported feature or defect, and permit a ROS 2 readiness claim only when
all mandatory tests and G4.1 through G4.4 pass in CI.

**Rationale:** A reproducible, reviewable qualification set is stronger than a
claim based only on demonstration programs.

### ORT-RMW-023 — Bounded adapter-foundation limits

**Status:** Verified

**Verification:** Test, Analysis

The implementation-independent RMW foundation shall accept explicit maximum
node and guard-condition counts at initialization, reject zero or
compile-time-capacity-exceeding limits before changing context state, and
reject runtime exhaustion without modifying the caller's output handle.

**Rationale:** The ROS-facing layer needs bounded ownership before C ABI
objects can safely be attached to it.

### ORT-RMW-024 — Foundation context state machine

**Status:** Verified

**Verification:** Test, Demonstration

The foundation context shall enforce `zero`, `initialized`, `active`,
`shutting_down`, and `finalizable` lifecycle states; reject invalid transition
order; assign a new nonzero generation to each initialization; advance the
context wake generation on shutdown; and permit finalization only after all
owned handles are released.

**Rationale:** A small tested state machine prevents partial activation,
use-after-finalize, and shutdown that silently abandons owned entities.

### ORT-RMW-025 — Bounded node slots and stale-handle rejection

**Status:** Verified

**Verification:** Test, Demonstration

The foundation shall store node names and namespaces in fixed-capacity slots,
validate null, empty, and over-bound inputs before slot commitment, encode
context and slot generations in each node handle, and reject released or
reused handles without changing the active node.

**Rationale:** Future opaque `rmw_node_t` data must not turn a reused pool slot
into a valid stale ROS handle.

### ORT-RMW-026 — Guard-condition generation semantics

**Status:** Verified

**Verification:** Test, Demonstration

The foundation shall create guard conditions from fixed-capacity slots,
represent triggers with an advancing generation that reserves zero and wraps
from its maximum to one, expose readiness by comparing observed generations,
advance a context wake generation on trigger and shutdown, and reject
triggering after shutdown begins.

**Rationale:** Generation comparison provides a bounded primitive from which a
later wait-set implementation can avoid lost wakeups.

### ORT-RMW-027 — Pinned Jazzy RMW baseline

**Status:** Verified

**Verification:** Test, Inspection

The foundation shall expose the implementation identifier
`rmw_openrtdds_cpp`, ROS distribution `jazzy`, and upstream `rmw` package
version `7.3.4`, and the baseline record shall identify the exact upstream
`package.xml` blob used to select that version.

**Rationale:** Adapter symbols and behavior must be implemented against a
reviewable ABI source rather than an unbounded rolling target.

### ORT-RMW-028 — Stable foundation error vocabulary

**Status:** Verified

**Verification:** Test, Inspection

Every foundation operation shall return a symbolic `AdapterError` without
throwing, output handles shall remain unchanged on failure, and every defined
error shall have a stable diagnostic name while unknown values map to
`unknown`.

**Rationale:** Exact errors can later be mapped to `rmw_ret_t` without losing
the internal cause or allowing exceptions across the C ABI.

### ORT-RMW-029 — Jazzy package discovery and shared-library identity

**Status:** Verified

**Verification:** Test, Inspection

The adapter shall build as the `rmw_openrtdds_cpp` ament package against Jazzy
`rmw` versions greater than or equal to 7.3.3 and less than 7.4.0, retain
7.3.4 as its exact reviewed header baseline, install a loadable shared library,
register it in the `rmw_typesupport` ament resource index, and export the exact
implementation identifier `rmw_openrtdds_cpp` without registering a ROS type
support backend before pub/sub support exists.

**Rationale:** ROS package discovery and library identity can be qualified
without implying that an incomplete adapter can carry ROS topics.

### ORT-RMW-030 — Jazzy initialization C ABI slice

**Status:** Verified

**Verification:** Test, Inspection

The adapter shall implement init-options initialize/copy/finalize and context
initialize/shutdown/finalize entry points using the caller-provided allocator,
deep-copy owned option fields, map the default domain deterministically, reject
invalid lifecycle order, and prevent finalization while adapter entities remain
owned.

**Rationale:** A real allocator-correct context is the minimum safe owner for
all later ROS entities.

### ORT-RMW-031 — Jazzy node lifecycle C ABI slice

**Status:** Verified

**Verification:** Test, Inspection

The adapter shall validate ROS node names and namespaces, create and destroy
`rmw_node_t` objects backed by bounded foundation slots, copy ABI-visible
strings with the context allocator, reject creation after shutdown, and expose
the context graph guard-condition handle without claiming graph propagation.

**Rationale:** Node ownership can be proven before endpoints and graph-cache
semantics are introduced.

### ORT-RMW-032 — Jazzy guard-condition lifecycle C ABI slice

**Status:** Verified

**Verification:** Test, Inspection

The adapter shall create, trigger, and destroy `rmw_guard_condition_t` objects
through bounded foundation handles, validate implementation and context
ownership on every operation, reserve one bounded guard for the context graph
handle, and reject triggering after shutdown.

**Rationale:** Guard conditions establish the wake primitive used by a later
wait-set implementation while retaining exact bounded ownership.

### ORT-RMW-033 — Honest lifecycle-slice qualification gate

**Status:** Verified

**Verification:** Test, Inspection

CI shall build and test the adapter in a ROS 2 Jazzy environment, verify every
symbol listed for the lifecycle slice is exported, verify package registration
and shared-library loading, and report all optional RMW features as unsupported
until their semantics have dedicated requirements and tests.

**Rationale:** A checked partial ABI is useful evidence only when it cannot be
mistaken for full G4.1 or ROS 2 application compatibility.

### ORT-RMW-034 — Complete Jazzy proxy symbol surface

**Status:** Verified

**Verification:** Test, Inspection

The adapter shared library shall export every one of the 95 symbols requested
by the pinned Jazzy `rmw_implementation` proxy source, record the exact
upstream commit and source-blob identity used to produce that list, and fail CI
when any required `rmw_*` export is missing or an unreviewed export appears.

**Rationale:** The Jazzy proxy prefetches its complete function table during
`rmw_init`; one absent symbol can leave runtime selection with a delayed load
failure even when lifecycle functions themselves exist.

### ORT-RMW-035 — Type-correct unsupported ABI stubs

**Status:** Verified

**Verification:** Test, Inspection

Each not-yet-implemented Jazzy entry point shall have its declared C signature,
shall not mutate caller or middleware state, shall return
`RMW_RET_UNSUPPORTED` for `rmw_ret_t` APIs or null for handle-producing APIs
while setting a diagnostic, and shall return false without setting an error for
capability predicates.

**Rationale:** Type-correct stubs allow complete dynamic loading while making
unimplemented behavior immediate and observable instead of fabricating
success or relying on calling-convention-unsafe aliases.

### ORT-RMW-036 — Standard runtime-selection lifecycle smoke

**Status:** Verified

**Verification:** Test

CI shall link a lifecycle executable only to the standard
`rmw_implementation` proxy, select OpenRTDDS through
`RMW_IMPLEMENTATION=rmw_openrtdds_cpp`, and successfully execute init-options,
context, node, guard-condition, shutdown, release, and finalization operations
without a missing-symbol diagnostic.

**Rationale:** Directly linking the adapter does not prove a ROS installation
can discover and dispatch through the standard runtime-selection path.

### ORT-RMW-037 — Bounded lifecycle stress and allocation balance

**Status:** Verified

**Verification:** Test

The adapter shall complete at least 256 consecutive init, maximum-capacity
node and guard-condition creation, resource-exhaustion rejection, shutdown,
release, and finalization cycles while a caller-supplied accounting allocator
reports zero outstanding allocations after every cycle.

**Rationale:** A single successful lifecycle does not expose cumulative leaks,
slot reuse faults, or incomplete cleanup after bounded pool exhaustion.

### ORT-RMW-038 — Allocation-failure atomicity

**Status:** Verified

**Verification:** Test

Tests shall inject allocation failure at every adapter-owned allocation edge
during context, graph-guard, node, and user guard-condition creation. Each
failed operation shall leave the destination zero or null, restore the prior
allocation balance and bounded-slot availability, and permit a subsequent
valid operation.

**Rationale:** Deterministic resource exhaustion is safe only when partial
construction cannot leak memory, consume a slot, or poison later lifecycle
operations.

### ORT-RMW-039 — G4.1 memory-safety qualification

**Status:** Verified

**Verification:** Test, Analysis

CI shall build the ROS 2 Jazzy adapter, ROS-free core dependency, and lifecycle
tests with AddressSanitizer and UndefinedBehaviorSanitizer, enable leak
detection, and run normal, invalid-input, failure-injection, maximum-capacity,
stress, and standard runtime-selection paths with zero sanitizer findings.

**Rationale:** G4.1 load and lifecycle claims require explicit evidence against
invalid access, undefined behavior, and leaks in addition to functional return
codes.

### ORT-RMW-040 — Bounded Jazzy introspection analysis

**Status:** Verified

**Verification:** Test, Analysis

The adapter shall inspect direct Jazzy C and C++ introspection message handles
without allocation, derive the ROS DDS type identity, and calculate a maximum
XCDR1 serialized size including encapsulation for supported scalars, fixed
arrays, bounded sequences, bounded strings, and nested messages. It shall
reject null or malformed metadata, unsupported kinds, unbounded fields,
recursive descriptions, nesting beyond 16 levels, arithmetic overflow, and
sizes above the caller's sample limit without modifying the prior output.

**Rationale:** Publisher and subscription creation cannot reserve deterministic
storage until the type-erased ROS schema has a reviewed finite wire bound.

### ORT-RMW-041 — Allocation-free ROS-to-DDS name mapping

**Status:** Verified

**Verification:** Test, Inspection

The adapter shall map ordinary topics with the `rt` prefix, service requests
with `rq` and `Request`, and service responses with `rr` and `Reply`; omit only
the prefix when `avoid_ros_namespace_conventions` is true; derive
`<namespace>::dds_::<message>_` type identities from both C and C++
introspection namespaces; reject empty or over-255-byte results; allocate no
memory; and leave output unchanged on failure.

**Rationale:** Freezing bounded golden mapping vectors before endpoint creation
prevents discovery names from drifting away from the pinned ROS DDS convention.

## Authoritative upstream references

- [ROS 2 guide for creating an RMW implementation](https://github.com/ros2/ros2_documentation/blob/rolling/source/ROS-Framework/client-libraries/Working-with-Client-Libraries/Creating-An-RMW-Implementation.rst)
- [ROS 2 `rmw` interface repository](https://github.com/ros2/rmw)
- [ROS 2 Jazzy `rmw` API](https://docs.ros.org/en/jazzy/p/rmw/)

The pinned Jazzy headers and upstream tests used by CI are the normative ABI
source. The links above explain the interface but do not replace that pin.
