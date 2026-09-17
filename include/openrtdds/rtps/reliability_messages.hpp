#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "openrtdds/rtps/types.hpp"
#include "openrtdds/serialization/cdr.hpp"

namespace openrtdds::rtps {

// Requirements: ORT-REL-002, ORT-REL-003

enum class ReliabilityMessageError : std::uint8_t {
  none = 0,
  invalid_argument,
  buffer_overflow,
  truncated,
  invalid_protocol,
  unsupported_version,
  unsupported_submessage,
  unsupported_feature,
  invalid_submessage,
  invalid_sequence_number,
  bitmap_bound_exceeded,
};

[[nodiscard]] const char* to_string(ReliabilityMessageError error) noexcept;

class SequenceNumberSet final {
 public:
  static constexpr std::uint32_t maximum_bits = 256U;
  static constexpr std::size_t maximum_words = 8U;

  [[nodiscard]] bool reset(std::uint64_t bitmap_base,
                           std::uint32_t num_bits) noexcept;
  [[nodiscard]] bool set(std::uint32_t offset) noexcept;
  [[nodiscard]] bool test(std::uint32_t offset) const noexcept;

  [[nodiscard]] std::uint64_t bitmap_base() const noexcept {
    return bitmap_base_;
  }
  [[nodiscard]] std::uint32_t num_bits() const noexcept { return num_bits_; }
  [[nodiscard]] std::size_t word_count() const noexcept {
    return static_cast<std::size_t>((num_bits_ + 31U) / 32U);
  }
  [[nodiscard]] std::uint32_t word(std::size_t index) const noexcept {
    return index < word_count() ? bitmap_[index] : 0U;
  }

 private:
  std::uint64_t bitmap_base_{1U};
  std::uint32_t num_bits_{0U};
  std::array<std::uint32_t, maximum_words> bitmap_{};
};

struct ReliabilityMessageHeader final {
  ProtocolVersion version{};
  VendorId vendor_id{};
  GuidPrefix guid_prefix{};
  serialization::ByteOrder submessage_byte_order{
      serialization::ByteOrder::little_endian};
};

struct HeartbeatConfig final {
  ReliabilityMessageHeader header{};
  EntityId reader_id{};
  EntityId writer_id{};
  std::uint64_t first_sequence_number{1U};
  std::uint64_t last_sequence_number{0U};
  std::int32_t count{1};
  bool final_flag{false};
  bool liveliness_flag{false};
};

struct AckNackConfig final {
  ReliabilityMessageHeader header{};
  EntityId reader_id{};
  EntityId writer_id{};
  SequenceNumberSet reader_state{};
  std::int32_t count{1};
  bool final_flag{false};
};

class ReliabilityMessageBuilder final {
 public:
  ReliabilityMessageBuilder(std::uint8_t* buffer,
                            std::size_t capacity) noexcept;

  [[nodiscard]] bool build_heartbeat(const HeartbeatConfig& config) noexcept;
  [[nodiscard]] bool build_acknack(const AckNackConfig& config) noexcept;

  [[nodiscard]] const std::uint8_t* data() const noexcept { return buffer_; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] ReliabilityMessageError error() const noexcept {
    return error_;
  }
  [[nodiscard]] bool good() const noexcept {
    return error_ == ReliabilityMessageError::none;
  }

 private:
  [[nodiscard]] bool begin(const ReliabilityMessageHeader& header,
                           std::size_t message_size) noexcept;
  [[nodiscard]] bool fail(ReliabilityMessageError error) noexcept;

  std::uint8_t* buffer_{nullptr};
  std::size_t capacity_{0U};
  std::size_t size_{0U};
  ReliabilityMessageError error_{ReliabilityMessageError::none};
};

struct HeartbeatView final {
  ReliabilityMessageHeader header{};
  EntityId reader_id{};
  EntityId writer_id{};
  std::uint64_t first_sequence_number{0U};
  std::uint64_t last_sequence_number{0U};
  std::int32_t count{0};
  bool final_flag{false};
  bool liveliness_flag{false};
};

struct AckNackView final {
  ReliabilityMessageHeader header{};
  EntityId reader_id{};
  EntityId writer_id{};
  SequenceNumberSet reader_state{};
  std::int32_t count{0};
  bool final_flag{false};
};

[[nodiscard]] ReliabilityMessageError parse_heartbeat_message(
    const std::uint8_t* message, std::size_t message_size,
    HeartbeatView& view) noexcept;

[[nodiscard]] ReliabilityMessageError parse_acknack_message(
    const std::uint8_t* message, std::size_t message_size,
    AckNackView& view) noexcept;

}  // namespace openrtdds::rtps
