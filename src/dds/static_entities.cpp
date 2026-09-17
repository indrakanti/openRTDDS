#include "openrtdds/dds/static_entities.hpp"

namespace openrtdds::dds {
namespace {

[[nodiscard]] bool nonzero_guid_prefix(
    const rtps::GuidPrefix& prefix) noexcept {
  for (const std::uint8_t octet : prefix.value) {
    if (octet != 0U) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool nonzero_entity_id(const rtps::EntityId& id) noexcept {
  for (const std::uint8_t octet : id.value) {
    if (octet != 0U) {
      return true;
    }
  }
  return false;
}

}  // namespace

const char* to_string(const DdsError error) noexcept {
  switch (error) {
    case DdsError::none:
      return "none";
    case DdsError::invalid_participant:
      return "invalid static domain participant";
    case DdsError::invalid_topic:
      return "invalid static topic or type binding";
    case DdsError::invalid_endpoint:
      return "invalid static endpoint configuration";
    case DdsError::serialization_failed:
      return "typed sample serialization failed";
    case DdsError::deserialization_failed:
      return "typed sample deserialization failed";
    case DdsError::rtps_data_failed:
      return "RTPS DATA operation failed";
    case DdsError::reliability_message_failed:
      return "RTPS reliability message operation failed";
    case DdsError::reliability_state_failed:
      return "reliability state operation failed";
    case DdsError::unexpected_participant:
      return "RTPS message came from an unexpected participant";
    case DdsError::unexpected_endpoint:
      return "RTPS message used an unexpected endpoint";
    case DdsError::invalid_action:
      return "action cannot be converted to an RTPS DATA message";
  }
  return "unknown static DDS error";
}

DomainParticipant::DomainParticipant(
    const DomainParticipantConfig config) noexcept
    : config_(config),
      valid_((config.version.major == 2U) &&
             (config.version.minor != 0U) &&
             (config.version.minor <= 5U) &&
             nonzero_guid_prefix(config.guid_prefix)) {}

namespace detail {

bool valid_endpoint(const StaticEndpointConfig& endpoint) noexcept {
  const bool byte_order_valid =
      (endpoint.byte_order == serialization::ByteOrder::big_endian) ||
      (endpoint.byte_order == serialization::ByteOrder::little_endian);
  return nonzero_entity_id(endpoint.reader_id) &&
         nonzero_entity_id(endpoint.writer_id) &&
         nonzero_guid_prefix(endpoint.remote_guid_prefix) && byte_order_valid;
}

bool same_guid_prefix(const rtps::GuidPrefix& left,
                      const rtps::GuidPrefix& right) noexcept {
  return left.value == right.value;
}

}  // namespace detail
}  // namespace openrtdds::dds
