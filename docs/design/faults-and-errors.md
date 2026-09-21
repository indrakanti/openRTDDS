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
| `ORT-FLT-UDP-001` | Fatal at startup | socket, bind, or endpoint-query failure | startup controller |
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

Severity is deployment-configurable except where the selected deterministic
profile cannot satisfy its assumptions. The component detecting an API error
does not decide system-level safe state.

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
