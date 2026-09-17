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
| Reliability control | `ReliabilityMessageError` | HEARTBEAT/ACKNACK construction, bounds, subset, or parse failure |
| Reliability state | `ReliabilityError` + `RepairFailure` | history, receive window, count ordering, repair, or timing result |
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

Current state-machine mappings are:

| Reliability state result | Fault code |
|---|---|
| terminal `repair_limit_exceeded` or `repair_window_expired` action | `ORT-FLT-REL-001` |
| `gap_not_repairable` | `ORT-FLT-REL-001` |
| `invalid_sequence_number` | `ORT-FLT-RTPS-003` |
| `time_regression` | `ORT-FLT-TIME-001` |
| `history_full`, `receive_window_exceeded`, `action_capacity_exceeded` | resource/configuration policy |
| `stale_control`, `duplicate_data`, `stale_data` | observable status; no fault by itself |

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
