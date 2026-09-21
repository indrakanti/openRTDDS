#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "openrtdds/rtps/spdp.hpp"

namespace openrtdds::rtps {

// Requirements: ORT-SEDP-001, ORT-SEDP-002, ORT-SEDP-003,
// Requirements: ORT-SEDP-004, ORT-SEDP-005, ORT-SEDP-006

constexpr std::size_t sedp_max_topic_name = 255U;
constexpr std::size_t sedp_max_type_name = 255U;
constexpr std::size_t sedp_max_locators = 4U;

enum class EndpointKind : std::uint8_t {
  writer = 0,
  reader,
};

enum class ReliabilityKind : std::int32_t {
  best_effort = 1,
  reliable = 2,
};

enum class DurabilityKind : std::int32_t {
  volatile_durability = 0,
  transient_local = 1,
};

enum class SedpError : std::uint8_t {
  none = 0,
  invalid_argument,
  invalid_configuration,
  buffer_overflow,
  rtps_error,
  unsupported_representation,
  malformed_parameter,
  unknown_required_parameter,
  missing_required_parameter,
  duplicate_parameter,
  locator_bound_exceeded,
  invalid_locator,
  invalid_duration,
  invalid_identity,
  participant_mismatch,
  table_full,
  stale_announcement,
  endpoint_not_found,
  action_capacity_exceeded,
};

[[nodiscard]] const char* to_string(SedpError error) noexcept;

struct SedpResult final {
  SedpError error{SedpError::none};
  RtpsError rtps_error{RtpsError::none};
  std::size_t bytes{0U};

  [[nodiscard]] bool ok() const noexcept { return error == SedpError::none; }
};

struct SedpEndpointData final {
  ProtocolVersion protocol_version{};
  VendorId vendor_id{};
  GuidPrefix participant_guid_prefix{};
  EntityId endpoint_id{};
  EndpointKind kind{EndpointKind::writer};
  std::array<char, sedp_max_topic_name + 1U> topic_name{};
  std::size_t topic_name_size{0U};
  std::array<char, sedp_max_type_name + 1U> type_name{};
  std::size_t type_name_size{0U};
  BoundedLocatorList<sedp_max_locators> unicast_locators{};
  BoundedLocatorList<sedp_max_locators> multicast_locators{};
  ReliabilityKind reliability{ReliabilityKind::best_effort};
  DurabilityKind durability{DurabilityKind::volatile_durability};
  std::uint64_t max_blocking_time_ns{100'000'000ULL};
  bool expects_inline_qos{false};
};

struct SedpAnnouncementConfig final {
  SedpEndpointData endpoint{};
  std::uint64_t sequence_number{1U};
  serialization::ByteOrder submessage_byte_order{
      serialization::ByteOrder::little_endian};
  serialization::ByteOrder parameter_byte_order{
      serialization::ByteOrder::little_endian};
};

class SedpMessageBuilder final {
 public:
  SedpMessageBuilder(std::uint8_t* buffer, std::size_t capacity) noexcept;

  [[nodiscard]] bool build(const SedpAnnouncementConfig& config) noexcept;
  [[nodiscard]] const std::uint8_t* data() const noexcept { return buffer_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] SedpError error() const noexcept { return error_; }
  [[nodiscard]] RtpsError rtps_error() const noexcept { return rtps_error_; }

 private:
  std::uint8_t* buffer_{nullptr};
  std::size_t capacity_{0U};
  std::size_t size_{0U};
  SedpError error_{SedpError::none};
  RtpsError rtps_error_{RtpsError::none};
};

struct SedpMessageView final {
  SedpEndpointData endpoint{};
  std::uint64_t sequence_number{0U};
  serialization::ByteOrder submessage_byte_order{
      serialization::ByteOrder::little_endian};
  serialization::ByteOrder parameter_byte_order{
      serialization::ByteOrder::little_endian};
};

[[nodiscard]] SedpResult parse_sedp_message(
    const std::uint8_t* message, std::size_t message_size,
    const GuidPrefix& expected_participant, SedpMessageView& view) noexcept;

enum class EndpointUpdate : std::uint8_t {
  none = 0,
  added,
  updated,
};

struct EndpointTableResult final {
  SedpError error{SedpError::none};
  EndpointUpdate update{EndpointUpdate::none};
  std::size_t index{0U};

  [[nodiscard]] bool ok() const noexcept { return error == SedpError::none; }
};

struct DiscoveredEndpoint final {
  SedpEndpointData data{};
  std::uint64_t sequence_number{0U};
  bool occupied{false};
};

template <std::size_t Capacity>
class DiscoveredEndpointTable final {
  static_assert(Capacity > 0U, "endpoint table must be nonzero");

 public:
  [[nodiscard]] EndpointTableResult upsert(
      const SedpMessageView& announcement) noexcept {
    for (std::size_t index = 0U; index < Capacity; ++index) {
      auto& entry = entries_[index];
      if (!entry.occupied ||
          (entry.data.participant_guid_prefix.value !=
           announcement.endpoint.participant_guid_prefix.value) ||
          (entry.data.endpoint_id != announcement.endpoint.endpoint_id)) {
        continue;
      }
      if (announcement.sequence_number <= entry.sequence_number) {
        return {SedpError::stale_announcement, EndpointUpdate::none, index};
      }
      entry.data = announcement.endpoint;
      entry.sequence_number = announcement.sequence_number;
      return {SedpError::none, EndpointUpdate::updated, index};
    }

    for (std::size_t index = 0U; index < Capacity; ++index) {
      auto& entry = entries_[index];
      if (entry.occupied) {
        continue;
      }
      entry.data = announcement.endpoint;
      entry.sequence_number = announcement.sequence_number;
      entry.occupied = true;
      ++size_;
      return {SedpError::none, EndpointUpdate::added, index};
    }
    return {SedpError::table_full, EndpointUpdate::none, 0U};
  }

  [[nodiscard]] SedpError remove_participant(
      const GuidPrefix& participant, EntityId* const removed,
      const std::size_t removed_capacity,
      std::size_t& removed_size) noexcept {
    removed_size = 0U;
    std::size_t due = 0U;
    for (const auto& entry : entries_) {
      if (entry.occupied &&
          (entry.data.participant_guid_prefix.value == participant.value)) {
        ++due;
      }
    }
    if ((due != 0U) && (removed == nullptr)) {
      return SedpError::invalid_argument;
    }
    if (due > removed_capacity) {
      return SedpError::action_capacity_exceeded;
    }
    for (auto& entry : entries_) {
      if (!entry.occupied ||
          (entry.data.participant_guid_prefix.value != participant.value)) {
        continue;
      }
      removed[removed_size] = entry.data.endpoint_id;
      ++removed_size;
      entry = {};
      --size_;
    }
    return SedpError::none;
  }

  [[nodiscard]] const DiscoveredEndpoint* find(
      const GuidPrefix& participant, const EntityId& endpoint) const noexcept {
    for (const auto& entry : entries_) {
      if (entry.occupied &&
          (entry.data.participant_guid_prefix.value == participant.value) &&
          (entry.data.endpoint_id == endpoint)) {
        return &entry;
      }
    }
    return nullptr;
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr std::size_t capacity() const noexcept {
    return Capacity;
  }

 private:
  std::array<DiscoveredEndpoint, Capacity> entries_{};
  std::size_t size_{0U};
};

struct LocalEndpointDescriptor final {
  EndpointKind kind{EndpointKind::writer};
  const char* topic_name{nullptr};
  std::size_t topic_name_size{0U};
  const char* type_name{nullptr};
  std::size_t type_name_size{0U};
  ReliabilityKind reliability{ReliabilityKind::best_effort};
  DurabilityKind durability{DurabilityKind::volatile_durability};
};

enum class MatchStatus : std::uint8_t {
  matched = 0,
  invalid_local_endpoint,
  same_endpoint_kind,
  topic_mismatch,
  type_mismatch,
  reliability_incompatible,
  durability_incompatible,
};

[[nodiscard]] const char* to_string(MatchStatus status) noexcept;
[[nodiscard]] MatchStatus evaluate_endpoint_match(
    const LocalEndpointDescriptor& local,
    const SedpEndpointData& remote) noexcept;

}  // namespace openrtdds::rtps
