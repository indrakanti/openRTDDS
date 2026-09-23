cmake_minimum_required(VERSION 3.20)

project(
  OpenRTDDS
  VERSION 0.1.0
  DESCRIPTION "Deterministic, safety-oriented DDS for Linux"
  LANGUAGES CXX
)

option(OPENRTDDS_BUILD_TESTS "Build OpenRTDDS tests" ON)
option(OPENRTDDS_BUILD_EXAMPLES "Build OpenRTDDS examples" ON)
option(OPENRTDDS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

add_library(openrtdds
  src/dds/static_entities.cpp
  src/core/runtime_limits.cpp
  src/core/version.cpp
  src/os/linux/realtime.cpp
  src/rtps/data_message.cpp
  src/rtps/message_router.cpp
  src/rtps/reliability_messages.cpp
  src/rtps/reliability_state.cpp
  src/rtps/sedp.cpp
  src/rtps/spdp.cpp
  src/serialization/cdr.cpp
  src/transport/udp_socket.cpp
)
add_library(OpenRTDDS::openrtdds ALIAS openrtdds)

target_compile_features(openrtdds PUBLIC cxx_std_17)
target_include_directories(openrtdds
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_options(openrtdds PRIVATE -Wall -Wextra -Wpedantic)
if(OPENRTDDS_WARNINGS_AS_ERRORS)
  target_compile_options(openrtdds PRIVATE -Werror)
endif()
target_link_libraries(openrtdds PUBLIC pthread)

if(OPENRTDDS_BUILD_EXAMPLES)
  add_executable(openrtdds_runtime_probe examples/runtime_probe.cpp)
  target_link_libraries(openrtdds_runtime_probe PRIVATE OpenRTDDS::openrtdds)

  add_executable(openrtdds_vehicle_state examples/vehicle_state.cpp)
  target_link_libraries(openrtdds_vehicle_state PRIVATE OpenRTDDS::openrtdds)

  add_executable(openrtdds_rtps_static_message
    examples/rtps_static_message.cpp
  )
  target_link_libraries(openrtdds_rtps_static_message
    PRIVATE OpenRTDDS::openrtdds
  )

  add_executable(openrtdds_rtps_reliability_message
    examples/rtps_reliability_message.cpp
  )
  target_link_libraries(openrtdds_rtps_reliability_message
    PRIVATE OpenRTDDS::openrtdds
  )

  add_executable(openrtdds_reliable_pair examples/reliable_pair.cpp)
  target_link_libraries(openrtdds_reliable_pair
    PRIVATE OpenRTDDS::openrtdds
  )

  add_executable(openrtdds_static_dds_udp examples/static_dds_udp.cpp)
  target_link_libraries(openrtdds_static_dds_udp
    PRIVATE OpenRTDDS::openrtdds
  )

  add_executable(openrtdds_spdp_participants examples/spdp_participants.cpp)
  target_link_libraries(openrtdds_spdp_participants
    PRIVATE OpenRTDDS::openrtdds
  )

  add_executable(openrtdds_sedp_matching examples/sedp_matching.cpp)
  target_link_libraries(openrtdds_sedp_matching
    PRIVATE OpenRTDDS::openrtdds
  )

  add_executable(openrtdds_compound_rtps_message
    examples/compound_rtps_message.cpp
  )
  target_link_libraries(openrtdds_compound_rtps_message
    PRIVATE OpenRTDDS::openrtdds
  )

  foreach(example_target IN ITEMS
      openrtdds_runtime_probe
      openrtdds_vehicle_state
      openrtdds_rtps_static_message
      openrtdds_rtps_reliability_message
      openrtdds_reliable_pair
      openrtdds_static_dds_udp
      openrtdds_spdp_participants
      openrtdds_sedp_matching
      openrtdds_compound_rtps_message)
    target_compile_options(${example_target} PRIVATE -Wall -Wextra -Wpedantic)
    if(OPENRTDDS_WARNINGS_AS_ERRORS)
      target_compile_options(${example_target} PRIVATE -Werror)
    endif()
  endforeach()
endif()

include(CTest)
if(BUILD_TESTING AND OPENRTDDS_BUILD_TESTS)
  add_executable(openrtdds_vendor_packet_probe
    tests/interop/vendor_packet_probe.cpp
  )
  target_link_libraries(openrtdds_vendor_packet_probe
    PRIVATE OpenRTDDS::openrtdds
  )
  target_compile_options(openrtdds_vendor_packet_probe
    PRIVATE -Wall -Wextra -Wpedantic
  )
  if(OPENRTDDS_WARNINGS_AS_ERRORS)
    target_compile_options(openrtdds_vendor_packet_probe PRIVATE -Werror)
  endif()

  add_executable(openrtdds_unit_tests
    tests/test_main.cpp
    tests/test_runtime_limits.cpp
    tests/test_cdr.cpp
    tests/test_keep_last_history.cpp
    tests/test_message_router.cpp
    tests/test_realtime.cpp
    tests/test_reliability_messages.cpp
    tests/test_reliability_state.cpp
    tests/test_rtps_data_message.cpp
    tests/test_static_pool.cpp
    tests/test_static_dds.cpp
    tests/test_sedp.cpp
    tests/test_spdp.cpp
    tests/test_udp_socket.cpp
  )
  target_link_libraries(openrtdds_unit_tests PRIVATE OpenRTDDS::openrtdds)
  target_compile_options(openrtdds_unit_tests PRIVATE -Wall -Wextra -Wpedantic)
  if(OPENRTDDS_WARNINGS_AS_ERRORS)
    target_compile_options(openrtdds_unit_tests PRIVATE -Werror)
  endif()
  add_test(NAME openrtdds_unit_tests COMMAND openrtdds_unit_tests)
  add_test(NAME openrtdds_fastdds_spdp_fixture
    COMMAND openrtdds_vendor_packet_probe
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/spdp.rtps
  )
  add_test(NAME openrtdds_cyclonedds_spdp_fixture
    COMMAND openrtdds_vendor_packet_probe
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/cyclonedds/0.10.4-1.1build3/spdp.rtps
  )
  add_test(NAME openrtdds_fastdds_sedp_fixture
    COMMAND openrtdds_vendor_packet_probe --sedp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/sedp.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/sedp-participant.rtps
  )
  add_test(NAME openrtdds_cyclonedds_sedp_fixture
    COMMAND openrtdds_vendor_packet_probe --sedp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/cyclonedds/0.10.4-1.1build3/sedp.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/cyclonedds/0.10.4-1.1build3/sedp-participant.rtps
  )
  add_test(NAME openrtdds_sedp_rejects_foreign_participant
    COMMAND openrtdds_vendor_packet_probe --sedp
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/sedp.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/cyclonedds/0.10.4-1.1build3/sedp-participant.rtps
  )
  set_tests_properties(openrtdds_sedp_rejects_foreign_participant
    PROPERTIES WILL_FAIL TRUE)
  add_test(NAME openrtdds_fastdds_best_effort_data_fixture
    COMMAND openrtdds_vendor_packet_probe --data
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/data-best-effort.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/data-best-effort-endpoint.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/data-best-effort-participant.rtps
  )
  add_test(NAME openrtdds_cyclonedds_best_effort_data_fixture
    COMMAND openrtdds_vendor_packet_probe --data
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/cyclonedds/0.10.4-1.1build3/data-best-effort.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/cyclonedds/0.10.4-1.1build3/data-best-effort-endpoint.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/cyclonedds/0.10.4-1.1build3/data-best-effort-participant.rtps
  )
  add_test(NAME openrtdds_data_rejects_foreign_endpoint
    COMMAND openrtdds_vendor_packet_probe --data
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/data-best-effort.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/cyclonedds/0.10.4-1.1build3/data-best-effort-endpoint.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/data-best-effort-participant.rtps
  )
  add_test(NAME openrtdds_data_rejects_foreign_participant
    COMMAND openrtdds_vendor_packet_probe --data
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/data-best-effort.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/fastdds/2.11.2+ds-6.1build3/data-best-effort-endpoint.rtps
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop/fixtures/cyclonedds/0.10.4-1.1build3/data-best-effort-participant.rtps
  )
  set_tests_properties(
    openrtdds_data_rejects_foreign_endpoint
    openrtdds_data_rejects_foreign_participant
    PROPERTIES WILL_FAIL TRUE)
  if(OPENRTDDS_BUILD_EXAMPLES)
    add_test(NAME openrtdds_vehicle_state_example COMMAND openrtdds_vehicle_state)
    add_test(NAME openrtdds_rtps_static_message_example
      COMMAND openrtdds_rtps_static_message
    )
    add_test(NAME openrtdds_rtps_reliability_message_example
      COMMAND openrtdds_rtps_reliability_message
    )
    add_test(NAME openrtdds_reliable_pair_example
      COMMAND openrtdds_reliable_pair
    )
    add_test(NAME openrtdds_static_dds_udp_example
      COMMAND openrtdds_static_dds_udp
    )
    add_test(NAME openrtdds_spdp_participants_example
      COMMAND openrtdds_spdp_participants
    )
    add_test(NAME openrtdds_sedp_matching_example
      COMMAND openrtdds_sedp_matching
    )
    add_test(NAME openrtdds_compound_rtps_message_example
      COMMAND openrtdds_compound_rtps_message
    )
  endif()
endif()
