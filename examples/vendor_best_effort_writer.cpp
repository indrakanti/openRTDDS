#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <thread>

#include <arpa/inet.h>

#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/rtps/message_router.hpp"
#include "openrtdds/rtps/reliability_messages.hpp"
#include "openrtdds/rtps/reliability_state.hpp"
#include "openrtdds/rtps/sedp.hpp"
#include "openrtdds/rtps/spdp.hpp"
#include "openrtdds/serialization/cdr.hpp"
#include "openrtdds/transport/udp_socket.hpp"

// Requirements: ORT-INT-008, ORT-INT-010, ORT-UDP-006
// Demonstrates: ORT-INT-008, ORT-INT-010, ORT-UDP-006

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

[[nodiscard]] bool parse_address(const char* const text,
                                 Ipv4Address& address) noexcept {
  if (text == nullptr) {
    return false;
  }
  in_addr native{};
  if (::inet_pton(AF_INET, text, &native) != 1) {
    return false;
  }
  std::memcpy(address.octets.data(), &native.s_addr,
              address.octets.size());
  return !(address == Ipv4Address::any());
}

void write_u16_le(std::uint8_t* const output,
                  const std::uint16_t value) noexcept {
  output[0] = static_cast<std::uint8_t>(value & 0xFFU);
  output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

// ORT-INT-008: reliable built-in control traffic is explicitly directed to
// the discovered participant. Some vendors intentionally reject an ACKNACK
// with an unknown destination prefix even when its writer EntityId matches.
template <std::size_t Capacity>
[[nodiscard]] bool direct_message(
    const std::uint8_t* const message, const std::size_t message_size,
    const GuidPrefix& destination, std::array<std::uint8_t, Capacity>& output,
    std::size_t& output_size) noexcept {
  constexpr std::size_t rtps_header_size = 20U;
  constexpr std::size_t info_destination_size = 16U;
  if (message == nullptr || message_size < rtps_header_size ||
      message_size > Capacity - info_destination_size) {
    output_size = 0U;
    return false;
  }

  std::memcpy(output.data(), message, rtps_header_size);
  output[20] = openrtdds::rtps::submessage_id_info_destination;
  output[21] = 0x01U;
  write_u16_le(&output[22], 12U);
  std::memcpy(&output[24], destination.value.data(),
              destination.value.size());
  std::memcpy(&output[36], &message[rtps_header_size],
              message_size - rtps_header_size);
  output_size = message_size + info_destination_size;
  return true;
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

[[nodiscard]] std::uint64_t monotonic_now_ns() noexcept {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace

int main(const int argc, char** const argv) {
  using namespace openrtdds::rtps;
  using openrtdds::serialization::ByteOrder;

  Ipv4Address local_address = loopback;
  bool address_set = false;
  bool reliable = false;
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--reliable") == 0) {
      reliable = true;
    } else if (!address_set && parse_address(argv[index], local_address)) {
      address_set = true;
    } else {
      std::cerr << "usage: openrtdds_vendor_best_effort_writer "
                   "[--reliable] [local-ipv4]\n";
      return 1;
    }
  }

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
      !multicast_socket.join_multicast(spdp_group, local_address).ok() ||
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
  if (!make_udp_v4_locator(local_address.octets, metadata_port,
                           metadata_locator) ||
      !make_udp_v4_locator(local_address.octets, user_port, user_locator) ||
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
  publication.endpoint.reliability = reliable
      ? ReliabilityKind::reliable : ReliabilityKind::best_effort;
  publication.endpoint.durability = DurabilityKind::volatile_durability;
  publication.reader_id = publications_reader;
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
  if (!spdp.build(participant) || !sedp.build(publication)) {
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
  local_writer.reliability = reliable
      ? ReliabilityKind::reliable : ReliabilityKind::best_effort;
  local_writer.durability = DurabilityKind::volatile_durability;

  SpdpMessageView remote_participant{};
  SedpMessageView remote_reader{};
  bool participant_found = false;
  bool reader_found = false;
  bool publication_requested = false;
  UdpEndpoint remote_metadata{};
  UdpEndpoint remote_user{};
  std::int32_t acknack_count = 1;
  std::int32_t heartbeat_count = 1;
  auto next_announcement = std::chrono::steady_clock::time_point::min();
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(10);
  std::array<std::uint8_t, 2048U> incoming{};
  while (std::chrono::steady_clock::now() < deadline &&
         (!reader_found || !publication_requested)) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_announcement) {
      if (!metadata_socket.send_to(spdp_destination, spdp.data(), spdp.size())
               .ok()) {
        std::cerr << "SPDP send failed\n";
        return 5;
      }
      if (participant_found) {
        heartbeat_config.count = heartbeat_count++;
        if (!heartbeat.build_heartbeat(heartbeat_config) ||
            !metadata_socket.send_to(remote_metadata, sedp.data(), sedp.size())
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
      AckNackView remote_acknack{};
      if (parse_acknack_message(incoming.data(), incoming_size,
                                remote_acknack) ==
              ReliabilityMessageError::none &&
          remote_acknack.reader_id == publications_reader &&
          remote_acknack.writer_id == publications_writer) {
        if (!metadata_socket.send_to(remote_metadata, sedp.data(), sedp.size())
                 .ok()) {
          std::cerr << "SEDP repair send failed\n";
          return 6;
        }
        publication_requested = true;
        continue;
      }
      SedpMessageView endpoint{};
      const auto parsed = parse_sedp_message(
          incoming.data(), incoming_size,
          remote_participant.participant.guid_prefix, endpoint);
      if (parsed.ok() && endpoint.endpoint.kind == EndpointKind::reader &&
          evaluate_endpoint_match(local_writer, endpoint.endpoint) ==
              MatchStatus::matched) {
        remote_reader = endpoint;
        reader_found = first_endpoint(endpoint.endpoint.unicast_locators,
                                      remote_user) ||
            first_endpoint(remote_participant.participant.default_unicast,
                           remote_user);
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
          std::array<std::uint8_t, 144U> directed_acknack{};
          std::size_t directed_acknack_size = 0U;
          ReliabilityMessageBuilder acknack(
              acknack_bytes.data(), acknack_bytes.size());
          if (!complete || !acknack.build_acknack(acknack_config) ||
              !direct_message(
                  acknack.data(), acknack.size(),
                  remote_participant.participant.guid_prefix,
                  directed_acknack, directed_acknack_size) ||
              !metadata_socket.send_to(remote_metadata,
                                       directed_acknack.data(),
                                       directed_acknack_size).ok()) {
            std::cerr << "SEDP ACKNACK send failed\n";
            return 6;
          }
        }
        continue;
      }
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
  if (!publication_requested) {
    std::cerr << "vendor publications reader handshake timed out\n";
    return 12;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

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
  if (!reliable) {
    for (unsigned attempt = 0U; attempt < 4U; ++attempt) {
      if (!user_socket.send_to(remote_user, data.data(), data.size()).ok()) {
        std::cerr << "user DATA send failed\n";
        return 11;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::cout << "OpenRTDDS delivered best-effort value=" << sample_value
              << " reader="
              << static_cast<unsigned>(
                     remote_reader.endpoint.endpoint_id.value[2])
              << ':'
              << static_cast<unsigned>(
                     remote_reader.endpoint.endpoint_id.value[3])
              << '\n';
    return 0;
  }

  ReliableWriterConfig reliable_config{};
  reliable_config.reader_id = remote_reader.endpoint.endpoint_id;
  reliable_config.writer_id = user_writer;
  reliable_config.max_repair_attempts = 2U;
  reliable_config.repair_window_ns = 3'000'000'000ULL;
  ReliableWriter<1U, 256U> reliable_writer(reliable_config);
  ReliabilityActionBuffer<4U> actions;
  const auto write_error = reliable_writer.write(
      data.data(), data.size(), 1U, monotonic_now_ns(), actions);
  if (write_error != ReliabilityError::none || actions.size() != 1U ||
      actions[0U].kind != ReliabilityActionKind::send_data ||
      !user_socket.send_to(remote_user, actions[0U].data,
                           actions[0U].data_size).ok()) {
    std::cerr << "reliable DATA initial send failed: "
              << to_string(write_error) << '\n';
    return 11;
  }

  HeartbeatConfig user_heartbeat{};
  user_heartbeat.header.version = {2U, 3U};
  user_heartbeat.header.vendor_id = vendor;
  user_heartbeat.header.guid_prefix = prefix;
  user_heartbeat.final_flag = false;
  std::array<std::uint8_t, 128U> user_heartbeat_bytes{};
  std::array<std::uint8_t, 144U> directed_heartbeat{};
  ReliabilityMessageBuilder user_heartbeat_builder(
      user_heartbeat_bytes.data(), user_heartbeat_bytes.size());

  bool delivered = false;
  bool terminal_failure = false;
  auto next_user_heartbeat = std::chrono::steady_clock::time_point::min();
  const auto reliable_deadline = std::chrono::steady_clock::now() +
                                 std::chrono::seconds(4);
  while (std::chrono::steady_clock::now() < reliable_deadline &&
         !delivered && !terminal_failure) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_user_heartbeat) {
      std::size_t directed_size = 0U;
      if (reliable_writer.fill_heartbeat(user_heartbeat) !=
              ReliabilityError::none ||
          !user_heartbeat_builder.build_heartbeat(user_heartbeat) ||
          !direct_message(user_heartbeat_builder.data(),
                          user_heartbeat_builder.size(),
                          remote_participant.participant.guid_prefix,
                          directed_heartbeat, directed_size) ||
          !user_socket.send_to(remote_user, directed_heartbeat.data(),
                               directed_size).ok()) {
        std::cerr << "reliable HEARTBEAT send failed\n";
        return 13;
      }
      next_user_heartbeat = now + std::chrono::milliseconds(200);
    }

    std::size_t incoming_size = 0U;
    if (receive_one(user_socket, incoming, incoming_size)) {
      AckNackView acknack{};
      const auto parse_error = parse_acknack_message(
          incoming.data(), incoming_size, acknack);
      if (parse_error == ReliabilityMessageError::none &&
          acknack.header.guid_prefix.value ==
              remote_participant.participant.guid_prefix.value &&
          acknack.reader_id == remote_reader.endpoint.endpoint_id &&
          acknack.writer_id == user_writer) {
        actions.clear();
        const auto state_error = reliable_writer.on_acknack(
            acknack, monotonic_now_ns(), actions);
        if (state_error != ReliabilityError::none &&
            state_error != ReliabilityError::stale_control) {
          std::cerr << "reliable ACKNACK state failed: "
                    << to_string(state_error) << '\n';
          return 14;
        }
        for (std::size_t action_index = 0U;
             action_index < actions.size(); ++action_index) {
          const auto& action = actions[action_index];
          if (action.kind == ReliabilityActionKind::retransmit_data) {
            if (!user_socket.send_to(remote_user, action.data,
                                     action.data_size).ok()) {
              std::cerr << "reliable DATA repair send failed\n";
              return 11;
            }
            next_user_heartbeat =
                std::chrono::steady_clock::time_point::min();
          } else if (action.kind ==
                     ReliabilityActionKind::sample_delivered) {
            delivered = true;
          } else if (action.kind == ReliabilityActionKind::sample_failed) {
            terminal_failure = true;
          }
        }
      }
    }

    actions.clear();
    const auto timer_error = reliable_writer.on_timer(
        monotonic_now_ns(), actions);
    if (timer_error != ReliabilityError::none) {
      std::cerr << "reliable timer state failed: "
                << to_string(timer_error) << '\n';
      return 14;
    }
    for (std::size_t action_index = 0U;
         action_index < actions.size(); ++action_index) {
      if (actions[action_index].kind ==
          ReliabilityActionKind::sample_failed) {
        terminal_failure = true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (terminal_failure) {
    std::cerr << "reliable DATA repair bound exhausted\n";
    return 15;
  }
  if (!delivered) {
    std::cerr << "reliable DATA acknowledgment timed out\n";
    return 16;
  }

  std::cout << "OpenRTDDS delivered reliable value=" << sample_value
            << " reader="
            << static_cast<unsigned>(
                   remote_reader.endpoint.endpoint_id.value[2])
            << ':'
            << static_cast<unsigned>(
                   remote_reader.endpoint.endpoint_id.value[3])
            << '\n';
  return 0;
}
