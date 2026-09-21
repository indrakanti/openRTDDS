#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "openrtdds/rtps/spdp.hpp"
#include "test_support.hpp"

// Verifies: ORT-SPDP-001, ORT-SPDP-002, ORT-SPDP-003,
// Verifies: ORT-SPDP-004, ORT-SPDP-005, ORT-SPDP-006

namespace {

[[nodiscard]] openrtdds::rtps::SpdpAnnouncementConfig announcement(
    const std::uint8_t seed,
    const openrtdds::serialization::ByteOrder parameter_order =
        openrtdds::serialization::ByteOrder::little_endian) {
  using openrtdds::rtps::Locator;
  openrtdds::rtps::SpdpAnnouncementConfig config{};
  config.participant.domain_id = 7U;
  config.participant.vendor_id.value = {{0x12U, 0x34U}};
  for (std::size_t index = 0U;
       index < config.participant.guid_prefix.value.size(); ++index) {
    config.participant.guid_prefix.value[index] =
        static_cast<std::uint8_t>(seed + index);
  }
  Locator locator{};
  CHECK(openrtdds::rtps::make_udp_v4_locator(
      {{127U, 0U, 0U, 1U}}, 9160U, locator));
  CHECK(config.participant.metatraffic_unicast.push_back(locator));
  locator.port = 9161U;
  CHECK(config.participant.default_unicast.push_back(locator));
  locator.port = 9150U;
  locator.address[12] = 239U;
  locator.address[13] = 255U;
  locator.address[14] = 0U;
  locator.address[15] = 1U;
  CHECK(config.participant.metatraffic_multicast.push_back(locator));
  config.participant.lease_duration_ns = 2'500'000'000ULL;
  constexpr char name[] = "openrtdds-test";
  config.participant.entity_name_size = sizeof(name) - 1U;
  std::memcpy(config.participant.entity_name.data(), name, sizeof(name) - 1U);
  config.sequence_number = 7U;
  config.parameter_byte_order = parameter_order;
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

void test_port_mapping_and_locators() {
  std::uint16_t port = 0U;
  CHECK(openrtdds::rtps::spdp_multicast_port(
      7U, openrtdds::rtps::SpdpPortConfig{}, port));
  CHECK(port == 9150U);
  CHECK(openrtdds::rtps::spdp_unicast_port(
      7U, 0U, openrtdds::rtps::SpdpPortConfig{}, port));
  CHECK(port == 9160U);
  CHECK(!openrtdds::rtps::spdp_multicast_port(
      1000U, openrtdds::rtps::SpdpPortConfig{}, port));
  openrtdds::rtps::SpdpPortConfig overflow{};
  overflow.port_base = 0xFFFFFFFFU;
  overflow.multicast_offset = 0xFFFFFFFFU;
  CHECK(!openrtdds::rtps::spdp_multicast_port(0xFFFFFFFFU, overflow, port));
  CHECK(!openrtdds::rtps::spdp_unicast_port(
      0xFFFFFFFFU, 0xFFFFFFFFU, overflow, port));

  openrtdds::rtps::Locator locator{};
  CHECK(openrtdds::rtps::make_udp_v4_locator(
      {{192U, 168U, 1U, 10U}}, 7400U, locator));
  CHECK(openrtdds::rtps::valid_udp_v4_locator(locator));
  locator.address[0] = 1U;
  CHECK(!openrtdds::rtps::valid_udp_v4_locator(locator));
}

void test_roundtrip(const openrtdds::serialization::ByteOrder order) {
  auto config = announcement(0x10U, order);
  config.submessage_byte_order = order;
  std::array<std::uint8_t, 1024U> message{};
  openrtdds::rtps::SpdpMessageBuilder builder(message.data(), message.size());
  CHECK(builder.build(config));
  CHECK(builder.size() > 100U);

  openrtdds::rtps::SpdpMessageView view{};
  const auto result = openrtdds::rtps::parse_spdp_message(
      message.data(), builder.size(), 7U, view);
  CHECK(result.ok());
  CHECK(result.bytes == builder.size());
  CHECK(view.sequence_number == 7U);
  CHECK(view.participant.guid_prefix.value ==
        config.participant.guid_prefix.value);
  CHECK(view.participant.domain_id == 7U);
  CHECK(view.participant.metatraffic_unicast.size == 1U);
  CHECK(view.participant.metatraffic_multicast.size == 1U);
  CHECK(view.participant.default_unicast.size == 1U);
  CHECK(view.participant.lease_duration_ns >= 2'499'999'999ULL);
  CHECK(view.participant.entity_name_size ==
        config.participant.entity_name_size);
}

void test_defensive_parsing() {
  using openrtdds::rtps::SpdpError;
  auto config = announcement(0x20U);
  std::array<std::uint8_t, 1024U> message{};
  openrtdds::rtps::SpdpMessageBuilder builder(message.data(), message.size());
  CHECK(builder.build(config));
  const std::size_t bytes = builder.size();
  openrtdds::rtps::SpdpMessageView prior{};
  prior.sequence_number = 99U;

  CHECK(openrtdds::rtps::parse_spdp_message(
            message.data(), bytes - 1U, 7U, prior)
            .error != SpdpError::none);
  CHECK(prior.sequence_number == 99U);
  CHECK(openrtdds::rtps::parse_spdp_message(
            message.data(), bytes, 8U, prior)
            .error == SpdpError::domain_mismatch);

  auto corrupted = message;
  const std::size_t domain = find_parameter(
      corrupted.data(), bytes, 0x000FU);
  CHECK(domain < bytes);
  corrupted[domain] = 0x0FU;
  corrupted[domain + 1U] = 0x40U;
  CHECK(openrtdds::rtps::parse_spdp_message(
            corrupted.data(), bytes, 7U, prior)
            .error == SpdpError::unknown_required_parameter);

  corrupted = message;
  corrupted[8U] ^= 0x80U;
  CHECK(openrtdds::rtps::parse_spdp_message(
            corrupted.data(), bytes, 7U, prior)
            .error == SpdpError::invalid_identity);

  config.participant.default_unicast.size = 0U;
  CHECK(!builder.build(config));
  CHECK(builder.error() == SpdpError::invalid_configuration);
}

void test_bounded_table_and_expiration() {
  using Table = openrtdds::rtps::DiscoveredParticipantTable<1U>;
  using openrtdds::rtps::ParticipantUpdate;
  using openrtdds::rtps::SpdpError;
  auto first = announcement(0x30U);
  first.participant.lease_duration_ns = 1'000U;
  std::array<std::uint8_t, 1024U> message{};
  openrtdds::rtps::SpdpMessageBuilder builder(message.data(), message.size());
  CHECK(builder.build(first));
  openrtdds::rtps::SpdpMessageView view{};
  CHECK(openrtdds::rtps::parse_spdp_message(
            message.data(), builder.size(), 7U, view).ok());

  openrtdds::rtps::GuidPrefix local{};
  local.value[0] = 0xF0U;
  Table table(local);
  auto update = table.upsert(view, 1'000U);
  CHECK(update.ok());
  CHECK(update.update == ParticipantUpdate::added);
  CHECK(table.size() == 1U);
  update = table.upsert(view, 1'050U);
  CHECK(update.ok());
  CHECK(update.update == ParticipantUpdate::refreshed);

  auto second = announcement(0x60U);
  CHECK(builder.build(second));
  openrtdds::rtps::SpdpMessageView second_view{};
  CHECK(openrtdds::rtps::parse_spdp_message(
            message.data(), builder.size(), 7U, second_view).ok());
  CHECK(table.upsert(second_view, 1'060U).error == SpdpError::table_full);

  view.sequence_number = 6U;
  CHECK(table.upsert(view, 1'070U).error == SpdpError::stale_announcement);
  std::array<openrtdds::rtps::GuidPrefix, 1U> expired{};
  std::size_t expired_size = 0U;
  CHECK(table.expire(2'048U, expired.data(), expired.size(), expired_size) ==
        SpdpError::none);
  CHECK(expired_size == 0U);
  CHECK(table.expire(2'049U, expired.data(), expired.size(), expired_size) ==
        SpdpError::none);
  CHECK(expired_size == 1U);
  CHECK(table.size() == 0U);
}

void test_static_properties() {
  using Table = openrtdds::rtps::DiscoveredParticipantTable<8U>;
  static_assert(std::is_nothrow_destructible<Table>::value,
                "SPDP table destruction must not throw");
  CHECK(sizeof(Table) < 8192U);
}

}  // namespace

void test_spdp() {
  test_port_mapping_and_locators();
  test_roundtrip(openrtdds::serialization::ByteOrder::little_endian);
  test_roundtrip(openrtdds::serialization::ByteOrder::big_endian);
  test_defensive_parsing();
  test_bounded_table_and_expiration();
  test_static_properties();
}
