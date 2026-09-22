#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "openrtdds/rtps/sedp.hpp"
#include "test_support.hpp"

// Verifies: ORT-SEDP-001, ORT-SEDP-002, ORT-SEDP-003,
// Verifies: ORT-SEDP-004, ORT-SEDP-005, ORT-SEDP-006, ORT-INT-005

namespace {

[[nodiscard]] openrtdds::rtps::SedpAnnouncementConfig announcement(
    const openrtdds::rtps::EndpointKind kind,
    const std::uint8_t seed,
    const openrtdds::serialization::ByteOrder order =
        openrtdds::serialization::ByteOrder::little_endian) {
  openrtdds::rtps::SedpAnnouncementConfig config{};
  config.endpoint.vendor_id.value = {{0x12U, 0x34U}};
  for (std::size_t index = 0U;
       index < config.endpoint.participant_guid_prefix.value.size(); ++index) {
    config.endpoint.participant_guid_prefix.value[index] =
        static_cast<std::uint8_t>(seed + index);
  }
  config.endpoint.kind = kind;
  config.endpoint.endpoint_id = kind == openrtdds::rtps::EndpointKind::writer
      ? openrtdds::rtps::EntityId{{0U, 0U, 0x10U, 0x02U}}
      : openrtdds::rtps::EntityId{{0U, 0U, 0x20U, 0x07U}};
  constexpr char topic[] = "VehicleState";
  constexpr char type[] = "openrtdds::VehicleState";
  config.endpoint.topic_name_size = sizeof(topic) - 1U;
  config.endpoint.type_name_size = sizeof(type) - 1U;
  std::memcpy(config.endpoint.topic_name.data(), topic, sizeof(topic) - 1U);
  std::memcpy(config.endpoint.type_name.data(), type, sizeof(type) - 1U);
  openrtdds::rtps::Locator locator{};
  CHECK(openrtdds::rtps::make_udp_v4_locator(
      {{127U, 0U, 0U, 1U}}, 9200U, locator));
  CHECK(config.endpoint.unicast_locators.push_back(locator));
  config.endpoint.reliability = openrtdds::rtps::ReliabilityKind::reliable;
  config.endpoint.durability =
      openrtdds::rtps::DurabilityKind::transient_local;
  config.endpoint.expects_inline_qos = kind ==
      openrtdds::rtps::EndpointKind::reader;
  config.sequence_number = 5U;
  config.submessage_byte_order = order;
  config.parameter_byte_order = order;
  return config;
}

[[nodiscard]] std::size_t find_parameter(std::uint8_t* const message,
                                         const std::size_t size,
                                         const std::uint16_t wanted) {
  if (size < 52U) {
    return size;
  }
  const bool little = message[45U] == 3U;
  std::size_t offset = 48U;
  while ((offset + 4U) <= size) {
    const std::uint16_t id = little
        ? static_cast<std::uint16_t>(message[offset]) |
              (static_cast<std::uint16_t>(message[offset + 1U]) << 8U)
        : (static_cast<std::uint16_t>(message[offset]) << 8U) |
              static_cast<std::uint16_t>(message[offset + 1U]);
    const std::uint16_t length = little
        ? static_cast<std::uint16_t>(message[offset + 2U]) |
              (static_cast<std::uint16_t>(message[offset + 3U]) << 8U)
        : (static_cast<std::uint16_t>(message[offset + 2U]) << 8U) |
              static_cast<std::uint16_t>(message[offset + 3U]);
    if (id == wanted) {
      return offset;
    }
    if (id == 1U) {
      return size;
    }
    offset += 4U + length;
  }
  return size;
}

void test_roundtrip(const openrtdds::rtps::EndpointKind kind,
                    const openrtdds::serialization::ByteOrder order) {
  const auto config = announcement(kind, 0x10U, order);
  std::array<std::uint8_t, 1600U> message{};
  openrtdds::rtps::SedpMessageBuilder builder(message.data(), message.size());
  CHECK(builder.build(config));
  CHECK(builder.size() > 120U);

  openrtdds::rtps::SedpMessageView view{};
  const auto result = openrtdds::rtps::parse_sedp_message(
      message.data(), builder.size(),
      config.endpoint.participant_guid_prefix, view);
  CHECK(result.ok());
  CHECK(result.bytes == builder.size());
  CHECK(view.sequence_number == config.sequence_number);
  CHECK(view.endpoint.kind == kind);
  CHECK(view.endpoint.endpoint_id == config.endpoint.endpoint_id);
  CHECK(view.endpoint.topic_name_size == config.endpoint.topic_name_size);
  CHECK(view.endpoint.type_name_size == config.endpoint.type_name_size);
  CHECK(view.endpoint.reliability ==
        openrtdds::rtps::ReliabilityKind::reliable);
  CHECK(view.endpoint.durability ==
        openrtdds::rtps::DurabilityKind::transient_local);
  CHECK(view.endpoint.unicast_locators.size == 1U);
}

void test_defensive_parsing() {
  using openrtdds::rtps::SedpError;
  auto config = announcement(openrtdds::rtps::EndpointKind::writer, 0x30U);
  std::array<std::uint8_t, 1600U> message{};
  openrtdds::rtps::SedpMessageBuilder builder(message.data(), message.size());
  CHECK(builder.build(config));
  const std::size_t bytes = builder.size();
  openrtdds::rtps::SedpMessageView prior{};
  prior.sequence_number = 99U;

  CHECK(openrtdds::rtps::parse_sedp_message(
            message.data(), bytes - 1U,
            config.endpoint.participant_guid_prefix, prior)
            .error != SedpError::none);
  CHECK(prior.sequence_number == 99U);

  auto other = config.endpoint.participant_guid_prefix;
  other.value[0] ^= 0x80U;
  CHECK(openrtdds::rtps::parse_sedp_message(
            message.data(), bytes, other, prior)
            .error == SedpError::participant_mismatch);
  CHECK(prior.sequence_number == 99U);

  auto corrupted = message;
  const std::size_t type = find_parameter(corrupted.data(), bytes, 0x0007U);
  CHECK(type < bytes);
  corrupted[type] = 0x07U;
  corrupted[type + 1U] = 0x40U;
  CHECK(openrtdds::rtps::parse_sedp_message(
            corrupted.data(), bytes,
            config.endpoint.participant_guid_prefix, prior)
            .error == SedpError::unknown_required_parameter);

  config.endpoint.endpoint_id.value[3] = 0x07U;
  CHECK(!builder.build(config));
  CHECK(builder.error() == SedpError::invalid_configuration);
}

void test_unsupported_transport_requires_supported_locator() {
  // Verifies: ORT-SEDP-002, ORT-INT-005
  auto config = announcement(openrtdds::rtps::EndpointKind::writer, 0x37U);
  openrtdds::rtps::Locator second{};
  CHECK(openrtdds::rtps::make_udp_v4_locator(
      {{127U, 0U, 0U, 2U}}, 9201U, second));
  CHECK(config.endpoint.unicast_locators.push_back(second));
  std::array<std::uint8_t, 1600U> message{};
  openrtdds::rtps::SedpMessageBuilder builder(message.data(), message.size());
  CHECK(builder.build(config));
  const auto first = find_parameter(message.data(), builder.size(), 0x002FU);
  CHECK(first + 8U < builder.size());
  message[first + 4U] = 16U; // shared-memory kind in a little-endian PL_CDR
  openrtdds::rtps::SedpMessageView view{};
  CHECK(openrtdds::rtps::parse_sedp_message(
      message.data(), builder.size(),
      config.endpoint.participant_guid_prefix, view).ok());
  CHECK(view.endpoint.unicast_locators.size == 1U);

  // A packet with only unsupported locators must still fail.
  config.endpoint.unicast_locators.size = 1U;
  CHECK(builder.build(config));
  const auto only = find_parameter(message.data(), builder.size(), 0x002FU);
  CHECK(only + 8U < builder.size());
  message[only + 4U] = 16U;
  CHECK(openrtdds::rtps::parse_sedp_message(
      message.data(), builder.size(),
      config.endpoint.participant_guid_prefix, view).error ==
      openrtdds::rtps::SedpError::missing_required_parameter);
}

void test_bounded_table() {
  using Table = openrtdds::rtps::DiscoveredEndpointTable<2U>;
  using openrtdds::rtps::EndpointUpdate;
  using openrtdds::rtps::SedpError;
  std::array<std::uint8_t, 1600U> message{};
  openrtdds::rtps::SedpMessageBuilder builder(message.data(), message.size());
  auto config = announcement(openrtdds::rtps::EndpointKind::writer, 0x50U);
  CHECK(builder.build(config));
  openrtdds::rtps::SedpMessageView view{};
  CHECK(openrtdds::rtps::parse_sedp_message(
            message.data(), builder.size(),
            config.endpoint.participant_guid_prefix, view).ok());

  Table table{};
  auto update = table.upsert(view);
  CHECK(update.ok());
  CHECK(update.update == EndpointUpdate::added);
  CHECK(table.size() == 1U);
  CHECK(table.find(config.endpoint.participant_guid_prefix,
                   config.endpoint.endpoint_id) != nullptr);
  CHECK(table.upsert(view).error == SedpError::stale_announcement);

  view.sequence_number = 6U;
  view.endpoint.reliability =
      openrtdds::rtps::ReliabilityKind::best_effort;
  update = table.upsert(view);
  CHECK(update.ok());
  CHECK(update.update == EndpointUpdate::updated);

  auto second = announcement(openrtdds::rtps::EndpointKind::reader, 0x50U);
  CHECK(builder.build(second));
  openrtdds::rtps::SedpMessageView second_view{};
  CHECK(openrtdds::rtps::parse_sedp_message(
            message.data(), builder.size(),
            second.endpoint.participant_guid_prefix, second_view).ok());
  CHECK(table.upsert(second_view).ok());
  CHECK(table.size() == 2U);

  std::size_t removed_size = 77U;
  std::array<openrtdds::rtps::EntityId, 1U> too_small{};
  CHECK(table.remove_participant(config.endpoint.participant_guid_prefix,
                                 too_small.data(), too_small.size(),
                                 removed_size) ==
        SedpError::action_capacity_exceeded);
  CHECK(table.size() == 2U);
  std::array<openrtdds::rtps::EntityId, 2U> removed{};
  CHECK(table.remove_participant(config.endpoint.participant_guid_prefix,
                                 removed.data(), removed.size(),
                                 removed_size) == SedpError::none);
  CHECK(removed_size == 2U);
  CHECK(table.size() == 0U);
}

void test_matching() {
  using namespace openrtdds::rtps;
  auto remote = announcement(EndpointKind::writer, 0x70U).endpoint;
  constexpr char topic[] = "VehicleState";
  constexpr char type[] = "openrtdds::VehicleState";
  LocalEndpointDescriptor local{};
  local.kind = EndpointKind::reader;
  local.topic_name = topic;
  local.topic_name_size = sizeof(topic) - 1U;
  local.type_name = type;
  local.type_name_size = sizeof(type) - 1U;
  local.reliability = ReliabilityKind::reliable;
  local.durability = DurabilityKind::volatile_durability;
  CHECK(evaluate_endpoint_match(local, remote) == MatchStatus::matched);

  remote.reliability = ReliabilityKind::best_effort;
  CHECK(evaluate_endpoint_match(local, remote) ==
        MatchStatus::reliability_incompatible);
  remote.reliability = ReliabilityKind::reliable;
  local.durability = DurabilityKind::transient_local;
  remote.durability = DurabilityKind::volatile_durability;
  CHECK(evaluate_endpoint_match(local, remote) ==
        MatchStatus::durability_incompatible);
  local.durability = DurabilityKind::volatile_durability;
  local.kind = EndpointKind::writer;
  CHECK(evaluate_endpoint_match(local, remote) ==
        MatchStatus::same_endpoint_kind);
  local.kind = EndpointKind::reader;
  local.topic_name_size = sizeof(topic) - 2U;
  CHECK(evaluate_endpoint_match(local, remote) == MatchStatus::topic_mismatch);
}

void test_static_properties() {
  using Table = openrtdds::rtps::DiscoveredEndpointTable<8U>;
  static_assert(std::is_nothrow_destructible<Table>::value,
                "SEDP table destruction must not throw");
  CHECK(sizeof(Table) < 16'384U);
}

}  // namespace

void test_sedp() {
  test_roundtrip(openrtdds::rtps::EndpointKind::writer,
                 openrtdds::serialization::ByteOrder::little_endian);
  test_roundtrip(openrtdds::rtps::EndpointKind::reader,
                 openrtdds::serialization::ByteOrder::big_endian);
  test_defensive_parsing();
  test_unsupported_transport_requires_supported_locator();
  test_bounded_table();
  test_matching();
  test_static_properties();
}
