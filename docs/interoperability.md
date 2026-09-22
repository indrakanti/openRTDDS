# Interoperability qualification plan

OpenRTDDS interoperability is an evidence-backed property, not a label applied
because the code uses RTPS field names. The project advances through the gates
below, and documentation must not claim a later gate before its CI evidence is
green.

| Gate | Evidence | Current state |
|---|---|---|
| G0 — Internal wire conformance | Golden RTPS 2.5 subset bytes and defensive parser tests | Passed |
| G1 — Compound message routing | INFO/PAD/unknown submessages plus DATA and reliability dispatch | Passed by PR13 |
| G2 — Vendor packet corpus | Pinned Fast DDS and Cyclone DDS packet captures parsed in CI | Partial: SPDP and SEDP receive (PR14 and PR15); user DATA pending |
| G3 — Live DDS exchange | OpenRTDDS writer/reader exchanges discovery and data both ways with each vendor | Planned |
| G4 — ROS 2 RMW | `rmw_openrtdds` passes selected ROS 2 conformance and graph tests | Planned |

## Vendor matrix planned for G2 and G3

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
capture and inbound parse gate against each pinned package. G2 remains partial
until best-effort and reliable user DATA also pass. The corpus requirements and behavior
are in [vendor packet evidence](requirements/interoperability/requirements.md)
and [detailed design](design/interoperability/detailed-design.md).

## ROS 2 boundary

ROS 2 can run over OpenRTDDS only after a separate ROS middleware adapter,
`rmw_openrtdds`, implements the ROS middleware interface and OpenRTDDS supports
the DDS behavior used by ROS 2. RTPS wire support alone is insufficient. The
adapter follows G3 rather than preceding it because it depends on proven DDS
discovery, type support, QoS mapping, graph discovery, services, and clients.

## Claim policy

Until G3 passes, release notes may say “RTPS 2.5-oriented subset” and identify
the passed gates. They shall not say “Fast DDS compatible,” “Cyclone DDS
compatible,” “drop-in DDS,” or “ROS 2 ready.”
