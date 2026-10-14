#include <cstddef>
#include <cstdint>
#include <iostream>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw_openrtdds_cpp/publisher.hpp"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

// Demonstrates: ORT-RMW-045, ORT-RMW-046
namespace {

struct Counter final {
  std::uint32_t value{0U};
};

struct CounterType final {
  rosidl_typesupport_introspection_c__MessageMember field{};
  rosidl_typesupport_introspection_c__MessageMembers members{};
  rosidl_message_type_support_t handle{};

  CounterType() {
    field.name_ = "value";
    field.type_id_ = rosidl_typesupport_introspection_c__ROS_TYPE_UINT32;
    field.offset_ = offsetof(Counter, value);
    members.message_namespace_ = "openrtdds_examples__msg";
    members.message_name_ = "Counter";
    members.member_count_ = 1U;
    members.size_of_ = sizeof(Counter);
    members.members_ = &field;
    handle.typesupport_identifier =
        rosidl_typesupport_introspection_c__identifier;
    handle.data = &members;
  }
};

}  // namespace

int main() {
  CounterType type{};
  const auto allocator = rcutils_get_default_allocator();
  auto options = rmw_get_zero_initialized_init_options();
  auto context = rmw_get_zero_initialized_context();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return 1;
  }
  options.enclave = rcutils_strdup("/openrtdds_publisher_example", allocator);
  if ((options.enclave == nullptr) ||
      (rmw_init(&options, &context) != RMW_RET_OK)) {
    return 1;
  }
  rmw_node_t* node = rmw_create_node(&context, "counter", "/examples");
  auto qos = rmw_qos_profile_default;
  qos.depth = 4U;
  const auto publisher_options = rmw_get_default_publisher_options();
  rmw_publisher_t* publisher = rmw_create_publisher(
      node, &type.handle, "/counter", &qos, &publisher_options);
  Counter sample{30U};
  if ((publisher == nullptr) ||
      (rmw_publish(publisher, &sample, nullptr) != RMW_RET_OK)) {
    return 2;
  }
  rmw_openrtdds_cpp::PublisherSnapshot snapshot{};
  if (!rmw_openrtdds_cpp::inspect_publisher(publisher, snapshot)) {
    return 3;
  }
  std::cout << snapshot.names.topic_name.data() << " sequence="
            << snapshot.last_sample.sequence_number << " serialized="
            << snapshot.last_sample.serialized_size << " rtps="
            << snapshot.last_data_message_size << '\n';

  if ((rmw_destroy_publisher(node, publisher) != RMW_RET_OK) ||
      (rmw_destroy_node(node) != RMW_RET_OK) ||
      (rmw_shutdown(&context) != RMW_RET_OK) ||
      (rmw_context_fini(&context) != RMW_RET_OK) ||
      (rmw_init_options_fini(&options) != RMW_RET_OK)) {
    return 4;
  }
  return 0;
}
