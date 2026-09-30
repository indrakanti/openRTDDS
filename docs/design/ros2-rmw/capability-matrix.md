# ROS 2 RMW capability-gap matrix

This matrix separates existing OpenRTDDS building blocks from adapter work and
missing middleware capabilities. `Available` means a bounded primitive exists;
it does not mean the corresponding `rmw` function is implemented.

| RMW capability | Existing OpenRTDDS basis | Gap before adapter support | Planned gate |
|---|---|---|---|
| Package discovery and selection | CMake library | ament package, shared ABI, exported C symbols, implementation registration | G4.1 |
| Init/context/shutdown | runtime limits, static pools, Linux clocks | context owner, allocator contract, lifecycle state machine | G4.1 |
| Nodes | participant identity | logical ROS node records and graph publication | G4.1/G4.3 |
| Publishers | typed static writer, CDR, UDP, reliability | type-erased handle, ROS mapping, QoS conversion, metadata | G4.2 |
| Subscriptions | typed static reader, bounded history | readiness/take contract, type-erased decode, metadata | G4.2 |
| Type support | bounded XCDR1 primitives | Jazzy introspection traversal, max-size analysis, ROS strings/sequences | G4.2 |
| Topic/type names | bounded SEDP names | ROS DDS name/type mapping and bound validation | G4.2 |
| QoS | reliability, volatile durability, KEEP_LAST | complete supported-policy map, defaults, compatibility API, events | G4.2/G4.5 |
| Wait sets | nonblocking UDP | readiness registry, wakeup primitive, monotonic timeout, shutdown wake | G4.2 |
| Guard conditions | none | bounded trigger/reset object and wait integration | G4.1/G4.2 |
| ROS graph | SPDP/SEDP caches | logical nodes, graph discovery protocol/cache, queries, graph guard | G4.3 |
| Services/clients | pub/sub primitives | request/response topics, identities, correlation, readiness | G4.4 |
| GIDs and message info | RTPS GUID and sequence | stable `rmw_gid_t` encoding and `rmw_message_info_t` conversion | G4.2 |
| Events | parser/state errors | bounded status counters and waitable events | G4.5 |
| Feature reporting | documented limitations | `rmw_feature_supported` map | G4.1/G4.5 |
| Security | none | DDS Security and SROS 2 integration | excluded initially |
| Loaned messages/zero-copy | fixed sample/history storage | ownership-compatible loan API | excluded initially |
| Content filtering | none | expression/filter implementation | excluded initially |
| Dynamic types | static adapters only | dynamic type representation and negotiation | excluded initially |

## Required implementation order

1. G4.1: package, ABI surface, bounded context, node, guard condition, and
   lifecycle/error scaffolding.
2. G4.2: type support, topic mapping, publisher/subscription, QoS, wait/take,
   and metadata.
3. G4.3: distributed graph cache, discovery propagation, queries, and expiry.
4. G4.4: client/service request-response channels and correlation.
5. G4.5: supported events, upstream conformance allowlist, stress, sanitizer,
   and deterministic-profile analysis.

An earlier gate may introduce a function that returns `RMW_RET_UNSUPPORTED`.
It may not return success until its required semantics and evidence exist.
