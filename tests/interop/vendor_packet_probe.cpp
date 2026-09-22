// Requirements: ORT-INT-003
// Verifies: ORT-INT-002, ORT-INT-003
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>

#include "openrtdds/rtps/spdp.hpp"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: vendor_packet_probe <SPDP UDP payload.rtps>\n";
    return 2;
  }
  constexpr std::size_t max_datagram_size = 65'507U;
  std::array<std::uint8_t, max_datagram_size + 1U> bytes{};
  std::ifstream input(argv[1], std::ios::binary);
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
