#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/rtps/sedp.hpp"
#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rmw_openrtdds_cpp/publisher.hpp"
#include "rmw_openrtdds_cpp/subscription.hpp"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

// Verifies: ORT-RMW-005, ORT-RMW-012, ORT-RMW-014, ORT-RMW-015
// Verifies: ORT-RMW-047, ORT-RMW-048
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

[[nodiscard]] int fail(const int code) {
  rmw_reset_error();
  return code;
}

[[nodiscard]] bool publish_and_receive(
    rmw_publisher_t* const publisher,
    const rmw_subscription_t* const subscription, const std::uint32_t value,
    rmw_openrtdds_cpp::SubscriptionReceiveResult& result) {
  Message message{value};
  if (rmw_publish(publisher, &message, nullptr) != RMW_RET_OK) {
    return false;
  }
  rmw_openrtdds_cpp::PublisherSnapshot snapshot{};
  if (!rmw_openrtdds_cpp::inspect_publisher(publisher, snapshot)) {
    return false;
  }
  return rmw_openrtdds_cpp::receive_data_message(
      subscription, snapshot.last_data_message,
      snapshot.last_data_message_size, snapshot.last_sample.source_timestamp_ns,
      snapshot.last_sample.source_timestamp_ns + 100U, result);
}

}  // namespace

int main() {
  Fixture fixture{};
  const rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return fail(1);
  }
  options.instance_id = 31U;
  options.domain_id = 31U;
  options.enclave = rcutils_strdup("/openrtdds_subscription", allocator);
  if (options.enclave == nullptr) {
    return fail(2);
  }
  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return fail(3);
  }
  rmw_node_t* node = rmw_create_node(&context, "subscription", "/openrtdds");
  rmw_node_t* other_node = rmw_create_node(&context, "other", "/openrtdds");
  if ((node == nullptr) || (other_node == nullptr)) {
    return fail(4);
  }

  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.depth = 2U;
  const rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  const rmw_subscription_options_t subscription_options =
      rmw_get_default_subscription_options();
  rmw_publisher_t* publisher = rmw_create_publisher(
      node, &fixture.handle, "/counter", &qos, &publisher_options);
  rmw_subscription_t* subscription = rmw_create_subscription(
      node, &fixture.handle, "/counter", &qos, &subscription_options);
  if ((publisher == nullptr) || (subscription == nullptr)) {
    return fail(5);
  }
  if (rmw_destroy_subscription(other_node, subscription) !=
      RMW_RET_INVALID_ARGUMENT) {
    return fail(6);
  }
  rmw_reset_error();
  if (rmw_destroy_node(node) != RMW_RET_ERROR) {
    return fail(7);
  }
  rmw_reset_error();

  rmw_openrtdds_cpp::SubscriptionSnapshot snapshot{};
  if (!rmw_openrtdds_cpp::inspect_subscription(subscription, snapshot) ||
      (std::strcmp(snapshot.names.topic_name.data(), "rt/counter") != 0) ||
      (snapshot.history_capacity != 2U) || (snapshot.history_size != 0U) ||
      snapshot.ready || (snapshot.next_reception_sequence_number != 1U) ||
      (snapshot.sedp_announcement_size == 0U)) {
    return fail(8);
  }
  openrtdds::rtps::GuidPrefix prefix{};
  std::memcpy(prefix.value.data(), snapshot.reader_guid.data(),
              prefix.value.size());
  openrtdds::rtps::SedpMessageView sedp{};
  const auto sedp_result = openrtdds::rtps::parse_sedp_message(
      snapshot.sedp_announcement, snapshot.sedp_announcement_size, prefix,
      sedp);
  if (!sedp_result.ok() ||
      (sedp.endpoint.kind != openrtdds::rtps::EndpointKind::reader) ||
      (std::memcmp(sedp.endpoint.endpoint_id.value.data(),
                   &snapshot.reader_guid[12], 4U) != 0)) {
    return fail(9);
  }

  rmw_qos_profile_t actual{};
  std::size_t matches = 99U;
  if ((rmw_subscription_get_actual_qos(subscription, &actual) != RMW_RET_OK) ||
      (actual.depth != 2U) ||
      (rmw_subscription_count_matched_publishers(subscription, &matches) !=
       RMW_RET_OK) ||
      (matches != 0U)) {
    return fail(10);
  }

  Message output{99U};
  bool taken = true;
  if ((rmw_take(subscription, &output, &taken, nullptr) != RMW_RET_OK) ||
      taken || (output.value != 99U)) {
    return fail(11);
  }

  rmw_openrtdds_cpp::SubscriptionReceiveResult receive{};
  if (!publish_and_receive(publisher, subscription, 10U, receive) ||
      !receive.accepted || (receive.publication_sequence_number != 1U) ||
      (receive.reception_sequence_number != 1U)) {
    return fail(12);
  }
  rmw_openrtdds_cpp::PublisherSnapshot publisher_snapshot{};
  if (!rmw_openrtdds_cpp::inspect_publisher(publisher, publisher_snapshot)) {
    return fail(13);
  }
  openrtdds::rtps::DataMessageView published_view{};
  if (openrtdds::rtps::parse_data_message(
          publisher_snapshot.last_data_message,
          publisher_snapshot.last_data_message_size,
          published_view) != openrtdds::rtps::RtpsError::none) {
    return fail(13);
  }
  openrtdds::rtps::DataMessageConfig wrong_reader_config{};
  wrong_reader_config.version = published_view.version;
  wrong_reader_config.vendor_id = published_view.vendor_id;
  wrong_reader_config.guid_prefix = published_view.guid_prefix;
  wrong_reader_config.reader_id = {{0U, 0U, 99U, 0x04U}};
  wrong_reader_config.writer_id = published_view.writer_id;
  wrong_reader_config.sequence_number = published_view.sequence_number;
  wrong_reader_config.submessage_byte_order =
      published_view.submessage_byte_order;
  std::array<std::uint8_t, 256U> wrong_reader_packet{};
  openrtdds::rtps::DataMessageBuilder wrong_reader_builder(
      wrong_reader_packet.data(), wrong_reader_packet.size());
  if (!wrong_reader_builder.build(wrong_reader_config,
                                  published_view.serialized_payload,
                                  published_view.payload_size) ||
      rmw_openrtdds_cpp::receive_data_message(
          subscription, wrong_reader_builder.data(), wrong_reader_builder.size(),
          publisher_snapshot.last_sample.source_timestamp_ns,
          publisher_snapshot.last_sample.source_timestamp_ns + 150U,
          receive) ||
      (receive.error !=
       rmw_openrtdds_cpp::SubscriptionReceiveError::wrong_reader) ||
      !rmw_openrtdds_cpp::inspect_subscription(subscription, snapshot) ||
      (snapshot.history_size != 1U) || (snapshot.accepted_samples != 1U)) {
    return fail(13);
  }
  if (!rmw_openrtdds_cpp::receive_data_message(
          subscription, publisher_snapshot.last_data_message,
          publisher_snapshot.last_data_message_size,
          publisher_snapshot.last_sample.source_timestamp_ns,
          publisher_snapshot.last_sample.source_timestamp_ns + 200U,
          receive) ||
      !receive.duplicate_or_stale || receive.accepted) {
    return fail(13);
  }

  rmw_message_info_t info{};
  taken = false;
  if ((rmw_take_with_info(subscription, &output, &taken, &info, nullptr) !=
       RMW_RET_OK) ||
      !taken || (output.value != 10U) ||
      (info.publication_sequence_number != 1) ||
      (info.reception_sequence_number != 1) ||
      (info.source_timestamp == 0) ||
      (info.received_timestamp != info.source_timestamp + 100) ||
      info.from_intra_process ||
      (std::memcmp(info.publisher_gid.data, publisher_snapshot.gid.data,
                   RMW_GID_STORAGE_SIZE) != 0)) {
    return fail(14);
  }

  rmw_subscription_options_t ignore_options = subscription_options;
  ignore_options.ignore_local_publications = true;
  rmw_subscription_t* ignore_subscription = rmw_create_subscription(
      other_node, &fixture.handle, "/counter", &qos, &ignore_options);
  if ((ignore_subscription == nullptr) ||
      !publish_and_receive(publisher, ignore_subscription, 11U, receive) ||
      !receive.ignored_local || receive.accepted ||
      !rmw_openrtdds_cpp::inspect_subscription(ignore_subscription, snapshot) ||
      snapshot.ready) {
    return fail(15);
  }
  if (rmw_destroy_subscription(other_node, ignore_subscription) != RMW_RET_OK) {
    return fail(16);
  }

  if (!publish_and_receive(publisher, subscription, 20U, receive) ||
      !publish_and_receive(publisher, subscription, 30U, receive) ||
      !publish_and_receive(publisher, subscription, 40U, receive) ||
      !receive.dropped_oldest ||
      !rmw_openrtdds_cpp::inspect_subscription(subscription, snapshot) ||
      (snapshot.history_size != 2U) || (snapshot.dropped_samples != 1U)) {
    return fail(17);
  }

  Message first{};
  Message second{};
  std::array<void*, 2U> message_storage{{&first, &second}};
  std::array<rmw_message_info_t, 2U> info_storage{};
  rmw_message_sequence_t messages{};
  messages.data = message_storage.data();
  messages.capacity = message_storage.size();
  rmw_message_info_sequence_t infos{};
  infos.data = info_storage.data();
  infos.capacity = info_storage.size();
  std::size_t taken_count = 0U;
  if ((rmw_take_sequence(subscription, 2U, &messages, &infos, &taken_count,
                         nullptr) != RMW_RET_OK) ||
      (taken_count != 2U) || (messages.size != 2U) || (infos.size != 2U) ||
      (first.value != 30U) || (second.value != 40U) ||
      (info_storage[0].publication_sequence_number != 4) ||
      (info_storage[1].publication_sequence_number != 5) ||
      (info_storage[0].reception_sequence_number != 3) ||
      (info_storage[1].reception_sequence_number != 4)) {
    return fail(18);
  }

  std::array<rmw_subscription_t*, 7U> extra{};
  for (auto& item : extra) {
    item = rmw_create_subscription(node, &fixture.handle, "/counter", &qos,
                                   &subscription_options);
    if (item == nullptr) {
      return fail(19);
    }
  }
  if (rmw_create_subscription(node, &fixture.handle, "/overflow", &qos,
                              &subscription_options) != nullptr) {
    return fail(20);
  }
  rmw_reset_error();
  for (auto* item : extra) {
    if (rmw_destroy_subscription(node, item) != RMW_RET_OK) {
      return fail(21);
    }
  }

  if (rmw_shutdown(&context) != RMW_RET_OK) {
    return fail(22);
  }
  if (rmw_openrtdds_cpp::receive_data_message(
          subscription, publisher_snapshot.last_data_message,
          publisher_snapshot.last_data_message_size, 1U, 2U, receive) ||
      (receive.error !=
       rmw_openrtdds_cpp::SubscriptionReceiveError::inactive_context)) {
    return fail(23);
  }
  rmw_reset_error();
  if (rmw_take(subscription, &output, &taken, nullptr) != RMW_RET_ERROR) {
    return fail(24);
  }
  rmw_reset_error();
  if ((rmw_destroy_subscription(node, subscription) != RMW_RET_OK) ||
      (rmw_destroy_publisher(node, publisher) != RMW_RET_OK) ||
      (rmw_destroy_node(other_node) != RMW_RET_OK) ||
      (rmw_destroy_node(node) != RMW_RET_OK) ||
      (rmw_context_fini(&context) != RMW_RET_OK) ||
      (rmw_init_options_fini(&options) != RMW_RET_OK)) {
    return fail(25);
  }
  return 0;
}
