# RMW QoS normalization and endpoint resource-plan detailed design

**Design status:** Current for deterministic normalization, requested/offered
compatibility, and pre-creation resource planning; endpoint consumption is
Planned in PR30 and PR31

**Requirements:** ORT-RMW-043, ORT-RMW-044

## Scope and invariants

This component converts the pinned ROS 2 Jazzy `rmw_qos_profile_t` ABI into one
canonical profile before an endpoint can reserve storage. It does not create a
DDS writer or reader. The following invariants apply to all three APIs:

- no heap allocation, locks, waits, retries, clocks, or network operations;
- execution is bounded by a fixed set of QoS fields;
- normalization and planning commit their output only after full validation;
- `QosError` values are immediate API results, not stable numeric fault codes;
- future endpoint creation, SEDP, matching, and actual-QoS reporting consume
  the same `NormalizedQos` object without rereading the raw profile.

## Public types and ownership

| Type | Responsibility | Ownership/lifetime |
|---|---|---|
| `QosLimits` | default and maximum history depth, maximum serialized sample, maximum history bytes | copied input; caller retains no referenced storage |
| `NormalizedQos` | fully explicit accepted Jazzy profile | value object owned by caller or future endpoint |
| `EndpointRole` | selects publisher or subscription resource semantics | value enum |
| `EndpointResourcePlan` | accepted QoS plus exact maximum sample, history slots/bytes, and reliable repair slots | value object committed atomically |
| `QosError` | exact normalization/planning rejection | symbolic value; integer representation is not persistent |

The API is declared in `rmw_openrtdds_cpp/qos.hpp`. The C ABI function
`rmw_qos_profile_check_compatible` retains its upstream Jazzy signature and
writes only into caller-owned output buffers.

## Normalization behavior

| Input policy | Accepted canonical value | Rejection |
|---|---|---|
| history | KEEP_LAST; SYSTEM_DEFAULT becomes KEEP_LAST | KEEP_ALL → `unsupported_history`; UNKNOWN/invalid → `invalid_policy` |
| depth | explicit positive depth; SYSTEM_DEFAULT history uses `default_history_depth` | zero or above maximum → `depth_out_of_range` |
| reliability | BEST_EFFORT or RELIABLE; SYSTEM_DEFAULT becomes RELIABLE | BEST_AVAILABLE → `unsupported_best_available`; UNKNOWN/invalid → `invalid_policy` |
| durability | VOLATILE; SYSTEM_DEFAULT becomes VOLATILE | TRANSIENT_LOCAL → `unsupported_durability`; BEST_AVAILABLE/UNKNOWN rejected |
| deadline | unspecified or infinite becomes explicit infinite | finite → `unsupported_deadline`; best available rejected |
| lifespan | unspecified or infinite becomes explicit infinite | finite → `unsupported_lifespan` |
| liveliness | AUTOMATIC; SYSTEM_DEFAULT becomes AUTOMATIC | manual modes → `unsupported_liveliness`; BEST_AVAILABLE/UNKNOWN rejected |
| lease | unspecified or infinite becomes explicit infinite | finite → `unsupported_lease_duration`; best available rejected |
| namespace convention | preserved unchanged | none |

A duration with nanoseconds at or above one second is malformed and returns
`invalid_argument`. Discovery-dependent BEST_AVAILABLE policies are rejected
because their selection would depend on endpoint discovery order.

```mermaid
sequenceDiagram
    participant C as Endpoint creator
    participant Q as QoS normalizer
    participant P as Resource planner
    C->>Q: raw profile + limits
    Q->>Q: validate every policy
    alt accepted
        Q-->>C: canonical profile
        C->>P: profile + type bound + role
        P->>P: check multiplication and limits
        P-->>C: exact reservation plan
    else rejected
        Q-->>C: QosError; prior output unchanged
    end
```

## Compatibility behavior

`qos_compatible` accepts normalized profiles and applies the DDS
requested/offered relation: a best-effort publisher cannot satisfy a reliable
subscription, and a volatile publisher cannot satisfy a transient-local
subscription. The latter cannot be produced by the current normalizer but is
retained as a defensive relation for future profile expansion.

The standard `rmw_qos_profile_check_compatible` API also examines raw profiles:

- known reliability, durability, deadline, liveliness kind, and lease-duration
  incompatibilities produce `RMW_QOS_COMPATIBILITY_ERROR`;
- SYSTEM_DEFAULT, UNKNOWN, or BEST_AVAILABLE values that prevent a definite
  enum comparison, and BEST_AVAILABLE durations, produce
  `RMW_QOS_COMPATIBILITY_WARNING`; unspecified durations mean no finite
  requirement and are compared deterministically;
- definite errors take precedence over warnings;
- error explanations precede warnings and are semicolon separated;
- the caller buffer is always NUL terminated when its size is nonzero;
- a null compatibility pointer, inconsistent reason pointer/size, or malformed
  duration returns `RMW_RET_INVALID_ARGUMENT` and sets the RMW error state.

History/depth and lifespan do not participate in DDS requested/offered
compatibility. They remain subject to creation-time normalization.

## Resource equations and bounds

After normalization and ORT-RMW-040 type analysis:

\[
\text{history slots} = \text{normalized depth}
\]

\[
\text{history bytes} = \text{history slots} \times
\text{maximum serialized sample size}
\]

Reliable publisher repair slots equal history slots. Best-effort publishers
and all subscriptions reserve zero writer-repair slots in this plan. The
future subscription implementation still reserves its reported history slots
for receive samples.

Multiplication is checked before evaluation. A zero sample size, sample above
`maximum_sample_size`, or result above `maximum_history_bytes` is rejected.
The plan is a capacity declaration; it performs no allocation itself.

## API contract

| Function | Preconditions | Success | Failure atomicity |
|---|---|---|---|
| `normalize_qos` | valid `QosLimits` and Jazzy profile | explicit `NormalizedQos`, `QosError::none` | output unchanged; exact `QosError` |
| `plan_endpoint_resources` | valid role, analyzed nonzero type bound, accepted profile and limits | exact `EndpointResourcePlan` | output unchanged; exact `QosError` |
| `qos_compatible` | profiles already normalized | requested/offered Boolean | no output mutation |
| `rmw_qos_profile_check_compatible` | valid output pointers and duration representation | compatibility enum and bounded reason | invalid arguments leave compatibility unchanged |

## Errors, faults, and recovery

`QosError` string names are stable diagnostic text for the current release,
while enum integer values are not an ABI promise. Endpoint-configuration
rejections map to `ORT-FLT-RMW-006`; exhausted byte/sample bounds additionally
map to resource fault `ORT-FLT-RMW-002`. The application corrects the profile
or startup limits and retries endpoint creation. There is no automatic
downgrade, allocation growth, hidden retry, or fallback to discovery-selected
QoS.

## Verification

`test_qos.cpp` covers canonical defaults, every rejection family, atomic output,
exact history math, byte bounds, arithmetic overflow, normalized compatibility,
the C ABI OK/WARNING/ERROR results, invalid arguments, reason text, and bounded
truncation. `qos_resource_plan.cpp` demonstrates planning the standard Jazzy
sensor-data profile. Jazzy sanitizer CI builds and executes the test with the
pinned RMW ABI.

## Deferred behavior

- transient-local durability and durable late-joiner history;
- finite deadline/lifespan monitors and finite liveliness leases;
- manual liveliness assertion;
- BEST_AVAILABLE discovery-driven selection;
- endpoint allocation and plan consumption (PR30/PR31);
- incompatible-QoS event publication (later G4.5 work).
