#include <cstddef>
#include <cstdint>
#include <cstring>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/names_and_types.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

// Verifies: ORT-RMW-010, ORT-RMW-020, ORT-RMW-055, ORT-RMW-056
namespace {

struct Message final {
  std::uint32_t value{0U};
};

struct Fixture final {
  rosidl_typesupport_introspection_c__MessageMember field{};
  rosidl_typesupport_introspection_c__MessageMembers members{};
  rosidl_message_type_support_t handle{};

  Fixture() {
    field.name_ = "value";
    field.type_id_ = rosidl_typesupport_introspection_c__ROS_TYPE_UINT32;
    field.offset_ = offsetof(Message, value);
    members.message_namespace_ = "openrtdds_test_msgs__msg";
    members.message_name_ = "Counter";
    members.member_count_ = 1U;
    members.size_of_ = sizeof(Message);
    members.members_ = &field;
    handle.typesupport_identifier =
        rosidl_typesupport_introspection_c__identifier;
    handle.data = &members;
  }
};

[[nodiscard]] bool contains(const rcutils_string_array_t& values,
                            const char* const expected) {
  for (std::size_t index = 0U; index < values.size; ++index) {
    if ((values.data[index] != nullptr) &&
        (std::strcmp(values.data[index], expected) == 0)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool contains_topic(const rmw_names_and_types_t& values,
                                  const char* const topic,
                                  const char* const type) {
  for (std::size_t index = 0U; index < values.names.size; ++index) {
    if ((values.names.data[index] != nullptr) &&
        (std::strcmp(values.names.data[index], topic) == 0) &&
        contains(values.types[index], type)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] int fail(const int code) {
  rmw_reset_error();
  return code;
}

}  // namespace

int main() {
  Fixture fixture{};
  const auto allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return fail(1);
  }
  options.instance_id = 3501U;
  options.domain_id = 35U;
  options.enclave = rcutils_strdup("/openrtdds/graph_test", allocator);
  if ((options.enclave == nullptr) ||
      (rmw_init(&options, &context) != RMW_RET_OK)) {
    return fail(2);
  }
  rmw_node_t* node = rmw_create_node(&context, "graph_test", "/openrtdds");
  if (node == nullptr) {
    return fail(3);
  }
  const rmw_guard_condition_t* graph_guard =
      rmw_node_get_graph_guard_condition(node);
  rmw_wait_set_t* wait_set = rmw_create_wait_set(&context, 1U);
  if ((graph_guard == nullptr) || (wait_set == nullptr)) {
    return fail(4);
  }

  void* guard_data = graph_guard->data;
  rmw_guard_conditions_t guards{1U, &guard_data};
  const rmw_time_t no_wait{0U, 0U};
  if ((rmw_wait(nullptr, &guards, nullptr, nullptr, nullptr, wait_set,
                &no_wait) != RMW_RET_OK) ||
      (guard_data != graph_guard->data)) {
    return fail(5);
  }

  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  qos.depth = 2U;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  const auto publisher_options = rmw_get_default_publisher_options();
  rmw_publisher_t* publisher = rmw_create_publisher(
      node, &fixture.handle, "/graph_counter", &qos, &publisher_options);
  if (publisher == nullptr) {
    return fail(6);
  }

  guard_data = graph_guard->data;
  guards.guard_conditions = &guard_data;
  if ((rmw_wait(nullptr, &guards, nullptr, nullptr, nullptr, wait_set,
                &no_wait) != RMW_RET_OK) ||
      (guard_data != graph_guard->data)) {
    return fail(7);
  }

  std::size_t publisher_count = 0U;
  if ((rmw_count_publishers(node, "/graph_counter", &publisher_count) !=
       RMW_RET_OK) ||
      (publisher_count != 1U)) {
    return fail(8);
  }
  rcutils_string_array_t node_names =
      rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t node_namespaces =
      rcutils_get_zero_initialized_string_array();
  if ((rmw_get_node_names(node, &node_names, &node_namespaces) != RMW_RET_OK) ||
      !contains(node_names, "graph_test") ||
      !contains(node_namespaces, "/openrtdds") ||
      (rcutils_string_array_fini(&node_namespaces) != RCUTILS_RET_OK) ||
      (rcutils_string_array_fini(&node_names) != RCUTILS_RET_OK)) {
    return fail(9);
  }

  auto allocator_copy = allocator;
  rmw_names_and_types_t topics =
      rmw_get_zero_initialized_names_and_types();
  if ((rmw_get_topic_names_and_types(node, &allocator_copy, false, &topics) !=
       RMW_RET_OK) ||
      !contains_topic(topics, "/graph_counter",
                      "openrtdds_test_msgs/msg/Counter") ||
      (rmw_names_and_types_fini(&topics) != RMW_RET_OK)) {
    return fail(10);
  }

  if (rmw_destroy_publisher(node, publisher) != RMW_RET_OK) {
    return fail(11);
  }
  publisher_count = 1U;
  if ((rmw_count_publishers(node, "/graph_counter", &publisher_count) !=
       RMW_RET_OK) ||
      (publisher_count != 0U)) {
    return fail(12);
  }
  guard_data = graph_guard->data;
  guards.guard_conditions = &guard_data;
  if ((rmw_wait(nullptr, &guards, nullptr, nullptr, nullptr, wait_set,
                &no_wait) != RMW_RET_OK) ||
      (guard_data != graph_guard->data)) {
    return fail(13);
  }

  if ((rmw_destroy_node(node) != RMW_RET_OK) ||
      (rmw_shutdown(&context) != RMW_RET_OK) ||
      (rmw_destroy_wait_set(wait_set) != RMW_RET_OK) ||
      (rmw_context_fini(&context) != RMW_RET_OK) ||
      (rmw_init_options_fini(&options) != RMW_RET_OK)) {
    return fail(14);
  }
  return 0;
}
