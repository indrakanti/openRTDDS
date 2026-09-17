#include <array>
#include <cstdint>
#include <iostream>

#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/serialization/cdr.hpp"

// Demonstrates: ORT-RTPS-001, ORT-RTPS-002, ORT-RTPS-003, ORT-RTPS-004,
// Demonstrates: ORT-RTPS-005, ORT-SER-001

int main() {
  std::array<std::uint8_t, 16U> payload{};
  openrtdds::serialization::CdrWriter cdr(payload.data(), payload.size());
  if (!cdr.begin(openrtdds::serialization::ByteOrder::little_endian) ||
      !cdr.write_uint32(100U)) {
    return 1;
  }

  openrtdds::rtps::DataMessageConfig config{};
  config.vendor_id.value = {{0x12U, 0x34U}};
  config.reader_id.value = {{0U, 0U, 1U, 4U}};
  config.writer_id.value = {{0U, 0U, 2U, 3U}};
  config.sequence_number = 7U;

  std::array<std::uint8_t, 128U> message{};
  openrtdds::rtps::DataMessageBuilder builder(message.data(), message.size());
  if (!builder.build(config, payload.data(), cdr.size())) {
    return 1;
  }

  openrtdds::rtps::DataMessageView view{};
  const auto result = openrtdds::rtps::parse_data_message(
      builder.data(), builder.size(), view);
  if (result != openrtdds::rtps::RtpsError::none) {
    return 1;
  }

  std::cout << "RTPS DATA bytes=" << builder.size()
            << " sequence=" << view.sequence_number << '\n';
  return view.sequence_number == 7U ? 0 : 1;
}
