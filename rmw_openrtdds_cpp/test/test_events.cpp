#include <atomic>
#include <cstddef>
#include <cstdint>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/events_statuses/matched.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

// Verifies: ORT-RMW-009, ORT-RMW-013, ORT-RMW-015, ORT-RMW-022
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

struct Runtime final {
  rmw_init_options_t options{rmw_get_zero_initialized_init_options()};
  rmw_context_t context{rmw_get_zero_initialized_context()};
  rmw_node_t* node{nullptr};
};

extern "C" void event_callback(const void* user_data,
                               const std::size_t number_of_events) {
  if (user_data != nullptr) {
    auto* count = static_cast<std::atomic_size_t*>(
        const_cast<void*>(user_data));
    count->fetch_add(number_of_events, std::memory_order_relaxed);
  }
}

[[nodiscard]] bool initialize(Runtime& runtime, const std::uint64_t instance,
                              const char* name) {
  const auto allocator = rcutils_get_default_allocator();
  if (rmw_init_options_init(&runtime.options, allocator) != RMW_RET_OK) {
    return false;
  }
  runtime.options.instance_id = instance;
  runtime.options.domain_id = 37U;
  runtime.options.enclave = rcutils_strdup("/openrtdds_events", allocator);
  if ((runtime.options.enclave == nullptr) ||
      (rmw_init(&runtime.options, &runtime.context) != RMW_RET_OK)) {
    return false;
  }
  runtime.node =
      rmw_create_node(&runtime.context, name, "/openrtdds_events");
  return runtime.node != nullptr;
}

[[nodiscard]] bool wait_ready(rmw_context_t& context, rmw_event_t& event) {
  rmw_wait_set_t* wait_set = rmw_create_wait_set(&context, 1U);
  if (wait_set == nullptr) {
    return false;
  }
  void* event_data = event.data;
  rmw_events_t events{1U, &event_data};
  const rmw_time_t timeout{5U, 0U};
  const rmw_ret_t result = rmw_wait(
      nullptr, nullptr, nullptr, nullptr, &events, wait_set, &timeout);
  const rmw_ret_t destroy_result = rmw_destroy_wait_set(wait_set);
  return (result == RMW_RET_OK) && (event_data == event.data) &&
         (destroy_result == RMW_RET_OK);
}

[[nodiscard]] int fail(const int code) {
  rmw_reset_error();
  return code;
}

}  // namespace

int main() {
  if (!rmw_event_type_is_supported(RMW_EVENT_PUBLICATION_MATCHED) ||
      !rmw_event_type_is_supported(RMW_EVENT_SUBSCRIPTION_MATCHED) ||
      rmw_event_type_is_supported(RMW_EVENT_LIVELINESS_CHANGED) ||
      rmw_error_is_set()) {
    return fail(1);
  }

  Fixture fixture{};
  Runtime first{};
  Runtime second{};
  if (!initialize(first, 37U, "first") ||
      !initialize(second, 38U, "second")) {
    return fail(2);
  }
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.depth = 2U;
  const auto publisher_options = rmw_get_default_publisher_options();
  const auto subscription_options = rmw_get_default_subscription_options();

  rmw_publisher_t* publication_source = rmw_create_publisher(
      first.node, &fixture.handle, "/publication_match", &qos,
      &publisher_options);
  rmw_subscription_t* subscription_source = rmw_create_subscription(
      second.node, &fixture.handle, "/subscription_match", &qos,
      &subscription_options);
  if ((publication_source == nullptr) || (subscription_source == nullptr)) {
    return fail(3);
  }

  rmw_event_t publication = rmw_get_zero_initialized_event();
  rmw_event_t subscription = rmw_get_zero_initialized_event();
  if ((rmw_publisher_event_init(
           &publication, publication_source,
           RMW_EVENT_PUBLICATION_MATCHED) != RMW_RET_OK) ||
      (rmw_subscription_event_init(
           &subscription, subscription_source,
           RMW_EVENT_SUBSCRIPTION_MATCHED) != RMW_RET_OK)) {
    return fail(4);
  }
  rmw_event_t unsupported = rmw_get_zero_initialized_event();
  if (rmw_publisher_event_init(
          &unsupported, publication_source, RMW_EVENT_LIVELINESS_LOST) !=
      RMW_RET_UNSUPPORTED) {
    return fail(5);
  }
  rmw_reset_error();

  std::atomic_size_t publication_callbacks{0U};
  std::atomic_size_t subscription_callbacks{0U};
  if ((rmw_event_set_callback(
           &publication, event_callback, &publication_callbacks) !=
       RMW_RET_OK) ||
      (rmw_event_set_callback(
           &subscription, event_callback, &subscription_callbacks) !=
       RMW_RET_OK)) {
    return fail(6);
  }

  rmw_matched_status_t status{};
  bool taken = false;
  if ((rmw_take_event(&publication, &status, &taken) != RMW_RET_OK) ||
      !taken || (status.total_count != 0U) ||
      (status.current_count != 0U) ||
      (rmw_take_event(&subscription, &status, &taken) != RMW_RET_OK) ||
      !taken || (status.total_count != 0U) ||
      (status.current_count != 0U)) {
    return fail(7);
  }

  rmw_subscription_t* publication_peer = rmw_create_subscription(
      second.node, &fixture.handle, "/publication_match", &qos,
      &subscription_options);
  rmw_publisher_t* subscription_peer = rmw_create_publisher(
      first.node, &fixture.handle, "/subscription_match", &qos,
      &publisher_options);
  if ((publication_peer == nullptr) || (subscription_peer == nullptr) ||
      !wait_ready(first.context, publication) ||
      !wait_ready(second.context, subscription)) {
    return fail(8);
  }

  rmw_matched_status_t publication_status{};
  rmw_matched_status_t subscription_status{};
  bool publication_taken = false;
  bool subscription_taken = false;
  if ((rmw_take_event(
           &publication, &publication_status, &publication_taken) !=
       RMW_RET_OK) ||
      (rmw_take_event(
           &subscription, &subscription_status, &subscription_taken) !=
       RMW_RET_OK) ||
      !publication_taken || !subscription_taken ||
      (publication_status.total_count != 1U) ||
      (publication_status.total_count_change != 1U) ||
      (publication_status.current_count != 1U) ||
      (publication_status.current_count_change != 1) ||
      (subscription_status.total_count != 1U) ||
      (subscription_status.total_count_change != 1U) ||
      (subscription_status.current_count != 1U) ||
      (subscription_status.current_count_change != 1) ||
      (publication_callbacks.load(std::memory_order_relaxed) == 0U) ||
      (subscription_callbacks.load(std::memory_order_relaxed) == 0U)) {
    return fail(9);
  }
  if ((rmw_take_event(
           &publication, &publication_status, &publication_taken) !=
       RMW_RET_OK) || publication_taken) {
    return fail(10);
  }

  if ((rmw_event_fini(&publication) != RMW_RET_OK) ||
      (rmw_event_fini(&subscription) != RMW_RET_OK) ||
      (rmw_destroy_subscription(second.node, publication_peer) != RMW_RET_OK) ||
      (rmw_destroy_publisher(first.node, subscription_peer) != RMW_RET_OK) ||
      (rmw_destroy_subscription(second.node, subscription_source) != RMW_RET_OK) ||
      (rmw_destroy_publisher(first.node, publication_source) != RMW_RET_OK) ||
      (rmw_destroy_node(second.node) != RMW_RET_OK) ||
      (rmw_destroy_node(first.node) != RMW_RET_OK) ||
      (rmw_shutdown(&second.context) != RMW_RET_OK) ||
      (rmw_shutdown(&first.context) != RMW_RET_OK) ||
      (rmw_context_fini(&second.context) != RMW_RET_OK) ||
      (rmw_context_fini(&first.context) != RMW_RET_OK) ||
      (rmw_init_options_fini(&second.options) != RMW_RET_OK) ||
      (rmw_init_options_fini(&first.options) != RMW_RET_OK)) {
    return fail(11);
  }
  return 0;
}
