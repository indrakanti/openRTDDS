#include "openrtdds/rtps/data_message.hpp"

#include <cstring>
#include <limits>

namespace openrtdds::rtps {
namespace {

constexpr std::size_t rtps_header_size = 20U;
constexpr std::size_t submessage_header_size = 4U;
constexpr std::size_t data_fixed_content_size = 20U;
constexpr std::size_t data_message_overhead =
    rtps_header_size + submessage_header_size + data_fixed_content_size;
constexpr std::size_t minimum_serialized_payload_size = 4U;
constexpr std::size_t maximum_udp_payload_size = 65'507U;
constexpr std::uint8_t data_submessage_id = 0x15U;
constexpr std::uint8_t endianness_flag = 0x01U;
constexpr std::uint8_t inline_qos_flag = 0x02U;
constexpr std::uint8_t data_flag = 0x04U;
constexpr std::uint8_t key_flag = 0x08U;
constexpr std::uint8_t nonstandard_payload_flag = 0x10U;
constexpr std::uint16_t octets_to_inline_qos = 16U;

[[nodiscard]] bool supported_byte_order(
    const serialization::ByteOrder byte_order) noexcept {
  return (byte_order == serialization::ByteOrder::big_endian) ||
         (byte_order == serialization::ByteOrder::little_endian);
}

[[nodiscard]] bool supported_serialized_payload(
    const std::uint8_t* const payload,
    const std::size_t payload_size) noexcept {
  return (payload != nullptr) &&
         (payload_size >= minimum_serialized_payload_size) &&
         (payload[0] == 0U) && (payload[1] <= 3U);
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

}  // namespace

const char* to_string(const RtpsError error) noexcept {
  switch (error) {
    case RtpsError::none:
      return "none";
    case RtpsError::invalid_argument:
      return "invalid argument";
    case RtpsError::buffer_overflow:
      return "output buffer overflow";
    case RtpsError::message_too_large:
      return "message exceeds UDP payload limit";
    case RtpsError::truncated:
      return "truncated RTPS message";
    case RtpsError::invalid_protocol:
      return "invalid RTPS protocol header";
    case RtpsError::unsupported_version:
      return "unsupported RTPS protocol version";
    case RtpsError::unsupported_submessage:
      return "unsupported RTPS submessage";
    case RtpsError::unsupported_feature:
      return "unsupported RTPS feature";
    case RtpsError::invalid_submessage:
      return "invalid DATA submessage";
    case RtpsError::invalid_sequence_number:
      return "invalid RTPS sequence number";
    case RtpsError::invalid_serialized_payload:
      return "invalid or unsupported serialized payload";
  }
  return "unknown RTPS error";
}

DataMessageBuilder::DataMessageBuilder(std::uint8_t* const buffer,
                                       const std::size_t capacity) noexcept
    : buffer_(buffer), capacity_(capacity) {
  if ((buffer_ == nullptr) && (capacity_ != 0U)) {
    error_ = RtpsError::invalid_argument;
  }
}

bool DataMessageBuilder::build(const DataMessageConfig& config,
                               const std::uint8_t* const serialized_payload,
                               const std::size_t payload_size) noexcept {
  size_ = 0U;
  error_ = RtpsError::none;

  if ((buffer_ == nullptr) && (capacity_ != 0U)) {
    return fail(RtpsError::invalid_argument);
  }
  if ((config.version.major != 2U) || (config.version.minor == 0U) ||
      (config.version.minor > 5U)) {
    return fail(RtpsError::unsupported_version);
  }
  if (!supported_byte_order(config.submessage_byte_order)) {
    return fail(RtpsError::invalid_argument);
  }
  if ((config.sequence_number == 0U) ||
      (config.sequence_number >
       static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))) {
    return fail(RtpsError::invalid_sequence_number);
  }
  if (!supported_serialized_payload(serialized_payload, payload_size)) {
    return fail(RtpsError::invalid_serialized_payload);
  }
  if (payload_size >
      (std::numeric_limits<std::size_t>::max() - data_message_overhead)) {
    return fail(RtpsError::message_too_large);
  }

  const std::size_t message_size = data_message_overhead + payload_size;
  if (message_size > maximum_udp_payload_size) {
    return fail(RtpsError::message_too_large);
  }
  if (message_size > capacity_) {
    return fail(RtpsError::buffer_overflow);
  }

  const std::size_t submessage_content_size =
      data_fixed_content_size + payload_size;
  if (submessage_content_size >
      static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max())) {
    return fail(RtpsError::message_too_large);
  }

  buffer_[0] = static_cast<std::uint8_t>('R');
  buffer_[1] = static_cast<std::uint8_t>('T');
  buffer_[2] = static_cast<std::uint8_t>('P');
  buffer_[3] = static_cast<std::uint8_t>('S');
  buffer_[4] = config.version.major;
  buffer_[5] = config.version.minor;
  std::memcpy(&buffer_[6], config.vendor_id.value.data(),
              config.vendor_id.value.size());
  std::memcpy(&buffer_[8], config.guid_prefix.value.data(),
              config.guid_prefix.value.size());

  buffer_[20] = data_submessage_id;
  buffer_[21] = data_flag;
  if (config.submessage_byte_order ==
      serialization::ByteOrder::little_endian) {
    buffer_[21] |= endianness_flag;
  }
  write_uint16(&buffer_[22],
               static_cast<std::uint16_t>(submessage_content_size),
               config.submessage_byte_order);
  write_uint16(&buffer_[24], 0U, config.submessage_byte_order);
  write_uint16(&buffer_[26], octets_to_inline_qos,
               config.submessage_byte_order);
  std::memcpy(&buffer_[28], config.reader_id.value.data(),
              config.reader_id.value.size());
  std::memcpy(&buffer_[32], config.writer_id.value.data(),
              config.writer_id.value.size());

  const std::uint32_t sequence_high =
      static_cast<std::uint32_t>(config.sequence_number >> 32U);
  const std::uint32_t sequence_low =
      static_cast<std::uint32_t>(config.sequence_number & 0xFFFFFFFFULL);
  write_uint32(&buffer_[36], sequence_high, config.submessage_byte_order);
  write_uint32(&buffer_[40], sequence_low, config.submessage_byte_order);
  std::memcpy(&buffer_[44], serialized_payload, payload_size);
  size_ = message_size;
  return true;
}

bool DataMessageBuilder::fail(const RtpsError error) noexcept {
  if (error_ == RtpsError::none) {
    error_ = error;
  }
  return false;
}

RtpsError parse_data_message(const std::uint8_t* const message,
                             const std::size_t message_size,
                             DataMessageView& view) noexcept {
  if ((message == nullptr) && (message_size != 0U)) {
    return RtpsError::invalid_argument;
  }
  if (message_size < (data_message_overhead +
                      minimum_serialized_payload_size)) {
    return RtpsError::truncated;
  }
  if ((message[0] != static_cast<std::uint8_t>('R')) ||
      (message[1] != static_cast<std::uint8_t>('T')) ||
      (message[2] != static_cast<std::uint8_t>('P')) ||
      (message[3] != static_cast<std::uint8_t>('S'))) {
    return RtpsError::invalid_protocol;
  }
  if ((message[4] != 2U) || (message[5] == 0U)) {
    return RtpsError::unsupported_version;
  }
  if (message[20] != data_submessage_id) {
    return RtpsError::unsupported_submessage;
  }

  const std::uint8_t flags = message[21];
  const auto byte_order = (flags & endianness_flag) != 0U
                              ? serialization::ByteOrder::little_endian
                              : serialization::ByteOrder::big_endian;
  if ((flags & data_flag) == 0U) {
    return RtpsError::invalid_submessage;
  }
  if ((flags & (inline_qos_flag | key_flag | nonstandard_payload_flag)) !=
      0U) {
    return RtpsError::unsupported_feature;
  }

  const std::uint16_t declared_content_size = read_uint16(&message[22],
                                                           byte_order);
  const std::size_t content_available =
      message_size - rtps_header_size - submessage_header_size;
  const std::size_t content_size =
      declared_content_size == 0U
          ? content_available
          : static_cast<std::size_t>(declared_content_size);
  if (content_size > content_available) {
    return RtpsError::truncated;
  }
  if (content_size <
      (data_fixed_content_size + minimum_serialized_payload_size)) {
    return RtpsError::invalid_submessage;
  }

  const std::size_t submessage_end =
      rtps_header_size + submessage_header_size + content_size;
  const std::uint16_t extra_flags = read_uint16(&message[24], byte_order);
  if (extra_flags != 0U) {
    return RtpsError::unsupported_feature;
  }
  const std::uint16_t inline_qos_offset = read_uint16(&message[26],
                                                       byte_order);
  if (inline_qos_offset < octets_to_inline_qos) {
    return RtpsError::invalid_submessage;
  }

  constexpr std::size_t after_inline_qos_offset_field = 28U;
  const std::size_t payload_offset =
      after_inline_qos_offset_field +
      static_cast<std::size_t>(inline_qos_offset);
  if ((payload_offset > submessage_end) ||
      ((submessage_end - payload_offset) < minimum_serialized_payload_size)) {
    return RtpsError::invalid_submessage;
  }

  const std::uint32_t sequence_high_bits = read_uint32(&message[36],
                                                        byte_order);
  std::int32_t sequence_high = 0;
  std::memcpy(&sequence_high, &sequence_high_bits, sizeof(sequence_high));
  const std::uint32_t sequence_low = read_uint32(&message[40], byte_order);
  if (sequence_high < 0) {
    return RtpsError::invalid_sequence_number;
  }
  const std::uint64_t sequence_number =
      (static_cast<std::uint64_t>(static_cast<std::uint32_t>(sequence_high))
       << 32U) |
      static_cast<std::uint64_t>(sequence_low);
  if (sequence_number == 0U) {
    return RtpsError::invalid_sequence_number;
  }

  const std::size_t payload_size = submessage_end - payload_offset;
  const std::uint8_t* const payload = &message[payload_offset];
  if (!supported_serialized_payload(payload, payload_size)) {
    return RtpsError::invalid_serialized_payload;
  }

  DataMessageView candidate{};
  candidate.version = {message[4], message[5]};
  std::memcpy(candidate.vendor_id.value.data(), &message[6],
              candidate.vendor_id.value.size());
  std::memcpy(candidate.guid_prefix.value.data(), &message[8],
              candidate.guid_prefix.value.size());
  std::memcpy(candidate.reader_id.value.data(), &message[28],
              candidate.reader_id.value.size());
  std::memcpy(candidate.writer_id.value.data(), &message[32],
              candidate.writer_id.value.size());
  candidate.sequence_number = sequence_number;
  candidate.submessage_byte_order = byte_order;
  candidate.serialized_payload = payload;
  candidate.payload_size = payload_size;
  view = candidate;
  return RtpsError::none;
}

}  // namespace openrtdds::rtps
