#pragma once

#include <cstddef>
#include <cstdint>

#include "openrtdds/rtps/types.hpp"
#include "openrtdds/serialization/cdr.hpp"

namespace openrtdds::rtps {

// Requirements: ORT-RTPS-001, ORT-RTPS-002, ORT-RTPS-003, ORT-RTPS-004,
// Requirements: ORT-RTPS-005, ORT-RTPS-006, ORT-RTPS-007

enum class RtpsError : std::uint8_t {
  none = 0,
  invalid_argument,
  buffer_overflow,
  message_too_large,
  truncated,
  invalid_protocol,
  unsupported_version,
  unsupported_submessage,
  unsupported_feature,
  invalid_submessage,
  invalid_sequence_number,
  invalid_serialized_payload,
};

[[nodiscard]] const char* to_string(RtpsError error) noexcept;

struct DataMessageConfig final {
  ProtocolVersion version{};
  VendorId vendor_id{};
  GuidPrefix guid_prefix{};
  EntityId reader_id{};
  EntityId writer_id{};
  std::uint64_t sequence_number{1U};
  serialization::ByteOrder submessage_byte_order{
      serialization::ByteOrder::little_endian};
};

// Constructs one RTPS message containing exactly one unfragmented DATA
// submessage and one already-serialized DDS payload. The output is suitable
// for one UDP datagram and no allocation occurs.
class DataMessageBuilder final {
 public:
  DataMessageBuilder(std::uint8_t* buffer, std::size_t capacity) noexcept;

  [[nodiscard]] bool build(const DataMessageConfig& config,
                           const std::uint8_t* serialized_payload,
                           std::size_t payload_size) noexcept;

  [[nodiscard]] const std::uint8_t* data() const noexcept { return buffer_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] RtpsError error() const noexcept { return error_; }
  [[nodiscard]] bool good() const noexcept { return error_ == RtpsError::none; }

 private:
  [[nodiscard]] bool fail(RtpsError error) noexcept;

  std::uint8_t* buffer_{nullptr};
  std::size_t capacity_{0U};
  std::size_t size_{0U};
  RtpsError error_{RtpsError::none};
};

// Non-owning view into a validated RTPS DATA message. The source buffer must
// outlive this view.
struct DataMessageView final {
  ProtocolVersion version{};
  VendorId vendor_id{};
  GuidPrefix guid_prefix{};
  EntityId reader_id{};
  EntityId writer_id{};
  std::uint64_t sequence_number{0U};
  serialization::ByteOrder submessage_byte_order{
      serialization::ByteOrder::little_endian};
  const std::uint8_t* serialized_payload{nullptr};
  std::size_t payload_size{0U};
};

// Parses the first submessage of a bounded RTPS message. PR3 intentionally
// supports only an unfragmented DATA submessage without inline QoS.
[[nodiscard]] RtpsError parse_data_message(const std::uint8_t* message,
                                           std::size_t message_size,
                                           DataMessageView& view) noexcept;

}  // namespace openrtdds::rtps
