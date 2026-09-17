#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "openrtdds/rtps/spdp.hpp"
#include "openrtdds/transport/udp_socket.hpp"

// Demonstrates: ORT-SPDP-001, ORT-SPDP-002, ORT-SPDP-004,
// Demonstrates: ORT-SPDP-005, ORT-SPDP-006

int main() {
  using openrtdds::rtps::Locator;
  using openrtdds::transport::Ipv4Address;
  using openrtdds::transport::UdpEndpoint;
  using openrtdds::transport::UdpSocket;

  openrtdds::rtps::SpdpAnnouncementConfig config{};
  config.participant.domain_id = 7U;
  config.participant.vendor_id.value = {{0x12U, 0x34U}};
  for (std::size_t index = 0U;
       index < config.participant.guid_prefix.value.size(); ++index) {
    config.participant.guid_prefix.value[index] =
        static_cast<std::uint8_t>(0x10U + index);
  }
  std::uint16_t meta_port = 0U;
  if (!openrtdds::rtps::spdp_unicast_port(
          config.participant.domain_id, 0U,
          openrtdds::rtps::SpdpPortConfig{}, meta_port)) {
    return 1;
  }
  Locator locator{};
  if (!openrtdds::rtps::make_udp_v4_locator(
          {{127U, 0U, 0U, 1U}}, meta_port, locator) ||
      !config.participant.metatraffic_unicast.push_back(locator)) {
    return 2;
  }
  locator.port = static_cast<std::uint32_t>(meta_port + 1U);
  if (!config.participant.default_unicast.push_back(locator)) {
    return 3;
  }
  constexpr char name[] = "openrtdds-participant";
  config.participant.entity_name_size = sizeof(name) - 1U;
  std::memcpy(config.participant.entity_name.data(), name, sizeof(name) - 1U);
  config.participant.lease_duration_ns = 1'000U;

  UdpSocket receiver;
  UdpSocket sender;
  if (!receiver.open().ok() ||
      !receiver.bind({Ipv4Address::loopback(), 0U}).ok() ||
      !sender.open().ok()) {
    return 4;
  }
  UdpEndpoint endpoint{};
  if (!receiver.local_endpoint(endpoint).ok()) {
    return 5;
  }

  std::array<std::uint8_t, 1024U> transmit{};
  openrtdds::rtps::SpdpMessageBuilder builder(
      transmit.data(), transmit.size());
  if (!builder.build(config) ||
      !sender.send_to(endpoint, transmit.data(), builder.size()).ok()) {
    return 6;
  }

  std::array<std::uint8_t, 1024U> receive{};
  UdpEndpoint remote{};
  const auto received = receiver.receive_from(
      receive.data(), receive.size(), remote);
  openrtdds::rtps::SpdpMessageView view{};
  if (!received.ok() ||
      !openrtdds::rtps::parse_spdp_message(
           receive.data(), received.bytes, 7U, view).ok()) {
    return 7;
  }

  openrtdds::rtps::GuidPrefix local{};
  local.value[0] = 0xF0U;
  openrtdds::rtps::DiscoveredParticipantTable<4U> table(local);
  if (!table.upsert(view, 10U).ok() || (table.size() != 1U)) {
    return 8;
  }
  std::array<openrtdds::rtps::GuidPrefix, 4U> expired{};
  std::size_t expired_size = 0U;
  if ((table.expire(1'010U, expired.data(), expired.size(), expired_size) !=
       openrtdds::rtps::SpdpError::none) ||
      (expired_size != 1U) || (table.size() != 0U)) {
    return 9;
  }

  std::cout << "SPDP discovered and expired participant sequence="
            << view.sequence_number << " port=" << meta_port << '\n';
  return 0;
}
