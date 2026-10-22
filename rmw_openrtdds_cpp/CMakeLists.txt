cmake_minimum_required(VERSION 3.20)
project(rmw_openrtdds_cpp VERSION 0.1.0 LANGUAGES C CXX)

option(OPENRTDDS_RMW_ENABLE_SANITIZERS
  "Enable AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer"
  OFF)

if(OPENRTDDS_RMW_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR
      "OPENRTDDS_RMW_ENABLE_SANITIZERS requires GCC or Clang")
  endif()
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
endif()

find_package(ament_cmake REQUIRED)
find_package(Threads REQUIRED)
find_package(rcutils REQUIRED)
find_package(rmw 7.3 REQUIRED)
find_package(rosidl_runtime_c REQUIRED)
find_package(rosidl_typesupport_introspection_c REQUIRED)
find_package(rosidl_typesupport_introspection_cpp REQUIRED)
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
  src/events.cpp
  src/graph.cpp
  src/network_runtime.cpp
  src/publisher.cpp
  src/qos.cpp
  src/rmw_adapter.cpp
  src/rmw_unsupported.cpp
  src/services.cpp
  src/subscription.cpp
  src/type_support.cpp
  src/wait.cpp
  src/xcdr1_codec.cpp
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  RMW_OPENRTDDS_CPP_BUILDING_DLL)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_include_directories(${PROJECT_NAME} PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  "$<INSTALL_INTERFACE:include/${PROJECT_NAME}>"
)
target_link_libraries(${PROJECT_NAME} PRIVATE
  OpenRTDDS::openrtdds
  Threads::Threads
  rcutils::rcutils
  rmw::rmw
  rosidl_runtime_c::rosidl_runtime_c
  rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
  rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp
)
configure_rmw_library(${PROJECT_NAME})

add_executable(openrtdds_rmw_qos_plan examples/qos_resource_plan.cpp)
target_compile_features(openrtdds_rmw_qos_plan PRIVATE cxx_std_17)
target_compile_options(openrtdds_rmw_qos_plan PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(openrtdds_rmw_qos_plan PRIVATE
  ${PROJECT_NAME}
  rmw::rmw
)

add_executable(openrtdds_rmw_publisher_pipeline
  examples/publisher_pipeline.cpp)
target_compile_features(openrtdds_rmw_publisher_pipeline PRIVATE cxx_std_17)
target_compile_options(openrtdds_rmw_publisher_pipeline PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(openrtdds_rmw_publisher_pipeline PRIVATE
  ${PROJECT_NAME}
  rmw::rmw
  rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
)

add_executable(openrtdds_rmw_subscription_pipeline
  examples/subscription_pipeline.cpp)
target_compile_features(openrtdds_rmw_subscription_pipeline PRIVATE cxx_std_17)
target_compile_options(openrtdds_rmw_subscription_pipeline PRIVATE
  -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(openrtdds_rmw_subscription_pipeline PRIVATE
  ${PROJECT_NAME}
  rmw::rmw
  rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
)

# Register both bounded introspection backends used by the networked pub/sub
# path.  Generic ROS-generated handles are resolved to one of these backends.
register_rmw_implementation(
  "c:rosidl_typesupport_introspection_c"
  "cpp:rosidl_typesupport_introspection_cpp")
ament_export_libraries(${PROJECT_NAME})
ament_export_dependencies(
  rmw
  rosidl_runtime_c
  rosidl_typesupport_introspection_c
  rosidl_typesupport_introspection_cpp
)

install(TARGETS ${PROJECT_NAME}
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(TARGETS
  openrtdds_rmw_qos_plan
  openrtdds_rmw_publisher_pipeline
  openrtdds_rmw_subscription_pipeline
  RUNTIME DESTINATION lib/${PROJECT_NAME}
)
install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME})

if(BUILD_TESTING)
  enable_testing()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  find_package(rcl REQUIRED)
  find_package(rclcpp REQUIRED)
  find_package(rmw_implementation REQUIRED)
  find_package(example_interfaces REQUIRED)
  find_package(std_msgs REQUIRED)

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

  add_executable(test_rmw_type_support test/test_type_support.cpp)
  target_compile_features(test_rmw_type_support PRIVATE cxx_std_17)
  target_compile_options(test_rmw_type_support PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_type_support PRIVATE
    ${PROJECT_NAME}
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
    rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp
  )
  add_test(NAME rmw_openrtdds_type_support COMMAND test_rmw_type_support)

  add_executable(test_rmw_xcdr1_codec test/test_xcdr1_codec.cpp)
  target_compile_features(test_rmw_xcdr1_codec PRIVATE cxx_std_17)
  target_compile_options(test_rmw_xcdr1_codec PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_xcdr1_codec PRIVATE
    ${PROJECT_NAME}
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
    rosidl_typesupport_introspection_cpp::rosidl_typesupport_introspection_cpp
  )
  add_test(NAME rmw_openrtdds_xcdr1_codec COMMAND test_rmw_xcdr1_codec)

  add_executable(test_rmw_qos test/test_qos.cpp)
  target_compile_features(test_rmw_qos PRIVATE cxx_std_17)
  target_compile_options(test_rmw_qos PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_qos PRIVATE
    ${PROJECT_NAME}
    rcutils::rcutils
    rmw::rmw
  )
  add_test(NAME rmw_openrtdds_qos COMMAND test_rmw_qos)

  add_executable(test_rmw_publisher test/test_publisher.cpp)
  target_compile_features(test_rmw_publisher PRIVATE cxx_std_17)
  target_compile_options(test_rmw_publisher PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_publisher PRIVATE
    ${PROJECT_NAME}
    OpenRTDDS::openrtdds
    rcutils::rcutils
    rmw::rmw
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
  )
  add_test(NAME rmw_openrtdds_publisher COMMAND test_rmw_publisher)

  add_executable(test_rmw_subscription test/test_subscription.cpp)
  target_compile_features(test_rmw_subscription PRIVATE cxx_std_17)
  target_compile_options(test_rmw_subscription PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_subscription PRIVATE
    ${PROJECT_NAME}
    OpenRTDDS::openrtdds
    rcutils::rcutils
    rmw::rmw
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
  )
  add_test(NAME rmw_openrtdds_subscription COMMAND test_rmw_subscription)

  add_executable(test_rmw_wait test/test_wait.cpp)
  target_compile_features(test_rmw_wait PRIVATE cxx_std_17)
  target_compile_options(test_rmw_wait PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_wait PRIVATE
    ${PROJECT_NAME}
    Threads::Threads
    rcutils::rcutils
    rmw::rmw
  )
  add_test(NAME rmw_openrtdds_wait COMMAND test_rmw_wait)

  add_executable(test_rmw_events test/test_events.cpp)
  target_compile_features(test_rmw_events PRIVATE cxx_std_17)
  target_compile_options(test_rmw_events PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_events PRIVATE
    ${PROJECT_NAME}
    OpenRTDDS::openrtdds
    Threads::Threads
    rcutils::rcutils
    rmw::rmw
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
  )
  add_test(NAME rmw_openrtdds_events COMMAND test_rmw_events)
  set_tests_properties(rmw_openrtdds_events PROPERTIES TIMEOUT 20)

  add_test(NAME rmw_openrtdds_conformance_allowlist
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/test/check_conformance_allowlist.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/test/jazzy_conformance_allowlist.txt"
      "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt")

  add_executable(test_rmw_services test/test_services.cpp)
  target_compile_features(test_rmw_services PRIVATE cxx_std_17)
  target_compile_options(test_rmw_services PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_services PRIVATE
    ${PROJECT_NAME}
    Threads::Threads
    rcutils::rcutils
    rmw::rmw
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
  )
  add_test(NAME rmw_openrtdds_services COMMAND test_rmw_services)
  set_tests_properties(rmw_openrtdds_services PROPERTIES TIMEOUT 20)

  add_executable(test_rmw_graph test/test_graph.cpp)
  target_compile_features(test_rmw_graph PRIVATE cxx_std_17)
  target_compile_options(test_rmw_graph PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_graph PRIVATE
    ${PROJECT_NAME}
    rcutils::rcutils
    rmw::rmw
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
  )
  add_test(NAME rmw_openrtdds_graph COMMAND test_rmw_graph)

  add_executable(test_rmw_network_runtime test/test_network_runtime.cpp)
  target_compile_features(test_rmw_network_runtime PRIVATE cxx_std_17)
  target_compile_options(test_rmw_network_runtime PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_network_runtime PRIVATE
    ${PROJECT_NAME}
    OpenRTDDS::openrtdds
    Threads::Threads
    rcutils::rcutils
    rmw::rmw
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
  )
  add_test(NAME rmw_openrtdds_network_runtime
    COMMAND test_rmw_network_runtime)
  set_tests_properties(rmw_openrtdds_network_runtime PROPERTIES TIMEOUT 20)

  add_executable(openrtdds_ros2_talker_listener
    examples/ros2_talker_listener.cpp)
  target_compile_features(openrtdds_ros2_talker_listener
    PRIVATE cxx_std_17)
  target_compile_options(openrtdds_ros2_talker_listener PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  ament_target_dependencies(openrtdds_ros2_talker_listener
    rclcpp std_msgs)

  add_executable(openrtdds_ros2_c_talker_listener
    examples/ros2_c_talker_listener.c)
  target_compile_features(openrtdds_ros2_c_talker_listener PRIVATE c_std_11)
  target_compile_options(openrtdds_ros2_c_talker_listener PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  ament_target_dependencies(openrtdds_ros2_c_talker_listener rcl std_msgs)

  add_test(NAME rmw_openrtdds_ros2_topic_matrix
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/test/run_ros2_topic_matrix.py"
      "$<TARGET_FILE:openrtdds_ros2_talker_listener>"
      "$<TARGET_FILE:openrtdds_ros2_c_talker_listener>")
  set_tests_properties(rmw_openrtdds_ros2_topic_matrix PROPERTIES TIMEOUT 110)

  add_executable(openrtdds_ros2_graph_probe
    examples/ros2_graph_probe.cpp)
  target_compile_features(openrtdds_ros2_graph_probe PRIVATE cxx_std_17)
  target_compile_options(openrtdds_ros2_graph_probe PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  ament_target_dependencies(openrtdds_ros2_graph_probe rclcpp std_msgs)

  add_test(NAME rmw_openrtdds_ros2_graph
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/test/run_ros2_graph.py"
      "$<TARGET_FILE:openrtdds_ros2_graph_probe>")
  set_tests_properties(rmw_openrtdds_ros2_graph PROPERTIES TIMEOUT 30)

  add_executable(openrtdds_ros2_add_service
    examples/ros2_add_service.cpp)
  target_compile_features(openrtdds_ros2_add_service PRIVATE cxx_std_17)
  target_compile_options(openrtdds_ros2_add_service PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  ament_target_dependencies(openrtdds_ros2_add_service
    example_interfaces rclcpp)

  add_test(NAME rmw_openrtdds_ros2_service
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/test/run_ros2_service.py"
      "$<TARGET_FILE:openrtdds_ros2_add_service>")
  set_tests_properties(rmw_openrtdds_ros2_service PROPERTIES TIMEOUT 20)

  add_executable(test_rmw_memory_qualification
    test/test_rmw_memory_qualification.cpp)
  target_compile_features(test_rmw_memory_qualification PRIVATE cxx_std_17)
  target_compile_options(test_rmw_memory_qualification PRIVATE
    -Wall -Wextra -Wpedantic -Werror)
  target_link_libraries(test_rmw_memory_qualification PRIVATE
    ${PROJECT_NAME}
    rcutils::rcutils
    rmw::rmw
    rosidl_typesupport_introspection_c::rosidl_typesupport_introspection_c
  )
  add_test(NAME rmw_openrtdds_memory_qualification
    COMMAND test_rmw_memory_qualification)
  set_tests_properties(rmw_openrtdds_memory_qualification PROPERTIES
    TIMEOUT 30)

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
