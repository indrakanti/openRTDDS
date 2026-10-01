# Interoperability qualification plan

OpenRTDDS interoperability is an evidence-backed property, not a label applied
because the code uses RTPS field names. The project advances through the gates
below, and documentation must not claim a later gate before its CI evidence is
green.

| Gate | Evidence | Current state |
|---|---|---|
| G0 — Internal wire conformance | Golden RTPS 2.5 subset bytes and defensive parser tests | Passed |
| G1 — Compound message routing | INFO/PAD/unknown submessages plus DATA and reliability dispatch | Passed by PR13 |
| G2 — Vendor packet corpus | Pinned Fast DDS and Cyclone DDS packet captures parsed in CI | Passed by PR17 |
| G3 — Live DDS exchange | OpenRTDDS writer/reader exchanges discovery and data both ways with each vendor | Passed by PR21 for pinned best-effort and bounded reliable scope |
| G4 — ROS 2 RMW | `rmw_openrtdds_cpp` passes staged Jazzy build, topic, graph, service, and selected conformance gates | Planned by PR22 |

## Vendor matrix qualified for G2 and bounded G3 scope

| Peer | Discovery | Best-effort DATA | Reliable DATA | Direction |
|---|---|---|---|---|
| eProsima Fast DDS | SPDP + SEDP | Required | Required | Both |
| Eclipse Cyclone DDS | SPDP + SEDP | Required | Required | Both |

Fixture provenance shall record vendor name, exact version, configuration,
generator command, capture format, and expected result. Live CI shall pin
vendor versions and run in a network environment that supports UDP multicast.

PR14 captures SPDP packets with Fast DDS `2.11.2+ds-6.1build3` and Cyclone
DDS `0.10.4-1.1build3` on Ubuntu 24.04. Each run publishes the raw payload and
JSON provenance as a CI artifact. Frozen captures under
`tests/interop/fixtures/` also run in normal GCC/Clang CTest and their SHA-256
digests are checked on each PR. A green vendor job proves only the inbound SPDP
path for those exact versions and settings. PR15 adds a publications SEDP
capture and inbound parse gate against each pinned package. PR16 adds live and frozen best-effort
user DATA gates, correlated to same-run SEDP and SPDP evidence. The frozen
chains replay in normal GCC and Clang CTest without vendor packages, while the
live job regenerates the evidence from both pinned implementations. The corpus
requirements and behavior are in [vendor packet evidence](requirements/interoperability/requirements.md)
and [detailed design](design/interoperability/detailed-design.md).

PR17 adds live and frozen reliable DATA, HEARTBEAT, and ACKNACK chains for both
vendors. The production parsers correlate publisher and subscriber discovery,
control identities, the DATA sequence range, reliable QoS, and the fixed CDR
sample. This completes ORT-INT-004, promotes ORT-INT-007 to Verified, and
passes G2 for the pinned package versions and settings.

PR18 starts G3 with a live best-effort writer path from OpenRTDDS to both vendor
readers. OpenRTDDS joins the standard discovery multicast group, parses live
SPDP and subscription SEDP traffic, applies endpoint matching, and sends the
bounded probe sample to the discovered reader locator. The reverse vendor
writer to OpenRTDDS reader direction is implemented by PR19. The bounded
OpenRTDDS reader announces its subscription, repairs reliable built-in
discovery, correlates live SPDP/SEDP/DATA identities, and validates the exact
CDR sample. Both pinned vendors complete both live directions in CI, so G3 is
passed for the documented best-effort package versions and settings.

PR20 adds the first live reliable direction. The OpenRTDDS writer offers
reliable QoS, retains one bounded DATA datagram, emits directed HEARTBEATs,
processes correlated vendor ACKNACKs through the production reliability state
machine, permits at most two requested repairs inside three seconds, and
requires positive delivery acknowledgment. Fast DDS and Cyclone DDS readers
must both take the exact sample inside the shared 20-second CI deadline. The
reverse reliable vendor-writer direction remains a separate gate.

PR21 adds the reverse live reliable direction. The OpenRTDDS reader requests
reliable QoS, tracks an eight-sequence fixed receive window, processes DATA
before HEARTBEAT in compound datagrams, requests a missing vendor sequence,
and sends a directed final ACKNACK only after validating the exact sample.
Fast DDS and Cyclone DDS writers both complete inside the shared 20-second CI
deadline. This establishes bidirectional reliable exchange only for the exact
pinned versions and documented bounds.

## ROS 2 boundary

ROS 2 can run over OpenRTDDS only after a separate ROS middleware adapter,
`rmw_openrtdds`, implements the ROS middleware interface and OpenRTDDS supports
the DDS behavior used by ROS 2. RTPS wire support alone is insufficient. The
adapter follows G3 rather than preceding it because it depends on proven DDS
discovery, type support, QoS mapping, graph discovery, services, and clients.

PR22 divides G4 into evidence gates. G4.1 proves package discovery, ABI
loading, context/node lifecycle, and shutdown. G4.2 proves ROS topic
publish/wait/take for the bounded QoS and type subset. G4.3 proves distributed
graph add/query/removal behavior. G4.4 proves correlated service/client
request-response. G4.5 runs a version-controlled allowlist of upstream Jazzy
conformance tests and records every exclusion. Passing a lower gate does not
enable a general ROS 2 compatibility claim. The requirements and planned
interfaces are in [ROS 2 RMW requirements](requirements/ros2-rmw/requirements.md)
and [detailed design](design/ros2-rmw/detailed-design.md).

PR23 implements and verifies the ROS-independent bounded foundation for
context states, node slots, guard-condition generations, adapter errors, and
the pinned Jazzy `rmw` 7.3.4 baseline. It intentionally does not register an
RMW package or export the ROS C ABI, so G4.1 remains Planned.

PR24 adds the discoverable Jazzy ament package, loadable shared library, and a
tested C ABI slice for init options, contexts, nodes, and guard conditions. It
registers no type-support backend and does not implement the remaining
mandatory ABI, wait sets, graph propagation, endpoints, or services. G4.1
therefore remains Planned even though its package/load/lifecycle evidence has
started.

## Claim policy

With G3 passed, release notes may claim bounded live best-effort
interoperability with the exact pinned Fast DDS and Cyclone DDS versions and
settings. They shall not generalize this evidence to reliable live exchange,
drop-in DDS compatibility, other vendor versions, or ROS 2 readiness.

With ORT-INT-010 Verified, release notes may additionally claim the
OpenRTDDS reliable-writer to vendor-reader direction for those exact versions,
settings, history depth, retry bound, and repair window. Bidirectional reliable
interoperability remains unqualified until the reverse direction passes.

With ORT-INT-011 Verified, release notes may claim bounded bidirectional
reliable interoperability for the exact pinned versions and settings. They
shall not generalize that evidence to arbitrary histories, payload sizes,
vendor versions, drop-in DDS compatibility, or ROS 2 readiness.
