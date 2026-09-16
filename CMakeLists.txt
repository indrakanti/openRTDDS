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
  src/core/runtime_limits.cpp
  src/core/version.cpp
  src/os/linux/realtime.cpp
  src/serialization/cdr.cpp
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
endif()

include(CTest)
if(BUILD_TESTING AND OPENRTDDS_BUILD_TESTS)
  add_executable(openrtdds_unit_tests
    tests/test_main.cpp
    tests/test_runtime_limits.cpp
    tests/test_cdr.cpp
    tests/test_keep_last_history.cpp
    tests/test_static_pool.cpp
  )
  target_link_libraries(openrtdds_unit_tests PRIVATE OpenRTDDS::openrtdds)
  target_compile_options(openrtdds_unit_tests PRIVATE -Wall -Wextra -Wpedantic)
  if(OPENRTDDS_WARNINGS_AS_ERRORS)
    target_compile_options(openrtdds_unit_tests PRIVATE -Werror)
  endif()
  add_test(NAME openrtdds_unit_tests COMMAND openrtdds_unit_tests)
endif()
