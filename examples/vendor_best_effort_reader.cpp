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

// Requirements: ORT-INT-009, ORT-INT-011, ORT-UDP-006
// Demonstrates: ORT-INT-009, ORT-INT-011, ORT-UDP-006

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
constexpr EntityId user_reader{{0U, 0U, 1U, 0x04U}};
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
                   0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x13U}};
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

// ORT-INT-009: direct reliable discovery control to the participant that owns
// the publications writer; a matching EntityId alone is not a full RTPS GUID.
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

[[nodiscard]] bool receive_one(
    UdpSocket& socket, std::array<std::uint8_t, 2048U>& datagram,
    std::size_t& size) noexcept {
  UdpEndpoint source{};
  const auto result = socket.receive_from(datagram.data(), datagram.size(),
                                          source);
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

[[nodiscard]] bool same_prefix(const GuidPrefix& left,
                               const GuidPrefix& right) noexcept {
  return left.value == right.value;
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

}  // namespace

int main(const int argc, char** const argv) {
  using namespace openrtdds::rtps;

  Ipv4Address local_address = loopback;
  bool address_set = false;
  bool reliable = false;
  for (int index = 1; index < argc; ++index) {
    if (std::strcmp(argv[index], "--reliable") == 0) {
      reliable = true;
    } else if (!address_set && parse_address(argv[index], local_address)) {
      address_set = true;
    } else {
      std::cerr << "usage: openrtdds_vendor_best_effort_reader "
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
      spdp_endpoint_publications_detector |
      spdp_endpoint_subscriptions_announcer;
  participant.participant.lease_duration_ns = 20'000'000'000ULL;
  static_cast<void>(participant.participant.metatraffic_unicast.push_back(
      metadata_locator));
  static_cast<void>(participant.participant.metatraffic_multicast.push_back(
      multicast_locator));
  static_cast<void>(participant.participant.default_unicast.push_back(
      user_locator));
  constexpr char participant_name[] = "openrtdds-pr19";
  participant.participant.entity_name_size = sizeof(participant_name) - 1U;
  std::memcpy(participant.participant.entity_name.data(), participant_name,
              participant.participant.entity_name_size);

  SedpAnnouncementConfig subscription{};
  subscription.endpoint.protocol_version = {2U, 3U};
  subscription.endpoint.vendor_id = vendor;
  subscription.endpoint.participant_guid_prefix = prefix;
  subscription.endpoint.endpoint_id = user_reader;
  subscription.endpoint.kind = EndpointKind::reader;
  subscription.endpoint.topic_name_size = sizeof(topic_name) - 1U;
  subscription.endpoint.type_name_size = sizeof(type_name) - 1U;
  std::memcpy(subscription.endpoint.topic_name.data(), topic_name,
              subscription.endpoint.topic_name_size);
  std::memcpy(subscription.endpoint.type_name.data(), type_name,
              subscription.endpoint.type_name_size);
  subscription.endpoint.reliability = reliable
      ? ReliabilityKind::reliable : ReliabilityKind::best_effort;
  subscription.endpoint.durability = DurabilityKind::volatile_durability;
  subscription.reader_id = subscriptions_reader;
  static_cast<void>(subscription.endpoint.unicast_locators.push_back(
      user_locator));

  std::array<std::uint8_t, 1600U> spdp_bytes{};
  std::array<std::uint8_t, 1600U> sedp_bytes{};
  std::array<std::uint8_t, 128U> heartbeat_bytes{};
  SpdpMessageBuilder spdp(spdp_bytes.data(), spdp_bytes.size());
  SedpMessageBuilder sedp(sedp_bytes.data(), sedp_bytes.size());
  ReliabilityMessageBuilder heartbeat(heartbeat_bytes.data(),
                                      heartbeat_bytes.size());
  HeartbeatConfig heartbeat_config{};
  heartbeat_config.header.version = {2U, 3U};
  heartbeat_config.header.vendor_id = vendor;
  heartbeat_config.header.guid_prefix = prefix;
  heartbeat_config.reader_id = subscriptions_reader;
  heartbeat_config.writer_id = subscriptions_writer;
  heartbeat_config.first_sequence_number = 1U;
  heartbeat_config.last_sequence_number = 1U;
  if (!spdp.build(participant) || !sedp.build(subscription)) {
    std::cerr << "OpenRTDDS discovery message construction failed\n";
    return 4;
  }

  LocalEndpointDescriptor local_reader{};
  local_reader.kind = EndpointKind::reader;
  local_reader.topic_name = topic_name;
  local_reader.topic_name_size = sizeof(topic_name) - 1U;
  local_reader.type_name = type_name;
  local_reader.type_name_size = sizeof(type_name) - 1U;
  local_reader.reliability = reliable
      ? ReliabilityKind::reliable : ReliabilityKind::best_effort;
  local_reader.durability = DurabilityKind::volatile_durability;

  const UdpEndpoint spdp_destination{spdp_group, multicast_port};
  SpdpMessageView remote_participant{};
  SedpMessageView remote_writer{};
  UdpEndpoint remote_metadata{};
  UdpEndpoint remote_user{};
  bool participant_found = false;
  bool writer_found = false;
  bool subscription_requested = false;
  bool sample_received = false;
  bool delivery_ack_sent = false;
  bool reliable_state_ready = false;
  std::uint64_t accepted_sequence = 0U;
  ReliableReader<8U> reliable_reader;
  std::int32_t acknack_count = 1;
  std::int32_t heartbeat_count = 1;
  auto next_announcement = std::chrono::steady_clock::time_point::min();
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(12);
  std::array<std::uint8_t, 2048U> incoming{};

  while (std::chrono::steady_clock::now() < deadline &&
         (!sample_received || (reliable && !delivery_ack_sent))) {
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
          !same_prefix(discovered.participant.guid_prefix, prefix) &&
          discovered.participant.metatraffic_unicast.size != 0U) {
        const Locator& locator =
            discovered.participant.metatraffic_unicast[0U];
        if (valid_udp_v4_locator(locator)) {
          remote_metadata.address.octets =
              {{locator.address[12], locator.address[13],
                locator.address[14], locator.address[15]}};
          remote_metadata.port = static_cast<std::uint16_t>(locator.port);
          remote_participant = discovered;
          participant_found = true;
        }
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
          remote_acknack.reader_id == subscriptions_reader &&
          remote_acknack.writer_id == subscriptions_writer) {
        if (!metadata_socket.send_to(remote_metadata, sedp.data(), sedp.size())
                 .ok()) {
          std::cerr << "SEDP repair send failed\n";
          return 6;
        }
        subscription_requested = true;
        continue;
      }

      SedpMessageView endpoint{};
      const auto parsed = parse_sedp_message(
          incoming.data(), incoming_size,
          remote_participant.participant.guid_prefix, endpoint);
      if (parsed.ok() && endpoint.endpoint.kind == EndpointKind::writer &&
          evaluate_endpoint_match(local_reader, endpoint.endpoint) ==
              MatchStatus::matched) {
        remote_writer = endpoint;
        writer_found = first_endpoint(endpoint.endpoint.unicast_locators,
                                      remote_user) ||
            first_endpoint(remote_participant.participant.default_unicast,
                           remote_user);
        if (reliable && writer_found && !reliable_state_ready) {
          ReliableReaderConfig reader_config{};
          reader_config.reader_id = user_reader;
          reader_config.writer_id = endpoint.endpoint.endpoint_id;
          reader_config.initial_sequence_number = 1U;
          reliable_reader = ReliableReader<8U>(reader_config);
          reliable_state_ready = true;
        }
        continue;
      }

      HeartbeatView remote_heartbeat{};
      if (parse_heartbeat_message(incoming.data(), incoming_size,
                                  remote_heartbeat) !=
              ReliabilityMessageError::none ||
          remote_heartbeat.writer_id != publications_writer ||
          (remote_heartbeat.reader_id != unknown_reader &&
           remote_heartbeat.reader_id != publications_reader) ||
          remote_heartbeat.last_sequence_number <
              remote_heartbeat.first_sequence_number) {
        continue;
      }
      const std::uint64_t range =
          remote_heartbeat.last_sequence_number -
          remote_heartbeat.first_sequence_number + 1U;
      if (range > SequenceNumberSet::maximum_bits) {
        continue;
      }
      AckNackConfig acknack_config{};
      acknack_config.header.version = {2U, 3U};
      acknack_config.header.vendor_id = vendor;
      acknack_config.header.guid_prefix = prefix;
      acknack_config.reader_id = publications_reader;
      acknack_config.writer_id = publications_writer;
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
      ReliabilityMessageBuilder acknack(acknack_bytes.data(),
                                        acknack_bytes.size());
      if (!complete || !acknack.build_acknack(acknack_config) ||
          !direct_message(acknack.data(), acknack.size(),
                          remote_participant.participant.guid_prefix,
                          directed_acknack, directed_acknack_size) ||
          !metadata_socket.send_to(remote_metadata, directed_acknack.data(),
                                   directed_acknack_size).ok()) {
        std::cerr << "SEDP ACKNACK send failed\n";
        return 6;
      }
    }

    std::size_t incoming_size = 0U;
    if (writer_found && receive_one(user_socket, incoming, incoming_size)) {
      DataMessageView sample{};
      const auto parsed = parse_data_message(incoming.data(), incoming_size,
                                             sample);
      const bool addressed_to_reader =
          sample.reader_id == unknown_reader || sample.reader_id == user_reader;
      const bool addressed_to_participant =
          !sample.has_destination ||
          same_prefix(sample.destination_guid_prefix, prefix);
      if (parsed == RtpsError::none &&
          same_prefix(sample.guid_prefix,
                      remote_participant.participant.guid_prefix) &&
          sample.writer_id == remote_writer.endpoint.endpoint_id &&
          addressed_to_reader && addressed_to_participant) {
        openrtdds::serialization::CdrReader cdr(sample.serialized_payload,
                                                sample.payload_size);
        std::uint32_t value = 0U;
        if (cdr.begin() && cdr.read_uint32(value) &&
            cdr.remaining() == 0U && value == sample_value) {
          if (!reliable) {
            sample_received = true;
          } else if (reliable_state_ready) {
            ReliabilityActionBuffer<4U> actions;
            const auto state_error = reliable_reader.on_data(
                sample.sequence_number, actions);
            if (state_error == ReliabilityError::none &&
                actions.size() == 1U &&
                actions[0U].kind ==
                    ReliabilityActionKind::sample_received) {
              sample_received = true;
              accepted_sequence = sample.sequence_number;
            } else if (state_error != ReliabilityError::duplicate_data &&
                       state_error != ReliabilityError::stale_data) {
              std::cerr << "reliable DATA state failed: "
                        << to_string(state_error) << '\n';
              return 11;
            }
          }
        }
      }

      if (reliable && reliable_state_ready) {
        HeartbeatView user_heartbeat{};
        const auto heartbeat_error = parse_heartbeat_message(
            incoming.data(), incoming_size, user_heartbeat);
        const bool addressed_to_reader =
            user_heartbeat.reader_id == unknown_reader ||
            user_heartbeat.reader_id == user_reader;
        if (heartbeat_error == ReliabilityMessageError::none &&
            same_prefix(user_heartbeat.header.guid_prefix,
                        remote_participant.participant.guid_prefix) &&
            user_heartbeat.writer_id ==
                remote_writer.endpoint.endpoint_id &&
            addressed_to_reader) {
          user_heartbeat.reader_id = user_reader;
          ReliabilityActionBuffer<4U> actions;
          const auto state_error = reliable_reader.on_heartbeat(
              user_heartbeat, actions);
          if (state_error != ReliabilityError::none &&
              state_error != ReliabilityError::stale_control) {
            std::cerr << "reliable HEARTBEAT state failed: "
                      << to_string(state_error) << '\n';
            return 11;
          }
          for (std::size_t action_index = 0U;
               action_index < actions.size(); ++action_index) {
            const auto& action = actions[action_index];
            if (action.kind != ReliabilityActionKind::send_acknack) {
              continue;
            }
            AckNackConfig acknack_config{};
            acknack_config.header.version = {2U, 3U};
            acknack_config.header.vendor_id = vendor;
            acknack_config.header.guid_prefix = prefix;
            acknack_config.reader_id = action.reader_id;
            acknack_config.writer_id = action.writer_id;
            acknack_config.reader_state = action.reader_state;
            acknack_config.count = action.control_count;
            acknack_config.final_flag = action.final_flag;
            std::array<std::uint8_t, 128U> acknack_bytes{};
            std::array<std::uint8_t, 144U> directed_acknack{};
            std::size_t directed_size = 0U;
            ReliabilityMessageBuilder acknack(
                acknack_bytes.data(), acknack_bytes.size());
            if (!acknack.build_acknack(acknack_config) ||
                !direct_message(
                    acknack.data(), acknack.size(),
                    remote_participant.participant.guid_prefix,
                    directed_acknack, directed_size) ||
                !user_socket.send_to(remote_user, directed_acknack.data(),
                                     directed_size).ok()) {
              std::cerr << "reliable user ACKNACK send failed\n";
              return 12;
            }
            if (sample_received &&
                action.reader_state.bitmap_base() > accepted_sequence &&
                action.reader_state.num_bits() == 0U) {
              delivery_ack_sent = true;
            }
          }
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  if (!participant_found) {
    std::cerr << "vendor participant discovery timed out\n";
    return 7;
  }
  if (!writer_found) {
    std::cerr << "vendor writer discovery timed out\n";
    return 8;
  }
  if (!subscription_requested) {
    std::cerr << "vendor subscriptions reader handshake timed out\n";
    return 9;
  }
  if (!sample_received) {
    std::cerr << "vendor DATA receive timed out\n";
    return 10;
  }
  if (reliable && !delivery_ack_sent) {
    std::cerr << "reliable delivery ACKNACK timed out\n";
    return 13;
  }

  std::cout << "OpenRTDDS received "
            << (reliable ? "reliable" : "best-effort")
            << " value=" << sample_value
            << " writer="
            << static_cast<unsigned>(remote_writer.endpoint.endpoint_id.value[2])
            << ':'
            << static_cast<unsigned>(remote_writer.endpoint.endpoint_id.value[3])
            << '\n';
  return 0;
}
