#include "openrtdds/rtps/message_router.hpp"

#include <cstring>

namespace openrtdds::rtps {
namespace {

constexpr std::size_t header_size = 20U;
constexpr std::size_t submessage_header_size = 4U;
constexpr std::uint8_t endianness_flag = 0x01U;
constexpr std::uint8_t invalidate_flag = 0x02U;

[[nodiscard]] std::uint16_t read_u16(
    const std::uint8_t* const input,
    const serialization::ByteOrder order) noexcept {
  if (order == serialization::ByteOrder::little_endian) {
    return static_cast<std::uint16_t>(input[0]) |
           (static_cast<std::uint16_t>(input[1]) << 8U);
  }
  return (static_cast<std::uint16_t>(input[0]) << 8U) |
         static_cast<std::uint16_t>(input[1]);
}

[[nodiscard]] std::uint32_t read_u32(
    const std::uint8_t* const input,
    const serialization::ByteOrder order) noexcept {
  std::uint32_t value = 0U;
  for (std::size_t index = 0U; index < 4U; ++index) {
    const std::size_t shift =
        order == serialization::ByteOrder::little_endian ? index : 3U - index;
    value |= static_cast<std::uint32_t>(input[index]) << (shift * 8U);
  }
  return value;
}

[[nodiscard]] bool nonzero_prefix(const GuidPrefix& prefix) noexcept {
  for (const std::uint8_t octet : prefix.value) {
    if (octet != 0U) {
      return true;
    }
  }
  return false;
}

struct RouteContext final {
  ProtocolVersion version{};
  VendorId vendor_id{};
  GuidPrefix source{};
  GuidPrefix destination{};
  bool has_destination{false};
  RtpsTimestamp timestamp{};
  bool has_timestamp{false};
};

[[nodiscard]] MessageRouteError apply_info(
    const std::uint8_t id, const std::uint8_t flags,
    const serialization::ByteOrder order, const std::uint8_t* const content,
    const std::size_t content_size, RouteContext& context) noexcept {
  if (id == submessage_id_info_destination) {
    if (content_size != 12U) {
      return MessageRouteError::invalid_info_submessage;
    }
    std::memcpy(context.destination.value.data(), content, 12U);
    context.has_destination = nonzero_prefix(context.destination);
  } else if (id == submessage_id_info_source) {
    if (content_size != 20U) {
      return MessageRouteError::invalid_info_submessage;
    }
    context.version = {content[4], content[5]};
    if ((context.version.major != 2U) || (context.version.minor == 0U) ||
        (context.version.minor > 5U)) {
      return MessageRouteError::unsupported_version;
    }
    std::memcpy(context.vendor_id.value.data(), &content[6], 2U);
    std::memcpy(context.source.value.data(), &content[8], 12U);
  } else if (id == submessage_id_info_timestamp) {
    if ((flags & invalidate_flag) != 0U) {
      if (content_size != 0U) {
        return MessageRouteError::invalid_info_submessage;
      }
      context.has_timestamp = false;
    } else {
      if (content_size != 8U) {
        return MessageRouteError::invalid_info_submessage;
      }
      const std::uint32_t seconds_bits = read_u32(content, order);
      std::memcpy(&context.timestamp.seconds, &seconds_bits,
                  sizeof(context.timestamp.seconds));
      context.timestamp.fraction = read_u32(&content[4], order);
      context.has_timestamp = true;
    }
  }
  return MessageRouteError::none;
}

}  // namespace

const char* to_string(const MessageRouteError error) noexcept {
  switch (error) {
    case MessageRouteError::none: return "none";
    case MessageRouteError::invalid_argument: return "invalid argument";
    case MessageRouteError::truncated: return "truncated RTPS message";
    case MessageRouteError::invalid_protocol: return "invalid RTPS protocol";
    case MessageRouteError::unsupported_version: return "unsupported RTPS version";
    case MessageRouteError::malformed_submessage: return "malformed RTPS submessage";
    case MessageRouteError::invalid_info_submessage: return "invalid RTPS INFO submessage";
    case MessageRouteError::submessage_not_found: return "RTPS submessage not found";
  }
  return "unknown RTPS routing error";
}

MessageRouteError find_submessage(
    const std::uint8_t* const message, const std::size_t message_size,
    const std::uint8_t wanted_id, const std::size_t wanted_occurrence,
    RoutedSubmessageView& view) noexcept {
  if ((message == nullptr) && (message_size != 0U)) {
    return MessageRouteError::invalid_argument;
  }
  if (message_size < header_size) {
    return MessageRouteError::truncated;
  }
  if ((message[0] != static_cast<std::uint8_t>('R')) ||
      (message[1] != static_cast<std::uint8_t>('T')) ||
      (message[2] != static_cast<std::uint8_t>('P')) ||
      (message[3] != static_cast<std::uint8_t>('S'))) {
    return MessageRouteError::invalid_protocol;
  }
  if ((message[4] != 2U) || (message[5] == 0U) || (message[5] > 5U)) {
    return MessageRouteError::unsupported_version;
  }

  RouteContext context{};
  context.version = {message[4], message[5]};
  std::memcpy(context.vendor_id.value.data(), &message[6], 2U);
  std::memcpy(context.source.value.data(), &message[8], 12U);
  RoutedSubmessageView candidate{};
  bool found = false;
  std::size_t occurrence = 0U;
  std::size_t offset = header_size;

  while (offset < message_size) {
    if ((message_size - offset) < submessage_header_size) {
      return MessageRouteError::malformed_submessage;
    }
    const std::uint8_t id = message[offset];
    const std::uint8_t flags = message[offset + 1U];
    const auto order = (flags & endianness_flag) != 0U
                           ? serialization::ByteOrder::little_endian
                           : serialization::ByteOrder::big_endian;
    const std::size_t declared = read_u16(&message[offset + 2U], order);
    const std::size_t content_offset = offset + submessage_header_size;
    const std::size_t available = message_size - content_offset;
    std::size_t content_size = declared;
    bool extends_to_end = false;
    if (declared == 0U) {
      if ((id == submessage_id_pad) ||
          (id == submessage_id_info_timestamp)) {
        content_size = 0U;
      } else {
        content_size = available;
        extends_to_end = true;
      }
    }
    if (content_size > available) {
      return MessageRouteError::truncated;
    }

    if ((id == wanted_id) && (occurrence == wanted_occurrence)) {
      candidate.version = context.version;
      candidate.vendor_id = context.vendor_id;
      candidate.source_guid_prefix = context.source;
      candidate.destination_guid_prefix = context.destination;
      candidate.has_destination = context.has_destination;
      candidate.source_timestamp = context.timestamp;
      candidate.has_source_timestamp = context.has_timestamp;
      candidate.id = id;
      candidate.flags = flags;
      candidate.byte_order = order;
      candidate.content = &message[content_offset];
      candidate.content_size = content_size;
      candidate.offset = offset;
      found = true;
    }
    if (id == wanted_id) {
      ++occurrence;
    }

    const MessageRouteError info_error = apply_info(
        id, flags, order, &message[content_offset], content_size, context);
    if (info_error != MessageRouteError::none) {
      return info_error;
    }
    offset = content_offset + content_size;
    if (extends_to_end) {
      break;
    }
  }
  if (offset != message_size) {
    return MessageRouteError::malformed_submessage;
  }
  if (!found) {
    return MessageRouteError::submessage_not_found;
  }
  view = candidate;
  return MessageRouteError::none;
}

}  // namespace openrtdds::rtps
