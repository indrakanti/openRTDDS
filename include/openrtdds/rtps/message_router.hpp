#pragma once

#include <cstddef>
#include <cstdint>

#include "openrtdds/rtps/types.hpp"
#include "openrtdds/serialization/cdr.hpp"

namespace openrtdds::rtps {

// Requirements: ORT-ROUTE-001, ORT-ROUTE-002, ORT-ROUTE-003,
// Requirements: ORT-ROUTE-004, ORT-ROUTE-005, ORT-ROUTE-006

constexpr std::uint8_t submessage_id_pad = 0x01U;
constexpr std::uint8_t submessage_id_acknack = 0x06U;
constexpr std::uint8_t submessage_id_heartbeat = 0x07U;
constexpr std::uint8_t submessage_id_info_timestamp = 0x09U;
constexpr std::uint8_t submessage_id_info_source = 0x0CU;
constexpr std::uint8_t submessage_id_info_destination = 0x0EU;
constexpr std::uint8_t submessage_id_data = 0x15U;

enum class MessageRouteError : std::uint8_t {
  none = 0,
  invalid_argument,
  truncated,
  invalid_protocol,
  unsupported_version,
  malformed_submessage,
  invalid_info_submessage,
  submessage_not_found,
};

[[nodiscard]] const char* to_string(MessageRouteError error) noexcept;

struct RtpsTimestamp final {
  std::int32_t seconds{0};
  std::uint32_t fraction{0U};
};

struct RoutedSubmessageView final {
  ProtocolVersion version{};
  VendorId vendor_id{};
  GuidPrefix source_guid_prefix{};
  GuidPrefix destination_guid_prefix{};
  bool has_destination{false};
  RtpsTimestamp source_timestamp{};
  bool has_source_timestamp{false};
  std::uint8_t id{0U};
  std::uint8_t flags{0U};
  serialization::ByteOrder byte_order{
      serialization::ByteOrder::little_endian};
  const std::uint8_t* content{nullptr};
  std::size_t content_size{0U};
  std::size_t offset{0U};
};

// Walks and validates a complete RTPS message, returning the selected
// occurrence of a submessage together with the effective INFO_SRC,
// INFO_DST, and INFO_TS context that precedes it. Unknown submessages are
// skipped by their declared bound. The output is committed only on success.
[[nodiscard]] MessageRouteError find_submessage(
    const std::uint8_t* message, std::size_t message_size,
    std::uint8_t submessage_id, std::size_t occurrence,
    RoutedSubmessageView& view) noexcept;

}  // namespace openrtdds::rtps
