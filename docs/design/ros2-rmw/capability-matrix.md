# ROS 2 RMW capability-gap matrix

This matrix separates existing OpenRTDDS building blocks from adapter work and
missing middleware capabilities. `Available` means a bounded primitive exists;
it does not mean the corresponding `rmw` function is implemented.

| RMW capability | Existing OpenRTDDS basis | Gap before adapter support | Planned gate |
|---|---|---|---|
| Package discovery and selection | ament registration, loadable shared library, reviewed 95-symbol Jazzy proxy manifest, `RMW_IMPLEMENTATION` lifecycle smoke, and sanitizer qualification | semantic APIs beyond lifecycle | G4.1 passed |
| Init/context/shutdown | fixed foundation context, ROS allocator wrapper, C ABI lifecycle state machine, wait shutdown broadcast, 256-cycle stress, fault injection, and zero-balance accounting | transport workers belong to G4.2 | G4.1 passed/G4.2 foundation through PR32 |
| Nodes | bounded generation-checked slots, `rmw_node_t` wrapper, exhaustion rollback, and sanitizer stress | graph publication | G4.1 passed/G4.3 |
| Publishers | bounded `rmw_publisher_t`, ROS name/type/QoS mapping, stable GID, SEDP packet, XCDR1 staging, RTPS DATA, KEEP_LAST history, sequence/timestamp metadata | discovery/network worker, graph record, remote matching, reliable repair, and talker/listener evidence | G4.2 foundation through PR30 |
| Subscriptions | bounded `rmw_subscription_t`, reader SEDP, fixed writer-state table, accepted-DATA KEEP_LAST history, XCDR1 take/sequence, message metadata, and wait readiness | UDP receive worker, discovery matching, reliable repair, and listener evidence | G4.2 foundation through PR32 |
| Type support | allocation-free Jazzy C/C++ introspection traversal, bounded XCDR1 size/alignment analysis, two-pass big/little-endian codec, and exact rejection errors | generic handle resolution, bounded staging ownership, and endpoint integration | G4.2 foundation through PR28 |
| Topic/type names | fixed `rt/rq/rr` mapping, service suffixes, C/C++ DDS type identity, and 255-byte validation | publisher/subscription/service/client consumption and SEDP announcement | G4.2 foundation by PR27 |
| QoS | reliability, volatile durability, KEEP_LAST | complete supported-policy map, defaults, compatibility API, events | G4.2/G4.5 |
| Wait sets | bounded 8-set registry, 32-condition ceiling, subscription/guard filtering, generation wake, monotonic timeout, and shutdown unblock | service/client/event readiness follows their feature gates | G4.2 foundation through PR32 |
| Guard conditions | fixed slots, trigger/wake generations, `rmw_guard_condition_t` wrapper, blocking wait integration, and exhaustion rollback | graph changes must trigger the reserved graph guard | G4.2 foundation through PR32/G4.3 |
| ROS graph | SPDP/SEDP caches | logical nodes, graph discovery protocol/cache, queries, graph guard | G4.3 |
| Services/clients | pub/sub primitives | request/response topics, identities, correlation, readiness | G4.4 |
| GIDs and message info | stable writer `rmw_gid_t`, publication/reception sequences, source/receive timestamps, and take conversion | remote discovery correlation and end-to-end ROS evidence | G4.2 foundation through PR31 |
| Events | parser/state errors | bounded status counters and waitable events | G4.5 |
| Feature reporting | all Jazzy optional features conservatively report unsupported | update the map only as qualified features are added | G4.1/G4.5 |
| Security | none | DDS Security and SROS 2 integration | excluded initially |
| Loaned messages/zero-copy | fixed sample/history storage | ownership-compatible loan API | excluded initially |
| Content filtering | none | expression/filter implementation | excluded initially |
| Dynamic types | static adapters only | dynamic type representation and negotiation | excluded initially |

## Required implementation order

1. G4.1 (passed by PR26): package, ABI surface, bounded context, node, guard
   condition, lifecycle/error scaffolding, and memory-safety qualification.
2. G4.2: type support, topic mapping, publisher/subscription, QoS, wait/take,
   and metadata.
3. G4.3: distributed graph cache, discovery propagation, queries, and expiry.
4. G4.4: client/service request-response channels and correlation.
5. G4.5: supported events, upstream conformance allowlist, stress, sanitizer,
   and deterministic-profile analysis.

## Remaining focused change plan

After PR32, the planned initial ROS 2 scope has five focused PRs remaining. A PR
may split if review or CI exposes an ownership boundary, but features shall not
be merged into one oversized change.

| PR | Planned change | Gate |
|---:|---|---|
| 30 | Publisher lifecycle, bounded staging, SEDP writer, and publish metadata (completed) | G4.2 |
| 31 | Subscription lifecycle, reader history, take, and message metadata (completed) | G4.2 |
| 32 | Wait-set readiness, guard integration, timeout, and shutdown wake (current) | G4.2 |
| 33 | C/C++ ROS talker-listener qualification and G4.2 closure | G4.2 |
| 34 | Bounded ROS graph cache, discovery propagation, queries, and expiry | G4.3 |
| 35 | Service/client request-reply identity, correlation, and readiness | G4.4 |
| 36 | Events/status conditions and selected Jazzy conformance allowlist | G4.5 |
| 37 | Deployment hardening: stress, latency, packaging, and safety evidence | G4.5 |

An earlier gate may introduce a function that returns `RMW_RET_UNSUPPORTED`.
It may not return success until its required semantics and evidence exist.
