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
| ORT-UDP-006 | Verified | `udp_socket.hpp`, `udp_socket.cpp` | `test_udp_socket.cpp`, live vendor CI | `vendor_best_effort_writer.cpp` |
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
| ORT-SPDP-001 | Verified | `rtps/spdp.hpp` | `test_spdp.cpp` | `spdp_participants.cpp` |
| ORT-SPDP-002 | Verified | `rtps/spdp.hpp` | `test_spdp.cpp` | `spdp_participants.cpp` |
| ORT-SPDP-003 | Verified | `rtps/spdp.hpp` | `test_spdp.cpp` | — |
| ORT-SPDP-004 | Verified | `rtps/spdp.hpp` | `test_spdp.cpp` | `spdp_participants.cpp` |
| ORT-SPDP-005 | Verified | `rtps/spdp.hpp` | `test_spdp.cpp` | `spdp_participants.cpp` |
| ORT-SPDP-006 | Verified | `rtps/spdp.hpp` | `test_spdp.cpp` | `spdp_participants.cpp` |
| ORT-SEDP-001 | Verified | `rtps/sedp.hpp` | `test_sedp.cpp` | `sedp_matching.cpp` |
| ORT-SEDP-002 | Verified | `rtps/sedp.hpp` | `test_sedp.cpp` | — |
| ORT-SEDP-003 | Verified | `rtps/sedp.hpp` | `test_sedp.cpp` | `sedp_matching.cpp` |
| ORT-SEDP-004 | Verified | `rtps/sedp.hpp` | `test_sedp.cpp` | `sedp_matching.cpp` |
| ORT-SEDP-005 | Verified | `rtps/sedp.hpp` | `test_sedp.cpp` | `sedp_matching.cpp` |
| ORT-SEDP-006 | Verified | `rtps/sedp.hpp` | `test_sedp.cpp` | `sedp_matching.cpp` |
| ORT-ROUTE-001 | Verified | `rtps/message_router.hpp` | `test_message_router.cpp` | — |
| ORT-ROUTE-002 | Verified | `rtps/message_router.hpp` | `test_message_router.cpp` | — |
| ORT-ROUTE-003 | Verified | `rtps/message_router.hpp`, `rtps/data_message.hpp` | `test_message_router.cpp` | `compound_rtps_message.cpp` |
| ORT-ROUTE-004 | Verified | `rtps/message_router.hpp`, `rtps/reliability_messages.hpp` | `test_message_router.cpp` | — |
| ORT-ROUTE-005 | Verified | `rtps/message_router.hpp` | `test_message_router.cpp` | — |
| ORT-ROUTE-006 | Verified | `rtps/message_router.hpp` | `test_message_router.cpp` | `compound_rtps_message.cpp` |
| ORT-INT-001 | Verified | `tests/interop/emit_*.{c,cpp}`, `capture_spdp.py` | `vendor-spdp-packets` CI job | — |
| ORT-INT-002 | Verified | `tests/interop/capture_spdp.py`, `verify_fixtures.py` | pinned fixtures, CI artifacts | — |
| ORT-INT-003 | Verified | `tests/interop/vendor_packet_probe.cpp` | `vendor-spdp-packets` CI job | — |
| ORT-INT-004 | Verified | `tests/interop/verify_fixtures.py`, pinned vendor corpus | frozen fixture replay and SHA-256 verification, `vendor-packets` CI job | — |
| ORT-INT-005 | Verified | `rtps/sedp.cpp`, `tests/interop/emit_*.{c,cpp}`, `capture_sedp.py`, `vendor_packet_probe.cpp` | `test_sedp.cpp`, pinned SEDP fixtures, `vendor-packets` CI job | — |
| ORT-INT-006 | Verified | `rtps/data_message.hpp`, `tests/interop/emit_*.{c,cpp}`, `capture_data.py`, `vendor_packet_probe.cpp` | `test_capture_data.py`, pinned best-effort DATA fixtures, `vendor-packets` CI job | — |
| ORT-INT-007 | Verified | `rtps/data_message.hpp`, `rtps/reliability_messages.hpp`, `tests/interop/emit_*.{c,cpp}`, `capture_reliable.py`, `vendor_packet_probe.cpp` | `test_capture_reliable.py`, pinned reliable fixtures, `vendor-packets` CI job | — |
| ORT-INT-008 | Verified | `udp_socket.cpp`, `examples/vendor_best_effort_writer.cpp`, `tests/interop/emit_*.{c,cpp}` | `run_live_writer.py`, `vendor-packets` CI job | `vendor_best_effort_writer.cpp` |
| ORT-INT-009 | Verified | `examples/vendor_best_effort_reader.cpp`, `tests/interop/emit_*.{c,cpp}` | `test_run_live_reader.py`, `run_live_reader.py`, `vendor-packets` CI job | `vendor_best_effort_reader.cpp` |
| ORT-INT-010 | Verified | `rtps/reliability_state.hpp`, `examples/vendor_best_effort_writer.cpp`, `tests/interop/emit_*.{c,cpp}` | `test_run_live_writer.py`, `run_live_writer.py`, `vendor-packets` CI job | `vendor_best_effort_writer.cpp --reliable` |
| ORT-INT-011 | Verified | `rtps/reliability_state.hpp`, `examples/vendor_best_effort_reader.cpp`, `tests/interop/emit_*.{c,cpp}` | `test_run_live_reader.py`, `run_live_reader.py`, `vendor-packets` CI job | `vendor_best_effort_reader.cpp --reliable` |
| ORT-RMW-001 | Draft | — | Planned G4.1 test and inspection | — |
| ORT-RMW-002 | Draft | — | Planned lifecycle tests | — |
| ORT-RMW-003 | Draft | — | Planned node and graph tests | — |
| ORT-RMW-004 | Draft | — | Planned publisher tests and talker demonstration | Planned talker |
| ORT-RMW-005 | Draft | — | Planned subscription tests and listener demonstration | Planned listener |
| ORT-RMW-006 | Draft | — | Planned type-support tests and bound analysis | — |
| ORT-RMW-007 | Draft | — | Planned mapping vectors and inspection | — |
| ORT-RMW-008 | Draft | — | Planned QoS mapping/compatibility tests and analysis | — |
| ORT-RMW-009 | Draft | — | Planned wait/guard tests and race analysis | — |
| ORT-RMW-010 | Draft | — | Planned graph tests and multi-process demonstration | Planned graph probe |
| ORT-RMW-011 | Draft | — | Planned service tests and client/service demonstration | Planned service pair |
| ORT-RMW-012 | Draft | — | Planned GID and message-info tests | — |
| ORT-RMW-013 | Draft | — | Planned event/status tests | — |
| ORT-RMW-014 | Draft | — | Planned ABI error tests and inspection | — |
| ORT-RMW-015 | Draft | — | Planned exhaustion/allocation tests and analysis | — |
| ORT-RMW-016 | Draft | — | Planned concurrency/shutdown tests and analysis | — |
| ORT-RMW-017 | Draft | — | Planned feature-reporting tests and inspection | — |
| ORT-RMW-018 | Draft | — | Planned G4.1 build/load test | — |
| ORT-RMW-019 | Draft | — | Planned G4.2 topic tests and demonstration | Planned ROS topic pair |
| ORT-RMW-020 | Draft | — | Planned G4.3 graph tests and demonstration | Planned ROS graph pair |
| ORT-RMW-021 | Draft | — | Planned G4.4 service tests and demonstration | Planned ROS service pair |
| ORT-RMW-022 | Draft | — | Planned G4.5 conformance tests and analysis | — |
| ORT-RMW-023 | Verified | `rmw/foundation.hpp` | `test_rmw_foundation.cpp`, bounded-storage analysis | — |
| ORT-RMW-024 | Verified | `rmw/foundation.hpp` | `test_rmw_foundation.cpp` | `rmw_foundation_lifecycle.cpp` |
| ORT-RMW-025 | Verified | `rmw/foundation.hpp` | `test_rmw_foundation.cpp` | `rmw_foundation_lifecycle.cpp` |
| ORT-RMW-026 | Verified | `rmw/foundation.hpp` | `test_rmw_foundation.cpp` | `rmw_foundation_lifecycle.cpp` |
| ORT-RMW-027 | Verified | `rmw/foundation.hpp` | `test_rmw_foundation.cpp`, `rmw_openrtdds_cpp/BASELINE.md` inspection | — |
| ORT-RMW-028 | Verified | `rmw/foundation.hpp`, `rmw/foundation.cpp` | `test_rmw_foundation.cpp` | — |
| ORT-RMW-029 | Verified | `rmw_openrtdds_cpp/CMakeLists.txt`, `rmw_adapter.cpp` | Jazzy package/load/symbol CI, `test_rmw_lifecycle.cpp` | — |
| ORT-RMW-030 | Verified | `rmw_openrtdds_cpp/src/rmw_adapter.cpp` | `test_rmw_lifecycle.cpp` | — |
| ORT-RMW-031 | Verified | `rmw_openrtdds_cpp/src/rmw_adapter.cpp` | `test_rmw_lifecycle.cpp` | — |
| ORT-RMW-032 | Verified | `rmw_openrtdds_cpp/src/rmw_adapter.cpp` | `test_rmw_lifecycle.cpp` | — |
| ORT-RMW-033 | Verified | `rmw_adapter.cpp`, `abi_symbols.txt`, `check_rmw_symbols.py` | Jazzy package/load/symbol CI, `test_rmw_lifecycle.cpp` | — |
| ORT-RMW-034 | Verified | `rmw_adapter.cpp`, `rmw_unsupported.cpp`, `abi_symbols.txt` | exact symbol-manifest CI | — |
| ORT-RMW-035 | Verified | `rmw_unsupported.cpp` | `test_rmw_unsupported.cpp` | — |
| ORT-RMW-036 | Verified | `rmw_openrtdds_cpp/CMakeLists.txt`, complete ABI sources | proxy-linked `test_rmw_lifecycle.cpp`, Jazzy CI | — |

Paths in the matrix are relative to `include/openrtdds/`, `tests/`, or
`examples/` as appropriate. The machine checker independently tracks each
individual requirement ID.
