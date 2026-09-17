#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "openrtdds/rtps/data_message.hpp"

namespace openrtdds::rtps {

// Requirements: ORT-SPDP-001, ORT-SPDP-002, ORT-SPDP-003,
// Requirements: ORT-SPDP-004, ORT-SPDP-005, ORT-SPDP-006

constexpr std::size_t spdp_max_locators = 4U;
constexpr std::size_t spdp_max_entity_name = 63U;
constexpr std::uint32_t spdp_endpoint_participant_announcer = 1U << 0U;
constexpr std::uint32_t spdp_endpoint_participant_detector = 1U << 1U;

enum class SpdpError : std::uint8_t {
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
  domain_mismatch,
  self_announcement,
  table_full,
  stale_announcement,
  time_regression,
  expiration_overflow,
  action_capacity_exceeded,
};

[[nodiscard]] const char* to_string(SpdpError error) noexcept;

struct SpdpResult final {
  SpdpError error{SpdpError::none};
  RtpsError rtps_error{RtpsError::none};
  std::size_t bytes{0U};

  [[nodiscard]] bool ok() const noexcept { return error == SpdpError::none; }
};

struct Locator final {
  std::int32_t kind{1};
  std::uint32_t port{0U};
  std::array<std::uint8_t, 16U> address{};
};

[[nodiscard]] bool make_udp_v4_locator(
    const std::array<std::uint8_t, 4U>& address, std::uint16_t port,
    Locator& locator) noexcept;
[[nodiscard]] bool valid_udp_v4_locator(const Locator& locator) noexcept;

struct SpdpPortConfig final {
  std::uint32_t port_base{7400U};
  std::uint32_t domain_gain{250U};
  std::uint32_t participant_gain{2U};
  std::uint32_t multicast_offset{0U};
  std::uint32_t unicast_offset{10U};
};

[[nodiscard]] bool spdp_multicast_port(std::uint32_t domain_id,
                                       const SpdpPortConfig& config,
                                       std::uint16_t& port) noexcept;
[[nodiscard]] bool spdp_unicast_port(std::uint32_t domain_id,
                                     std::uint32_t participant_id,
                                     const SpdpPortConfig& config,
                                     std::uint16_t& port) noexcept;

template <std::size_t Capacity>
struct BoundedLocatorList final {
  std::array<Locator, Capacity> values{};
  std::size_t size{0U};

  [[nodiscard]] bool push_back(const Locator& locator) noexcept {
    if (size >= Capacity) {
      return false;
    }
    values[size] = locator;
    ++size;
    return true;
  }

  [[nodiscard]] const Locator& operator[](const std::size_t index) const
      noexcept {
    return values[index];
  }
};

struct SpdpParticipantData final {
  ProtocolVersion protocol_version{};
  VendorId vendor_id{};
  GuidPrefix guid_prefix{};
  std::uint32_t domain_id{0U};
  bool expects_inline_qos{false};
  BoundedLocatorList<spdp_max_locators> metatraffic_unicast{};
  BoundedLocatorList<spdp_max_locators> metatraffic_multicast{};
  BoundedLocatorList<spdp_max_locators> default_unicast{};
  BoundedLocatorList<spdp_max_locators> default_multicast{};
  std::uint32_t available_builtin_endpoints{
      spdp_endpoint_participant_announcer |
      spdp_endpoint_participant_detector};
  std::uint64_t lease_duration_ns{100'000'000'000ULL};
  std::array<char, spdp_max_entity_name + 1U> entity_name{};
  std::size_t entity_name_size{0U};
};

struct SpdpAnnouncementConfig final {
  SpdpParticipantData participant{};
  std::uint64_t sequence_number{1U};
  serialization::ByteOrder submessage_byte_order{
      serialization::ByteOrder::little_endian};
  serialization::ByteOrder parameter_byte_order{
      serialization::ByteOrder::little_endian};
};

class SpdpMessageBuilder final {
 public:
  SpdpMessageBuilder(std::uint8_t* buffer, std::size_t capacity) noexcept;

  [[nodiscard]] bool build(const SpdpAnnouncementConfig& config) noexcept;
  [[nodiscard]] const std::uint8_t* data() const noexcept { return buffer_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] SpdpError error() const noexcept { return error_; }
  [[nodiscard]] RtpsError rtps_error() const noexcept { return rtps_error_; }

 private:
  std::uint8_t* buffer_{nullptr};
  std::size_t capacity_{0U};
  std::size_t size_{0U};
  SpdpError error_{SpdpError::none};
  RtpsError rtps_error_{RtpsError::none};
};

struct SpdpMessageView final {
  SpdpParticipantData participant{};
  std::uint64_t sequence_number{0U};
  serialization::ByteOrder submessage_byte_order{
      serialization::ByteOrder::little_endian};
  serialization::ByteOrder parameter_byte_order{
      serialization::ByteOrder::little_endian};
};

[[nodiscard]] SpdpResult parse_spdp_message(
    const std::uint8_t* message, std::size_t message_size,
    std::uint32_t expected_domain_id, SpdpMessageView& view) noexcept;

enum class ParticipantUpdate : std::uint8_t {
  none = 0,
  added,
  refreshed,
};

struct ParticipantTableResult final {
  SpdpError error{SpdpError::none};
  ParticipantUpdate update{ParticipantUpdate::none};
  std::size_t index{0U};

  [[nodiscard]] bool ok() const noexcept { return error == SpdpError::none; }
};

struct DiscoveredParticipant final {
  SpdpParticipantData data{};
  std::uint64_t sequence_number{0U};
  std::uint64_t last_seen_ns{0U};
  std::uint64_t expires_at_ns{0U};
  bool occupied{false};
};

template <std::size_t Capacity>
class DiscoveredParticipantTable final {
  static_assert(Capacity > 0U, "participant table must be nonzero");

 public:
  explicit DiscoveredParticipantTable(const GuidPrefix& local_guid_prefix)
      noexcept
      : local_guid_prefix_(local_guid_prefix) {}

  [[nodiscard]] ParticipantTableResult upsert(
      const SpdpMessageView& announcement,
      const std::uint64_t now_ns) noexcept {
    if (now_ns < last_time_ns_) {
      return {SpdpError::time_regression, ParticipantUpdate::none, 0U};
    }
    if (announcement.participant.guid_prefix.value ==
        local_guid_prefix_.value) {
      return {SpdpError::self_announcement, ParticipantUpdate::none, 0U};
    }
    if (announcement.participant.lease_duration_ns == 0U) {
      return {SpdpError::invalid_duration, ParticipantUpdate::none, 0U};
    }
    if (announcement.participant.lease_duration_ns >
        (std::numeric_limits<std::uint64_t>::max() - now_ns)) {
      return {SpdpError::expiration_overflow, ParticipantUpdate::none, 0U};
    }

    for (std::size_t index = 0U; index < Capacity; ++index) {
      auto& entry = entries_[index];
      if (!entry.occupied ||
          (entry.data.guid_prefix.value !=
           announcement.participant.guid_prefix.value)) {
        continue;
      }
      if (announcement.sequence_number < entry.sequence_number) {
        return {SpdpError::stale_announcement, ParticipantUpdate::none,
                index};
      }
      if (announcement.sequence_number > entry.sequence_number) {
        entry.data = announcement.participant;
        entry.sequence_number = announcement.sequence_number;
      }
      entry.last_seen_ns = now_ns;
      entry.expires_at_ns =
          now_ns + announcement.participant.lease_duration_ns;
      last_time_ns_ = now_ns;
      return {SpdpError::none, ParticipantUpdate::refreshed, index};
    }

    for (std::size_t index = 0U; index < Capacity; ++index) {
      auto& entry = entries_[index];
      if (entry.occupied) {
        continue;
      }
      entry.data = announcement.participant;
      entry.sequence_number = announcement.sequence_number;
      entry.last_seen_ns = now_ns;
      entry.expires_at_ns =
          now_ns + announcement.participant.lease_duration_ns;
      entry.occupied = true;
      ++size_;
      last_time_ns_ = now_ns;
      return {SpdpError::none, ParticipantUpdate::added, index};
    }
    return {SpdpError::table_full, ParticipantUpdate::none, 0U};
  }

  [[nodiscard]] SpdpError expire(
      const std::uint64_t now_ns, GuidPrefix* const expired,
      const std::size_t expired_capacity,
      std::size_t& expired_size) noexcept {
    expired_size = 0U;
    if (now_ns < last_time_ns_) {
      return SpdpError::time_regression;
    }
    std::size_t due = 0U;
    for (const auto& entry : entries_) {
      if (entry.occupied && (now_ns >= entry.expires_at_ns)) {
        ++due;
      }
    }
    if ((due != 0U) && (expired == nullptr)) {
      return SpdpError::invalid_argument;
    }
    if (due > expired_capacity) {
      return SpdpError::action_capacity_exceeded;
    }
    for (auto& entry : entries_) {
      if (!entry.occupied || (now_ns < entry.expires_at_ns)) {
        continue;
      }
      expired[expired_size] = entry.data.guid_prefix;
      ++expired_size;
      entry = {};
      --size_;
    }
    last_time_ns_ = now_ns;
    return SpdpError::none;
  }

  [[nodiscard]] const DiscoveredParticipant* find(
      const GuidPrefix& guid_prefix) const noexcept {
    for (const auto& entry : entries_) {
      if (entry.occupied &&
          (entry.data.guid_prefix.value == guid_prefix.value)) {
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
  GuidPrefix local_guid_prefix_{};
  std::array<DiscoveredParticipant, Capacity> entries_{};
  std::size_t size_{0U};
  std::uint64_t last_time_ns_{0U};
};

}  // namespace openrtdds::rtps
