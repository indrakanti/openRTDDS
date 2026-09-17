# Requirements traceability

This matrix is the human-reviewed index. CI independently derives references
from source tags and rejects missing or unknown evidence.

| Requirement | Status | Implementation | Verification | Example |
|---|---|---|---|---|
| ORT-CORE-001 | Verified | `runtime_limits.hpp` | `test_runtime_limits.cpp` | — |
| ORT-CORE-002 | Verified | `static_pool.hpp` | `test_static_pool.cpp` | — |
| ORT-LNX-001 | Verified | `realtime.hpp` | Demonstration | `runtime_probe.cpp` |
| ORT-LNX-002 | Verified | `realtime.hpp` | `test_realtime.cpp` | — |
| ORT-LNX-003 | Verified | `realtime.hpp` | `test_realtime.cpp` | — |
| ORT-LNX-004 | Verified | `realtime.hpp` | `test_realtime.cpp` | `runtime_probe.cpp` |
| ORT-SER-001 | Verified | `cdr.hpp` | `test_cdr.cpp` | `vehicle_state.cpp`, `rtps_static_message.cpp` |
| ORT-SER-002 | Verified | `cdr.hpp` | `test_cdr.cpp` | `vehicle_state.cpp` |
| ORT-SER-003 | Verified | `cdr.hpp` | `test_cdr.cpp` | `vehicle_state.cpp` |
| ORT-SER-004 | Verified | `cdr.hpp` | `test_cdr.cpp` | `vehicle_state.cpp` |
| ORT-SER-005 | Verified | `cdr.hpp` | `test_cdr.cpp` | — |
| ORT-SER-006 | Verified | `cdr.hpp` | `test_cdr.cpp` | — |
| ORT-HIST-001 | Verified | `keep_last_history.hpp` | `test_keep_last_history.cpp` | — |
| ORT-HIST-002 | Verified | `keep_last_history.hpp` | `test_keep_last_history.cpp` | — |
| ORT-HIST-003 | Verified | `keep_last_history.hpp` | `test_keep_last_history.cpp` | — |
| ORT-HIST-004 | Verified | `keep_last_history.hpp` | `test_keep_last_history.cpp` | — |
| ORT-RTPS-001 | Verified | `data_message.hpp` | `test_rtps_data_message.cpp` | `rtps_static_message.cpp` |
| ORT-RTPS-002 | Verified | `data_message.hpp` | `test_rtps_data_message.cpp` | `rtps_static_message.cpp` |
| ORT-RTPS-003 | Verified | `data_message.hpp` | `test_rtps_data_message.cpp` | `rtps_static_message.cpp` |
| ORT-RTPS-004 | Verified | `data_message.hpp` | `test_rtps_data_message.cpp` | `rtps_static_message.cpp` |
| ORT-RTPS-005 | Verified | `data_message.hpp` | `test_rtps_data_message.cpp` | `rtps_static_message.cpp` |
| ORT-RTPS-006 | Verified | `data_message.hpp` | `test_rtps_data_message.cpp` | — |
| ORT-RTPS-007 | Verified | `data_message.hpp` | `test_rtps_data_message.cpp` | — |
| ORT-UDP-001 | Verified | `udp_socket.hpp` | `test_udp_socket.cpp` | — |
| ORT-UDP-002 | Verified | `udp_socket.hpp` | `test_udp_socket.cpp` | — |
| ORT-UDP-003 | Verified | `udp_socket.hpp` | `test_udp_socket.cpp` | — |
| ORT-UDP-004 | Verified | `udp_socket.hpp` | `test_udp_socket.cpp` | — |
| ORT-UDP-005 | Verified | `udp_socket.hpp` | `test_udp_socket.cpp` | — |
| ORT-REL-001 | Verified | `rtps/reliability_state.hpp` | `test_reliability_state.cpp` | `reliable_pair.cpp` |
| ORT-REL-002 | Verified | `rtps/reliability_messages.hpp` | `test_reliability_messages.cpp` | `rtps_reliability_message.cpp` |
| ORT-REL-003 | Verified | `rtps/reliability_messages.hpp` | `test_reliability_messages.cpp` | `rtps_reliability_message.cpp` |
| ORT-REL-004 | Verified | `rtps/reliability_state.hpp` | `test_reliability_state.cpp` | — |
| ORT-REL-005 | Verified | `rtps/reliability_state.hpp` | `test_reliability_state.cpp` | — |
| ORT-REL-006 | Verified | `rtps/reliability_state.hpp` | `test_reliability_state.cpp` | `reliable_pair.cpp` |
| ORT-DDS-001 | Verified | `dds/static_entities.hpp` | `test_static_dds.cpp` | `static_dds_udp.cpp` |
| ORT-DDS-002 | Verified | `dds/static_entities.hpp` | `test_static_dds.cpp` | `static_dds_udp.cpp` |
| ORT-DDS-003 | Verified | `dds/static_entities.hpp` | `test_static_dds.cpp` | `static_dds_udp.cpp` |
| ORT-DDS-004 | Verified | `dds/static_entities.hpp` | `test_static_dds.cpp` | `static_dds_udp.cpp` |
| ORT-DDS-005 | Verified | `dds/static_entities.hpp` | `test_static_dds.cpp` | `static_dds_udp.cpp` |
| ORT-DDS-006 | Verified | `dds/static_entities.hpp` | `test_static_dds.cpp` | `static_dds_udp.cpp` |

Paths in the matrix are relative to `include/openrtdds/`, `tests/`, or
`examples/` as appropriate. The machine checker independently tracks each
individual requirement ID.
