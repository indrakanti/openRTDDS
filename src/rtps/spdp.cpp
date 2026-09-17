#include "openrtdds/rtps/spdp.hpp"

#include <cstring>
#include <limits>

namespace openrtdds::rtps {
namespace {

constexpr std::uint16_t pid_sentinel = 0x0001U;
constexpr std::uint16_t pid_participant_lease_duration = 0x0002U;
constexpr std::uint16_t pid_domain_id = 0x000FU;
constexpr std::uint16_t pid_protocol_version = 0x0015U;
constexpr std::uint16_t pid_vendor_id = 0x0016U;
constexpr std::uint16_t pid_default_unicast_locator = 0x0031U;
constexpr std::uint16_t pid_metatraffic_unicast_locator = 0x0032U;
constexpr std::uint16_t pid_metatraffic_multicast_locator = 0x0033U;
constexpr std::uint16_t pid_expects_inline_qos = 0x0043U;
constexpr std::uint16_t pid_default_multicast_locator = 0x0048U;
constexpr std::uint16_t pid_participant_guid = 0x0050U;
constexpr std::uint16_t pid_builtin_endpoint_set = 0x0058U;
constexpr std::uint16_t pid_entity_name = 0x0062U;
constexpr std::uint16_t must_understand_mask = 0x4000U;
constexpr std::size_t payload_capacity = 768U;
constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;
constexpr EntityId entity_id_unknown{{0U, 0U, 0U, 0U}};
constexpr EntityId entity_id_participant{{0U, 0U, 1U, 0xC1U}};
constexpr EntityId entity_id_spdp_writer{{0U, 1U, 0U, 0xC2U}};
constexpr EntityId entity_id_spdp_reader{{0U, 1U, 0U, 0xC7U}};

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

[[nodiscard]] bool nonzero_guid(const GuidPrefix& prefix) noexcept {
  for (const std::uint8_t octet : prefix.value) {
    if (octet != 0U) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool same_vendor(const VendorId& left,
                               const VendorId& right) noexcept {
  return left.value == right.value;
}

[[nodiscard]] std::size_t padded_size(const std::size_t size) noexcept {
  return (size + 3U) & ~std::size_t{3U};
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
    std::uint8_t* const value = append(pid_sentinel, 0U);
    return value != nullptr;
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }

 private:
  std::uint8_t* buffer_{nullptr};
  std::size_t capacity_{0U};
  serialization::ByteOrder order_{serialization::ByteOrder::little_endian};
  std::size_t size_{0U};
};

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

[[nodiscard]] SpdpError append_parsed_locator(
    const std::uint8_t* const value, const std::size_t length,
    const serialization::ByteOrder order,
    BoundedLocatorList<spdp_max_locators>& output) noexcept {
  if (length != 24U) {
    return SpdpError::malformed_parameter;
  }
  Locator locator{};
  const std::uint32_t kind_bits = read_u32(value, order);
  std::memcpy(&locator.kind, &kind_bits, sizeof(locator.kind));
  locator.port = read_u32(&value[4], order);
  std::memcpy(locator.address.data(), &value[8], locator.address.size());
  if (!valid_udp_v4_locator(locator)) {
    return SpdpError::invalid_locator;
  }
  if (!output.push_back(locator)) {
    return SpdpError::locator_bound_exceeded;
  }
  return SpdpError::none;
}

[[nodiscard]] SpdpResult fail(const SpdpError error,
                              const RtpsError rtps_error = RtpsError::none)
    noexcept {
  SpdpResult result{};
  result.error = error;
  result.rtps_error = rtps_error;
  return result;
}

}  // namespace

const char* to_string(const SpdpError error) noexcept {
  switch (error) {
    case SpdpError::none: return "none";
    case SpdpError::invalid_argument: return "invalid argument";
    case SpdpError::invalid_configuration: return "invalid SPDP configuration";
    case SpdpError::buffer_overflow: return "SPDP output buffer overflow";
    case SpdpError::rtps_error: return "RTPS DATA operation failed";
    case SpdpError::unsupported_representation: return "unsupported discovery representation";
    case SpdpError::malformed_parameter: return "malformed SPDP parameter";
    case SpdpError::unknown_required_parameter: return "unknown must-understand parameter";
    case SpdpError::missing_required_parameter: return "missing required SPDP parameter";
    case SpdpError::duplicate_parameter: return "duplicate singleton SPDP parameter";
    case SpdpError::locator_bound_exceeded: return "SPDP locator bound exceeded";
    case SpdpError::invalid_locator: return "invalid UDPv4 locator";
    case SpdpError::invalid_duration: return "invalid participant lease duration";
    case SpdpError::invalid_identity: return "invalid SPDP participant identity";
    case SpdpError::domain_mismatch: return "SPDP domain mismatch";
    case SpdpError::self_announcement: return "local participant announcement ignored";
    case SpdpError::table_full: return "discovered participant table full";
    case SpdpError::stale_announcement: return "stale participant announcement";
    case SpdpError::time_regression: return "monotonic time regression";
    case SpdpError::expiration_overflow: return "lease expiration overflow";
    case SpdpError::action_capacity_exceeded: return "expiration output capacity exceeded";
  }
  return "unknown SPDP error";
}

bool make_udp_v4_locator(const std::array<std::uint8_t, 4U>& address,
                         const std::uint16_t port,
                         Locator& locator) noexcept {
  if ((port == 0U) ||
      ((address[0] | address[1] | address[2] | address[3]) == 0U)) {
    return false;
  }
  Locator candidate{};
  candidate.kind = 1;
  candidate.port = port;
  candidate.address[12] = address[0];
  candidate.address[13] = address[1];
  candidate.address[14] = address[2];
  candidate.address[15] = address[3];
  locator = candidate;
  return true;
}

bool valid_udp_v4_locator(const Locator& locator) noexcept {
  if ((locator.kind != 1) || (locator.port == 0U) ||
      (locator.port > 65'535U)) {
    return false;
  }
  for (std::size_t index = 0U; index < 12U; ++index) {
    if (locator.address[index] != 0U) {
      return false;
    }
  }
  return (locator.address[12] | locator.address[13] |
          locator.address[14] | locator.address[15]) != 0U;
}

bool spdp_multicast_port(const std::uint32_t domain_id,
                         const SpdpPortConfig& config,
                         std::uint16_t& port) noexcept {
  const std::uint64_t fixed =
      static_cast<std::uint64_t>(config.port_base) +
      config.multicast_offset;
  if (fixed > 65'535U) {
    return false;
  }
  const std::uint64_t remaining = 65'535U - fixed;
  if ((config.domain_gain != 0U) &&
      (domain_id > (remaining / config.domain_gain))) {
    return false;
  }
  const std::uint64_t value =
      fixed + static_cast<std::uint64_t>(config.domain_gain) * domain_id;
  if (value == 0U) {
    return false;
  }
  port = static_cast<std::uint16_t>(value);
  return true;
}

bool spdp_unicast_port(const std::uint32_t domain_id,
                       const std::uint32_t participant_id,
                       const SpdpPortConfig& config,
                       std::uint16_t& port) noexcept {
  const std::uint64_t fixed =
      static_cast<std::uint64_t>(config.port_base) + config.unicast_offset;
  if (fixed > 65'535U) {
    return false;
  }
  const std::uint64_t domain_term =
      static_cast<std::uint64_t>(config.domain_gain) * domain_id;
  if (domain_term > (65'535U - fixed)) {
    return false;
  }
  const std::uint64_t partial = fixed + domain_term;
  const std::uint64_t remaining = 65'535U - partial;
  if ((config.participant_gain != 0U) &&
      (participant_id > (remaining / config.participant_gain))) {
    return false;
  }
  const std::uint64_t value = partial +
      static_cast<std::uint64_t>(config.participant_gain) * participant_id;
  if (value == 0U) {
    return false;
  }
  port = static_cast<std::uint16_t>(value);
  return true;
}

SpdpMessageBuilder::SpdpMessageBuilder(std::uint8_t* const buffer,
                                       const std::size_t capacity) noexcept
    : buffer_(buffer), capacity_(capacity) {
  if ((buffer_ == nullptr) && (capacity_ != 0U)) {
    error_ = SpdpError::invalid_argument;
  }
}

bool SpdpMessageBuilder::build(const SpdpAnnouncementConfig& config) noexcept {
  size_ = 0U;
  error_ = SpdpError::none;
  rtps_error_ = RtpsError::none;
  const auto& participant = config.participant;
  if (((buffer_ == nullptr) && (capacity_ != 0U)) ||
      !supported_byte_order(config.submessage_byte_order) ||
      !supported_byte_order(config.parameter_byte_order)) {
    error_ = SpdpError::invalid_argument;
    return false;
  }
  if ((participant.protocol_version.major != 2U) ||
      (participant.protocol_version.minor == 0U) ||
      (participant.protocol_version.minor > 5U) ||
      !nonzero_guid(participant.guid_prefix) ||
      (participant.entity_name_size > spdp_max_entity_name) ||
      (participant.lease_duration_ns < 2U) ||
      ((participant.available_builtin_endpoints &
        (spdp_endpoint_participant_announcer |
         spdp_endpoint_participant_detector)) !=
       (spdp_endpoint_participant_announcer |
        spdp_endpoint_participant_detector)) ||
      (participant.default_unicast.size == 0U) ||
      ((participant.metatraffic_unicast.size +
        participant.metatraffic_multicast.size) == 0U)) {
    error_ = SpdpError::invalid_configuration;
    return false;
  }
  if (!valid_locators(participant.metatraffic_unicast) ||
      !valid_locators(participant.metatraffic_multicast) ||
      !valid_locators(participant.default_unicast) ||
      !valid_locators(participant.default_multicast)) {
    error_ = SpdpError::invalid_locator;
    return false;
  }
  const std::uint64_t seconds =
      participant.lease_duration_ns / nanoseconds_per_second;
  if (seconds > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int32_t>::max())) {
    error_ = SpdpError::invalid_duration;
    return false;
  }

  std::array<std::uint8_t, payload_capacity> payload{};
  ParameterWriter parameters(payload.data(), payload.size(),
                             config.parameter_byte_order);
  std::uint8_t* value = parameters.append(pid_protocol_version, 2U);
  if (value != nullptr) {
    value[0] = participant.protocol_version.major;
    value[1] = participant.protocol_version.minor;
  }
  if (value != nullptr) {
    value = parameters.append(pid_vendor_id, 2U);
  }
  if (value != nullptr) {
    std::memcpy(value, participant.vendor_id.value.data(), 2U);
    value = parameters.append(pid_domain_id, 4U);
  }
  if (value != nullptr) {
    write_u32(value, participant.domain_id, config.parameter_byte_order);
    value = parameters.append(pid_participant_guid, 16U);
  }
  if (value != nullptr) {
    std::memcpy(value, participant.guid_prefix.value.data(), 12U);
    std::memcpy(&value[12], entity_id_participant.value.data(), 4U);
    value = parameters.append(pid_expects_inline_qos, 1U);
  }
  if (value != nullptr) {
    value[0] = participant.expects_inline_qos ? 1U : 0U;
    value = parameters.append(pid_builtin_endpoint_set, 4U);
  }
  if (value != nullptr) {
    write_u32(value, participant.available_builtin_endpoints,
              config.parameter_byte_order);
    value = parameters.append(pid_participant_lease_duration, 8U);
  }
  if (value != nullptr) {
    write_u32(value, static_cast<std::uint32_t>(seconds),
              config.parameter_byte_order);
    const std::uint64_t remainder =
        participant.lease_duration_ns % nanoseconds_per_second;
    const std::uint32_t fraction = static_cast<std::uint32_t>(
        (remainder << 32U) / nanoseconds_per_second);
    write_u32(&value[4], fraction, config.parameter_byte_order);
  }

  const auto append_list = [&](const std::uint16_t id,
                               const auto& list) noexcept {
    for (std::size_t index = 0U; index < list.size; ++index) {
      if (!append_locator(parameters, id, list[index],
                          config.parameter_byte_order)) {
        return false;
      }
    }
    return true;
  };
  bool complete = value != nullptr;
  complete = complete && append_list(pid_metatraffic_unicast_locator,
                                     participant.metatraffic_unicast);
  complete = complete && append_list(pid_metatraffic_multicast_locator,
                                     participant.metatraffic_multicast);
  complete = complete && append_list(pid_default_unicast_locator,
                                     participant.default_unicast);
  complete = complete && append_list(pid_default_multicast_locator,
                                     participant.default_multicast);
  if (complete && (participant.entity_name_size != 0U)) {
    value = parameters.append(pid_entity_name,
                              4U + participant.entity_name_size + 1U);
    if (value != nullptr) {
      write_u32(value,
                static_cast<std::uint32_t>(participant.entity_name_size + 1U),
                config.parameter_byte_order);
      std::memcpy(&value[4], participant.entity_name.data(),
                  participant.entity_name_size);
      value[4U + participant.entity_name_size] = 0U;
    }
    complete = value != nullptr;
  }
  complete = complete && parameters.finish();
  if (!complete) {
    error_ = SpdpError::buffer_overflow;
    return false;
  }

  DataMessageConfig data{};
  data.version = participant.protocol_version;
  data.vendor_id = participant.vendor_id;
  data.guid_prefix = participant.guid_prefix;
  data.reader_id = entity_id_unknown;
  data.writer_id = entity_id_spdp_writer;
  data.sequence_number = config.sequence_number;
  data.submessage_byte_order = config.submessage_byte_order;
  DataMessageBuilder builder(buffer_, capacity_);
  if (!builder.build(data, payload.data(), parameters.size())) {
    error_ = SpdpError::rtps_error;
    rtps_error_ = builder.error();
    return false;
  }
  size_ = builder.size();
  return true;
}

SpdpResult parse_spdp_message(const std::uint8_t* const message,
                              const std::size_t message_size,
                              const std::uint32_t expected_domain_id,
                              SpdpMessageView& view) noexcept {
  DataMessageView data{};
  const RtpsError rtps_error =
      parse_data_message(message, message_size, data);
  if (rtps_error != RtpsError::none) {
    return fail(SpdpError::rtps_error, rtps_error);
  }
  if ((data.writer_id != entity_id_spdp_writer) ||
      ((data.reader_id != entity_id_unknown) &&
       (data.reader_id != entity_id_spdp_reader))) {
    return fail(SpdpError::invalid_identity);
  }
  if ((data.payload_size < 8U) || (data.serialized_payload[0] != 0U) ||
      ((data.serialized_payload[1] != 2U) &&
       (data.serialized_payload[1] != 3U))) {
    return fail(SpdpError::unsupported_representation);
  }
  const auto order = data.serialized_payload[1] == 3U
                         ? serialization::ByteOrder::little_endian
                         : serialization::ByteOrder::big_endian;
  const std::uint8_t* const payload = data.serialized_payload;
  const std::size_t payload_size = data.payload_size;
  SpdpMessageView candidate{};
  candidate.participant.domain_id = expected_domain_id;
  candidate.sequence_number = data.sequence_number;
  candidate.submessage_byte_order = data.submessage_byte_order;
  candidate.parameter_byte_order = order;
  bool seen_protocol = false;
  bool seen_vendor = false;
  bool seen_guid = false;
  bool seen_domain = false;
  bool seen_inline = false;
  bool seen_endpoints = false;
  bool seen_lease = false;
  bool seen_name = false;
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
      return fail(SpdpError::malformed_parameter);
    }
    const std::uint8_t* const value = &payload[offset];
    SpdpError locator_error = SpdpError::none;
    switch (id) {
      case 0U:
        break;
      case pid_protocol_version:
        if (seen_protocol) return fail(SpdpError::duplicate_parameter);
        if (length != 4U) return fail(SpdpError::malformed_parameter);
        candidate.participant.protocol_version = {value[0], value[1]};
        seen_protocol = true;
        break;
      case pid_vendor_id:
        if (seen_vendor) return fail(SpdpError::duplicate_parameter);
        if (length != 4U) return fail(SpdpError::malformed_parameter);
        std::memcpy(candidate.participant.vendor_id.value.data(), value, 2U);
        seen_vendor = true;
        break;
      case pid_domain_id:
        if (seen_domain) return fail(SpdpError::duplicate_parameter);
        if (length != 4U) return fail(SpdpError::malformed_parameter);
        candidate.participant.domain_id = read_u32(value, order);
        seen_domain = true;
        break;
      case pid_participant_guid:
        if (seen_guid) return fail(SpdpError::duplicate_parameter);
        if (length != 16U) return fail(SpdpError::malformed_parameter);
        std::memcpy(candidate.participant.guid_prefix.value.data(), value, 12U);
        if (std::memcmp(&value[12], entity_id_participant.value.data(), 4U) !=
            0) {
          return fail(SpdpError::invalid_identity);
        }
        seen_guid = true;
        break;
      case pid_expects_inline_qos:
        if (seen_inline) return fail(SpdpError::duplicate_parameter);
        if ((length != 4U) || (value[0] > 1U)) {
          return fail(SpdpError::malformed_parameter);
        }
        candidate.participant.expects_inline_qos = value[0] != 0U;
        seen_inline = true;
        break;
      case pid_builtin_endpoint_set:
        if (seen_endpoints) return fail(SpdpError::duplicate_parameter);
        if (length != 4U) return fail(SpdpError::malformed_parameter);
        candidate.participant.available_builtin_endpoints =
            read_u32(value, order);
        seen_endpoints = true;
        break;
      case pid_participant_lease_duration: {
        if (seen_lease) return fail(SpdpError::duplicate_parameter);
        if (length != 8U) return fail(SpdpError::malformed_parameter);
        const std::uint32_t seconds_bits = read_u32(value, order);
        std::int32_t seconds = 0;
        std::memcpy(&seconds, &seconds_bits, sizeof(seconds));
        if (seconds < 0) return fail(SpdpError::invalid_duration);
        const std::uint32_t fraction = read_u32(&value[4], order);
        candidate.participant.lease_duration_ns =
            static_cast<std::uint64_t>(seconds) * nanoseconds_per_second +
            ((static_cast<std::uint64_t>(fraction) *
              nanoseconds_per_second) >> 32U);
        if (candidate.participant.lease_duration_ns == 0U) {
          return fail(SpdpError::invalid_duration);
        }
        seen_lease = true;
        break;
      }
      case pid_metatraffic_unicast_locator:
        locator_error = append_parsed_locator(
            value, length, order, candidate.participant.metatraffic_unicast);
        break;
      case pid_metatraffic_multicast_locator:
        locator_error = append_parsed_locator(
            value, length, order, candidate.participant.metatraffic_multicast);
        break;
      case pid_default_unicast_locator:
        locator_error = append_parsed_locator(
            value, length, order, candidate.participant.default_unicast);
        break;
      case pid_default_multicast_locator:
        locator_error = append_parsed_locator(
            value, length, order, candidate.participant.default_multicast);
        break;
      case pid_entity_name: {
        if (seen_name) return fail(SpdpError::duplicate_parameter);
        if (length < 8U) return fail(SpdpError::malformed_parameter);
        const std::size_t string_size = read_u32(value, order);
        if ((string_size == 0U) || (string_size > (length - 4U)) ||
            (string_size > (spdp_max_entity_name + 1U)) ||
            (value[4U + string_size - 1U] != 0U)) {
          return fail(SpdpError::malformed_parameter);
        }
        candidate.participant.entity_name_size = string_size - 1U;
        std::memcpy(candidate.participant.entity_name.data(), &value[4],
                    candidate.participant.entity_name_size);
        seen_name = true;
        break;
      }
      default:
        if ((id & must_understand_mask) != 0U) {
          return fail(SpdpError::unknown_required_parameter);
        }
        break;
    }
    if (locator_error != SpdpError::none) {
      return fail(locator_error);
    }
    offset += length;
  }

  if (!seen_sentinel) {
    return fail(SpdpError::malformed_parameter);
  }
  if (!seen_protocol || !seen_vendor || !seen_guid || !seen_endpoints ||
      (candidate.participant.default_unicast.size == 0U) ||
      ((candidate.participant.metatraffic_unicast.size +
        candidate.participant.metatraffic_multicast.size) == 0U)) {
    return fail(SpdpError::missing_required_parameter);
  }
  if ((candidate.participant.protocol_version.major != 2U) ||
      (candidate.participant.protocol_version.minor == 0U) ||
      (candidate.participant.protocol_version.minor > 5U) ||
      !nonzero_guid(candidate.participant.guid_prefix) ||
      (candidate.participant.guid_prefix.value != data.guid_prefix.value) ||
      (candidate.participant.protocol_version.major != data.version.major) ||
      (candidate.participant.protocol_version.minor != data.version.minor) ||
      !same_vendor(candidate.participant.vendor_id, data.vendor_id)) {
    return fail(SpdpError::invalid_identity);
  }
  if (candidate.participant.domain_id != expected_domain_id) {
    return fail(SpdpError::domain_mismatch);
  }
  view = candidate;
  SpdpResult result{};
  result.bytes = message_size;
  return result;
}

}  // namespace openrtdds::rtps
