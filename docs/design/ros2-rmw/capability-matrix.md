# ROS 2 RMW capability-gap matrix

This matrix separates existing OpenRTDDS building blocks from adapter work and
missing middleware capabilities. `Available` means a bounded primitive exists;
it does not mean the corresponding `rmw` function is implemented.

| RMW capability | Existing OpenRTDDS basis | Gap before adapter support | Planned gate |
|---|---|---|---|
| Package discovery and selection | ament registration, loadable shared library, reviewed 95-symbol Jazzy proxy manifest, `RMW_IMPLEMENTATION` lifecycle smoke, and sanitizer qualification | semantic APIs beyond lifecycle | G4.1 passed |
| Init/context/shutdown | fixed foundation context, ROS allocator wrapper, C ABI lifecycle state machine, one joined network worker, three nonblocking sockets, wait shutdown broadcast, 256-cycle stress, fault injection, and zero-balance accounting | real-time worker configuration remains deployment hardening | G4.1 passed/G4.2 best effort through PR33 |
| Nodes | bounded generation-checked slots, `rmw_node_t` wrapper, exhaustion rollback, and sanitizer stress | graph publication | G4.1 passed/G4.3 |
| Publishers | bounded `rmw_publisher_t`, ROS name/type/QoS mapping, stable GID, SEDP, XCDR1/RTPS DATA KEEP_LAST history, remote matching, and best-effort UDP send | reliable per-reader repair and graph record | G4.2 best effort through PR33 |
| Subscriptions | bounded `rmw_subscription_t`, reader SEDP, fixed writer-state/history, matched UDP ingress, XCDR1 take/metadata, and wait readiness | reliable receive/repair and graph record | G4.2 best effort through PR33 |
| Type support | generated-handle resolution to allocation-free Jazzy C/C++ introspection, bounded size analysis, two-pass codec, and endpoint integration | expand qualified type/application matrix | G4.2 best effort through PR33 |
| Topic/type names | fixed `rt/rq/rr` mapping, service suffixes, C/C++ DDS type identity, and 255-byte validation | publisher/subscription/service/client consumption and SEDP announcement | G4.2 foundation by PR27 |
| QoS | reliability, volatile durability, KEEP_LAST | complete supported-policy map, defaults, compatibility API, events | G4.2/G4.5 |
| Wait sets | bounded 8-set registry, 32-condition ceiling, network subscription/guard filtering, generation wake, monotonic timeout, and shutdown unblock | service/client/event readiness follows their feature gates | G4.2 best effort through PR33 |
| Guard conditions | fixed slots, trigger/wake generations, `rmw_guard_condition_t` wrapper, blocking wait integration, and exhaustion rollback | graph changes must trigger the reserved graph guard | G4.2 best effort through PR33/G4.3 |
| ROS graph | SPDP/SEDP caches | logical nodes, graph discovery protocol/cache, queries, graph guard | G4.3 |
| Services/clients | pub/sub primitives | request/response topics, identities, correlation, readiness | G4.4 |
| GIDs and message info | stable writer `rmw_gid_t`, remote discovery correlation, publication/reception sequences, fallback source/receive timestamps, and ROS take evidence | RTPS `INFO_TS` source time | G4.2 best effort through PR33 |
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

After PR33, the planned initial ROS 2 scope has five focused PRs remaining. A PR
may split if review or CI exposes an ownership boundary, but features shall not
be merged into one oversized change.

| PR | Planned change | Gate |
|---:|---|---|
| 30 | Publisher lifecycle, bounded staging, SEDP writer, and publish metadata (completed) | G4.2 |
| 31 | Subscription lifecycle, reader history, take, and message metadata (completed) | G4.2 |
| 32 | Wait-set readiness, guard integration, timeout, and shutdown wake (completed) | G4.2 |
| 33 | Same-host bounded discovery/UDP worker and C++ best-effort ROS talker-listener (current) | G4.2 |
| 34 | Reliable worker repair, C/C++ topic matrix, and G4.2 closure | G4.2 |
| 35 | Bounded ROS graph cache, discovery propagation, queries, and expiry | G4.3 |
| 36 | Service/client request-reply identity, correlation, and readiness | G4.4 |
| 37 | Events/status conditions and selected Jazzy conformance allowlist | G4.5 |
| 38 | Deployment hardening: interface configuration, stress, latency, packaging, and safety evidence | G4.5 |

An earlier gate may introduce a function that returns `RMW_RET_UNSUPPORTED`.
It may not return success until its required semantics and evidence exist.
