#include "rmw_openrtdds_cpp/subscription.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

#include "internal.hpp"
#include "openrtdds/rtps/sedp.hpp"
#include "openrtdds/rtps/spdp.hpp"
#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/message_sequence.h"
#include "rmw/qos_profiles.h"
#include "rmw/validate_full_topic_name.h"
#include "rmw_openrtdds_cpp/xcdr1_codec.hpp"

namespace rmw_openrtdds_cpp {
namespace {

// Requirements: ORT-RMW-005, ORT-RMW-012, ORT-RMW-014, ORT-RMW-015
// Requirements: ORT-RMW-047, ORT-RMW-048
constexpr std::size_t kMaximumSampleSize = 65'000U;
constexpr std::size_t kSedpCapacity = 2'048U;
constexpr std::size_t kMaximumHistoryDepth = 1'024U;
constexpr std::size_t kWriterStateCapacity = 16U;
constexpr openrtdds::rtps::VendorId kVendor{{0x01U, 0x42U}};
constexpr openrtdds::rtps::ProtocolVersion kProtocol{2U, 3U};
constexpr openrtdds::rtps::EntityId kUnknownReader{{0U, 0U, 0U, 0U}};
constexpr openrtdds::rtps::EntityId kSubscriptionsReader{
    {0U, 0U, 4U, 0xC7U}};

struct SubscriptionSampleMetadata final {
  rmw_gid_t publisher_gid{};
  std::uint64_t publication_sequence_number{0U};
  std::uint64_t reception_sequence_number{0U};
  std::uint64_t source_timestamp_ns{0U};
  std::uint64_t received_timestamp_ns{0U};
  std::size_t serialized_size{0U};
};

struct WriterState final {
  std::array<std::uint8_t, RMW_GID_STORAGE_SIZE> guid{};
  std::uint64_t last_sequence{0U};
  bool occupied{false};
};

struct SubscriptionData final {
  rmw_subscription_t* handle{nullptr};
  const rmw_node_t* node{nullptr};
  const rosidl_message_type_support_t* type_support{nullptr};
  std::size_t slot_index{0U};
  std::uint32_t slot_generation{0U};
  TypeSupportInfo type_info{};
  DdsNames names{};
  RosChannel channel{RosChannel::topic};
  bool graph_visible{true};
  EndpointResourcePlan resources{};
  openrtdds::rtps::GuidPrefix participant_prefix{};
  openrtdds::rtps::EntityId reader_id{};
  std::array<std::uint8_t, RMW_GID_STORAGE_SIZE> reader_guid{};
  std::uint8_t* history{nullptr};
  std::size_t history_storage_size{0U};
  std::size_t serialized_capacity{0U};
  std::array<SubscriptionSampleMetadata, kMaximumHistoryDepth>
      history_metadata{};
  std::array<WriterState, kWriterStateCapacity> writers{};
  std::array<std::uint8_t, kSedpCapacity> sedp{};
  std::size_t sedp_size{0U};
  std::size_t history_head{0U};
  std::size_t history_size{0U};
  std::size_t observed_writer_count{0U};
  std::uint64_t next_reception_sequence{1U};
  std::uint64_t accepted_samples{0U};
  std::uint64_t dropped_samples{0U};
  std::size_t matched_publishers{0U};
  std::size_t total_matched_publishers{0U};
  std::uint64_t match_generation{1U};
  std::mutex mutex{};
};

[[nodiscard]] openrtdds::rtps::GuidPrefix participant_prefix(
    const rmw_context_t& context) noexcept {
  return context.impl->participant_prefix;
}

[[nodiscard]] openrtdds::rtps::EntityId reader_id(
    const std::size_t slot_index) noexcept {
  return {{0U, 0U, static_cast<std::uint8_t>(slot_index + 1U), 0x04U}};
}

void make_guid(const openrtdds::rtps::GuidPrefix& prefix,
               const openrtdds::rtps::EntityId& entity,
               std::array<std::uint8_t, RMW_GID_STORAGE_SIZE>& guid) noexcept {
  std::memcpy(guid.data(), prefix.value.data(), prefix.value.size());
  std::memcpy(&guid[prefix.value.size()], entity.value.data(),
              entity.value.size());
}

[[nodiscard]] rmw_gid_t make_rmw_gid(
    const openrtdds::rtps::GuidPrefix& prefix,
    const openrtdds::rtps::EntityId& entity) noexcept {
  rmw_gid_t result{};
  result.implementation_identifier = openrtdds::rmw::kImplementationIdentifier;
  std::memcpy(result.data, prefix.value.data(), prefix.value.size());
  std::memcpy(&result.data[prefix.value.size()], entity.value.data(),
              entity.value.size());
  return result;
}

[[nodiscard]] bool nonzero_prefix(
    const openrtdds::rtps::GuidPrefix& prefix) noexcept {
  for (const auto value : prefix.value) {
    if (value != 0U) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool valid_writer_id(
    const openrtdds::rtps::EntityId& entity) noexcept {
  const bool nonzero_key =
      (entity.value[0] | entity.value[1] | entity.value[2]) != 0U;
  return nonzero_key &&
         ((entity.value[3] == 0x02U) || (entity.value[3] == 0x03U));
}

[[nodiscard]] bool valid_subscription(
    const rmw_subscription_t* const subscription,
    SubscriptionData*& data) noexcept {
  if ((subscription == nullptr) || (subscription->data == nullptr) ||
      !detail::identifier_matches(subscription->implementation_identifier)) {
    return false;
  }
  auto* candidate = static_cast<SubscriptionData*>(subscription->data);
  if ((candidate->node == nullptr) || (candidate->node->context == nullptr) ||
      !detail::context_is_valid(candidate->node->context) ||
      (candidate->slot_index >= detail::kSubscriptionCapacity)) {
    return false;
  }
  const auto& slot =
      candidate->node->context->impl->subscriptions[candidate->slot_index];
  if (!slot.occupied || (slot.data != candidate) ||
      (slot.generation != candidate->slot_generation)) {
    return false;
  }
  data = candidate;
  return true;
}

void release_slot(detail::SubscriptionSlot& slot) noexcept {
  slot.data = nullptr;
  slot.occupied = false;
  ++slot.generation;
  if (slot.generation == 0U) {
    slot.generation = 1U;
  }
}

[[nodiscard]] bool build_sedp(SubscriptionData& data,
                              const rmw_context_t& context) noexcept {
  openrtdds::rtps::SedpAnnouncementConfig config{};
  config.endpoint.protocol_version = kProtocol;
  config.endpoint.vendor_id = kVendor;
  config.endpoint.participant_guid_prefix = data.participant_prefix;
  config.endpoint.endpoint_id = data.reader_id;
  config.endpoint.kind = openrtdds::rtps::EndpointKind::reader;
  config.reader_id = kSubscriptionsReader;
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

  openrtdds::rtps::Locator locator{};
  if (!openrtdds::rtps::make_udp_v4_locator(
          {{127U, 0U, 0U, 1U}},
          context.impl->user_port, locator) ||
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

[[nodiscard]] rmw_ret_t invalid_subscription_error(
    const rmw_subscription_t* const subscription) noexcept {
  if ((subscription != nullptr) &&
      (subscription->implementation_identifier != nullptr) &&
      !detail::identifier_matches(subscription->implementation_identifier)) {
    RMW_SET_ERROR_MSG("subscription belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  RMW_SET_ERROR_MSG("invalid OpenRTDDS subscription");
  return RMW_RET_INVALID_ARGUMENT;
}

[[nodiscard]] std::uint64_t rtps_timestamp_ns(
    const openrtdds::rtps::DataMessageView& view) noexcept {
  if (!view.has_source_timestamp || (view.source_timestamp_seconds < 0)) {
    return 0U;
  }
  constexpr std::uint64_t billion = 1'000'000'000ULL;
  const auto seconds =
      static_cast<std::uint64_t>(view.source_timestamp_seconds);
  const auto fractional =
      (static_cast<std::uint64_t>(view.source_timestamp_fraction) * billion) >>
      32U;
  return (seconds * billion) + fractional;
}

[[nodiscard]] rmw_message_info_t message_info(
    const SubscriptionSampleMetadata& metadata) noexcept {
  rmw_message_info_t info = rmw_get_zero_initialized_message_info();
  info.source_timestamp =
      static_cast<rmw_time_point_value_t>(metadata.source_timestamp_ns);
  info.received_timestamp =
      static_cast<rmw_time_point_value_t>(metadata.received_timestamp_ns);
  info.publication_sequence_number = metadata.publication_sequence_number;
  info.reception_sequence_number = metadata.reception_sequence_number;
  info.publisher_gid = metadata.publisher_gid;
  info.from_intra_process = false;
  return info;
}

[[nodiscard]] rmw_ret_t take_one(
    const rmw_subscription_t* const subscription, void* const ros_message,
    bool* const taken, rmw_message_info_t* const info,
    rmw_subscription_allocation_t* const allocation) noexcept {
  if ((ros_message == nullptr) || (taken == nullptr)) {
    RMW_SET_ERROR_MSG("take requires message and taken outputs");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (allocation != nullptr) {
    RMW_SET_ERROR_MSG("subscription allocation objects are unsupported");
    return RMW_RET_UNSUPPORTED;
  }
  SubscriptionData* data = nullptr;
  if (!valid_subscription(subscription, data)) {
    return invalid_subscription_error(subscription);
  }
  if (data->node->context->impl->shutdown) {
    RMW_SET_ERROR_MSG("subscription context has been shutdown");
    return RMW_RET_ERROR;
  }

  std::lock_guard<std::mutex> lock(data->mutex);
  *taken = false;
  if (data->history_size == 0U) {
    return RMW_RET_OK;
  }
  const std::size_t index = data->history_head;
  const auto& metadata = data->history_metadata[index];
  CodecResult codec{};
  if (!deserialize_xcdr1(
          data->type_support,
          data->history + (index * data->serialized_capacity),
          metadata.serialized_size,
          data->type_info.maximum_serialized_size, ros_message, codec)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "subscription deserialization failed: %s", to_string(codec.error));
    return RMW_RET_ERROR;
  }
  if (info != nullptr) {
    *info = message_info(metadata);
  }
  data->history_head =
      (data->history_head + 1U) % data->resources.history_sample_slots;
  --data->history_size;
  *taken = true;
  return RMW_RET_OK;
}

[[nodiscard]] rmw_ret_t take_service(
    const rmw_subscription_t* const subscription,
    rmw_service_info_t* const service_info, void* const ros_message,
    bool* const taken,
    bool (*const accept)(const rmw_request_id_t&, void*),
    void* const accept_context) noexcept {
  if ((service_info == nullptr) || (ros_message == nullptr) ||
      (taken == nullptr)) {
    RMW_SET_ERROR_MSG("service take requires non-null outputs");
    return RMW_RET_INVALID_ARGUMENT;
  }
  SubscriptionData* data = nullptr;
  if (!valid_subscription(subscription, data)) {
    return invalid_subscription_error(subscription);
  }
  if (data->channel == RosChannel::topic) {
    RMW_SET_ERROR_MSG("subscription is not a service channel");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (data->node->context->impl->shutdown) {
    RMW_SET_ERROR_MSG("subscription context has been shutdown");
    return RMW_RET_ERROR;
  }

  std::lock_guard<std::mutex> lock(data->mutex);
  *taken = false;
  if (data->history_size == 0U) {
    return RMW_RET_OK;
  }
  const std::size_t index = data->history_head;
  const auto& metadata = data->history_metadata[index];
  if (metadata.serialized_size <= detail::kServiceCorrelationSize) {
    RMW_SET_ERROR_MSG("service sample has a truncated correlation envelope");
    return RMW_RET_ERROR;
  }
  const std::uint8_t* const sample =
      data->history + (index * data->serialized_capacity);
  rmw_service_info_t candidate{};
  candidate.source_timestamp = static_cast<rmw_time_point_value_t>(
      metadata.source_timestamp_ns);
  candidate.received_timestamp = static_cast<rmw_time_point_value_t>(
      metadata.received_timestamp_ns);
  std::memcpy(candidate.request_id.writer_guid, sample,
              RMW_GID_STORAGE_SIZE);
  std::uint64_t sequence = 0U;
  for (std::size_t byte = 0U; byte < sizeof(sequence); ++byte) {
    sequence |= static_cast<std::uint64_t>(
        sample[RMW_GID_STORAGE_SIZE + byte]) << (byte * 8U);
  }
  candidate.request_id.sequence_number = static_cast<std::int64_t>(sequence);
  if ((accept != nullptr) &&
      !accept(candidate.request_id, accept_context)) {
    data->history_head =
        (data->history_head + 1U) % data->resources.history_sample_slots;
    --data->history_size;
    return RMW_RET_OK;
  }
  CodecResult codec{};
  if (!deserialize_xcdr1(
          data->type_support, sample + detail::kServiceCorrelationSize,
          metadata.serialized_size - detail::kServiceCorrelationSize,
          data->type_info.maximum_serialized_size, ros_message, codec)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "service deserialization failed: %s", to_string(codec.error));
    return RMW_RET_ERROR;
  }
  *service_info = candidate;
  data->history_head =
      (data->history_head + 1U) % data->resources.history_sample_slots;
  --data->history_size;
  *taken = true;
  return RMW_RET_OK;
}

}  // namespace

const char* to_string(const SubscriptionReceiveError error) noexcept {
  switch (error) {
    case SubscriptionReceiveError::none:
      return "none";
    case SubscriptionReceiveError::invalid_argument:
      return "invalid_argument";
    case SubscriptionReceiveError::inactive_context:
      return "inactive_context";
    case SubscriptionReceiveError::invalid_timestamp:
      return "invalid_timestamp";
    case SubscriptionReceiveError::rtps_parse_failed:
      return "rtps_parse_failed";
    case SubscriptionReceiveError::wrong_reader:
      return "wrong_reader";
    case SubscriptionReceiveError::invalid_writer:
      return "invalid_writer";
    case SubscriptionReceiveError::payload_too_large:
      return "payload_too_large";
    case SubscriptionReceiveError::writer_table_full:
      return "writer_table_full";
    case SubscriptionReceiveError::reception_sequence_exhausted:
      return "reception_sequence_exhausted";
  }
  return "unknown";
}

bool receive_data_message(
    const rmw_subscription_t* const subscription,
    const std::uint8_t* const message, const std::size_t message_size,
    const std::uint64_t source_timestamp_ns,
    const std::uint64_t received_timestamp_ns,
    SubscriptionReceiveResult& result) noexcept {
  SubscriptionReceiveResult candidate{};
  SubscriptionData* data = nullptr;
  if ((message == nullptr) || (message_size == 0U) ||
      !valid_subscription(subscription, data)) {
    candidate.error = SubscriptionReceiveError::invalid_argument;
    result = candidate;
    return false;
  }
  if (data->node->context->impl->shutdown) {
    candidate.error = SubscriptionReceiveError::inactive_context;
    result = candidate;
    return false;
  }
  constexpr auto time_max =
      static_cast<std::uint64_t>(std::numeric_limits<rmw_time_point_value_t>::max());
  if ((received_timestamp_ns == 0U) || (received_timestamp_ns > time_max) ||
      (source_timestamp_ns > time_max)) {
    candidate.error = SubscriptionReceiveError::invalid_timestamp;
    result = candidate;
    return false;
  }

  openrtdds::rtps::DataMessageView view{};
  candidate.rtps_error = openrtdds::rtps::parse_data_message(
      message, message_size, view);
  if (candidate.rtps_error != openrtdds::rtps::RtpsError::none) {
    candidate.error = SubscriptionReceiveError::rtps_parse_failed;
    result = candidate;
    return false;
  }
  if ((view.has_destination &&
       (view.destination_guid_prefix.value !=
        data->participant_prefix.value)) ||
      ((view.reader_id != kUnknownReader) &&
       (view.reader_id != data->reader_id))) {
    candidate.error = SubscriptionReceiveError::wrong_reader;
    result = candidate;
    return false;
  }
  if (!nonzero_prefix(view.guid_prefix) || !valid_writer_id(view.writer_id)) {
    candidate.error = SubscriptionReceiveError::invalid_writer;
    result = candidate;
    return false;
  }
  if ((view.payload_size == 0U) ||
      (view.payload_size > data->serialized_capacity)) {
    candidate.error = SubscriptionReceiveError::payload_too_large;
    result = candidate;
    return false;
  }
  const std::uint64_t resolved_source_timestamp =
      source_timestamp_ns == 0U ? rtps_timestamp_ns(view)
                                : source_timestamp_ns;
  if (resolved_source_timestamp > time_max) {
    candidate.error = SubscriptionReceiveError::invalid_timestamp;
    result = candidate;
    return false;
  }

  std::unique_lock<std::mutex> lock(data->mutex);
  if (subscription->options.ignore_local_publications &&
      (view.guid_prefix.value == data->participant_prefix.value)) {
    candidate.ignored_local = true;
    candidate.publication_sequence_number = view.sequence_number;
    result = candidate;
    return true;
  }

  const rmw_gid_t writer_gid = make_rmw_gid(view.guid_prefix, view.writer_id);
  WriterState* writer = nullptr;
  WriterState* free_writer = nullptr;
  for (auto& state : data->writers) {
    if (!state.occupied) {
      if (free_writer == nullptr) {
        free_writer = &state;
      }
      continue;
    }
    if (std::memcmp(state.guid.data(), writer_gid.data,
                    RMW_GID_STORAGE_SIZE) == 0) {
      writer = &state;
      break;
    }
  }
  if ((writer != nullptr) && (view.sequence_number <= writer->last_sequence)) {
    candidate.duplicate_or_stale = true;
    candidate.publication_sequence_number = view.sequence_number;
    result = candidate;
    return true;
  }
  if ((writer == nullptr) && (free_writer == nullptr)) {
    candidate.error = SubscriptionReceiveError::writer_table_full;
    result = candidate;
    return false;
  }
  if (data->next_reception_sequence ==
      std::numeric_limits<std::uint64_t>::max()) {
    candidate.error = SubscriptionReceiveError::reception_sequence_exhausted;
    result = candidate;
    return false;
  }

  std::size_t history_index = 0U;
  if (data->history_size < data->resources.history_sample_slots) {
    history_index =
        (data->history_head + data->history_size) %
        data->resources.history_sample_slots;
    ++data->history_size;
  } else {
    history_index = data->history_head;
    data->history_head =
        (data->history_head + 1U) % data->resources.history_sample_slots;
    ++data->dropped_samples;
    candidate.dropped_oldest = true;
  }
  std::memcpy(
      data->history + (history_index * data->serialized_capacity),
      view.serialized_payload, view.payload_size);
  const std::uint64_t reception_sequence = data->next_reception_sequence;
  data->history_metadata[history_index] = {
      writer_gid, view.sequence_number, reception_sequence,
      resolved_source_timestamp, received_timestamp_ns, view.payload_size};
  if (writer == nullptr) {
    writer = free_writer;
    std::memcpy(writer->guid.data(), writer_gid.data, RMW_GID_STORAGE_SIZE);
    writer->occupied = true;
    ++data->observed_writer_count;
  }
  writer->last_sequence = view.sequence_number;
  ++data->next_reception_sequence;
  ++data->accepted_samples;

  candidate.accepted = true;
  candidate.publication_sequence_number = view.sequence_number;
  candidate.reception_sequence_number = reception_sequence;
  result = candidate;
  rmw_context_t* const context = data->node->context;
  lock.unlock();
  detail::notify_waiters(context);
  return true;
}

namespace detail {

// Requirements: ORT-RMW-049, ORT-RMW-050
bool subscription_wait_data_valid(
    const void* const opaque, const rmw_context_t* const context) noexcept {
  if ((opaque == nullptr) || !context_is_valid(context)) {
    return false;
  }
  auto* candidate = const_cast<SubscriptionData*>(
      static_cast<const SubscriptionData*>(opaque));
  if ((candidate->node == nullptr) ||
      (candidate->node->context != context) ||
      (candidate->slot_index >= kSubscriptionCapacity)) {
    return false;
  }
  const auto& slot = context->impl->subscriptions[candidate->slot_index];
  return slot.occupied && (slot.data == candidate) &&
         (slot.generation == candidate->slot_generation);
}

bool subscription_wait_ready(const void* const opaque) noexcept {
  if (opaque == nullptr) {
    return false;
  }
  auto* data = const_cast<SubscriptionData*>(
      static_cast<const SubscriptionData*>(opaque));
  std::lock_guard<std::mutex> lock(data->mutex);
  return data->history_size != 0U;
}

// Requirements: ORT-RMW-005, ORT-RMW-051, ORT-RMW-052
bool subscription_network_view(void* const opaque,
                               NetworkEndpointView& view) noexcept {
  if (opaque == nullptr) {
    return false;
  }
  auto* data = static_cast<SubscriptionData*>(opaque);
  std::lock_guard<std::mutex> lock(data->mutex);
  NetworkEndpointView candidate{};
  candidate.descriptor.kind = openrtdds::rtps::EndpointKind::reader;
  candidate.descriptor.topic_name = data->names.topic_name.data();
  candidate.descriptor.topic_name_size =
      std::strlen(data->names.topic_name.data());
  candidate.descriptor.type_name = data->names.type_name.data();
  candidate.descriptor.type_name_size =
      std::strlen(data->names.type_name.data());
  candidate.descriptor.reliability =
      data->resources.qos.profile.reliability ==
              RMW_QOS_POLICY_RELIABILITY_RELIABLE
          ? openrtdds::rtps::ReliabilityKind::reliable
          : openrtdds::rtps::ReliabilityKind::best_effort;
  candidate.descriptor.durability =
      openrtdds::rtps::DurabilityKind::volatile_durability;
  candidate.entity_id = data->reader_id;
  candidate.sedp = data->sedp.data();
  candidate.sedp_size = data->sedp_size;
  view = candidate;
  return true;
}

bool subscription_network_receive(
    void* const opaque, const std::uint8_t* const packet,
    const std::size_t packet_size,
    const std::uint64_t received_timestamp_ns) noexcept {
  if (opaque == nullptr) {
    return false;
  }
  auto* data = static_cast<SubscriptionData*>(opaque);
  SubscriptionReceiveResult result{};
  return receive_data_message(data->handle, packet, packet_size,
                              received_timestamp_ns,
                              received_timestamp_ns, result) &&
         (result.accepted || result.duplicate_or_stale ||
          result.ignored_local);
}

std::size_t subscription_network_set_match_count(
    void* const opaque, const std::size_t count) noexcept {
  if (opaque == nullptr) {
    return 0U;
  }
  auto* data = static_cast<SubscriptionData*>(opaque);
  bool changed = false;
  std::size_t transitions = 0U;
  {
    std::lock_guard<std::mutex> lock(data->mutex);
    if (count != data->matched_publishers) {
      transitions = count > data->matched_publishers
                        ? count - data->matched_publishers
                        : data->matched_publishers - count;
      if (count > data->matched_publishers) {
        data->total_matched_publishers += count - data->matched_publishers;
      }
      data->matched_publishers = count;
      ++data->match_generation;
      if (data->match_generation == 0U) {
        data->match_generation = 1U;
      }
      changed = true;
    }
  }
  if (changed) {
    notify_waiters(data->node->context);
  }
  return transitions;
}

bool subscription_event_source(
    const rmw_subscription_t* const subscription, rmw_context_t*& context,
    void*& source, MatchedStatusSnapshot& snapshot) noexcept {
  SubscriptionData* data = nullptr;
  if (!valid_subscription(subscription, data)) {
    return false;
  }
  context = data->node->context;
  source = data;
  std::lock_guard<std::mutex> lock(data->mutex);
  snapshot = {data->total_matched_publishers, data->matched_publishers,
              data->match_generation};
  return true;
}

bool subscription_matched_status(const void* const source,
                                 const rmw_context_t* const context,
                                 MatchedStatusSnapshot& snapshot) noexcept {
  if ((source == nullptr) || !context_is_valid(context)) {
    return false;
  }
  auto* data = const_cast<SubscriptionData*>(
      static_cast<const SubscriptionData*>(source));
  if ((data->slot_index >= kSubscriptionCapacity) ||
      !context->impl->subscriptions[data->slot_index].occupied ||
      (context->impl->subscriptions[data->slot_index].data != data) ||
      (context->impl->subscriptions[data->slot_index].generation !=
       data->slot_generation)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  snapshot = {data->total_matched_publishers, data->matched_publishers,
              data->match_generation};
  return true;
}

}  // namespace detail

bool inspect_subscription(
    const rmw_subscription_t* const subscription,
    SubscriptionSnapshot& snapshot) noexcept {
  SubscriptionData* data = nullptr;
  if (!valid_subscription(subscription, data)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  SubscriptionSnapshot candidate{};
  candidate.names = data->names;
  candidate.actual_qos = data->resources.qos.profile;
  candidate.reader_guid = data->reader_guid;
  candidate.maximum_serialized_sample_size =
      data->resources.maximum_serialized_sample_size;
  candidate.history_size = data->history_size;
  candidate.history_capacity = data->resources.history_sample_slots;
  candidate.observed_writer_count = data->observed_writer_count;
  candidate.next_reception_sequence_number = data->next_reception_sequence;
  candidate.accepted_samples = data->accepted_samples;
  candidate.dropped_samples = data->dropped_samples;
  candidate.ready = data->history_size != 0U;
  candidate.sedp_announcement = data->sedp.data();
  candidate.sedp_announcement_size = data->sedp_size;
  snapshot = candidate;
  return true;
}

}  // namespace rmw_openrtdds_cpp

// Requirements: ORT-RMW-005, ORT-RMW-047, ORT-RMW-048
rmw_subscription_t* rmw_openrtdds_cpp::detail::create_subscription_endpoint(
    const rmw_node_t* node,
    const rosidl_message_type_support_t* type_support,
    const char* topic_name,
    const rmw_qos_profile_t* qos_profile,
    const rmw_subscription_options_t* subscription_options,
    const rmw_openrtdds_cpp::RosChannel channel,
    const bool graph_visible) noexcept {
  using namespace rmw_openrtdds_cpp;
  if ((node == nullptr) || (node->context == nullptr) ||
      (node->data == nullptr) || (type_support == nullptr) ||
      (topic_name == nullptr) || (qos_profile == nullptr) ||
      (subscription_options == nullptr)) {
    RMW_SET_ERROR_MSG("subscription creation requires non-null arguments");
    return nullptr;
  }
  if (!detail::identifier_matches(node->implementation_identifier)) {
    RMW_SET_ERROR_MSG("node belongs to another RMW implementation");
    return nullptr;
  }
  rmw_context_t* const context = node->context;
  if (!detail::context_is_valid(context) || context->impl->shutdown) {
    RMW_SET_ERROR_MSG("subscription creation requires an active context");
    return nullptr;
  }
  if (subscription_options->rmw_specific_subscription_payload != nullptr) {
    RMW_SET_ERROR_MSG(
        "implementation-specific subscription payload is unsupported");
    return nullptr;
  }
  if (subscription_options->content_filter_options != nullptr) {
    RMW_SET_ERROR_MSG("content-filtered subscriptions are unsupported");
    return nullptr;
  }
  switch (subscription_options->require_unique_network_flow_endpoints) {
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
      RMW_SET_ERROR_MSG(
          "subscription topic is not a valid fully qualified ROS name");
      return nullptr;
    }
  } else if (topic_name[0] == '\0') {
    RMW_SET_ERROR_MSG("literal DDS topic name is empty");
    return nullptr;
  }

  TypeSupportInfo type_info{};
  TypeSupportError type_error = TypeSupportError::none;
  const auto* resolved_type_support =
      resolve_type_support(type_support, type_error);
  if ((resolved_type_support == nullptr) ||
      !analyze_type_support(resolved_type_support, kMaximumSampleSize, type_info,
                            type_error)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("subscription type rejected: %s",
                                         to_string(type_error));
    return nullptr;
  }
  DdsNames names{};
  if (!map_ros_to_dds_names(resolved_type_support, channel, topic_name,
                            qos_profile->avoid_ros_namespace_conventions,
                            names, type_error)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "subscription name mapping failed: %s", to_string(type_error));
    return nullptr;
  }
  QosLimits limits{};
  limits.maximum_sample_size = kMaximumSampleSize;
  EndpointResourcePlan resources{};
  QosError qos_error = QosError::none;
  if (!plan_endpoint_resources(*qos_profile, type_info,
                               EndpointRole::subscription, limits, resources,
                               qos_error)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("subscription QoS rejected: %s",
                                         to_string(qos_error));
    return nullptr;
  }

  std::lock_guard<std::mutex> registry_lock(
      context->impl->subscription_registry_mutex);
  std::size_t slot_index = detail::kSubscriptionCapacity;
  for (std::size_t index = 0U; index < detail::kSubscriptionCapacity; ++index) {
    if (!context->impl->subscriptions[index].occupied) {
      slot_index = index;
      context->impl->subscriptions[index].occupied = true;
      break;
    }
  }
  if (slot_index == detail::kSubscriptionCapacity) {
    RMW_SET_ERROR_MSG("OpenRTDDS subscription pool is exhausted");
    return nullptr;
  }
  auto& slot = context->impl->subscriptions[slot_index];
  auto& allocator = context->options.allocator;
  auto* subscription = static_cast<rmw_subscription_t*>(
      allocator.zero_allocate(1U, sizeof(rmw_subscription_t), allocator.state));
  void* data_storage =
      allocator.zero_allocate(1U, sizeof(SubscriptionData), allocator.state);
  auto* data = data_storage == nullptr
                   ? nullptr
                   : new (data_storage) SubscriptionData{};
  char* topic_copy = rcutils_strdup(topic_name, allocator);
  std::size_t serialized_capacity = type_info.maximum_serialized_size;
  if ((channel != RosChannel::topic) &&
      (serialized_capacity <=
       (std::numeric_limits<std::size_t>::max() -
        detail::kServiceCorrelationSize))) {
    serialized_capacity += detail::kServiceCorrelationSize;
  } else if (channel != RosChannel::topic) {
    serialized_capacity = 0U;
  }
  const std::size_t history_storage_bytes =
      (serialized_capacity != 0U) &&
              (resources.history_sample_slots <=
               (std::numeric_limits<std::size_t>::max() /
                serialized_capacity))
          ? resources.history_sample_slots * serialized_capacity
          : 0U;
  auto* history = history_storage_bytes == 0U
                      ? nullptr
                      : static_cast<std::uint8_t*>(allocator.allocate(
                            history_storage_bytes, allocator.state));
  if ((subscription == nullptr) || (data == nullptr) ||
      (topic_copy == nullptr) || (history == nullptr)) {
    if (history != nullptr) {
      allocator.deallocate(history, allocator.state);
    }
    if (topic_copy != nullptr) {
      allocator.deallocate(topic_copy, allocator.state);
    }
    if (data != nullptr) {
      data->~SubscriptionData();
      allocator.deallocate(data, allocator.state);
    }
    if (subscription != nullptr) {
      allocator.deallocate(subscription, allocator.state);
    }
    release_slot(slot);
    RMW_SET_ERROR_MSG("unable to allocate bounded subscription storage");
    return nullptr;
  }

  data->node = node;
  data->handle = subscription;
  data->type_support = resolved_type_support;
  data->slot_index = slot_index;
  data->slot_generation = slot.generation;
  data->type_info = type_info;
  data->names = names;
  data->channel = channel;
  data->graph_visible = graph_visible;
  data->resources = resources;
  data->participant_prefix = participant_prefix(*context);
  data->reader_id = reader_id(slot_index);
  make_guid(data->participant_prefix, data->reader_id, data->reader_guid);
  data->history = history;
  data->history_storage_size = history_storage_bytes;
  data->serialized_capacity = serialized_capacity;
  if (!build_sedp(*data, *context)) {
    allocator.deallocate(history, allocator.state);
    allocator.deallocate(topic_copy, allocator.state);
    data->~SubscriptionData();
    allocator.deallocate(data, allocator.state);
    allocator.deallocate(subscription, allocator.state);
    release_slot(slot);
    RMW_SET_ERROR_MSG("unable to build bounded subscription SEDP announcement");
    return nullptr;
  }

  auto* node_data = static_cast<detail::NodeData*>(node->data);
  if (graph_visible) {
    std::lock_guard<std::mutex> graph_lock(context->impl->graph_mutex);
    const auto graph_error = context->impl->graph.add_local_endpoint(
        data->participant_prefix, data->reader_id, node_data->graph_id,
        openrtdds::rmw::GraphEndpointKind::subscription, topic_name,
        data->names.ros_type_name.data(), data->names.topic_name.data(),
        data->names.type_name.data(),
        !qos_profile->avoid_ros_namespace_conventions);
    if (graph_error != openrtdds::rmw::GraphError::none) {
      allocator.deallocate(history, allocator.state);
      allocator.deallocate(topic_copy, allocator.state);
      data->~SubscriptionData();
      allocator.deallocate(data, allocator.state);
      allocator.deallocate(subscription, allocator.state);
      release_slot(slot);
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
          "unable to commit subscription graph record: %s",
          openrtdds::rmw::to_string(graph_error));
      return nullptr;
    }
  }

  subscription->implementation_identifier =
      openrtdds::rmw::kImplementationIdentifier;
  subscription->data = data;
  subscription->topic_name = topic_copy;
  subscription->options = *subscription_options;
  subscription->can_loan_messages = false;
  subscription->is_cft_enabled = false;
  slot.data = data;
  ++node_data->subscription_count;
  if (graph_visible) {
    detail::notify_graph_change(context);
  }
  return subscription;
}

rmw_ret_t rmw_openrtdds_cpp::detail::take_service_sample(
    const rmw_subscription_t* const subscription,
    rmw_service_info_t* const service_info, void* const ros_message,
    bool* const taken,
    bool (*const accept)(const rmw_request_id_t&, void*),
    void* const accept_context) noexcept {
  return take_service(subscription, service_info, ros_message, taken,
                      accept, accept_context);
}

extern "C" {

rmw_subscription_t* rmw_create_subscription(
    const rmw_node_t* node,
    const rosidl_message_type_support_t* type_support,
    const char* topic_name,
    const rmw_qos_profile_t* qos_profile,
    const rmw_subscription_options_t* subscription_options) {
  return rmw_openrtdds_cpp::detail::create_subscription_endpoint(
      node, type_support, topic_name, qos_profile, subscription_options,
      rmw_openrtdds_cpp::RosChannel::topic, true);
}

rmw_ret_t rmw_destroy_subscription(rmw_node_t* node,
                                   rmw_subscription_t* subscription) {
  using namespace rmw_openrtdds_cpp;
  if ((node == nullptr) || (node->context == nullptr) ||
      (node->data == nullptr) || (subscription == nullptr)) {
    RMW_SET_ERROR_MSG("invalid node or subscription");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!detail::identifier_matches(node->implementation_identifier)) {
    RMW_SET_ERROR_MSG("node belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  SubscriptionData* data = nullptr;
  if (!valid_subscription(subscription, data)) {
    return invalid_subscription_error(subscription);
  }
  if (data->node != node) {
    RMW_SET_ERROR_MSG("subscription does not belong to the supplied node");
    return RMW_RET_INVALID_ARGUMENT;
  }

  rmw_context_t* const context = node->context;
  std::lock_guard<std::mutex> registry_lock(
      context->impl->subscription_registry_mutex);
  auto& slot = context->impl->subscriptions[data->slot_index];
  auto& allocator = context->options.allocator;
  auto* node_data = static_cast<detail::NodeData*>(node->data);
  if (node_data->subscription_count == 0U) {
    RMW_SET_ERROR_MSG("subscription ownership count is inconsistent");
    return RMW_RET_ERROR;
  }
  const bool graph_visible = data->graph_visible;
  bool graph_removed = !graph_visible;
  if (graph_visible) {
    std::lock_guard<std::mutex> graph_lock(context->impl->graph_mutex);
    const auto graph_error =
        context->impl->graph.remove_local_endpoint(data->reader_id);
    graph_removed = graph_error == openrtdds::rmw::GraphError::none;
    if (!graph_removed) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
          "unable to remove subscription graph record: %s",
          openrtdds::rmw::to_string(graph_error));
    }
  }
  --node_data->subscription_count;
  release_slot(slot);
  allocator.deallocate(data->history, allocator.state);
  allocator.deallocate(const_cast<char*>(subscription->topic_name),
                       allocator.state);
  data->~SubscriptionData();
  allocator.deallocate(data, allocator.state);
  allocator.deallocate(subscription, allocator.state);
  if (graph_visible) {
    detail::notify_graph_change(context);
  }
  return graph_removed ? RMW_RET_OK : RMW_RET_ERROR;
}

rmw_ret_t rmw_take(const rmw_subscription_t* subscription,
                   void* ros_message, bool* taken,
                   rmw_subscription_allocation_t* allocation) {
  return rmw_openrtdds_cpp::take_one(subscription, ros_message, taken,
                                     nullptr, allocation);
}

rmw_ret_t rmw_take_with_info(
    const rmw_subscription_t* subscription, void* ros_message, bool* taken,
    rmw_message_info_t* message_info,
    rmw_subscription_allocation_t* allocation) {
  if (message_info == nullptr) {
    RMW_SET_ERROR_MSG("message info output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return rmw_openrtdds_cpp::take_one(
      subscription, ros_message, taken, message_info, allocation);
}

rmw_ret_t rmw_take_sequence(
    const rmw_subscription_t* subscription, size_t count,
    rmw_message_sequence_t* message_sequence,
    rmw_message_info_sequence_t* message_info_sequence, size_t* taken,
    rmw_subscription_allocation_t* allocation) {
  using namespace rmw_openrtdds_cpp;
  if ((message_sequence == nullptr) || (message_info_sequence == nullptr) ||
      (taken == nullptr) ||
      ((count != 0U) && ((message_sequence->data == nullptr) ||
                         (message_info_sequence->data == nullptr))) ||
      (message_sequence->capacity < count) ||
      (message_info_sequence->capacity < count)) {
    RMW_SET_ERROR_MSG("invalid take-sequence arguments or capacity");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (allocation != nullptr) {
    RMW_SET_ERROR_MSG("subscription allocation objects are unsupported");
    return RMW_RET_UNSUPPORTED;
  }
  SubscriptionData* data = nullptr;
  if (!valid_subscription(subscription, data)) {
    return invalid_subscription_error(subscription);
  }
  if (data->node->context->impl->shutdown) {
    RMW_SET_ERROR_MSG("subscription context has been shutdown");
    return RMW_RET_ERROR;
  }

  std::lock_guard<std::mutex> lock(data->mutex);
  const std::size_t available = std::min(count, data->history_size);
  if (available == 0U) {
    message_sequence->size = 0U;
    message_info_sequence->size = 0U;
    *taken = 0U;
    return RMW_RET_OK;
  }
  for (std::size_t offset = 0U; offset < available; ++offset) {
    if (message_sequence->data[offset] == nullptr) {
      RMW_SET_ERROR_MSG("take-sequence message storage is null");
      return RMW_RET_INVALID_ARGUMENT;
    }
    const std::size_t index =
        (data->history_head + offset) % data->resources.history_sample_slots;
    const auto& metadata = data->history_metadata[index];
    CodecResult codec{};
    if (!deserialize_xcdr1(
            data->type_support,
            data->history + (index * data->serialized_capacity),
            metadata.serialized_size,
            data->type_info.maximum_serialized_size,
            message_sequence->data[offset], codec)) {
      RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
          "subscription sequence deserialization failed: %s",
          to_string(codec.error));
      return RMW_RET_ERROR;
    }
    message_info_sequence->data[offset] = message_info(metadata);
  }
  data->history_head =
      (data->history_head + available) % data->resources.history_sample_slots;
  data->history_size -= available;
  message_sequence->size = available;
  message_info_sequence->size = available;
  *taken = available;
  return RMW_RET_OK;
}

rmw_ret_t rmw_subscription_count_matched_publishers(
    const rmw_subscription_t* subscription, size_t* publisher_count) {
  using namespace rmw_openrtdds_cpp;
  if (publisher_count == nullptr) {
    RMW_SET_ERROR_MSG("publisher count is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  SubscriptionData* data = nullptr;
  if (!valid_subscription(subscription, data)) {
    return invalid_subscription_error(subscription);
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  *publisher_count = data->matched_publishers;
  return RMW_RET_OK;
}

rmw_ret_t rmw_subscription_get_actual_qos(
    const rmw_subscription_t* subscription, rmw_qos_profile_t* qos) {
  using namespace rmw_openrtdds_cpp;
  if (qos == nullptr) {
    RMW_SET_ERROR_MSG("QoS output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  SubscriptionData* data = nullptr;
  if (!valid_subscription(subscription, data)) {
    return invalid_subscription_error(subscription);
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  *qos = data->resources.qos.profile;
  return RMW_RET_OK;
}

}  // extern "C"
