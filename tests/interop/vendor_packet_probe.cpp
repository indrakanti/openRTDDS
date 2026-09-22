// Requirements: ORT-INT-003, ORT-INT-005
// Verifies: ORT-INT-002, ORT-INT-003, ORT-INT-005
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

#include "openrtdds/rtps/spdp.hpp"
#include "openrtdds/rtps/sedp.hpp"
#include "openrtdds/rtps/message_router.hpp"

int main(int argc, char** argv) {
  const bool sedp = argc == 3 && std::string(argv[1]) == "--sedp";
  if (argc != 2 && !sedp) {
    std::cerr << "usage: vendor_packet_probe [--sedp] <UDP payload.rtps>\n";
    return 2;
  }
  constexpr std::size_t max_datagram_size = 65'507U;
  std::array<std::uint8_t, max_datagram_size + 1U> bytes{};
  std::ifstream input(argv[sedp ? 2 : 1], std::ios::binary);
  if (!input) {
    std::cerr << "failed to open packet\n";
    return 2;
  }
  input.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  const std::size_t size = static_cast<std::size_t>(input.gcount());
  if (size == 0U || size > max_datagram_size || input.bad()) {
    std::cerr << "empty, oversized, or unreadable UDP packet\n";
    return 2;
  }

  if (sedp) {
    openrtdds::rtps::RoutedSubmessageView routed{};
    const auto routed_error = openrtdds::rtps::find_submessage(
        bytes.data(), size, openrtdds::rtps::submessage_id_data, 0U, routed);
    if (routed_error != openrtdds::rtps::MessageRouteError::none) {
      std::cerr << "SEDP routing failed: "
                << openrtdds::rtps::to_string(routed_error) << '\n';
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
        endpoint.endpoint.type_name_size == 0U ||
        endpoint.endpoint.unicast_locators.size +
            endpoint.endpoint.multicast_locators.size == 0U) {
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
