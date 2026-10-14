#include "rmw_openrtdds_cpp/publisher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>
#include <time.h>

#include "internal.hpp"
#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/rtps/sedp.hpp"
#include "openrtdds/rtps/spdp.hpp"
#include "openrtdds/serialization/cdr.hpp"
#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/qos_profiles.h"
#include "rmw/validate_full_topic_name.h"
#include "rmw_openrtdds_cpp/xcdr1_codec.hpp"

namespace rmw_openrtdds_cpp {
namespace {

// Requirements: ORT-RMW-004, ORT-RMW-012, ORT-RMW-014, ORT-RMW-015
// Requirements: ORT-RMW-045, ORT-RMW-046
constexpr std::size_t kMaximumSampleSize = 65'000U;
constexpr std::size_t kDataMessageOverhead = 44U;
constexpr std::size_t kSedpCapacity = 2'048U;
constexpr std::size_t kMaximumHistoryDepth = 1'024U;
constexpr openrtdds::rtps::VendorId kVendor{{0x01U, 0x42U}};
constexpr openrtdds::rtps::ProtocolVersion kProtocol{2U, 3U};
constexpr openrtdds::rtps::EntityId kUnknownReader{{0U, 0U, 0U, 0U}};
constexpr openrtdds::rtps::EntityId kPublicationsReader{
    {0U, 0U, 3U, 0xC7U}};

struct PublisherData final {
  const rmw_node_t* node{nullptr};
  const rosidl_message_type_support_t* type_support{nullptr};
  std::size_t slot_index{0U};
  std::uint32_t slot_generation{0U};
  TypeSupportInfo type_info{};
  DdsNames names{};
  EndpointResourcePlan resources{};
  openrtdds::rtps::GuidPrefix participant_prefix{};
  openrtdds::rtps::EntityId writer_id{};
  rmw_gid_t gid{};
  std::uint8_t* storage{nullptr};
  std::size_t storage_size{0U};
  std::uint8_t* staging{nullptr};
  std::uint8_t* data_message_staging{nullptr};
  std::uint8_t* data_message{nullptr};
  std::uint8_t* history{nullptr};
  std::array<PublisherSampleMetadata, kMaximumHistoryDepth> history_metadata{};
  std::array<std::uint8_t, kSedpCapacity> sedp{};
  std::size_t sedp_size{0U};
  std::size_t data_message_size{0U};
  std::size_t history_size{0U};
  std::size_t history_next{0U};
  std::uint64_t next_sequence{1U};
  PublisherSampleMetadata last_sample{};
  std::mutex mutex{};
};

[[nodiscard]] bool checked_add(const std::size_t left,
                               const std::size_t right,
                               std::size_t& output) noexcept {
  if (left > (std::numeric_limits<std::size_t>::max() - right)) {
    return false;
  }
  output = left + right;
  return true;
}

[[nodiscard]] openrtdds::rtps::GuidPrefix participant_prefix(
    const rmw_context_t& context) noexcept {
  openrtdds::rtps::GuidPrefix result{};
  result.value = {{0x4FU, 0x52U, 0x54U,
                   static_cast<std::uint8_t>(context.actual_domain_id),
                   static_cast<std::uint8_t>(context.instance_id >> 56U),
                   static_cast<std::uint8_t>(context.instance_id >> 48U),
                   static_cast<std::uint8_t>(context.instance_id >> 40U),
                   static_cast<std::uint8_t>(context.instance_id >> 32U),
                   static_cast<std::uint8_t>(context.instance_id >> 24U),
                   static_cast<std::uint8_t>(context.instance_id >> 16U),
                   static_cast<std::uint8_t>(context.instance_id >> 8U),
                   static_cast<std::uint8_t>(context.instance_id)}};
  return result;
}

[[nodiscard]] openrtdds::rtps::EntityId writer_id(
    const std::size_t slot_index) noexcept {
  return {{0U, 0U, static_cast<std::uint8_t>(slot_index + 1U), 0x03U}};
}

[[nodiscard]] rmw_gid_t make_gid(
    const openrtdds::rtps::GuidPrefix& prefix,
    const openrtdds::rtps::EntityId& entity) noexcept {
  rmw_gid_t gid{};
  gid.implementation_identifier = openrtdds::rmw::kImplementationIdentifier;
  std::memcpy(gid.data, prefix.value.data(), prefix.value.size());
  std::memcpy(&gid.data[prefix.value.size()], entity.value.data(),
              entity.value.size());
  return gid;
}

[[nodiscard]] bool timestamp_now(std::uint64_t& result) noexcept {
  timespec value{};
  if (::clock_gettime(CLOCK_REALTIME, &value) != 0 || value.tv_sec < 0) {
    return false;
  }
  constexpr std::uint64_t billion = 1'000'000'000ULL;
  const auto seconds = static_cast<std::uint64_t>(value.tv_sec);
  if (seconds > (std::numeric_limits<std::uint64_t>::max() / billion)) {
    return false;
  }
  result = (seconds * billion) + static_cast<std::uint64_t>(value.tv_nsec);
  return true;
}

[[nodiscard]] bool valid_publisher(const rmw_publisher_t* const publisher,
                                   PublisherData*& data) noexcept {
  if ((publisher == nullptr) || (publisher->data == nullptr) ||
      !detail::identifier_matches(publisher->implementation_identifier)) {
    return false;
  }
  auto* candidate = static_cast<PublisherData*>(publisher->data);
  if ((candidate->node == nullptr) || (candidate->node->context == nullptr) ||
      !detail::context_is_valid(candidate->node->context) ||
      (candidate->slot_index >= detail::kPublisherCapacity)) {
    return false;
  }
  const auto& slot =
      candidate->node->context->impl->publishers[candidate->slot_index];
  if (!slot.occupied || (slot.data != candidate) ||
      (slot.generation != candidate->slot_generation)) {
    return false;
  }
  data = candidate;
  return true;
}

void release_slot(detail::PublisherSlot& slot) noexcept {
  slot.data = nullptr;
  slot.occupied = false;
  ++slot.generation;
  if (slot.generation == 0U) {
    slot.generation = 1U;
  }
}

[[nodiscard]] bool build_sedp(PublisherData& data,
                              const rmw_context_t& context) noexcept {
  openrtdds::rtps::SedpAnnouncementConfig config{};
  config.endpoint.protocol_version = kProtocol;
  config.endpoint.vendor_id = kVendor;
  config.endpoint.participant_guid_prefix = data.participant_prefix;
  config.endpoint.endpoint_id = data.writer_id;
  config.endpoint.kind = openrtdds::rtps::EndpointKind::writer;
  config.reader_id = kPublicationsReader;
  config.endpoint.topic_name_size = std::strlen(data.names.topic_name.data());
  config.endpoint.type_name_size = std::strlen(data.names.type_name.data());
  std::memcpy(config.endpoint.topic_name.data(), data.names.topic_name.data(),
              config.endpoint.topic_name_size);
  std::memcpy(config.endpoint.type_name.data(), data.names.type_name.data(),
              config.endpoint.type_name_size);
  config.endpoint.reliability =
      data.resources.qos.profile.reliability ==
              RMW_QOS_POLICY_RELIABILITY_RELIABLE
          ? openrtdds::rtps::ReliabilityKind::reliable
          : openrtdds::rtps::ReliabilityKind::best_effort;
  config.endpoint.durability =
      openrtdds::rtps::DurabilityKind::volatile_durability;

  openrtdds::rtps::SpdpPortConfig ports{};
  std::uint16_t metadata_port = 0U;
  if (!openrtdds::rtps::spdp_unicast_port(
          static_cast<std::uint32_t>(context.actual_domain_id), 0U, ports,
          metadata_port) ||
      (metadata_port == std::numeric_limits<std::uint16_t>::max())) {
    return false;
  }
  openrtdds::rtps::Locator locator{};
  if (!openrtdds::rtps::make_udp_v4_locator(
          {{127U, 0U, 0U, 1U}},
          static_cast<std::uint16_t>(metadata_port + 1U), locator) ||
      !config.endpoint.unicast_locators.push_back(locator)) {
    return false;
  }

  openrtdds::rtps::SedpMessageBuilder builder(data.sedp.data(),
                                               data.sedp.size());
  if (!builder.build(config)) {
    return false;
  }
  data.sedp_size = builder.size();
  return true;
}

[[nodiscard]] rmw_ret_t invalid_publisher_error(
    const rmw_publisher_t* const publisher) noexcept {
  if ((publisher != nullptr) &&
      (publisher->implementation_identifier != nullptr) &&
      !detail::identifier_matches(publisher->implementation_identifier)) {
    RMW_SET_ERROR_MSG("publisher belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  RMW_SET_ERROR_MSG("invalid OpenRTDDS publisher");
  return RMW_RET_INVALID_ARGUMENT;
}

}  // namespace

bool inspect_publisher(const rmw_publisher_t* const publisher,
                       PublisherSnapshot& snapshot) noexcept {
  PublisherData* data = nullptr;
  if (!valid_publisher(publisher, data)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  PublisherSnapshot candidate{};
  candidate.gid = data->gid;
  candidate.names = data->names;
  candidate.actual_qos = data->resources.qos.profile;
  candidate.maximum_serialized_sample_size =
      data->resources.maximum_serialized_sample_size;
  candidate.history_size = data->history_size;
  candidate.history_capacity = data->resources.history_sample_slots;
  candidate.next_sequence_number = data->next_sequence;
  candidate.last_sample = data->last_sample;
  candidate.sedp_announcement = data->sedp.data();
  candidate.sedp_announcement_size = data->sedp_size;
  candidate.last_data_message = data->data_message;
  candidate.last_data_message_size = data->data_message_size;
  snapshot = candidate;
  return true;
}

}  // namespace rmw_openrtdds_cpp

extern "C" {

// Requirements: ORT-RMW-004, ORT-RMW-045, ORT-RMW-046
rmw_publisher_t* rmw_create_publisher(
    const rmw_node_t* node,
    const rosidl_message_type_support_t* type_support,
    const char* topic_name,
    const rmw_qos_profile_t* qos_profile,
    const rmw_publisher_options_t* publisher_options) {
  using namespace rmw_openrtdds_cpp;
  if ((node == nullptr) || (node->context == nullptr) ||
      (node->data == nullptr) || (type_support == nullptr) ||
      (topic_name == nullptr) || (qos_profile == nullptr) ||
      (publisher_options == nullptr)) {
    RMW_SET_ERROR_MSG("publisher creation requires non-null arguments");
    return nullptr;
  }
  if (!detail::identifier_matches(node->implementation_identifier)) {
    RMW_SET_ERROR_MSG("node belongs to another RMW implementation");
    return nullptr;
  }
  rmw_context_t* const context = node->context;
  if (!detail::context_is_valid(context) || context->impl->shutdown) {
    RMW_SET_ERROR_MSG("publisher creation requires an active context");
    return nullptr;
  }
  if (publisher_options->rmw_specific_publisher_payload != nullptr) {
    RMW_SET_ERROR_MSG("implementation-specific publisher payload is unsupported");
    return nullptr;
  }
  switch (publisher_options->require_unique_network_flow_endpoints) {
    case RMW_UNIQUE_NETWORK_FLOW_ENDPOINTS_NOT_REQUIRED:
    case RMW_UNIQUE_NETWORK_FLOW_ENDPOINTS_OPTIONALLY_REQUIRED:
    case RMW_UNIQUE_NETWORK_FLOW_ENDPOINTS_SYSTEM_DEFAULT:
      break;
    case RMW_UNIQUE_NETWORK_FLOW_ENDPOINTS_STRICTLY_REQUIRED:
      RMW_SET_ERROR_MSG("unique network flow endpoints are unsupported");
      return nullptr;
    default:
      RMW_SET_ERROR_MSG("invalid unique network flow requirement");
      return nullptr;
  }
  if (!qos_profile->avoid_ros_namespace_conventions) {
    int validation = RMW_TOPIC_VALID;
    if ((rmw_validate_full_topic_name(topic_name, &validation, nullptr) !=
         RMW_RET_OK) ||
        (validation != RMW_TOPIC_VALID)) {
      RMW_SET_ERROR_MSG("publisher topic is not a valid fully qualified ROS name");
      return nullptr;
    }
  } else if (topic_name[0] == '\0') {
    RMW_SET_ERROR_MSG("literal DDS topic name is empty");
    return nullptr;
  }

  TypeSupportInfo type_info{};
  TypeSupportError type_error = TypeSupportError::none;
  if (!analyze_type_support(type_support, kMaximumSampleSize, type_info,
                            type_error)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("publisher type rejected: %s",
                                         to_string(type_error));
    return nullptr;
  }
  DdsNames names{};
  if (!map_ros_to_dds_names(type_support, RosChannel::topic, topic_name,
                            qos_profile->avoid_ros_namespace_conventions,
                            names, type_error)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("publisher name mapping failed: %s",
                                         to_string(type_error));
    return nullptr;
  }
  QosLimits limits{};
  limits.maximum_sample_size = kMaximumSampleSize;
  EndpointResourcePlan resources{};
  QosError qos_error = QosError::none;
  if (!plan_endpoint_resources(*qos_profile, type_info,
                               EndpointRole::publisher, limits, resources,
                               qos_error)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("publisher QoS rejected: %s",
                                         to_string(qos_error));
    return nullptr;
  }

  std::lock_guard<std::mutex> registry_lock(
      context->impl->publisher_registry_mutex);
  std::size_t slot_index = detail::kPublisherCapacity;
  for (std::size_t index = 0U; index < detail::kPublisherCapacity; ++index) {
    if (!context->impl->publishers[index].occupied) {
      slot_index = index;
      context->impl->publishers[index].occupied = true;
      break;
    }
  }
  if (slot_index == detail::kPublisherCapacity) {
    RMW_SET_ERROR_MSG("OpenRTDDS publisher pool is exhausted");
    return nullptr;
  }
  auto& slot = context->impl->publishers[slot_index];
  auto& allocator = context->options.allocator;
  rmw_publisher_t* publisher = static_cast<rmw_publisher_t*>(
      allocator.zero_allocate(1U, sizeof(rmw_publisher_t), allocator.state));
  void* data_storage =
      allocator.zero_allocate(1U, sizeof(PublisherData), allocator.state);
  auto* data = data_storage == nullptr
                   ? nullptr
                   : new (data_storage) PublisherData{};
  char* topic_copy = rcutils_strdup(topic_name, allocator);

  std::size_t data_capacity = 0U;
  std::size_t runtime_bytes = 0U;
  std::size_t packet_storage_bytes = 0U;
  if (!checked_add(type_info.maximum_serialized_size, kDataMessageOverhead,
                   data_capacity) ||
      !checked_add(data_capacity, data_capacity, packet_storage_bytes) ||
      !checked_add(type_info.maximum_serialized_size, packet_storage_bytes,
                   runtime_bytes) ||
      !checked_add(runtime_bytes, resources.history_storage_bytes,
                   runtime_bytes)) {
    runtime_bytes = 0U;
  }
  auto* storage = runtime_bytes == 0U
                      ? nullptr
                      : static_cast<std::uint8_t*>(
                            allocator.allocate(runtime_bytes, allocator.state));
  if ((publisher == nullptr) || (data == nullptr) || (topic_copy == nullptr) ||
      (storage == nullptr)) {
    if (storage != nullptr) {
      allocator.deallocate(storage, allocator.state);
    }
    if (topic_copy != nullptr) {
      allocator.deallocate(topic_copy, allocator.state);
    }
    if (data != nullptr) {
      data->~PublisherData();
      allocator.deallocate(data, allocator.state);
    }
    if (publisher != nullptr) {
      allocator.deallocate(publisher, allocator.state);
    }
    release_slot(slot);
    RMW_SET_ERROR_MSG("unable to allocate bounded publisher storage");
    return nullptr;
  }

  data->node = node;
  data->type_support = type_support;
  data->slot_index = slot_index;
  data->slot_generation = slot.generation;
  data->type_info = type_info;
  data->names = names;
  data->resources = resources;
  data->participant_prefix = participant_prefix(*context);
  data->writer_id = writer_id(slot_index);
  data->gid = make_gid(data->participant_prefix, data->writer_id);
  data->storage = storage;
  data->storage_size = runtime_bytes;
  data->staging = storage;
  data->data_message_staging =
      storage + type_info.maximum_serialized_size;
  data->data_message = data->data_message_staging + data_capacity;
  data->history = data->data_message + data_capacity;
  if (!build_sedp(*data, *context)) {
    allocator.deallocate(storage, allocator.state);
    allocator.deallocate(topic_copy, allocator.state);
    data->~PublisherData();
    allocator.deallocate(data, allocator.state);
    allocator.deallocate(publisher, allocator.state);
    release_slot(slot);
    RMW_SET_ERROR_MSG("unable to build bounded publisher SEDP announcement");
    return nullptr;
  }

  publisher->implementation_identifier =
      openrtdds::rmw::kImplementationIdentifier;
  publisher->data = data;
  publisher->topic_name = topic_copy;
  publisher->options = *publisher_options;
  publisher->can_loan_messages = false;
  slot.data = data;
  auto* node_data = static_cast<detail::NodeData*>(node->data);
  ++node_data->publisher_count;
  return publisher;
}

rmw_ret_t rmw_destroy_publisher(rmw_node_t* node,
                                rmw_publisher_t* publisher) {
  using namespace rmw_openrtdds_cpp;
  if ((node == nullptr) || (node->context == nullptr) ||
      (node->data == nullptr) || (publisher == nullptr)) {
    RMW_SET_ERROR_MSG("invalid node or publisher");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!detail::identifier_matches(node->implementation_identifier)) {
    RMW_SET_ERROR_MSG("node belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  PublisherData* data = nullptr;
  if (!valid_publisher(publisher, data)) {
    return invalid_publisher_error(publisher);
  }
  if (data->node != node) {
    RMW_SET_ERROR_MSG("publisher does not belong to the supplied node");
    return RMW_RET_INVALID_ARGUMENT;
  }

  rmw_context_t* const context = node->context;
  std::lock_guard<std::mutex> registry_lock(
      context->impl->publisher_registry_mutex);
  auto& slot = context->impl->publishers[data->slot_index];
  auto& allocator = context->options.allocator;
  auto* node_data = static_cast<detail::NodeData*>(node->data);
  if (node_data->publisher_count == 0U) {
    RMW_SET_ERROR_MSG("publisher ownership count is inconsistent");
    return RMW_RET_ERROR;
  }
  --node_data->publisher_count;
  release_slot(slot);
  allocator.deallocate(data->storage, allocator.state);
  allocator.deallocate(const_cast<char*>(publisher->topic_name),
                       allocator.state);
  data->~PublisherData();
  allocator.deallocate(data, allocator.state);
  allocator.deallocate(publisher, allocator.state);
  return RMW_RET_OK;
}

rmw_ret_t rmw_publish(const rmw_publisher_t* publisher,
                      const void* ros_message,
                      rmw_publisher_allocation_t* allocation) {
  using namespace rmw_openrtdds_cpp;
  if (ros_message == nullptr) {
    RMW_SET_ERROR_MSG("ROS message is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (allocation != nullptr) {
    RMW_SET_ERROR_MSG("publisher allocation objects are unsupported");
    return RMW_RET_UNSUPPORTED;
  }
  PublisherData* data = nullptr;
  if (!valid_publisher(publisher, data)) {
    return invalid_publisher_error(publisher);
  }
  if (data->node->context->impl->shutdown) {
    RMW_SET_ERROR_MSG("publisher context has been shutdown");
    return RMW_RET_ERROR;
  }

  std::lock_guard<std::mutex> lock(data->mutex);
  CodecResult codec{};
  if (!serialize_xcdr1(data->type_support, ros_message, data->staging,
                       data->type_info.maximum_serialized_size,
                       CodecByteOrder::little_endian, codec)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("publisher serialization failed: %s",
                                         to_string(codec.error));
    return RMW_RET_ERROR;
  }
  if (data->next_sequence >=
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
    RMW_SET_ERROR_MSG("publisher sequence space is exhausted");
    return RMW_RET_ERROR;
  }
  openrtdds::rtps::DataMessageConfig config{};
  config.version = kProtocol;
  config.vendor_id = kVendor;
  config.guid_prefix = data->participant_prefix;
  config.reader_id = kUnknownReader;
  config.writer_id = data->writer_id;
  config.sequence_number = data->next_sequence;
  config.submessage_byte_order =
      openrtdds::serialization::ByteOrder::little_endian;
  openrtdds::rtps::DataMessageBuilder builder(
      data->data_message_staging,
      data->type_info.maximum_serialized_size + kDataMessageOverhead);
  if (!builder.build(config, data->staging, codec.bytes_processed)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("RTPS DATA construction failed: %s",
                                         openrtdds::rtps::to_string(
                                             builder.error()));
    return RMW_RET_ERROR;
  }
  std::uint64_t timestamp = 0U;
  if (!timestamp_now(timestamp)) {
    RMW_SET_ERROR_MSG("unable to read publisher source timestamp");
    return RMW_RET_ERROR;
  }

  const std::size_t history_index = data->history_next;
  std::memcpy(data->history +
                  (history_index * data->type_info.maximum_serialized_size),
              data->staging, codec.bytes_processed);
  const PublisherSampleMetadata metadata{
      data->next_sequence, timestamp, codec.bytes_processed};
  std::memcpy(data->data_message, data->data_message_staging, builder.size());
  data->history_metadata[history_index] = metadata;
  data->history_next =
      (history_index + 1U) % data->resources.history_sample_slots;
  if (data->history_size < data->resources.history_sample_slots) {
    ++data->history_size;
  }
  data->last_sample = metadata;
  data->data_message_size = builder.size();
  ++data->next_sequence;
  return RMW_RET_OK;
}

rmw_ret_t rmw_publisher_count_matched_subscriptions(
    const rmw_publisher_t* publisher, size_t* subscription_count) {
  using namespace rmw_openrtdds_cpp;
  if (subscription_count == nullptr) {
    RMW_SET_ERROR_MSG("subscription count is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  PublisherData* data = nullptr;
  if (!valid_publisher(publisher, data)) {
    return invalid_publisher_error(publisher);
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  *subscription_count = 0U;
  return RMW_RET_OK;
}

rmw_ret_t rmw_publisher_get_actual_qos(const rmw_publisher_t* publisher,
                                       rmw_qos_profile_t* qos) {
  using namespace rmw_openrtdds_cpp;
  if (qos == nullptr) {
    RMW_SET_ERROR_MSG("QoS output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  PublisherData* data = nullptr;
  if (!valid_publisher(publisher, data)) {
    return invalid_publisher_error(publisher);
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  *qos = data->resources.qos.profile;
  return RMW_RET_OK;
}

// Requirements: ORT-RMW-012, ORT-RMW-045
rmw_ret_t rmw_get_gid_for_publisher(const rmw_publisher_t* publisher,
                                    rmw_gid_t* gid) {
  using namespace rmw_openrtdds_cpp;
  if (gid == nullptr) {
    RMW_SET_ERROR_MSG("GID output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  PublisherData* data = nullptr;
  if (!valid_publisher(publisher, data)) {
    return invalid_publisher_error(publisher);
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  *gid = data->gid;
  return RMW_RET_OK;
}

}  // extern "C"
