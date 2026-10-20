#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

#include "../src/internal.hpp"

// Verifies: ORT-RMW-004, ORT-RMW-005, ORT-RMW-009, ORT-RMW-016
// Verifies: ORT-RMW-051, ORT-RMW-052
// Verifies: ORT-RMW-053, ORT-RMW-054
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

struct Peer final {
  rmw_init_options_t options{rmw_get_zero_initialized_init_options()};
  rmw_context_t context{rmw_get_zero_initialized_context()};
  rmw_node_t* node{nullptr};
};

[[nodiscard]] bool initialize_peer(Peer& peer, const std::uint64_t instance_id,
                                   const char* const enclave,
                                   const char* const node_name) {
  const auto allocator = rcutils_get_default_allocator();
  if (rmw_init_options_init(&peer.options, allocator) != RMW_RET_OK) {
    return false;
  }
  peer.options.instance_id = instance_id;
  peer.options.domain_id = 37U;
  peer.options.enclave = rcutils_strdup(enclave, allocator);
  if ((peer.options.enclave == nullptr) ||
      (rmw_init(&peer.options, &peer.context) != RMW_RET_OK)) {
    return false;
  }
  peer.node = rmw_create_node(&peer.context, node_name, "/openrtdds");
  return peer.node != nullptr;
}

[[nodiscard]] int fail(const int code) {
  rmw_reset_error();
  return code;
}

}  // namespace

int main() {
  Fixture fixture{};
  Peer writer_peer{};
  Peer reader_peer{};
  if (!initialize_peer(writer_peer, 3701U, "/openrtdds/network_writer",
                       "network_writer")) {
    return fail(1);
  }
  if (!initialize_peer(reader_peer, 3702U, "/openrtdds/network_reader",
                       "network_reader")) {
    return fail(2);
  }

  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  qos.depth = 4U;
  qos.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  const auto publisher_options = rmw_get_default_publisher_options();
  const auto subscription_options = rmw_get_default_subscription_options();
  rmw_publisher_t* publisher = rmw_create_publisher(
      writer_peer.node, &fixture.handle, "/network_counter", &qos,
      &publisher_options);
  rmw_subscription_t* subscription = rmw_create_subscription(
      reader_peer.node, &fixture.handle, "/network_counter", &qos,
      &subscription_options);
  rmw_wait_set_t* wait_set = rmw_create_wait_set(&reader_peer.context, 1U);
  if ((publisher == nullptr) || (subscription == nullptr) ||
      (wait_set == nullptr)) {
    return fail(3);
  }

  constexpr auto discovery_timeout = std::chrono::seconds(4);
  const auto discovery_deadline =
      std::chrono::steady_clock::now() + discovery_timeout;
  std::size_t publisher_matches = 0U;
  std::size_t subscription_matches = 0U;
  while (std::chrono::steady_clock::now() < discovery_deadline) {
    if ((rmw_publisher_count_matched_subscriptions(
             publisher, &publisher_matches) != RMW_RET_OK) ||
        (rmw_subscription_count_matched_publishers(
             subscription, &subscription_matches) != RMW_RET_OK)) {
      return fail(4);
    }
    if ((publisher_matches == 1U) && (subscription_matches == 1U)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if ((publisher_matches != 1U) || (subscription_matches != 1U)) {
    return fail(5);
  }

  const Message input{42U};
  if (rmw_publish(publisher, &input, nullptr) != RMW_RET_OK) {
    return fail(6);
  }

  void* subscription_data = subscription->data;
  rmw_subscriptions_t subscriptions{1U, &subscription_data};
  const rmw_time_t receive_timeout{3U, 0U};
  if ((rmw_wait(&subscriptions, nullptr, nullptr, nullptr, nullptr, wait_set,
                &receive_timeout) != RMW_RET_OK) ||
      (subscription_data != subscription->data)) {
    return fail(7);
  }

  Message output{};
  rmw_message_info_t info{};
  bool taken = false;
  if ((rmw_take_with_info(subscription, &output, &taken, &info, nullptr) !=
       RMW_RET_OK) ||
      !taken || (output.value != input.value) ||
      (info.publication_sequence_number != 1) ||
      (info.reception_sequence_number != 1) ||
      (info.source_timestamp == 0) || (info.received_timestamp == 0)) {
    return fail(8);
  }

  rmw_qos_profile_t reliable_qos = qos;
  reliable_qos.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  rmw_publisher_t* reliable_publisher = rmw_create_publisher(
      writer_peer.node, &fixture.handle, "/reliable_network_counter",
      &reliable_qos, &publisher_options);
  rmw_subscription_t* reliable_subscription = rmw_create_subscription(
      reader_peer.node, &fixture.handle, "/reliable_network_counter",
      &reliable_qos, &subscription_options);
  if ((reliable_publisher == nullptr) || (reliable_subscription == nullptr)) {
    return fail(9);
  }

  publisher_matches = 0U;
  subscription_matches = 0U;
  const auto reliable_discovery_deadline =
      std::chrono::steady_clock::now() + discovery_timeout;
  while (std::chrono::steady_clock::now() < reliable_discovery_deadline) {
    if ((rmw_publisher_count_matched_subscriptions(
             reliable_publisher, &publisher_matches) != RMW_RET_OK) ||
        (rmw_subscription_count_matched_publishers(
             reliable_subscription, &subscription_matches) != RMW_RET_OK)) {
      return fail(10);
    }
    if ((publisher_matches == 1U) && (subscription_matches == 1U)) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if ((publisher_matches != 1U) || (subscription_matches != 1U)) {
    return fail(11);
  }

  rmw_openrtdds_cpp::detail::network_test_drop_next_reliable_data(
      &writer_peer.context, 1U);
  const Message reliable_input{84U};
  if (rmw_publish(reliable_publisher, &reliable_input, nullptr) != RMW_RET_OK) {
    return fail(12);
  }
  void* reliable_subscription_data = reliable_subscription->data;
  rmw_subscriptions_t reliable_subscriptions{
      1U, &reliable_subscription_data};
  if ((rmw_wait(&reliable_subscriptions, nullptr, nullptr, nullptr, nullptr,
                wait_set, &receive_timeout) != RMW_RET_OK) ||
      (reliable_subscription_data != reliable_subscription->data)) {
    return fail(13);
  }
  Message reliable_output{};
  info = {};
  taken = false;
  if ((rmw_take_with_info(reliable_subscription, &reliable_output, &taken,
                          &info, nullptr) != RMW_RET_OK) ||
      !taken || (reliable_output.value != reliable_input.value) ||
      (info.publication_sequence_number != 1) ||
      (info.reception_sequence_number != 1)) {
    return fail(14);
  }

  if ((rmw_shutdown(&writer_peer.context) != RMW_RET_OK) ||
      (rmw_shutdown(&reader_peer.context) != RMW_RET_OK) ||
      (rmw_destroy_wait_set(wait_set) != RMW_RET_OK) ||
      (rmw_destroy_subscription(reader_peer.node, reliable_subscription) !=
       RMW_RET_OK) ||
      (rmw_destroy_subscription(reader_peer.node, subscription) !=
       RMW_RET_OK) ||
      (rmw_destroy_publisher(writer_peer.node, reliable_publisher) !=
       RMW_RET_OK) ||
      (rmw_destroy_publisher(writer_peer.node, publisher) != RMW_RET_OK) ||
      (rmw_destroy_node(reader_peer.node) != RMW_RET_OK) ||
      (rmw_destroy_node(writer_peer.node) != RMW_RET_OK) ||
      (rmw_context_fini(&reader_peer.context) != RMW_RET_OK) ||
      (rmw_context_fini(&writer_peer.context) != RMW_RET_OK) ||
      (rmw_init_options_fini(&reader_peer.options) != RMW_RET_OK) ||
      (rmw_init_options_fini(&writer_peer.options) != RMW_RET_OK)) {
    return fail(15);
  }
  return 0;
}
