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
#include "rmw_openrtdds_cpp/xcdr1_codec.hpp"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

// Verifies: ORT-RMW-004, ORT-RMW-012, ORT-RMW-014, ORT-RMW-015
// Verifies: ORT-RMW-045, ORT-RMW-046
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

}  // namespace

int main() {
  Fixture fixture{};
  const rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return fail(1);
  }
  options.instance_id = 30U;
  options.domain_id = 30U;
  options.enclave = rcutils_strdup("/openrtdds_publisher", allocator);
  if (options.enclave == nullptr) {
    return fail(2);
  }
  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return fail(3);
  }
  rmw_node_t* node = rmw_create_node(&context, "publisher", "/openrtdds");
  rmw_node_t* other_node =
      rmw_create_node(&context, "other", "/openrtdds");
  if ((node == nullptr) || (other_node == nullptr)) {
    return fail(4);
  }

  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.depth = 2U;
  const rmw_publisher_options_t publisher_options =
      rmw_get_default_publisher_options();
  rmw_publisher_t* publisher = rmw_create_publisher(
      node, &fixture.handle, "/counter", &qos, &publisher_options);
  if (publisher == nullptr) {
    return fail(5);
  }
  if (rmw_destroy_publisher(other_node, publisher) !=
      RMW_RET_INVALID_ARGUMENT) {
    return fail(5);
  }
  rmw_reset_error();
  if (rmw_destroy_node(node) != RMW_RET_ERROR) {
    return fail(6);
  }
  rmw_reset_error();

  rmw_openrtdds_cpp::PublisherSnapshot snapshot{};
  if (!rmw_openrtdds_cpp::inspect_publisher(publisher, snapshot) ||
      (std::strcmp(snapshot.names.topic_name.data(), "rt/counter") != 0) ||
      (std::strcmp(snapshot.names.type_name.data(),
                   "openrtdds_test_msgs::msg::dds_::Counter_") != 0) ||
      (snapshot.history_capacity != 2U) || (snapshot.history_size != 0U) ||
      (snapshot.next_sequence_number != 1U) ||
      (snapshot.sedp_announcement_size == 0U)) {
    return fail(7);
  }

  openrtdds::rtps::GuidPrefix prefix{};
  std::memcpy(prefix.value.data(), snapshot.gid.data, prefix.value.size());
  openrtdds::rtps::SedpMessageView sedp{};
  const auto sedp_result = openrtdds::rtps::parse_sedp_message(
      snapshot.sedp_announcement, snapshot.sedp_announcement_size, prefix,
      sedp);
  if (!sedp_result.ok() ||
      (sedp.endpoint.kind != openrtdds::rtps::EndpointKind::writer) ||
      (std::memcmp(sedp.endpoint.endpoint_id.value.data(),
                   &snapshot.gid.data[12], 4U) != 0) ||
      (std::strncmp(sedp.endpoint.topic_name.data(), "rt/counter",
                    sedp.endpoint.topic_name_size) != 0)) {
    return fail(8);
  }

  rmw_gid_t gid{};
  rmw_qos_profile_t actual{};
  std::size_t matches = 99U;
  if ((rmw_get_gid_for_publisher(publisher, &gid) != RMW_RET_OK) ||
      (std::memcmp(gid.data, snapshot.gid.data, RMW_GID_STORAGE_SIZE) != 0) ||
      (rmw_publisher_get_actual_qos(publisher, &actual) != RMW_RET_OK) ||
      (actual.depth != 2U) ||
      (rmw_publisher_count_matched_subscriptions(publisher, &matches) !=
       RMW_RET_OK) ||
      (matches != 0U)) {
    return fail(9);
  }

  Message message{42U};
  if (rmw_publish(publisher, &message, nullptr) != RMW_RET_OK) {
    return fail(10);
  }
  if (!rmw_openrtdds_cpp::inspect_publisher(publisher, snapshot) ||
      (snapshot.history_size != 1U) ||
      (snapshot.next_sequence_number != 2U) ||
      (snapshot.last_sample.sequence_number != 1U) ||
      (snapshot.last_sample.source_timestamp_ns == 0U) ||
      (snapshot.last_data_message_size == 0U)) {
    return fail(11);
  }
  openrtdds::rtps::DataMessageView data{};
  if ((openrtdds::rtps::parse_data_message(
           snapshot.last_data_message, snapshot.last_data_message_size,
           data) != openrtdds::rtps::RtpsError::none) ||
      (data.sequence_number != 1U) ||
      (std::memcmp(data.writer_id.value.data(), &gid.data[12], 4U) != 0)) {
    return fail(12);
  }
  Message decoded{};
  rmw_openrtdds_cpp::CodecResult codec{};
  if (!rmw_openrtdds_cpp::deserialize_xcdr1(
          &fixture.handle, data.serialized_payload, data.payload_size,
          snapshot.maximum_serialized_sample_size, &decoded, codec) ||
      (decoded.value != 42U)) {
    return fail(13);
  }

  const auto before_failure = snapshot;
  if (rmw_publish(publisher, nullptr, nullptr) != RMW_RET_INVALID_ARGUMENT) {
    return fail(14);
  }
  rmw_reset_error();
  if (!rmw_openrtdds_cpp::inspect_publisher(publisher, snapshot) ||
      (snapshot.next_sequence_number != before_failure.next_sequence_number) ||
      (snapshot.history_size != before_failure.history_size)) {
    return fail(15);
  }
  message.value = 43U;
  if ((rmw_publish(publisher, &message, nullptr) != RMW_RET_OK) ||
      !rmw_openrtdds_cpp::inspect_publisher(publisher, snapshot) ||
      (snapshot.history_size != 2U) ||
      (snapshot.last_sample.sequence_number != 2U)) {
    return fail(16);
  }

  rmw_publisher_t* extra[7]{};
  for (auto& item : extra) {
    item = rmw_create_publisher(node, &fixture.handle, "/counter", &qos,
                                &publisher_options);
    if (item == nullptr) {
      return fail(17);
    }
  }
  if (rmw_create_publisher(node, &fixture.handle, "/overflow", &qos,
                           &publisher_options) != nullptr) {
    return fail(18);
  }
  rmw_reset_error();
  for (auto* item : extra) {
    if (rmw_destroy_publisher(node, item) != RMW_RET_OK) {
      return fail(19);
    }
  }

  if (rmw_shutdown(&context) != RMW_RET_OK) {
    return fail(20);
  }
  if (rmw_publish(publisher, &message, nullptr) != RMW_RET_ERROR) {
    return fail(21);
  }
  rmw_reset_error();
  if ((rmw_destroy_publisher(node, publisher) != RMW_RET_OK) ||
      (rmw_destroy_node(other_node) != RMW_RET_OK) ||
      (rmw_destroy_node(node) != RMW_RET_OK) ||
      (rmw_context_fini(&context) != RMW_RET_OK) ||
      (rmw_init_options_fini(&options) != RMW_RET_OK)) {
    return fail(22);
  }
  return 0;
}
