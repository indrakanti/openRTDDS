#include "openrtdds/rtps/reliability_messages.hpp"

#include <cstring>
#include <limits>

namespace openrtdds::rtps {
namespace {

constexpr std::size_t rtps_header_size = 20U;
constexpr std::size_t submessage_header_size = 4U;
constexpr std::size_t heartbeat_content_size = 28U;
constexpr std::size_t heartbeat_message_size =
    rtps_header_size + submessage_header_size + heartbeat_content_size;
constexpr std::size_t acknack_fixed_content_size = 24U;

constexpr std::uint8_t acknack_submessage_id = 0x06U;
constexpr std::uint8_t heartbeat_submessage_id = 0x07U;
constexpr std::uint8_t endianness_flag = 0x01U;
constexpr std::uint8_t final_flag = 0x02U;
constexpr std::uint8_t liveliness_flag = 0x04U;
constexpr std::uint8_t group_info_flag = 0x08U;

[[nodiscard]] bool supported_byte_order(
    const serialization::ByteOrder byte_order) noexcept {
  return (byte_order == serialization::ByteOrder::big_endian) ||
         (byte_order == serialization::ByteOrder::little_endian);
}

[[nodiscard]] bool supported_version(const ProtocolVersion version) noexcept {
  return (version.major == 2U) && (version.minor != 0U) &&
         (version.minor <= 5U);
}

[[nodiscard]] bool valid_sequence_number(
    const std::uint64_t sequence_number) noexcept {
  return (sequence_number != 0U) &&
         (sequence_number <= static_cast<std::uint64_t>(
                                 std::numeric_limits<std::int64_t>::max()));
}

void write_uint16(std::uint8_t* const destination, const std::uint16_t value,
                  const serialization::ByteOrder byte_order) noexcept {
  if (byte_order == serialization::ByteOrder::little_endian) {
    destination[0] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  } else {
    destination[0] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[1] = static_cast<std::uint8_t>(value & 0xFFU);
  }
}

void write_uint32(std::uint8_t* const destination, const std::uint32_t value,
                  const serialization::ByteOrder byte_order) noexcept {
  for (std::size_t index = 0U; index < 4U; ++index) {
    const std::size_t shift_index =
        byte_order == serialization::ByteOrder::little_endian
            ? index
            : 3U - index;
    destination[index] =
        static_cast<std::uint8_t>((value >> (shift_index * 8U)) & 0xFFU);
  }
}

void write_int32(std::uint8_t* const destination, const std::int32_t value,
                 const serialization::ByteOrder byte_order) noexcept {
  std::uint32_t bits = 0U;
  static_assert(sizeof(bits) == sizeof(value), "32-bit integer required");
  std::memcpy(&bits, &value, sizeof(bits));
  write_uint32(destination, bits, byte_order);
}

void write_sequence_number(
    std::uint8_t* const destination, const std::uint64_t sequence_number,
    const serialization::ByteOrder byte_order) noexcept {
  write_uint32(destination,
               static_cast<std::uint32_t>(sequence_number >> 32U),
               byte_order);
  write_uint32(destination + 4U,
               static_cast<std::uint32_t>(sequence_number & 0xFFFFFFFFULL),
               byte_order);
}

[[nodiscard]] std::uint16_t read_uint16(
    const std::uint8_t* const source,
    const serialization::ByteOrder byte_order) noexcept {
  if (byte_order == serialization::ByteOrder::little_endian) {
    return static_cast<std::uint16_t>(source[0]) |
           (static_cast<std::uint16_t>(source[1]) << 8U);
  }
  return (static_cast<std::uint16_t>(source[0]) << 8U) |
         static_cast<std::uint16_t>(source[1]);
}

[[nodiscard]] std::uint32_t read_uint32(
    const std::uint8_t* const source,
    const serialization::ByteOrder byte_order) noexcept {
  std::uint32_t value = 0U;
  for (std::size_t index = 0U; index < 4U; ++index) {
    const std::size_t shift_index =
        byte_order == serialization::ByteOrder::little_endian
            ? index
            : 3U - index;
    value |= static_cast<std::uint32_t>(source[index])
             << (shift_index * 8U);
  }
  return value;
}

[[nodiscard]] std::int32_t read_int32(
    const std::uint8_t* const source,
    const serialization::ByteOrder byte_order) noexcept {
  const std::uint32_t bits = read_uint32(source, byte_order);
  std::int32_t value = 0;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

[[nodiscard]] bool read_sequence_number(
    const std::uint8_t* const source,
    const serialization::ByteOrder byte_order, const bool allow_zero,
    std::uint64_t& sequence_number) noexcept {
  const std::uint32_t high_bits = read_uint32(source, byte_order);
  std::int32_t high = 0;
  std::memcpy(&high, &high_bits, sizeof(high));
  if (high < 0) {
    return false;
  }

  const std::uint64_t candidate =
      (static_cast<std::uint64_t>(high_bits) << 32U) |
      static_cast<std::uint64_t>(read_uint32(source + 4U, byte_order));
  if ((!allow_zero && (candidate == 0U)) ||
      (candidate > static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max()))) {
    return false;
  }
  sequence_number = candidate;
  return true;
}

void write_rtps_header(std::uint8_t* const buffer,
                       const ReliabilityMessageHeader& header) noexcept {
  buffer[0] = static_cast<std::uint8_t>('R');
  buffer[1] = static_cast<std::uint8_t>('T');
  buffer[2] = static_cast<std::uint8_t>('P');
  buffer[3] = static_cast<std::uint8_t>('S');
  buffer[4] = header.version.major;
  buffer[5] = header.version.minor;
  std::memcpy(&buffer[6], header.vendor_id.value.data(),
              header.vendor_id.value.size());
  std::memcpy(&buffer[8], header.guid_prefix.value.data(),
              header.guid_prefix.value.size());
}

struct ParsedPrefix final {
  ReliabilityMessageHeader header{};
  serialization::ByteOrder byte_order{
      serialization::ByteOrder::little_endian};
  std::uint8_t flags{0U};
  std::size_t content_size{0U};
};

[[nodiscard]] ReliabilityMessageError parse_prefix(
    const std::uint8_t* const message, const std::size_t message_size,
    const std::uint8_t expected_submessage_id,
    ParsedPrefix& prefix) noexcept {
  if ((message == nullptr) && (message_size != 0U)) {
    return ReliabilityMessageError::invalid_argument;
  }
  if (message_size < (rtps_header_size + submessage_header_size)) {
    return ReliabilityMessageError::truncated;
  }
  if ((message[0] != static_cast<std::uint8_t>('R')) ||
      (message[1] != static_cast<std::uint8_t>('T')) ||
      (message[2] != static_cast<std::uint8_t>('P')) ||
      (message[3] != static_cast<std::uint8_t>('S'))) {
    return ReliabilityMessageError::invalid_protocol;
  }

  const ProtocolVersion version{message[4], message[5]};
  if (!supported_version(version)) {
    return ReliabilityMessageError::unsupported_version;
  }
  if (message[20] != expected_submessage_id) {
    return ReliabilityMessageError::unsupported_submessage;
  }

  ParsedPrefix candidate{};
  candidate.header.version = version;
  std::memcpy(candidate.header.vendor_id.value.data(), &message[6],
              candidate.header.vendor_id.value.size());
  std::memcpy(candidate.header.guid_prefix.value.data(), &message[8],
              candidate.header.guid_prefix.value.size());
  candidate.flags = message[21];
  candidate.byte_order = (candidate.flags & endianness_flag) != 0U
                             ? serialization::ByteOrder::little_endian
                             : serialization::ByteOrder::big_endian;
  candidate.header.submessage_byte_order = candidate.byte_order;

  const std::size_t available =
      message_size - rtps_header_size - submessage_header_size;
  const std::uint16_t declared = read_uint16(&message[22],
                                              candidate.byte_order);
  candidate.content_size =
      declared == 0U ? available : static_cast<std::size_t>(declared);
  if (candidate.content_size > available) {
    return ReliabilityMessageError::truncated;
  }
  if ((declared != 0U) && (candidate.content_size != available)) {
    return ReliabilityMessageError::invalid_submessage;
  }

  prefix = candidate;
  return ReliabilityMessageError::none;
}

}  // namespace

const char* to_string(const ReliabilityMessageError error) noexcept {
  switch (error) {
    case ReliabilityMessageError::none:
      return "none";
    case ReliabilityMessageError::invalid_argument:
      return "invalid argument";
    case ReliabilityMessageError::buffer_overflow:
      return "output buffer overflow";
    case ReliabilityMessageError::truncated:
      return "truncated RTPS reliability message";
    case ReliabilityMessageError::invalid_protocol:
      return "invalid RTPS protocol header";
    case ReliabilityMessageError::unsupported_version:
      return "unsupported RTPS protocol version";
    case ReliabilityMessageError::unsupported_submessage:
      return "unsupported RTPS submessage";
    case ReliabilityMessageError::unsupported_feature:
      return "unsupported RTPS reliability feature";
    case ReliabilityMessageError::invalid_submessage:
      return "invalid RTPS reliability submessage";
    case ReliabilityMessageError::invalid_sequence_number:
      return "invalid RTPS sequence number";
    case ReliabilityMessageError::bitmap_bound_exceeded:
      return "ACKNACK bitmap exceeds 256 bits";
  }
  return "unknown RTPS reliability message error";
}

bool SequenceNumberSet::reset(const std::uint64_t bitmap_base,
                              const std::uint32_t num_bits) noexcept {
  if (!valid_sequence_number(bitmap_base) || (num_bits > maximum_bits)) {
    return false;
  }
  if ((num_bits != 0U) &&
      (bitmap_base >
       static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) -
           static_cast<std::uint64_t>(num_bits - 1U))) {
    return false;
  }

  bitmap_base_ = bitmap_base;
  num_bits_ = num_bits;
  bitmap_.fill(0U);
  return true;
}

bool SequenceNumberSet::set(const std::uint32_t offset) noexcept {
  if (offset >= num_bits_) {
    return false;
  }
  const std::size_t word_index = static_cast<std::size_t>(offset / 32U);
  const std::uint32_t bit_index = offset % 32U;
  bitmap_[word_index] |= (1U << (31U - bit_index));
  return true;
}

bool SequenceNumberSet::test(const std::uint32_t offset) const noexcept {
  if (offset >= num_bits_) {
    return false;
  }
  const std::size_t word_index = static_cast<std::size_t>(offset / 32U);
  const std::uint32_t bit_index = offset % 32U;
  return (bitmap_[word_index] & (1U << (31U - bit_index))) != 0U;
}

ReliabilityMessageBuilder::ReliabilityMessageBuilder(
    std::uint8_t* const buffer, const std::size_t capacity) noexcept
    : buffer_(buffer), capacity_(capacity) {
  if ((buffer_ == nullptr) && (capacity_ != 0U)) {
    error_ = ReliabilityMessageError::invalid_argument;
  }
}

bool ReliabilityMessageBuilder::begin(
    const ReliabilityMessageHeader& header,
    const std::size_t message_size) noexcept {
  if ((buffer_ == nullptr) && (capacity_ != 0U)) {
    return fail(ReliabilityMessageError::invalid_argument);
  }
  if (!supported_version(header.version)) {
    return fail(ReliabilityMessageError::unsupported_version);
  }
  if (!supported_byte_order(header.submessage_byte_order)) {
    return fail(ReliabilityMessageError::invalid_argument);
  }
  if (message_size > capacity_) {
    return fail(ReliabilityMessageError::buffer_overflow);
  }
  write_rtps_header(buffer_, header);
  return true;
}

bool ReliabilityMessageBuilder::build_heartbeat(
    const HeartbeatConfig& config) noexcept {
  size_ = 0U;
  error_ = ReliabilityMessageError::none;

  if (!valid_sequence_number(config.first_sequence_number) ||
      (config.last_sequence_number > static_cast<std::uint64_t>(
                                         std::numeric_limits<std::int64_t>::max())) ||
      (config.last_sequence_number < (config.first_sequence_number - 1U))) {
    return fail(ReliabilityMessageError::invalid_sequence_number);
  }
  if (!begin(config.header, heartbeat_message_size)) {
    return false;
  }

  const auto byte_order = config.header.submessage_byte_order;
  buffer_[20] = heartbeat_submessage_id;
  buffer_[21] = 0U;
  if (byte_order == serialization::ByteOrder::little_endian) {
    buffer_[21] |= endianness_flag;
  }
  if (config.final_flag) {
    buffer_[21] |= final_flag;
  }
  if (config.liveliness_flag) {
    buffer_[21] |= liveliness_flag;
  }
  write_uint16(&buffer_[22], static_cast<std::uint16_t>(heartbeat_content_size),
               byte_order);
  std::memcpy(&buffer_[24], config.reader_id.value.data(),
              config.reader_id.value.size());
  std::memcpy(&buffer_[28], config.writer_id.value.data(),
              config.writer_id.value.size());
  write_sequence_number(&buffer_[32], config.first_sequence_number,
                        byte_order);
  write_sequence_number(&buffer_[40], config.last_sequence_number,
                        byte_order);
  write_int32(&buffer_[48], config.count, byte_order);
  size_ = heartbeat_message_size;
  return true;
}

bool ReliabilityMessageBuilder::build_acknack(
    const AckNackConfig& config) noexcept {
  size_ = 0U;
  error_ = ReliabilityMessageError::none;

  if (!valid_sequence_number(config.reader_state.bitmap_base())) {
    return fail(ReliabilityMessageError::invalid_sequence_number);
  }
  if (config.reader_state.num_bits() > SequenceNumberSet::maximum_bits) {
    return fail(ReliabilityMessageError::bitmap_bound_exceeded);
  }

  const std::size_t content_size =
      acknack_fixed_content_size + (config.reader_state.word_count() * 4U);
  const std::size_t message_size =
      rtps_header_size + submessage_header_size + content_size;
  if (!begin(config.header, message_size)) {
    return false;
  }

  const auto byte_order = config.header.submessage_byte_order;
  buffer_[20] = acknack_submessage_id;
  buffer_[21] = 0U;
  if (byte_order == serialization::ByteOrder::little_endian) {
    buffer_[21] |= endianness_flag;
  }
  if (config.final_flag) {
    buffer_[21] |= final_flag;
  }
  write_uint16(&buffer_[22], static_cast<std::uint16_t>(content_size),
               byte_order);
  std::memcpy(&buffer_[24], config.reader_id.value.data(),
              config.reader_id.value.size());
  std::memcpy(&buffer_[28], config.writer_id.value.data(),
              config.writer_id.value.size());
  write_sequence_number(&buffer_[32], config.reader_state.bitmap_base(),
                        byte_order);
  write_uint32(&buffer_[40], config.reader_state.num_bits(), byte_order);

  std::size_t offset = 44U;
  for (std::size_t index = 0U; index < config.reader_state.word_count();
       ++index) {
    write_uint32(&buffer_[offset], config.reader_state.word(index), byte_order);
    offset += 4U;
  }
  write_int32(&buffer_[offset], config.count, byte_order);
  size_ = message_size;
  return true;
}

bool ReliabilityMessageBuilder::fail(
    const ReliabilityMessageError error) noexcept {
  if (error_ == ReliabilityMessageError::none) {
    error_ = error;
  }
  size_ = 0U;
  return false;
}

ReliabilityMessageError parse_heartbeat_message(
    const std::uint8_t* const message, const std::size_t message_size,
    HeartbeatView& view) noexcept {
  ParsedPrefix prefix{};
  const ReliabilityMessageError prefix_error = parse_prefix(
      message, message_size, heartbeat_submessage_id, prefix);
  if (prefix_error != ReliabilityMessageError::none) {
    return prefix_error;
  }

  if ((prefix.flags & group_info_flag) != 0U) {
    return ReliabilityMessageError::unsupported_feature;
  }
  constexpr std::uint8_t allowed_flags =
      endianness_flag | final_flag | liveliness_flag;
  if ((prefix.flags & static_cast<std::uint8_t>(~allowed_flags)) != 0U) {
    return ReliabilityMessageError::unsupported_feature;
  }
  if (prefix.content_size != heartbeat_content_size) {
    return ReliabilityMessageError::invalid_submessage;
  }

  HeartbeatView candidate{};
  candidate.header = prefix.header;
  std::memcpy(candidate.reader_id.value.data(), &message[24],
              candidate.reader_id.value.size());
  std::memcpy(candidate.writer_id.value.data(), &message[28],
              candidate.writer_id.value.size());
  if (!read_sequence_number(&message[32], prefix.byte_order, false,
                            candidate.first_sequence_number) ||
      !read_sequence_number(&message[40], prefix.byte_order, true,
                            candidate.last_sequence_number) ||
      (candidate.last_sequence_number <
       (candidate.first_sequence_number - 1U))) {
    return ReliabilityMessageError::invalid_sequence_number;
  }
  candidate.count = read_int32(&message[48], prefix.byte_order);
  candidate.final_flag = (prefix.flags & final_flag) != 0U;
  candidate.liveliness_flag = (prefix.flags & liveliness_flag) != 0U;
  view = candidate;
  return ReliabilityMessageError::none;
}

ReliabilityMessageError parse_acknack_message(
    const std::uint8_t* const message, const std::size_t message_size,
    AckNackView& view) noexcept {
  ParsedPrefix prefix{};
  const ReliabilityMessageError prefix_error = parse_prefix(
      message, message_size, acknack_submessage_id, prefix);
  if (prefix_error != ReliabilityMessageError::none) {
    return prefix_error;
  }

  constexpr std::uint8_t allowed_flags = endianness_flag | final_flag;
  if ((prefix.flags & static_cast<std::uint8_t>(~allowed_flags)) != 0U) {
    return ReliabilityMessageError::unsupported_feature;
  }
  if (prefix.content_size < acknack_fixed_content_size) {
    return ReliabilityMessageError::invalid_submessage;
  }

  std::uint64_t bitmap_base = 0U;
  if (!read_sequence_number(&message[32], prefix.byte_order, false,
                            bitmap_base)) {
    return ReliabilityMessageError::invalid_sequence_number;
  }
  const std::uint32_t num_bits = read_uint32(&message[40], prefix.byte_order);
  if (num_bits > SequenceNumberSet::maximum_bits) {
    return ReliabilityMessageError::bitmap_bound_exceeded;
  }
  const std::size_t word_count = static_cast<std::size_t>((num_bits + 31U) / 32U);
  const std::size_t expected_content_size =
      acknack_fixed_content_size + (word_count * 4U);
  if (prefix.content_size != expected_content_size) {
    return ReliabilityMessageError::invalid_submessage;
  }

  AckNackView candidate{};
  candidate.header = prefix.header;
  std::memcpy(candidate.reader_id.value.data(), &message[24],
              candidate.reader_id.value.size());
  std::memcpy(candidate.writer_id.value.data(), &message[28],
              candidate.writer_id.value.size());
  if (!candidate.reader_state.reset(bitmap_base, num_bits)) {
    return ReliabilityMessageError::invalid_sequence_number;
  }

  std::size_t offset = 44U;
  for (std::size_t word_index = 0U; word_index < word_count; ++word_index) {
    const std::uint32_t word = read_uint32(&message[offset], prefix.byte_order);
    for (std::uint32_t bit_index = 0U; bit_index < 32U; ++bit_index) {
      const std::uint32_t bit_offset =
          static_cast<std::uint32_t>(word_index * 32U) + bit_index;
      if ((bit_offset < num_bits) &&
          ((word & (1U << (31U - bit_index))) != 0U)) {
        static_cast<void>(candidate.reader_state.set(bit_offset));
      }
    }
    offset += 4U;
  }
  candidate.count = read_int32(&message[offset], prefix.byte_order);
  candidate.final_flag = (prefix.flags & final_flag) != 0U;
  view = candidate;
  return ReliabilityMessageError::none;
}

}  // namespace openrtdds::rtps
