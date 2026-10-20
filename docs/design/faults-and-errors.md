# Error and fault model

## Error versus fault

An **error code** describes the immediate result of one API call. A **fault
code** is a stable diagnostic classification used by an application or future
health monitor to aggregate errors and select recovery. The library currently
returns errors; fault-event publication is Planned.

`would_block` is an expected nonblocking condition, not a fault by itself. It
becomes a timing or availability fault only when the application-defined
deadline or retry bound is exceeded.

## Error domains

| Domain | Type | Meaning |
|---|---|---|
| Configuration | `LimitError` | invalid startup resource limits |
| Linux setup | `RealtimeError` + native errno | process/thread real-time setup failure |
| Serialization | `CdrError` | buffer, representation, bound, or data failure |
| History | `HistoryError` | invalid or oversized sample |
| RTPS | `RtpsError` | construction, protocol, subset, or parse failure |
| RTPS routing | `MessageRouteError` | compound-message bounds, interpreter context, or target lookup failure |
| Reliability control | `ReliabilityMessageError` | HEARTBEAT/ACKNACK construction, bounds, subset, or parse failure |
| Reliability state | `ReliabilityError` + `RepairFailure` | history, receive window, count ordering, repair, or timing result |
| Static DDS composition | `DdsError` + preserved lower-layer error | entity configuration, typed conversion, identity, DATA, or reliability operation |
| SPDP discovery | `SpdpError` + preserved `RtpsError` | discovery configuration, parsing, capacity, lease, or time failure |
| SEDP discovery | `SedpError` + preserved `RtpsError` | endpoint record, ownership, capacity, removal, or parse failure |
| UDP | `UdpError` + native errno/bytes | descriptor, endpoint, I/O, size, or availability result |
| RMW foundation and ROS adapter | `AdapterError`, `TypeSupportError`, `CodecError`, `QosError`, `SubscriptionReceiveError`; selected mapping to `rmw_ret_t` | bounded lifecycle, schema/name validation, XCDR1 conversion, QoS/resource planning, endpoint/wait failure, and network-worker containment |

Error enums are symbolic API values. Their implicit integer representation is
not a stable fault code.

## Stable fault catalog

These string identifiers are reserved for diagnostics. “Current mapping” means
the listed API errors exist; automatic fault emission is not yet implemented.

| Fault code | Severity default | Trigger/error mapping | Recovery owner |
|---|---|---|---|
| `ORT-FLT-CFG-001` | Fatal at startup | any non-`none` `LimitError` | process supervisor/application |
| `ORT-FLT-OS-001` | Fatal for deterministic profile | memory lock failure | startup controller |
| `ORT-FLT-OS-002` | Fatal for pinned profile | affinity failure | startup controller |
| `ORT-FLT-OS-003` | Fatal for RT profile | FIFO scheduling failure | startup controller |
| `ORT-FLT-TIME-001` | Error | monotonic clock returns zero | timing owner |
| `ORT-FLT-SER-001` | Error | writer overflow or declared bound exceeded | publisher/application |
| `ORT-FLT-SER-002` | Error | underflow, invalid data, unsupported encoding | subscriber/application |
| `ORT-FLT-HIST-001` | Error | invalid or oversized history sample | writer/application |
| `ORT-FLT-RTPS-001` | Warning/Error | malformed or truncated RTPS input | receiver/application |
| `ORT-FLT-RTPS-002` | Warning | unsupported version, submessage, or feature | compatibility owner |
| `ORT-FLT-RTPS-003` | Error | invalid writer sequence number | reliability owner |
| `ORT-FLT-UDP-001` | Fatal at startup | socket, bind, option, multicast-membership, or endpoint-query failure | startup controller |
| `ORT-FLT-UDP-002` | Error | send failure or oversize datagram | publisher/application |
| `ORT-FLT-UDP-003` | Error | receive failure or truncation | subscriber/application |
| `ORT-FLT-REL-001` | Error | repair/retry bound exceeded or gap no longer repairable | reliability/application |
| `ORT-FLT-DDS-001` | Fatal at startup | invalid participant, topic, endpoint, or type binding | startup controller |
| `ORT-FLT-DDS-002` | Warning/Error | unexpected participant or endpoint identity | receiver/application |
| `ORT-FLT-DISC-001` | Fatal at startup | invalid local discovery configuration | startup controller |
| `ORT-FLT-DISC-002` | Error | discovery buffer, locator, participant, or event bound exceeded | discovery/application |
| `ORT-FLT-DISC-003` | Warning/Error | malformed or unsupported discovery announcement | discovery/application |
| `ORT-FLT-DISC-004` | Warning/Error | unknown must-understand discovery parameter | compatibility owner |
| `ORT-FLT-DEADLINE-001` | Error/Fatal by topic | planned delivery deadline exceeded | application safety monitor |
| `ORT-FLT-RMW-001` (Current for lifecycle/endpoints) | Error/Fatal by context | invalid adapter lifecycle or stale active handle | context owner |
| `ORT-FLT-RMW-002` (Current for lifecycle/endpoints) | Error | adapter pool, graph, table, or byte bound exhausted | system integrator |
| `ORT-FLT-RMW-003` (Current for endpoints) | Fatal at endpoint creation | unsupported or over-bound ROS type support | application/build owner |
| `ORT-FLT-RMW-004` (Planned) | Warning/Error | invalid or over-bound ROS graph update | discovery/context owner |
| `ORT-FLT-RMW-005` (Current for wait) | Error/Fatal by context | wait ownership, observation, concurrent-use, or wake failure | context owner |
| `ORT-FLT-RMW-006` | Fatal at endpoint creation | unsupported, invalid, or over-bound QoS/resource plan | application/configuration owner |

Severity is deployment-configurable except where the selected deterministic
profile cannot satisfy its assumptions. The component detecting an API error
does not decide system-level safe state.

Current RMW codec mappings are:

| `CodecError` | Fault code / policy |
|---|---|
| `type_support_rejected`, `callback_missing` | `ORT-FLT-RMW-003`; reject endpoint/schema activation |
| `field_bound_exceeded`, `output_too_small` | `ORT-FLT-SER-001`; reject publish sample |
| `input_truncated`, `unsupported_encoding`, `invalid_wire_data`, `trailing_data` | `ORT-FLT-SER-002`; reject received sample without modifying destination |
| `invalid_message_storage`, `destination_size_mismatch` | `ORT-FLT-RMW-002`; endpoint staging/configuration defect |
| `invalid_argument`, `cdr_failure` | local integration defect; do not publish or accept sample |

The codec returns errors only; the current publisher/subscription owners map
them to `rmw_ret_t`, while diagnostic events remain planned. No codec error
triggers allocation, hidden retry, destination resizing, or partial receive
commit.

Current QoS/resource-plan mappings are:

| `QosError` | Fault code / policy |
|---|---|
| `invalid_argument`, `invalid_policy` | `ORT-FLT-RMW-006`; reject endpoint configuration |
| `unsupported_best_available`, `unsupported_history`, `unsupported_durability` | `ORT-FLT-RMW-006`; choose an implemented deterministic policy |
| `unsupported_deadline`, `unsupported_lifespan`, `unsupported_liveliness`, `unsupported_lease_duration` | `ORT-FLT-RMW-006`; feature remains outside the current profile |
| `depth_out_of_range`, `sample_size_out_of_range`, `history_bytes_out_of_range`, `arithmetic_overflow` | `ORT-FLT-RMW-002` and `ORT-FLT-RMW-006`; correct configured bounds before activation |

Normalization and planning return errors only and preserve their prior output.
The compatibility query returns `OK`, `WARNING`, or `ERROR` as data; only an
invalid API argument sets the RMW error state. No QoS result emits a fault event
in this increment.

Current publisher mappings are:

| Publisher failure | Fault code / policy |
|---|---|
| foreign, stale, shutdown, or parent-with-live-child lifecycle | `ORT-FLT-RMW-001`; reject without releasing valid ownership |
| publisher slot or allocator exhaustion | `ORT-FLT-RMW-002`; roll back every partial allocation and slot claim |
| unsupported, unbounded, malformed, or oversized type | `ORT-FLT-RMW-003`; reject before endpoint activation |
| unsupported or over-bound QoS/resource plan | `ORT-FLT-RMW-006`; reject before allocation |
| XCDR1 sample failure | existing `CodecError` mapping; do not advance history or sequence |
| RTPS DATA/sequence failure | applicable `ORT-FLT-RTPS-*`; do not advance history or sequence |
| source clock failure | `ORT-FLT-TIME-001`; do not commit the sample |

Publisher faults set the synchronous RMW error state only. `rmw_publish` does
not perform network I/O or graph mutation; after a successful history commit
it wakes the bounded worker, which applies the asynchronous send policy below.

Current subscription mappings are:

| Subscription failure | Fault code / policy |
|---|---|
| foreign, stale, shutdown, or parent-with-live-child lifecycle | `ORT-FLT-RMW-001`; reject while preserving ownership/history |
| subscription slot, allocator, history, writer-table, or reception-sequence exhaustion | `ORT-FLT-RMW-002`; roll back construction or reject ingress without corrupting existing state |
| unsupported, unbounded, malformed, or oversized type | `ORT-FLT-RMW-003`; reject before endpoint activation |
| unsupported or over-bound QoS/resource plan | `ORT-FLT-RMW-006`; reject before allocation |
| malformed RTPS DATA, invalid reader/writer, or invalid payload bound | `ORT-FLT-RTPS-001/002` or integration error; do not commit history |
| invalid source/receive timestamp | `ORT-FLT-TIME-001` or transport integration error; do not commit history |
| duplicate, stale, or ignored-local sample | normal filtered outcome; no fault and no history commit |
| XCDR1 take failure | existing `CodecError` mapping; preserve the queued sample and caller destination |

`SubscriptionReceiveError` communicates the exact synchronous ingress result.
It does not emit an asynchronous event, retry, allocate, or mutate the graph.
An accepted commit wakes the executor; the context network worker invokes this
boundary only after discovery and endpoint matching.

Current network-worker mappings are:

| Network condition | Fault code / policy |
|---|---|
| socket, bind, membership, locator, SPDP, or worker-start failure | `ORT-FLT-UDP-001`, `ORT-FLT-DISC-002`, or `ORT-FLT-RMW-001`; fail context initialization and roll back |
| malformed discovery or DATA | `ORT-FLT-DISC-003` or `ORT-FLT-RTPS-001`; discard without local endpoint mutation |
| fixed participant/endpoint table exhaustion | `ORT-FLT-RMW-002`; preserve existing records and discard the new record |
| unknown or incompatible remote endpoint | optional `ORT-FLT-DDS-002`; normal discard in the current profile |
| DATA send fails | `ORT-FLT-UDP-002`; advance best-effort progress and do not retry |
| lease expiry or nonblocking `would_block` | normal transition/result; no fault |

These asynchronous results currently have no public counter or event. Fault
event publication and waitable status conditions remain G4.5 work.

Current reliability-control mappings are:

| Reliability error | Fault code |
|---|---|
| `truncated`, `invalid_protocol`, `invalid_submessage`, `bitmap_bound_exceeded` | `ORT-FLT-RTPS-001` |
| `unsupported_version`, `unsupported_submessage`, `unsupported_feature` | `ORT-FLT-RTPS-002` |
| `invalid_sequence_number` | `ORT-FLT-RTPS-003` |
| `unexpected_peer` | routing/configuration error; reject without state change |
| `invalid_argument`, `buffer_overflow` | caller configuration/resource error |

The wire codec returns these errors but does not publish fault events. It
rejects malformed control input without modifying the caller's prior parsed
view.

For ORT-INT-010, live-writer exits map to the same catalog: DATA or repair send
failure (`11`) maps to `ORT-FLT-UDP-002`; HEARTBEAT construction or send
failure (`13`) maps to its preserved reliability-control or UDP error; state
failure (`14`) maps by `ReliabilityError`; terminal repair exhaustion (`15`)
maps to `ORT-FLT-REL-001`; and missing delivery confirmation (`16`) is an
availability/deadline fault owned by the application integration. These exit
values are example-process diagnostics, not stable library fault codes.

For ORT-INT-011, reliable receive-state failure (`11`) maps by
`ReliabilityError`, including `ORT-FLT-RTPS-003` for an invalid sequence and
`ORT-FLT-REL-001` for an unrepairable gap. ACKNACK construction or send failure
(`12`) maps to the preserved reliability-control error or
`ORT-FLT-UDP-002`. Missing final delivery acknowledgment (`13`) is an
availability/deadline fault owned by the application integration. As in
ORT-INT-010, these process exits are diagnostics rather than stable library
fault codes.

Current compound-message routing mappings are:

| `MessageRouteError` | Fault code / policy |
|---|---|
| `truncated`, `malformed_submessage`, `invalid_info_submessage` | `ORT-FLT-RTPS-001` |
| `unsupported_version`, `submessage_not_found` | `ORT-FLT-RTPS-002` |
| `invalid_protocol` | `ORT-FLT-RTPS-001`; reject datagram |
| `invalid_argument` | local caller defect; do not process input |

DATA and reliability parsers translate router errors into their existing
public error domains and preserve atomic output semantics.

Current state-machine mappings are:

| Reliability state result | Fault code |
|---|---|
| terminal `repair_limit_exceeded` or `repair_window_expired` action | `ORT-FLT-REL-001` |
| `gap_not_repairable` | `ORT-FLT-REL-001` |
| `invalid_sequence_number` | `ORT-FLT-RTPS-003` |
| `time_regression` | `ORT-FLT-TIME-001` |
| `history_full`, `receive_window_exceeded`, `action_capacity_exceeded` | resource/configuration policy |
| `stale_control`, `duplicate_data`, `stale_data` | observable status; no fault by itself |

Current static DDS composition mappings are:

| `DdsError` | Fault code / policy |
|---|---|
| `invalid_participant`, `invalid_topic`, `invalid_endpoint` | `ORT-FLT-DDS-001` |
| `unexpected_participant`, `unexpected_endpoint` | `ORT-FLT-DDS-002`; reject without state change |
| `serialization_failed`, `deserialization_failed` | map the preserved `CdrError` to `ORT-FLT-SER-001` or `ORT-FLT-SER-002` |
| `rtps_data_failed` | map the preserved `RtpsError` to the applicable `ORT-FLT-RTPS-*` code |
| `reliability_message_failed` | map the preserved `ReliabilityMessageError` to the applicable `ORT-FLT-RTPS-*` code |
| `reliability_state_failed` | map the preserved `ReliabilityError` to the applicable reliability, timing, or resource policy |
| `invalid_action` | local integration defect; do not transmit |

SPDP rejects malformed, wrong-domain, self, stale, and incompatible
announcements without committing the parsed view or a new participant state.
Participant expiry is a bounded discovery event, not a fault by itself.

SEDP applies the same `ORT-FLT-DISC-*` mappings to endpoint records. Invalid
identity or participant ownership is rejected without cache mutation. Stale
announcements and request/offered QoS incompatibility are observable discovery
outcomes, not faults by themselves. Participant-driven endpoint removal is
atomic when the caller-provided event array is too small.

## Fault event payload (Planned)

The future bounded diagnostic interface should carry:

```cpp
struct FaultEvent final {
  FaultCode code;
  FaultSeverity severity;
  std::uint64_t monotonic_timestamp_ns;
  std::uint32_t source_id;
  std::uint32_t occurrence_count;
  std::int32_t native_error;
  std::uint64_t context_a;
  std::uint64_t context_b;
};
```

The representation shall be fixed-size, allocation-free, and nonblocking.
`context_a` and `context_b` are fault-specific numeric evidence such as
expected/actual sizes or sequence numbers; they shall not contain pointers.

## Recovery rules

- Startup faults stop activation of the affected deterministic profile.
- Malformed remote input is rejected without modifying the caller's output
  view or accepted history.
- Buffer and bound failures do not trigger internal allocation or resizing.
- Transport errors do not trigger hidden retries.
- The application owns retry, deadline, degradation, restart, and safe-state
  decisions until a bounded health-monitor API is implemented.
