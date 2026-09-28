#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/rtps/reliability_messages.hpp"
#include "openrtdds/rtps/sedp.hpp"
#include "openrtdds/rtps/spdp.hpp"
#include "openrtdds/serialization/cdr.hpp"
#include "openrtdds/transport/udp_socket.hpp"

// Requirements: ORT-INT-008, ORT-UDP-006
// Demonstrates: ORT-INT-008, ORT-UDP-006

namespace {

using openrtdds::rtps::EntityId;
using openrtdds::rtps::GuidPrefix;
using openrtdds::rtps::Locator;
using openrtdds::transport::Ipv4Address;
using openrtdds::transport::UdpEndpoint;
using openrtdds::transport::UdpError;
using openrtdds::transport::UdpSocket;

constexpr std::uint32_t domain_id = 43U;
constexpr std::uint32_t participant_id = 5U;
constexpr std::uint32_t sample_value = 0x4F525444U;
constexpr Ipv4Address loopback{{{127U, 0U, 0U, 1U}}};
constexpr Ipv4Address spdp_group{{{239U, 255U, 0U, 1U}}};
constexpr EntityId user_writer{{0U, 0U, 1U, 0x03U}};
constexpr EntityId unknown_reader{{0U, 0U, 0U, 0x00U}};
constexpr EntityId publications_reader{{0U, 0U, 3U, 0xC7U}};
constexpr EntityId publications_writer{{0U, 0U, 3U, 0xC2U}};
constexpr EntityId subscriptions_reader{{0U, 0U, 4U, 0xC7U}};
constexpr EntityId subscriptions_writer{{0U, 0U, 4U, 0xC2U}};
constexpr char topic_name[] = "OpenRTDDSProbe";
constexpr char type_name[] = "VendorProbe";

[[nodiscard]] GuidPrefix local_prefix() noexcept {
  GuidPrefix prefix{};
  prefix.value = {{0x4FU, 0x52U, 0x54U, 0x44U, 0x44U, 0x53U,
                   0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x12U}};
  return prefix;
}

[[nodiscard]] bool to_endpoint(const Locator& locator,
                               UdpEndpoint& endpoint) noexcept {
  if (!openrtdds::rtps::valid_udp_v4_locator(locator)) {
    return false;
  }
  endpoint.address.octets = {{locator.address[12], locator.address[13],
                              locator.address[14], locator.address[15]}};
  endpoint.port = static_cast<std::uint16_t>(locator.port);
  return true;
}

template <std::size_t Capacity>
[[nodiscard]] bool first_endpoint(
    const openrtdds::rtps::BoundedLocatorList<Capacity>& locators,
    UdpEndpoint& endpoint) noexcept {
  for (std::size_t index = 0U; index < locators.size; ++index) {
    if (to_endpoint(locators[index], endpoint)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool receive_one(
    UdpSocket& socket, std::array<std::uint8_t, 2048U>& datagram,
    std::size_t& size) noexcept {
  UdpEndpoint source{};
  const auto result = socket.receive_from(
      datagram.data(), datagram.size(), source);
  if (result.ok()) {
    size = result.bytes;
    return true;
  }
  if (result.error != UdpError::would_block) {
    std::cerr << "receive failed: "
              << openrtdds::transport::to_string(result.error) << '\n';
  }
  size = 0U;
  return false;
}

}  // namespace

int main() {
  using namespace openrtdds::rtps;
  using openrtdds::serialization::ByteOrder;

  std::uint16_t multicast_port = 0U;
  std::uint16_t metadata_port = 0U;
  if (!spdp_multicast_port(domain_id, SpdpPortConfig{}, multicast_port) ||
      !spdp_unicast_port(domain_id, participant_id, SpdpPortConfig{},
                         metadata_port) ||
      metadata_port == 65'535U) {
    std::cerr << "invalid DDSI port mapping\n";
    return 1;
  }
  const std::uint16_t user_port =
      static_cast<std::uint16_t>(metadata_port + 1U);

  UdpSocket multicast_socket;
  UdpSocket metadata_socket;
  UdpSocket user_socket;
  if (!multicast_socket.open().ok() ||
      !multicast_socket.enable_address_reuse().ok() ||
      !multicast_socket.bind({Ipv4Address::any(), multicast_port}).ok() ||
      !multicast_socket.join_multicast(spdp_group).ok() ||
      !metadata_socket.open().ok() ||
      !metadata_socket.bind({Ipv4Address::any(), metadata_port}).ok() ||
      !user_socket.open().ok() ||
      !user_socket.bind({Ipv4Address::any(), user_port}).ok()) {
    std::cerr << "OpenRTDDS discovery socket setup failed\n";
    return 2;
  }

  const GuidPrefix prefix = local_prefix();
  VendorId vendor{};
  vendor.value = {{0x01U, 0x42U}};
  Locator metadata_locator{};
  Locator user_locator{};
  Locator multicast_locator{};
  if (!make_udp_v4_locator(loopback.octets, metadata_port,
                           metadata_locator) ||
      !make_udp_v4_locator(loopback.octets, user_port, user_locator) ||
      !make_udp_v4_locator(spdp_group.octets, multicast_port,
                           multicast_locator)) {
    return 3;
  }

  SpdpAnnouncementConfig participant{};
  participant.participant.protocol_version = {2U, 3U};
  participant.participant.vendor_id = vendor;
  participant.participant.guid_prefix = prefix;
  participant.participant.domain_id = domain_id;
  participant.participant.available_builtin_endpoints =
      spdp_endpoint_participant_announcer |
      spdp_endpoint_participant_detector |
      spdp_endpoint_publications_announcer |
      spdp_endpoint_subscriptions_detector;
  participant.participant.lease_duration_ns = 20'000'000'000ULL;
  static_cast<void>(participant.participant.metatraffic_unicast.push_back(
      metadata_locator));
  static_cast<void>(participant.participant.metatraffic_multicast.push_back(
      multicast_locator));
  static_cast<void>(participant.participant.default_unicast.push_back(
      user_locator));
  constexpr char participant_name[] = "openrtdds-pr18";
  participant.participant.entity_name_size = sizeof(participant_name) - 1U;
  std::memcpy(participant.participant.entity_name.data(), participant_name,
              participant.participant.entity_name_size);

  SedpAnnouncementConfig publication{};
  publication.endpoint.protocol_version = {2U, 3U};
  publication.endpoint.vendor_id = vendor;
  publication.endpoint.participant_guid_prefix = prefix;
  publication.endpoint.endpoint_id = user_writer;
  publication.endpoint.kind = EndpointKind::writer;
  publication.endpoint.topic_name_size = sizeof(topic_name) - 1U;
  publication.endpoint.type_name_size = sizeof(type_name) - 1U;
  std::memcpy(publication.endpoint.topic_name.data(), topic_name,
              publication.endpoint.topic_name_size);
  std::memcpy(publication.endpoint.type_name.data(), type_name,
              publication.endpoint.type_name_size);
  publication.endpoint.reliability = ReliabilityKind::best_effort;
  publication.endpoint.durability = DurabilityKind::volatile_durability;
  static_cast<void>(publication.endpoint.unicast_locators.push_back(
      user_locator));

  std::array<std::uint8_t, 1600U> spdp_bytes{};
  std::array<std::uint8_t, 1600U> sedp_bytes{};
  std::array<std::uint8_t, 128U> heartbeat_bytes{};
  SpdpMessageBuilder spdp(spdp_bytes.data(), spdp_bytes.size());
  SedpMessageBuilder sedp(sedp_bytes.data(), sedp_bytes.size());
  ReliabilityMessageBuilder heartbeat(
      heartbeat_bytes.data(), heartbeat_bytes.size());
  HeartbeatConfig heartbeat_config{};
  heartbeat_config.header.version = {2U, 3U};
  heartbeat_config.header.vendor_id = vendor;
  heartbeat_config.header.guid_prefix = prefix;
  heartbeat_config.reader_id = publications_reader;
  heartbeat_config.writer_id = publications_writer;
  heartbeat_config.first_sequence_number = 1U;
  heartbeat_config.last_sequence_number = 1U;
  heartbeat_config.count = 1;
  if (!spdp.build(participant) || !sedp.build(publication) ||
      !heartbeat.build_heartbeat(heartbeat_config)) {
    std::cerr << "OpenRTDDS discovery message construction failed\n";
    return 4;
  }

  const UdpEndpoint spdp_destination{spdp_group, multicast_port};
  LocalEndpointDescriptor local_writer{};
  local_writer.kind = EndpointKind::writer;
  local_writer.topic_name = topic_name;
  local_writer.topic_name_size = sizeof(topic_name) - 1U;
  local_writer.type_name = type_name;
  local_writer.type_name_size = sizeof(type_name) - 1U;
  local_writer.reliability = ReliabilityKind::best_effort;
  local_writer.durability = DurabilityKind::volatile_durability;

  SpdpMessageView remote_participant{};
  SedpMessageView remote_reader{};
  bool participant_found = false;
  bool reader_found = false;
  UdpEndpoint remote_metadata{};
  UdpEndpoint remote_user{};
  std::int32_t acknack_count = 1;
  auto next_announcement = std::chrono::steady_clock::time_point::min();
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(10);
  std::array<std::uint8_t, 2048U> incoming{};
  while (std::chrono::steady_clock::now() < deadline && !reader_found) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_announcement) {
      if (!metadata_socket.send_to(spdp_destination, spdp.data(), spdp.size())
               .ok()) {
        std::cerr << "SPDP send failed\n";
        return 5;
      }
      if (participant_found) {
        if (!metadata_socket.send_to(remote_metadata, sedp.data(), sedp.size())
                 .ok() ||
            !metadata_socket.send_to(remote_metadata, heartbeat.data(),
                                     heartbeat.size()).ok()) {
          std::cerr << "SEDP send failed\n";
          return 6;
        }
      }
      next_announcement = now + std::chrono::milliseconds(250);
    }

    for (UdpSocket* socket : {&multicast_socket, &metadata_socket}) {
      std::size_t incoming_size = 0U;
      if (!receive_one(*socket, incoming, incoming_size)) {
        continue;
      }
      SpdpMessageView discovered{};
      if (parse_spdp_message(incoming.data(), incoming_size, domain_id,
                             discovered).ok() &&
          discovered.participant.guid_prefix.value != prefix.value &&
          first_endpoint(discovered.participant.metatraffic_unicast,
                         remote_metadata)) {
        remote_participant = discovered;
        participant_found = true;
        continue;
      }
      if (!participant_found || incoming_size < 20U ||
          std::memcmp(&incoming[8],
                      remote_participant.participant.guid_prefix.value.data(),
                      remote_participant.participant.guid_prefix.value.size()) !=
              0) {
        continue;
      }
      HeartbeatView remote_heartbeat{};
      if (parse_heartbeat_message(incoming.data(), incoming_size,
                                  remote_heartbeat) ==
              ReliabilityMessageError::none &&
          remote_heartbeat.writer_id == subscriptions_writer &&
          (remote_heartbeat.reader_id == unknown_reader ||
           remote_heartbeat.reader_id == subscriptions_reader) &&
          remote_heartbeat.last_sequence_number >=
              remote_heartbeat.first_sequence_number) {
        const std::uint64_t range =
            remote_heartbeat.last_sequence_number -
            remote_heartbeat.first_sequence_number + 1U;
        if (range <= SequenceNumberSet::maximum_bits) {
          AckNackConfig acknack_config{};
          acknack_config.header.version = {2U, 3U};
          acknack_config.header.vendor_id = vendor;
          acknack_config.header.guid_prefix = prefix;
          acknack_config.reader_id = subscriptions_reader;
          acknack_config.writer_id = subscriptions_writer;
          acknack_config.count = acknack_count++;
          const auto bit_count = static_cast<std::uint32_t>(range);
          bool complete = acknack_config.reader_state.reset(
              remote_heartbeat.first_sequence_number, bit_count);
          for (std::uint32_t bit = 0U; complete && bit < bit_count; ++bit) {
            complete = acknack_config.reader_state.set(bit);
          }
          std::array<std::uint8_t, 128U> acknack_bytes{};
          ReliabilityMessageBuilder acknack(
              acknack_bytes.data(), acknack_bytes.size());
          if (!complete || !acknack.build_acknack(acknack_config) ||
              !metadata_socket.send_to(remote_metadata, acknack.data(),
                                       acknack.size()).ok()) {
            std::cerr << "SEDP ACKNACK send failed\n";
            return 6;
          }
        }
        continue;
      }
      SedpMessageView endpoint{};
      const auto parsed = parse_sedp_message(
          incoming.data(), incoming_size,
          remote_participant.participant.guid_prefix, endpoint);
      if (!parsed.ok() || endpoint.endpoint.kind != EndpointKind::reader ||
          evaluate_endpoint_match(local_writer, endpoint.endpoint) !=
              MatchStatus::matched) {
        continue;
      }
      remote_reader = endpoint;
      reader_found = first_endpoint(endpoint.endpoint.unicast_locators,
                                    remote_user) ||
          first_endpoint(remote_participant.participant.default_unicast,
                         remote_user);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  if (!participant_found) {
    std::cerr << "vendor participant discovery timed out\n";
    return 7;
  }
  if (!reader_found) {
    std::cerr << "vendor reader discovery timed out\n";
    return 8;
  }

  std::array<std::uint8_t, 16U> payload{};
  openrtdds::serialization::CdrWriter cdr(payload.data(), payload.size());
  if (!cdr.begin(ByteOrder::little_endian) ||
      !cdr.write_uint32(sample_value)) {
    return 9;
  }
  DataMessageConfig data_config{};
  data_config.version = {2U, 3U};
  data_config.vendor_id = vendor;
  data_config.guid_prefix = prefix;
  data_config.reader_id = remote_reader.endpoint.endpoint_id;
  data_config.writer_id = user_writer;
  data_config.sequence_number = 1U;
  std::array<std::uint8_t, 256U> data_bytes{};
  DataMessageBuilder data(data_bytes.data(), data_bytes.size());
  if (!data.build(data_config, payload.data(), cdr.size())) {
    return 10;
  }
  for (unsigned attempt = 0U; attempt < 4U; ++attempt) {
    if (!user_socket.send_to(remote_user, data.data(), data.size()).ok()) {
      std::cerr << "user DATA send failed\n";
      return 11;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::cout << "OpenRTDDS delivered best-effort value=" << sample_value
            << " reader="
            << static_cast<unsigned>(remote_reader.endpoint.endpoint_id.value[2])
            << ':'
            << static_cast<unsigned>(remote_reader.endpoint.endpoint_id.value[3])
            << '\n';
  return 0;
}
