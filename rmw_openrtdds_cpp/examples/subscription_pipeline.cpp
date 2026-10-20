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
#include "rmw_openrtdds_cpp/subscription.hpp"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

// Demonstrates: ORT-RMW-047, ORT-RMW-048, ORT-RMW-049, ORT-RMW-050
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
  options.domain_id = 31U;
  options.instance_id = 31U;
  options.enclave =
      rcutils_strdup("/openrtdds_subscription_example", allocator);
  if ((options.enclave == nullptr) ||
      (rmw_init(&options, &context) != RMW_RET_OK)) {
    return 1;
  }
  rmw_node_t* node = rmw_create_node(&context, "counter", "/examples");
  auto qos = rmw_qos_profile_default;
  qos.depth = 4U;
  const auto publisher_options = rmw_get_default_publisher_options();
  const auto subscription_options = rmw_get_default_subscription_options();
  rmw_publisher_t* publisher = rmw_create_publisher(
      node, &type.handle, "/counter", &qos, &publisher_options);
  rmw_subscription_t* subscription = rmw_create_subscription(
      node, &type.handle, "/counter", &qos, &subscription_options);
  rmw_wait_set_t* wait_set = rmw_create_wait_set(&context, 1U);
  Counter sent{31U};
  if ((publisher == nullptr) || (subscription == nullptr) ||
      (wait_set == nullptr) ||
      (rmw_publish(publisher, &sent, nullptr) != RMW_RET_OK)) {
    return 2;
  }

  rmw_openrtdds_cpp::PublisherSnapshot published{};
  rmw_openrtdds_cpp::SubscriptionReceiveResult received{};
  if (!rmw_openrtdds_cpp::inspect_publisher(publisher, published) ||
      !rmw_openrtdds_cpp::receive_data_message(
          subscription, published.last_data_message,
          published.last_data_message_size,
          published.last_sample.source_timestamp_ns,
          published.last_sample.source_timestamp_ns + 100U, received) ||
      !received.accepted) {
    return 3;
  }

  void* subscription_data = subscription->data;
  rmw_subscriptions_t subscriptions{1U, &subscription_data};
  if ((rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set,
                nullptr) != RMW_RET_OK) ||
      (subscription_data != subscription->data)) {
    return 3;
  }

  Counter taken_message{};
  rmw_message_info_t info{};
  bool taken = false;
  if ((rmw_take_with_info(subscription, &taken_message, &taken, &info,
                          nullptr) != RMW_RET_OK) ||
      !taken || (taken_message.value != sent.value)) {
    return 4;
  }
  std::cout << subscription->topic_name << " value=" << taken_message.value
            << " publication_sequence=" << info.publication_sequence_number
            << " reception_sequence=" << info.reception_sequence_number
            << '\n';

  if ((rmw_destroy_wait_set(wait_set) != RMW_RET_OK) ||
      (rmw_destroy_subscription(node, subscription) != RMW_RET_OK) ||
      (rmw_destroy_publisher(node, publisher) != RMW_RET_OK) ||
      (rmw_destroy_node(node) != RMW_RET_OK) ||
      (rmw_shutdown(&context) != RMW_RET_OK) ||
      (rmw_context_fini(&context) != RMW_RET_OK) ||
      (rmw_init_options_fini(&options) != RMW_RET_OK)) {
    return 5;
  }
  return 0;
}
