#include "openrtdds/rtps/sedp.hpp"

#include <cstring>
#include <limits>

namespace openrtdds::rtps {
namespace {

constexpr std::uint16_t pid_sentinel = 0x0001U;
constexpr std::uint16_t pid_topic_name = 0x0005U;
constexpr std::uint16_t pid_type_name = 0x0007U;
constexpr std::uint16_t pid_reliability = 0x001AU;
constexpr std::uint16_t pid_durability = 0x001DU;
constexpr std::uint16_t pid_unicast_locator = 0x002FU;
constexpr std::uint16_t pid_multicast_locator = 0x0030U;
constexpr std::uint16_t pid_expects_inline_qos = 0x0043U;
constexpr std::uint16_t pid_participant_guid = 0x0050U;
constexpr std::uint16_t pid_endpoint_guid = 0x005AU;
constexpr std::uint16_t must_understand_mask = 0x4000U;
constexpr std::size_t payload_capacity = 1280U;
constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;

constexpr EntityId entity_id_unknown{{0U, 0U, 0U, 0U}};
constexpr EntityId entity_id_participant{{0U, 0U, 1U, 0xC1U}};
constexpr EntityId entity_id_publications_writer{{0U, 0U, 3U, 0xC2U}};
constexpr EntityId entity_id_publications_reader{{0U, 0U, 3U, 0xC7U}};
constexpr EntityId entity_id_subscriptions_writer{{0U, 0U, 4U, 0xC2U}};
constexpr EntityId entity_id_subscriptions_reader{{0U, 0U, 4U, 0xC7U}};

[[nodiscard]] bool supported_byte_order(
    const serialization::ByteOrder order) noexcept {
  return (order == serialization::ByteOrder::little_endian) ||
         (order == serialization::ByteOrder::big_endian);
}

void write_u16(std::uint8_t* const output, const std::uint16_t value,
               const serialization::ByteOrder order) noexcept {
  if (order == serialization::ByteOrder::little_endian) {
    output[0] = static_cast<std::uint8_t>(value & 0xFFU);
    output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  } else {
    output[0] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    output[1] = static_cast<std::uint8_t>(value & 0xFFU);
  }
}

void write_u32(std::uint8_t* const output, const std::uint32_t value,
               const serialization::ByteOrder order) noexcept {
  for (std::size_t index = 0U; index < 4U; ++index) {
    const std::size_t shift =
        order == serialization::ByteOrder::little_endian ? index : 3U - index;
    output[index] =
        static_cast<std::uint8_t>((value >> (shift * 8U)) & 0xFFU);
  }
}

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

[[nodiscard]] std::size_t padded_size(const std::size_t size) noexcept {
  return (size + 3U) & ~std::size_t{3U};
}

[[nodiscard]] bool nonzero_guid(const GuidPrefix& prefix) noexcept {
  for (const std::uint8_t octet : prefix.value) {
    if (octet != 0U) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool valid_endpoint_id(const EntityId& id,
                                     const EndpointKind kind) noexcept {
  const std::uint8_t entity_kind = id.value[3];
  if ((id.value[0] | id.value[1] | id.value[2]) == 0U) {
    return false;
  }
  if (kind == EndpointKind::writer) {
    return (entity_kind == 0x02U) || (entity_kind == 0x03U);
  }
  return (entity_kind == 0x04U) || (entity_kind == 0x07U);
}

[[nodiscard]] bool valid_reliability(const ReliabilityKind kind) noexcept {
  return (kind == ReliabilityKind::best_effort) ||
         (kind == ReliabilityKind::reliable);
}

[[nodiscard]] bool valid_durability(const DurabilityKind kind) noexcept {
  return (kind == DurabilityKind::volatile_durability) ||
         (kind == DurabilityKind::transient_local);
}

template <std::size_t Capacity>
[[nodiscard]] bool valid_text(const std::array<char, Capacity>& text,
                              const std::size_t size) noexcept {
  if ((size == 0U) || (size >= Capacity)) {
    return false;
  }
  for (std::size_t index = 0U; index < size; ++index) {
    if (text[index] == '\0') {
      return false;
    }
  }
  return true;
}

template <std::size_t Capacity>
[[nodiscard]] bool valid_locators(
    const BoundedLocatorList<Capacity>& locators) noexcept {
  if (locators.size > Capacity) {
    return false;
  }
  for (std::size_t index = 0U; index < locators.size; ++index) {
    if (!valid_udp_v4_locator(locators[index])) {
      return false;
    }
  }
  return true;
}

class ParameterWriter final {
 public:
  ParameterWriter(std::uint8_t* const buffer, const std::size_t capacity,
                  const serialization::ByteOrder order) noexcept
      : buffer_(buffer), capacity_(capacity), order_(order) {
    if (capacity_ >= 4U) {
      buffer_[0] = 0U;
      buffer_[1] = order_ == serialization::ByteOrder::little_endian ? 3U : 2U;
      buffer_[2] = 0U;
      buffer_[3] = 0U;
      size_ = 4U;
    }
  }

  [[nodiscard]] std::uint8_t* append(const std::uint16_t id,
                                     const std::size_t value_size) noexcept {
    const std::size_t length = padded_size(value_size);
    if ((length > static_cast<std::size_t>(
                      std::numeric_limits<std::uint16_t>::max())) ||
        (size_ > capacity_) || ((capacity_ - size_) < (4U + length))) {
      return nullptr;
    }
    write_u16(&buffer_[size_], id, order_);
    write_u16(&buffer_[size_ + 2U], static_cast<std::uint16_t>(length), order_);
    std::uint8_t* const value = &buffer_[size_ + 4U];
    std::memset(value, 0, length);
    size_ += 4U + length;
    return value;
  }

  [[nodiscard]] bool finish() noexcept {
    return append(pid_sentinel, 0U) != nullptr;
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }

 private:
  std::uint8_t* buffer_{nullptr};
  std::size_t capacity_{0U};
  serialization::ByteOrder order_{serialization::ByteOrder::little_endian};
  std::size_t size_{0U};
};

[[nodiscard]] bool append_guid(ParameterWriter& writer, const std::uint16_t id,
                               const GuidPrefix& prefix,
                               const EntityId& entity) noexcept {
  std::uint8_t* const value = writer.append(id, 16U);
  if (value == nullptr) {
    return false;
  }
  std::memcpy(value, prefix.value.data(), prefix.value.size());
  std::memcpy(&value[12], entity.value.data(), entity.value.size());
  return true;
}

template <std::size_t Capacity>
[[nodiscard]] bool append_string(ParameterWriter& writer,
                                 const std::uint16_t id,
                                 const std::array<char, Capacity>& text,
                                 const std::size_t size,
                                 const serialization::ByteOrder order) noexcept {
  std::uint8_t* const value = writer.append(id, 4U + size + 1U);
  if (value == nullptr) {
    return false;
  }
  write_u32(value, static_cast<std::uint32_t>(size + 1U), order);
  std::memcpy(&value[4], text.data(), size);
  value[4U + size] = 0U;
  return true;
}

[[nodiscard]] bool append_locator(ParameterWriter& writer,
                                  const std::uint16_t id,
                                  const Locator& locator,
                                  const serialization::ByteOrder order) noexcept {
  std::uint8_t* const value = writer.append(id, 24U);
  if (value == nullptr) {
    return false;
  }
  write_u32(value, static_cast<std::uint32_t>(locator.kind), order);
  write_u32(&value[4], locator.port, order);
  std::memcpy(&value[8], locator.address.data(), locator.address.size());
  return true;
}

[[nodiscard]] bool append_duration(std::uint8_t* const value,
                                   const std::uint64_t duration_ns,
                                   const serialization::ByteOrder order) noexcept {
  const std::uint64_t seconds = duration_ns / nanoseconds_per_second;
  if (seconds > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max())) {
    return false;
  }
  write_u32(value, static_cast<std::uint32_t>(seconds), order);
  const std::uint64_t remainder = duration_ns % nanoseconds_per_second;
  const std::uint32_t fraction = static_cast<std::uint32_t>(
      (remainder << 32U) / nanoseconds_per_second);
  write_u32(&value[4], fraction, order);
  return true;
}

[[nodiscard]] SedpError parse_duration(const std::uint8_t* const value,
                                       const serialization::ByteOrder order,
                                       std::uint64_t& duration_ns) noexcept {
  const std::uint32_t seconds_bits = read_u32(value, order);
  std::int32_t seconds = 0;
  std::memcpy(&seconds, &seconds_bits, sizeof(seconds));
  if (seconds < 0) {
    return SedpError::invalid_duration;
  }
  duration_ns = static_cast<std::uint64_t>(seconds) * nanoseconds_per_second +
      ((static_cast<std::uint64_t>(read_u32(&value[4], order)) *
        nanoseconds_per_second) >> 32U);
  return SedpError::none;
}

[[nodiscard]] SedpError append_parsed_locator(
    const std::uint8_t* const value, const std::size_t length,
    const serialization::ByteOrder order,
    BoundedLocatorList<sedp_max_locators>& output) noexcept {
  if (length != 24U) {
    return SedpError::malformed_parameter;
  }
  Locator locator{};
  const std::uint32_t kind_bits = read_u32(value, order);
  std::memcpy(&locator.kind, &kind_bits, sizeof(locator.kind));
  // Requirements: ORT-SEDP-002, ORT-INT-005
  // Vendor endpoints may advertise shared memory or IPv6 beside UDPv4.
  // Preserve the supported transport requirement after parsing all locators.
  if (locator.kind != 1) {
    return SedpError::none;
  }
  locator.port = read_u32(&value[4], order);
  std::memcpy(locator.address.data(), &value[8], locator.address.size());
  if (!valid_udp_v4_locator(locator)) {
    return SedpError::invalid_locator;
  }
  if (!output.push_back(locator)) {
    return SedpError::locator_bound_exceeded;
  }
  return SedpError::none;
}

template <std::size_t Capacity>
[[nodiscard]] SedpError parse_string(
    const std::uint8_t* const value, const std::size_t length,
    const serialization::ByteOrder order, std::array<char, Capacity>& output,
    std::size_t& output_size) noexcept {
  if (length < 8U) {
    return SedpError::malformed_parameter;
  }
  const std::size_t encoded_size = read_u32(value, order);
  if ((encoded_size <= 1U) || (encoded_size > (length - 4U)) ||
      (encoded_size > Capacity) || (value[4U + encoded_size - 1U] != 0U)) {
    return SedpError::malformed_parameter;
  }
  for (std::size_t index = 0U; index + 1U < encoded_size; ++index) {
    if (value[4U + index] == 0U) {
      return SedpError::malformed_parameter;
    }
  }
  output_size = encoded_size - 1U;
  std::memcpy(output.data(), &value[4], output_size);
  return SedpError::none;
}

[[nodiscard]] SedpResult fail(const SedpError error,
                              const RtpsError rtps_error = RtpsError::none)
    noexcept {
  SedpResult result{};
  result.error = error;
  result.rtps_error = rtps_error;
  return result;
}

[[nodiscard]] bool same_text(const char* const left,
                             const std::size_t left_size,
                             const char* const right,
                             const std::size_t right_size) noexcept {
  return (left_size == right_size) &&
         ((left_size == 0U) || (std::memcmp(left, right, left_size) == 0));
}

}  // namespace

const char* to_string(const SedpError error) noexcept {
  switch (error) {
    case SedpError::none: return "none";
    case SedpError::invalid_argument: return "invalid argument";
    case SedpError::invalid_configuration: return "invalid SEDP configuration";
    case SedpError::buffer_overflow: return "SEDP output buffer overflow";
    case SedpError::rtps_error: return "RTPS DATA operation failed";
    case SedpError::unsupported_representation: return "unsupported discovery representation";
    case SedpError::malformed_parameter: return "malformed SEDP parameter";
    case SedpError::unknown_required_parameter: return "unknown must-understand parameter";
    case SedpError::missing_required_parameter: return "missing required SEDP parameter";
    case SedpError::duplicate_parameter: return "duplicate singleton SEDP parameter";
    case SedpError::locator_bound_exceeded: return "SEDP locator bound exceeded";
    case SedpError::invalid_locator: return "invalid UDPv4 locator";
    case SedpError::invalid_duration: return "invalid reliability duration";
    case SedpError::invalid_identity: return "invalid SEDP endpoint identity";
    case SedpError::participant_mismatch: return "endpoint participant mismatch";
    case SedpError::table_full: return "discovered endpoint table full";
    case SedpError::stale_announcement: return "stale endpoint announcement";
    case SedpError::endpoint_not_found: return "endpoint not found";
    case SedpError::action_capacity_exceeded: return "endpoint action capacity exceeded";
  }
  return "unknown SEDP error";
}

SedpMessageBuilder::SedpMessageBuilder(std::uint8_t* const buffer,
                                       const std::size_t capacity) noexcept
    : buffer_(buffer), capacity_(capacity) {
  if ((buffer_ == nullptr) && (capacity_ != 0U)) {
    error_ = SedpError::invalid_argument;
  }
}

bool SedpMessageBuilder::build(const SedpAnnouncementConfig& config) noexcept {
  size_ = 0U;
  error_ = SedpError::none;
  rtps_error_ = RtpsError::none;
  const auto& endpoint = config.endpoint;
  if (((buffer_ == nullptr) && (capacity_ != 0U)) ||
      !supported_byte_order(config.submessage_byte_order) ||
      !supported_byte_order(config.parameter_byte_order)) {
    error_ = SedpError::invalid_argument;
    return false;
  }
  if ((endpoint.protocol_version.major != 2U) ||
      (endpoint.protocol_version.minor == 0U) ||
      (endpoint.protocol_version.minor > 5U) ||
      !nonzero_guid(endpoint.participant_guid_prefix) ||
      !valid_endpoint_id(endpoint.endpoint_id, endpoint.kind) ||
      !valid_text(endpoint.topic_name, endpoint.topic_name_size) ||
      !valid_text(endpoint.type_name, endpoint.type_name_size) ||
      !valid_reliability(endpoint.reliability) ||
      !valid_durability(endpoint.durability) ||
      ((endpoint.unicast_locators.size + endpoint.multicast_locators.size) ==
       0U)) {
    error_ = SedpError::invalid_configuration;
    return false;
  }
  if (!valid_locators(endpoint.unicast_locators) ||
      !valid_locators(endpoint.multicast_locators)) {
    error_ = SedpError::invalid_locator;
    return false;
  }

  std::array<std::uint8_t, payload_capacity> payload{};
  ParameterWriter parameters(payload.data(), payload.size(),
                             config.parameter_byte_order);
  bool complete = append_guid(parameters, pid_endpoint_guid,
                              endpoint.participant_guid_prefix,
                              endpoint.endpoint_id);
  complete = complete && append_guid(parameters, pid_participant_guid,
                                     endpoint.participant_guid_prefix,
                                     entity_id_participant);
  complete = complete && append_string(parameters, pid_topic_name,
                                       endpoint.topic_name,
                                       endpoint.topic_name_size,
                                       config.parameter_byte_order);
  complete = complete && append_string(parameters, pid_type_name,
                                       endpoint.type_name,
                                       endpoint.type_name_size,
                                       config.parameter_byte_order);
  std::uint8_t* value = nullptr;
  if (complete) {
    value = parameters.append(pid_reliability, 12U);
    complete = value != nullptr;
  }
  if (complete) {
    write_u32(value, static_cast<std::uint32_t>(endpoint.reliability),
              config.parameter_byte_order);
    complete = append_duration(&value[4], endpoint.max_blocking_time_ns,
                               config.parameter_byte_order);
  }
  if (complete) {
    value = parameters.append(pid_durability, 4U);
    complete = value != nullptr;
  }
  if (complete) {
    write_u32(value, static_cast<std::uint32_t>(endpoint.durability),
              config.parameter_byte_order);
    value = parameters.append(pid_expects_inline_qos, 1U);
    complete = value != nullptr;
  }
  if (complete) {
    value[0] = endpoint.expects_inline_qos ? 1U : 0U;
  }
  for (std::size_t index = 0U;
       complete && (index < endpoint.unicast_locators.size); ++index) {
    complete = append_locator(parameters, pid_unicast_locator,
                              endpoint.unicast_locators[index],
                              config.parameter_byte_order);
  }
  for (std::size_t index = 0U;
       complete && (index < endpoint.multicast_locators.size); ++index) {
    complete = append_locator(parameters, pid_multicast_locator,
                              endpoint.multicast_locators[index],
                              config.parameter_byte_order);
  }
  complete = complete && parameters.finish();
  if (!complete) {
    error_ = SedpError::buffer_overflow;
    return false;
  }

  DataMessageConfig data{};
  data.version = endpoint.protocol_version;
  data.vendor_id = endpoint.vendor_id;
  data.guid_prefix = endpoint.participant_guid_prefix;
  data.reader_id = entity_id_unknown;
  data.writer_id = endpoint.kind == EndpointKind::writer
                       ? entity_id_publications_writer
                       : entity_id_subscriptions_writer;
  data.sequence_number = config.sequence_number;
  data.submessage_byte_order = config.submessage_byte_order;
  DataMessageBuilder builder(buffer_, capacity_);
  if (!builder.build(data, payload.data(), parameters.size())) {
    error_ = SedpError::rtps_error;
    rtps_error_ = builder.error();
    return false;
  }
  size_ = builder.size();
  return true;
}

SedpResult parse_sedp_message(const std::uint8_t* const message,
                              const std::size_t message_size,
                              const GuidPrefix& expected_participant,
                              SedpMessageView& view) noexcept {
  DataMessageView data{};
  const RtpsError rtps_error = parse_data_message(message, message_size, data);
  if (rtps_error != RtpsError::none) {
    return fail(SedpError::rtps_error, rtps_error);
  }

  EndpointKind kind = EndpointKind::writer;
  EntityId expected_reader = entity_id_publications_reader;
  if (data.writer_id == entity_id_publications_writer) {
    kind = EndpointKind::writer;
  } else if (data.writer_id == entity_id_subscriptions_writer) {
    kind = EndpointKind::reader;
    expected_reader = entity_id_subscriptions_reader;
  } else {
    return fail(SedpError::invalid_identity);
  }
  if ((data.reader_id != entity_id_unknown) &&
      (data.reader_id != expected_reader)) {
    return fail(SedpError::invalid_identity);
  }
  if (data.guid_prefix.value != expected_participant.value) {
    return fail(SedpError::participant_mismatch);
  }
  if ((data.payload_size < 8U) || (data.serialized_payload[0] != 0U) ||
      ((data.serialized_payload[1] != 2U) &&
       (data.serialized_payload[1] != 3U))) {
    return fail(SedpError::unsupported_representation);
  }

  const auto order = data.serialized_payload[1] == 3U
                         ? serialization::ByteOrder::little_endian
                         : serialization::ByteOrder::big_endian;
  const std::uint8_t* const payload = data.serialized_payload;
  const std::size_t payload_size = data.payload_size;
  SedpMessageView candidate{};
  candidate.endpoint.protocol_version = data.version;
  candidate.endpoint.vendor_id = data.vendor_id;
  candidate.endpoint.participant_guid_prefix = expected_participant;
  candidate.endpoint.kind = kind;
  candidate.sequence_number = data.sequence_number;
  candidate.submessage_byte_order = data.submessage_byte_order;
  candidate.parameter_byte_order = order;

  bool seen_endpoint = false;
  bool seen_participant = false;
  bool seen_locator = false;
  bool seen_topic = false;
  bool seen_type = false;
  bool seen_reliability = false;
  bool seen_durability = false;
  bool seen_inline = false;
  bool seen_sentinel = false;
  std::size_t offset = 4U;
  while ((offset + 4U) <= payload_size) {
    const std::uint16_t id = read_u16(&payload[offset], order);
    const std::size_t length = read_u16(&payload[offset + 2U], order);
    offset += 4U;
    if (id == pid_sentinel) {
      seen_sentinel = true;
      break;
    }
    if (((length & 3U) != 0U) || (length > (payload_size - offset))) {
      return fail(SedpError::malformed_parameter);
    }
    const std::uint8_t* const value = &payload[offset];
    SedpError parse_error = SedpError::none;
    switch (id) {
      case 0U:
        break;
      case pid_endpoint_guid:
        if (seen_endpoint) return fail(SedpError::duplicate_parameter);
        if (length != 16U) return fail(SedpError::malformed_parameter);
        if (std::memcmp(value, expected_participant.value.data(), 12U) != 0) {
          return fail(SedpError::participant_mismatch);
        }
        std::memcpy(candidate.endpoint.endpoint_id.value.data(), &value[12],
                    4U);
        seen_endpoint = true;
        break;
      case pid_participant_guid:
        if (seen_participant) return fail(SedpError::duplicate_parameter);
        if (length != 16U) return fail(SedpError::malformed_parameter);
        if ((std::memcmp(value, expected_participant.value.data(), 12U) != 0) ||
            (std::memcmp(&value[12], entity_id_participant.value.data(), 4U) !=
             0)) {
          return fail(SedpError::participant_mismatch);
        }
        seen_participant = true;
        break;
      case pid_topic_name:
        if (seen_topic) return fail(SedpError::duplicate_parameter);
        parse_error = parse_string(value, length, order,
                                   candidate.endpoint.topic_name,
                                   candidate.endpoint.topic_name_size);
        seen_topic = parse_error == SedpError::none;
        break;
      case pid_type_name:
        if (seen_type) return fail(SedpError::duplicate_parameter);
        parse_error = parse_string(value, length, order,
                                   candidate.endpoint.type_name,
                                   candidate.endpoint.type_name_size);
        seen_type = parse_error == SedpError::none;
        break;
      case pid_reliability: {
        if (seen_reliability) return fail(SedpError::duplicate_parameter);
        if (length != 12U) return fail(SedpError::malformed_parameter);
        const auto reliability = static_cast<ReliabilityKind>(
            static_cast<std::int32_t>(read_u32(value, order)));
        if (!valid_reliability(reliability)) {
          return fail(SedpError::malformed_parameter);
        }
        candidate.endpoint.reliability = reliability;
        parse_error = parse_duration(&value[4], order,
                                     candidate.endpoint.max_blocking_time_ns);
        seen_reliability = parse_error == SedpError::none;
        break;
      }
      case pid_durability: {
        if (seen_durability) return fail(SedpError::duplicate_parameter);
        if (length != 4U) return fail(SedpError::malformed_parameter);
        const auto durability = static_cast<DurabilityKind>(
            static_cast<std::int32_t>(read_u32(value, order)));
        if (!valid_durability(durability)) {
          return fail(SedpError::malformed_parameter);
        }
        candidate.endpoint.durability = durability;
        seen_durability = true;
        break;
      }
      case pid_expects_inline_qos:
        if (seen_inline) return fail(SedpError::duplicate_parameter);
        if ((length != 4U) || (value[0] > 1U)) {
          return fail(SedpError::malformed_parameter);
        }
        candidate.endpoint.expects_inline_qos = value[0] != 0U;
        seen_inline = true;
        break;
      case pid_unicast_locator:
        seen_locator = true;
        parse_error = append_parsed_locator(
            value, length, order, candidate.endpoint.unicast_locators);
        break;
      case pid_multicast_locator:
        seen_locator = true;
        parse_error = append_parsed_locator(
            value, length, order, candidate.endpoint.multicast_locators);
        break;
      default:
        if ((id & must_understand_mask) != 0U) {
          return fail(SedpError::unknown_required_parameter);
        }
        break;
    }
    if (parse_error != SedpError::none) {
      return fail(parse_error);
    }
    offset += length;
  }

  if (!seen_sentinel) {
    return fail(SedpError::malformed_parameter);
  }
  // Some peers omit PID_PARTICIPANT_GUID and locators. The endpoint GUID
  // supplies the participant prefix; empty locator lists inherit the matched
  // participant's SPDP default locators at the caller boundary.
  if (!seen_endpoint || !seen_topic || !seen_type ||
      (seen_locator && candidate.endpoint.unicast_locators.size +
           candidate.endpoint.multicast_locators.size == 0U)) {
    return fail(SedpError::missing_required_parameter);
  }
  if (!valid_endpoint_id(candidate.endpoint.endpoint_id, kind)) {
    return fail(SedpError::invalid_identity);
  }
  view = candidate;
  SedpResult result{};
  result.bytes = message_size;
  return result;
}

const char* to_string(const MatchStatus status) noexcept {
  switch (status) {
    case MatchStatus::matched: return "matched";
    case MatchStatus::invalid_local_endpoint: return "invalid local endpoint";
    case MatchStatus::same_endpoint_kind: return "same endpoint kind";
    case MatchStatus::topic_mismatch: return "topic mismatch";
    case MatchStatus::type_mismatch: return "type mismatch";
    case MatchStatus::reliability_incompatible: return "reliability incompatible";
    case MatchStatus::durability_incompatible: return "durability incompatible";
  }
  return "unknown match status";
}

MatchStatus evaluate_endpoint_match(
    const LocalEndpointDescriptor& local,
    const SedpEndpointData& remote) noexcept {
  if ((local.topic_name == nullptr) || (local.type_name == nullptr) ||
      (local.topic_name_size == 0U) ||
      (local.topic_name_size > sedp_max_topic_name) ||
      (local.type_name_size == 0U) ||
      (local.type_name_size > sedp_max_type_name) ||
      !valid_reliability(local.reliability) ||
      !valid_durability(local.durability)) {
    return MatchStatus::invalid_local_endpoint;
  }
  if (local.kind == remote.kind) {
    return MatchStatus::same_endpoint_kind;
  }
  if (!same_text(local.topic_name, local.topic_name_size,
                 remote.topic_name.data(), remote.topic_name_size)) {
    return MatchStatus::topic_mismatch;
  }
  if (!same_text(local.type_name, local.type_name_size,
                 remote.type_name.data(), remote.type_name_size)) {
    return MatchStatus::type_mismatch;
  }

  const ReliabilityKind offered_reliability =
      local.kind == EndpointKind::writer ? local.reliability
                                         : remote.reliability;
  const ReliabilityKind requested_reliability =
      local.kind == EndpointKind::reader ? local.reliability
                                         : remote.reliability;
  if (static_cast<std::int32_t>(offered_reliability) <
      static_cast<std::int32_t>(requested_reliability)) {
    return MatchStatus::reliability_incompatible;
  }

  const DurabilityKind offered_durability =
      local.kind == EndpointKind::writer ? local.durability
                                         : remote.durability;
  const DurabilityKind requested_durability =
      local.kind == EndpointKind::reader ? local.durability
                                         : remote.durability;
  if (static_cast<std::int32_t>(offered_durability) <
      static_cast<std::int32_t>(requested_durability)) {
    return MatchStatus::durability_incompatible;
  }
  return MatchStatus::matched;
}

}  // namespace openrtdds::rtps
