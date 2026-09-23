// Requirements: ORT-INT-003, ORT-INT-005, ORT-INT-006, ORT-INT-007
// Verifies: ORT-INT-002, ORT-INT-003, ORT-INT-005, ORT-INT-006, ORT-INT-007
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

#include "openrtdds/rtps/spdp.hpp"
#include "openrtdds/rtps/sedp.hpp"
#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/rtps/message_router.hpp"
#include "openrtdds/rtps/reliability_messages.hpp"

namespace {
constexpr std::size_t max_datagram_size = 65'507U;

bool read_packet(const char* path,
                 std::array<std::uint8_t, max_datagram_size + 1U>& bytes,
                 std::size_t& size) {
  std::ifstream input(path, std::ios::binary);
  if (!input) return false;
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  size = static_cast<std::size_t>(input.gcount());
  return size != 0U && size <= max_datagram_size && !input.bad();
}

std::uint32_t read_sample(const std::uint8_t* const payload,
                          const std::size_t size) {
  if (payload == nullptr || size != 8U || payload[0] != 0U ||
      payload[1] > 1U) {
    return 0U;
  }
  const bool little = payload[1] == 1U;
  std::uint32_t value = 0U;
  for (std::size_t index = 0U; index < 4U; ++index) {
    const std::size_t shift = little ? index : 3U - index;
    value |= static_cast<std::uint32_t>(payload[4U + index]) << (shift * 8U);
  }
  return value;
}

bool expected_endpoint(const openrtdds::rtps::SedpMessageView& endpoint,
                       const openrtdds::rtps::EndpointKind kind,
                       const openrtdds::rtps::EntityId& entity) {
  return endpoint.endpoint.kind == kind &&
      endpoint.endpoint.endpoint_id == entity &&
      endpoint.endpoint.reliability ==
          openrtdds::rtps::ReliabilityKind::reliable &&
      std::string(endpoint.endpoint.topic_name.data(),
                  endpoint.endpoint.topic_name_size) == "OpenRTDDSProbe" &&
      std::string(endpoint.endpoint.type_name.data(),
                  endpoint.endpoint.type_name_size) == "VendorProbe";
}

int probe_reliable(char** argv) {
  using Packet = std::array<std::uint8_t, max_datagram_size + 1U>;
  Packet data_bytes{}, publisher_endpoint_bytes{}, publisher_spdp_bytes{};
  Packet heartbeat_bytes{}, acknack_bytes{}, subscriber_endpoint_bytes{};
  Packet subscriber_spdp_bytes{};
  std::size_t data_size = 0U, publisher_endpoint_size = 0U;
  std::size_t publisher_spdp_size = 0U, heartbeat_size = 0U;
  std::size_t acknack_size = 0U, subscriber_endpoint_size = 0U;
  std::size_t subscriber_spdp_size = 0U;
  if (!read_packet(argv[2], data_bytes, data_size) ||
      !read_packet(argv[3], publisher_endpoint_bytes,
                   publisher_endpoint_size) ||
      !read_packet(argv[4], publisher_spdp_bytes, publisher_spdp_size) ||
      !read_packet(argv[5], heartbeat_bytes, heartbeat_size) ||
      !read_packet(argv[6], acknack_bytes, acknack_size) ||
      !read_packet(argv[7], subscriber_endpoint_bytes,
                   subscriber_endpoint_size) ||
      !read_packet(argv[8], subscriber_spdp_bytes, subscriber_spdp_size)) {
    std::cerr << "missing or invalid reliable evidence packet\n";
    return 2;
  }

  openrtdds::rtps::SpdpMessageView publisher{};
  openrtdds::rtps::SpdpMessageView subscriber{};
  const auto publisher_result = openrtdds::rtps::parse_spdp_message(
      publisher_spdp_bytes.data(), publisher_spdp_size, 43U, publisher);
  const auto subscriber_result = openrtdds::rtps::parse_spdp_message(
      subscriber_spdp_bytes.data(), subscriber_spdp_size, 43U, subscriber);
  if (!publisher_result.ok() || !subscriber_result.ok() ||
      publisher.participant.default_unicast.size == 0U ||
      subscriber.participant.default_unicast.size == 0U ||
      publisher.participant.guid_prefix.value ==
          subscriber.participant.guid_prefix.value) {
    std::cerr << "reliable participant evidence invalid\n";
    return 1;
  }

  openrtdds::rtps::DataMessageView sample{};
  const auto data_error = openrtdds::rtps::parse_data_message(
      data_bytes.data(), data_size, sample);
  if (data_error != openrtdds::rtps::RtpsError::none ||
      sample.guid_prefix.value != publisher.participant.guid_prefix.value ||
      (sample.writer_id.value[3] != 0x02U &&
       sample.writer_id.value[3] != 0x03U) ||
      read_sample(sample.serialized_payload, sample.payload_size) !=
          0x4F525444U) {
    std::cerr << "reliable user DATA invalid\n";
    return 1;
  }

  openrtdds::rtps::SedpMessageView publisher_endpoint{};
  const auto publisher_endpoint_result =
      openrtdds::rtps::parse_sedp_message(
          publisher_endpoint_bytes.data(), publisher_endpoint_size,
          publisher.participant.guid_prefix, publisher_endpoint);
  if (!publisher_endpoint_result.ok() ||
      !expected_endpoint(publisher_endpoint,
                         openrtdds::rtps::EndpointKind::writer,
                         sample.writer_id)) {
    std::cerr << "reliable DATA does not match publisher SEDP\n";
    return 1;
  }

  openrtdds::rtps::HeartbeatView heartbeat{};
  const auto heartbeat_error = openrtdds::rtps::parse_heartbeat_message(
      heartbeat_bytes.data(), heartbeat_size, heartbeat);
  if (heartbeat_error != openrtdds::rtps::ReliabilityMessageError::none ||
      heartbeat.header.guid_prefix.value != sample.guid_prefix.value ||
      heartbeat.writer_id != sample.writer_id ||
      sample.sequence_number < heartbeat.first_sequence_number ||
      sample.sequence_number > heartbeat.last_sequence_number) {
    std::cerr << "HEARTBEAT does not cover reliable DATA\n";
    return 1;
  }

  openrtdds::rtps::AckNackView acknack{};
  const auto acknack_error = openrtdds::rtps::parse_acknack_message(
      acknack_bytes.data(), acknack_size, acknack);
  if (acknack_error != openrtdds::rtps::ReliabilityMessageError::none ||
      acknack.header.guid_prefix.value !=
          subscriber.participant.guid_prefix.value ||
      acknack.writer_id != sample.writer_id ||
      (acknack.reader_id.value[3] != 0x04U &&
       acknack.reader_id.value[3] != 0x07U)) {
    std::cerr << "ACKNACK identity invalid\n";
    return 1;
  }

  openrtdds::rtps::SedpMessageView subscriber_endpoint{};
  const auto subscriber_endpoint_result =
      openrtdds::rtps::parse_sedp_message(
          subscriber_endpoint_bytes.data(), subscriber_endpoint_size,
          subscriber.participant.guid_prefix, subscriber_endpoint);
  if (!subscriber_endpoint_result.ok() ||
      !expected_endpoint(subscriber_endpoint,
                         openrtdds::rtps::EndpointKind::reader,
                         acknack.reader_id)) {
    std::cerr << "ACKNACK does not match subscriber SEDP\n";
    return 1;
  }

  std::cout << "reliable DATA chain accepted: bytes=" << data_size
            << " sequence=" << sample.sequence_number
            << " heartbeat=" << heartbeat.count
            << " acknack=" << acknack.count << '\n';
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  const bool sedp = argc == 4 && std::string(argv[1]) == "--sedp";
  const bool data = argc == 5 && std::string(argv[1]) == "--data";
  const bool reliable = argc == 9 && std::string(argv[1]) == "--reliable";
  if (argc != 2 && !sedp && !data && !reliable) {
    std::cerr << "usage: vendor_packet_probe <spdp.rtps> | --sedp "
                 "<sedp.rtps> <matched-participant.rtps> | --data "
                 "<data.rtps> <endpoint.rtps> <participant.rtps> | "
                 "--reliable <data> <publisher-endpoint> <publisher-spdp> "
                 "<heartbeat> <acknack> <subscriber-endpoint> "
                 "<subscriber-spdp>\n";
    return 2;
  }
  if (reliable) {
    return probe_reliable(argv);
  }
  std::array<std::uint8_t, max_datagram_size + 1U> bytes{};
  std::size_t size = 0U;
  if (!read_packet(argv[(sedp || data) ? 2 : 1], bytes, size)) {
    std::cerr << "empty, oversized, or unreadable UDP packet\n";
    return 2;
  }

  if (sedp || data) {
    std::array<std::uint8_t, max_datagram_size + 1U> participant_bytes{};
    std::size_t participant_size = 0U;
    if (!read_packet(argv[data ? 4 : 3], participant_bytes, participant_size)) {
      std::cerr << "missing or invalid participant packet\n";
      return 2;
    }
    openrtdds::rtps::SpdpMessageView participant{};
    const auto spdp = openrtdds::rtps::parse_spdp_message(
        participant_bytes.data(), participant_size, 43U, participant);
    if (!spdp.ok() || participant.participant.default_unicast.size == 0U) {
      std::cerr << "matched participant SPDP invalid\n";
      return 1;
    }
    if (data) {
      openrtdds::rtps::DataMessageView sample{};
      const auto data_error = openrtdds::rtps::parse_data_message(
          bytes.data(), size, sample);
      if (data_error != openrtdds::rtps::RtpsError::none) {
        std::cerr << "user DATA parse failed: "
                  << openrtdds::rtps::to_string(data_error) << '\n';
        return 1;
      }
      if (sample.guid_prefix.value != participant.participant.guid_prefix.value ||
          (sample.writer_id.value[3] != 0x02U &&
           sample.writer_id.value[3] != 0x03U)) {
        std::cerr << "user DATA source or writer identity invalid\n";
        return 1;
      }
      std::array<std::uint8_t, max_datagram_size + 1U> endpoint_bytes{};
      std::size_t endpoint_size = 0U;
      if (!read_packet(argv[3], endpoint_bytes, endpoint_size)) {
        std::cerr << "missing or invalid endpoint packet\n";
        return 2;
      }
      openrtdds::rtps::SedpMessageView endpoint{};
      const auto endpoint_result = openrtdds::rtps::parse_sedp_message(
          endpoint_bytes.data(), endpoint_size, sample.guid_prefix, endpoint);
      if (!endpoint_result.ok() ||
          endpoint.endpoint.kind != openrtdds::rtps::EndpointKind::writer ||
          endpoint.endpoint.endpoint_id != sample.writer_id ||
          endpoint.endpoint.reliability !=
              openrtdds::rtps::ReliabilityKind::best_effort ||
          std::string(endpoint.endpoint.topic_name.data(),
                      endpoint.endpoint.topic_name_size) != "OpenRTDDSProbe" ||
          std::string(endpoint.endpoint.type_name.data(),
                      endpoint.endpoint.type_name_size) != "VendorProbe") {
        std::cerr << "user DATA does not match its best-effort SEDP writer\n";
        return 1;
      }
      constexpr std::uint32_t expected_sample = 0x4F525444U;
      if (read_sample(sample.serialized_payload, sample.payload_size) !=
          expected_sample) {
        std::cerr << "user DATA payload differs from the known sample\n";
        return 1;
      }
      std::cout << "best-effort DATA accepted: bytes=" << size
                << " sequence=" << sample.sequence_number
                << " value=" << expected_sample << '\n';
      return 0;
    }
    openrtdds::rtps::RoutedSubmessageView routed{};
    const auto routed_error = openrtdds::rtps::find_submessage(
        bytes.data(), size, openrtdds::rtps::submessage_id_data, 0U, routed);
    if (routed_error != openrtdds::rtps::MessageRouteError::none) {
      std::cerr << "SEDP routing failed: "
                << openrtdds::rtps::to_string(routed_error) << '\n';
      return 1;
    }
    if (participant.participant.guid_prefix.value !=
        routed.source_guid_prefix.value) {
      std::cerr << "SEDP source differs from captured SPDP participant\n";
      return 1;
    }
    openrtdds::rtps::SedpMessageView endpoint{};
    const auto result = openrtdds::rtps::parse_sedp_message(
        bytes.data(), size, routed.source_guid_prefix, endpoint);
    if (!result.ok()) {
      std::cerr << "SEDP parse failed: "
                << openrtdds::rtps::to_string(result.error)
                << "; RTPS: "
                << openrtdds::rtps::to_string(result.rtps_error) << '\n';
      return 1;
    }
    if (endpoint.sequence_number == 0U ||
        endpoint.endpoint.topic_name_size == 0U ||
        endpoint.endpoint.type_name_size == 0U) {
      std::cerr << "SEDP publication lacks required identity/locators\n";
      return 1;
    }
    std::cout << "SEDP accepted: bytes=" << result.bytes
              << " topic=" << endpoint.endpoint.topic_name.data()
              << " type=" << endpoint.endpoint.type_name.data() << '\n';
    return 0;
  }
  openrtdds::rtps::SpdpMessageView view{};
  const auto result = openrtdds::rtps::parse_spdp_message(
      bytes.data(), size, 43U, view);
  if (!result.ok()) {
    std::cerr << "SPDP parse failed: "
              << openrtdds::rtps::to_string(result.error)
              << "; RTPS: "
              << openrtdds::rtps::to_string(result.rtps_error) << '\n';
    return 1;
  }
  if (view.sequence_number == 0U ||
      view.participant.metatraffic_unicast.size == 0U ||
      view.participant.default_unicast.size == 0U) {
    std::cerr << "SPDP announcement lacks required identity/locators\n";
    return 1;
  }
  std::cout << "SPDP accepted: bytes=" << result.bytes
            << " sequence=" << view.sequence_number
            << " vendor=" << static_cast<unsigned>(view.participant.vendor_id.value[0])
            << ':' << static_cast<unsigned>(view.participant.vendor_id.value[1])
            << '\n';
  return 0;
}
