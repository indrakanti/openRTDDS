cmake_minimum_required(VERSION 3.20)
project(rmw_openrtdds_cpp VERSION 0.1.0 LANGUAGES CXX)

find_package(ament_cmake REQUIRED)
find_package(rcutils REQUIRED)
find_package(rmw 7.3 REQUIRED)
if(rmw_VERSION VERSION_LESS "7.3.3" OR
    NOT rmw_VERSION VERSION_LESS "7.4.0")
  message(FATAL_ERROR
    "rmw_openrtdds_cpp supports Jazzy rmw >=7.3.3 and <7.4.0; found ${rmw_VERSION}")
endif()
message(STATUS
  "Building rmw_openrtdds_cpp against supported rmw ${rmw_VERSION} "
  "(reviewed baseline 7.3.4)")

# The ROS-free core remains independently buildable. The adapter consumes it
# as a private implementation dependency and does not leak ROS headers into it.
set(OPENRTDDS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(OPENRTDDS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.."
  "${CMAKE_CURRENT_BINARY_DIR}/openrtdds-core" EXCLUDE_FROM_ALL)
set_target_properties(openrtdds PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(${PROJECT_NAME} SHARED
  src/rmw_adapter.cpp
  src/rmw_unsupported.cpp
)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(${PROJECT_NAME} PRIVATE
  OpenRTDDS::openrtdds
  rcutils::rcutils
  rmw::rmw
)
configure_rmw_library(${PROJECT_NAME})

# Register the implementation itself but deliberately register no type-support
# backend until the publisher/subscription boundary is implemented and tested.
register_rmw_implementation()
ament_export_libraries(${PROJECT_NAME})

install(TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)

if(BUILD_TESTING)
  enable_testing()
  find_package(rmw_implementation REQUIRED)

  add_executable(test_rmw_lifecycle test/test_rmw_lifecycle.cpp)
  target_compile_features(test_rmw_lifecycle PRIVATE cxx_std_17)
  target_compile_options(test_rmw_lifecycle PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_lifecycle PRIVATE
    ${PROJECT_NAME}
    rcutils::rcutils
    rmw::rmw
  )
  add_test(NAME rmw_openrtdds_lifecycle COMMAND test_rmw_lifecycle)

  add_executable(test_rmw_unsupported test/test_rmw_unsupported.cpp)
  target_compile_features(test_rmw_unsupported PRIVATE cxx_std_17)
  target_compile_options(test_rmw_unsupported PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_unsupported PRIVATE
    ${PROJECT_NAME}
    rcutils::rcutils
    rmw::rmw
  )
  add_test(NAME rmw_openrtdds_unsupported COMMAND test_rmw_unsupported)

  add_executable(test_rmw_runtime_selection test/test_rmw_lifecycle.cpp)
  target_compile_features(test_rmw_runtime_selection PRIVATE cxx_std_17)
  target_compile_options(test_rmw_runtime_selection PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_runtime_selection PRIVATE
    rmw_implementation::rmw_implementation
    rcutils::rcutils
    rmw::rmw
  )
  add_test(NAME rmw_openrtdds_runtime_selection
    COMMAND test_rmw_runtime_selection)
  set_tests_properties(rmw_openrtdds_runtime_selection PROPERTIES
    ENVIRONMENT "RMW_IMPLEMENTATION=rmw_openrtdds_cpp")
endif()

ament_package()
