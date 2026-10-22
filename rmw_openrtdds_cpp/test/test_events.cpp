#include <cstddef>
#include <cstdint>

#include "../src/internal.hpp"
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

std::size_t callback_count = 0U;

extern "C" void event_callback(const void* user_data,
                               const std::size_t number_of_events) {
  if (user_data != nullptr) {
    *static_cast<std::size_t*>(const_cast<void*>(user_data)) +=
        number_of_events;
  }
}

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
  const auto allocator = rcutils_get_default_allocator();
  auto options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return fail(2);
  }
  options.instance_id = 37U;
  options.domain_id = 37U;
  options.enclave = rcutils_strdup("/openrtdds_events", allocator);
  auto context = rmw_get_zero_initialized_context();
  if ((options.enclave == nullptr) ||
      (rmw_init(&options, &context) != RMW_RET_OK)) {
    return fail(3);
  }
  rmw_node_t* node = rmw_create_node(&context, "events", "/openrtdds");
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.depth = 2U;
  const auto publisher_options = rmw_get_default_publisher_options();
  const auto subscription_options = rmw_get_default_subscription_options();
  rmw_publisher_t* publisher = rmw_create_publisher(
      node, &fixture.handle, "/events", &qos, &publisher_options);
  rmw_subscription_t* subscription = rmw_create_subscription(
      node, &fixture.handle, "/events", &qos, &subscription_options);
  rmw_wait_set_t* wait_set = rmw_create_wait_set(&context, 2U);
  if ((node == nullptr) || (publisher == nullptr) ||
      (subscription == nullptr) || (wait_set == nullptr)) {
    return fail(4);
  }

  rmw_event_t publication = rmw_get_zero_initialized_event();
  rmw_event_t subscription_match = rmw_get_zero_initialized_event();
  if ((rmw_publisher_event_init(
           &publication, publisher, RMW_EVENT_PUBLICATION_MATCHED) !=
       RMW_RET_OK) ||
      (rmw_subscription_event_init(
           &subscription_match, subscription,
           RMW_EVENT_SUBSCRIPTION_MATCHED) != RMW_RET_OK)) {
    return fail(5);
  }
  rmw_event_t unsupported = rmw_get_zero_initialized_event();
  if (rmw_publisher_event_init(
          &unsupported, publisher, RMW_EVENT_LIVELINESS_LOST) !=
      RMW_RET_UNSUPPORTED) {
    return fail(6);
  }
  rmw_reset_error();

  bool publication_taken = false;
  rmw_matched_status_t publication_status{};
  if ((rmw_event_set_callback(
           &publication, event_callback, &callback_count) != RMW_RET_OK) ||
      (rmw_take_event(
           &publication, &publication_status, &publication_taken) !=
       RMW_RET_OK) ||
      !publication_taken || (publication_status.total_count != 0U) ||
      (publication_status.current_count != 0U)) {
    return fail(7);
  }

  const std::size_t publisher_changes =
      rmw_openrtdds_cpp::detail::publisher_network_set_match_count(
          publisher->data, 1U);
  const std::size_t subscription_changes =
      rmw_openrtdds_cpp::detail::subscription_network_set_match_count(
          subscription->data, 1U);
  rmw_openrtdds_cpp::detail::notify_event_source(
      &context, publisher->data, publisher_changes);
  rmw_openrtdds_cpp::detail::notify_event_source(
      &context, subscription->data, subscription_changes);
  if (callback_count != 1U) {
    return fail(8);
  }
  void* event_data[2U]{publication.data, subscription_match.data};
  rmw_events_t events{2U, event_data};
  const rmw_time_t immediate{0U, 0U};
  if ((rmw_wait(nullptr, nullptr, nullptr, nullptr, &events, wait_set,
                &immediate) != RMW_RET_OK) ||
      (event_data[0] != publication.data) ||
      (event_data[1] != subscription_match.data)) {
    return fail(9);
  }

  rmw_matched_status_t subscription_status{};
  bool subscription_taken = false;
  if ((rmw_take_event(
           &publication, &publication_status, &publication_taken) !=
       RMW_RET_OK) ||
      (rmw_take_event(
           &subscription_match, &subscription_status,
           &subscription_taken) != RMW_RET_OK) ||
      !publication_taken || !subscription_taken ||
      (publication_status.total_count != 1U) ||
      (publication_status.total_count_change != 1U) ||
      (publication_status.current_count != 1U) ||
      (publication_status.current_count_change != 1) ||
      (subscription_status.total_count != 1U) ||
      (subscription_status.current_count != 1U)) {
    return fail(10);
  }
  if ((rmw_take_event(
           &publication, &publication_status, &publication_taken) !=
       RMW_RET_OK) || publication_taken) {
    return fail(11);
  }

  const std::size_t disconnect_changes =
      rmw_openrtdds_cpp::detail::publisher_network_set_match_count(
          publisher->data, 0U);
  rmw_openrtdds_cpp::detail::notify_event_source(
      &context, publisher->data, disconnect_changes);
  if ((rmw_take_event(
           &publication, &publication_status, &publication_taken) !=
       RMW_RET_OK) || !publication_taken ||
      (publication_status.total_count != 1U) ||
      (publication_status.total_count_change != 0U) ||
      (publication_status.current_count != 0U) ||
      (publication_status.current_count_change != -1)) {
    return fail(12);
  }

  event_data[0] = publication.data;
  event_data[1] = subscription_match.data;
  if ((rmw_wait(nullptr, nullptr, nullptr, nullptr, &events, wait_set,
                &immediate) != RMW_RET_TIMEOUT) ||
      (event_data[0] != nullptr) || (event_data[1] != nullptr)) {
    return fail(13);
  }

  if ((rmw_shutdown(&context) != RMW_RET_OK) ||
      (rmw_context_fini(&context) != RMW_RET_INVALID_ARGUMENT)) {
    return fail(14);
  }
  rmw_reset_error();
  if ((rmw_event_fini(&publication) != RMW_RET_OK) ||
      (rmw_event_fini(&subscription_match) != RMW_RET_OK) ||
      (rmw_destroy_wait_set(wait_set) != RMW_RET_OK) ||
      (rmw_destroy_subscription(node, subscription) != RMW_RET_OK) ||
      (rmw_destroy_publisher(node, publisher) != RMW_RET_OK) ||
      (rmw_destroy_node(node) != RMW_RET_OK) ||
      (rmw_context_fini(&context) != RMW_RET_OK) ||
      (rmw_init_options_fini(&options) != RMW_RET_OK)) {
    return fail(15);
  }
  return 0;
}
