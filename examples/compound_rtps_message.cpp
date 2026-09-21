#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/rtps/message_router.hpp"

// Demonstrates: ORT-ROUTE-003, ORT-ROUTE-006

namespace {

void write_u16_le(std::uint8_t* const output, const std::uint16_t value) {
  output[0] = static_cast<std::uint8_t>(value & 0xFFU);
  output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

}  // namespace

int main() {
  const std::array<std::uint8_t, 8U> serialized_payload{{
      0U, 1U, 0U, 0U, 0x2AU, 0U, 0U, 0U}};
  std::array<std::uint8_t, 64U> data_message{};

  openrtdds::rtps::DataMessageConfig config{};
  config.version = {2U, 5U};
  config.vendor_id.value = {{0x01U, 0x23U}};
  config.guid_prefix.value = {{0U, 1U, 2U, 3U, 4U, 5U,
                               6U, 7U, 8U, 9U, 10U, 11U}};
  config.reader_id.value = {{0U, 0U, 1U, 4U}};
  config.writer_id.value = {{0U, 0U, 2U, 3U}};
  config.sequence_number = 7U;

  openrtdds::rtps::DataMessageBuilder builder(data_message.data(),
                                               data_message.size());
  if (!builder.build(config, serialized_payload.data(),
                     serialized_payload.size())) {
    std::cerr << openrtdds::rtps::to_string(builder.error()) << '\n';
    return 1;
  }

  // Compose RTPS header + INFO_DST + DATA in caller-owned storage.
  std::array<std::uint8_t, 96U> compound{};
  constexpr std::size_t header_size = 20U;
  std::memcpy(compound.data(), data_message.data(), header_size);
  compound[20] = openrtdds::rtps::submessage_id_info_destination;
  compound[21] = 0x01U;
  write_u16_le(&compound[22], 12U);
  for (std::size_t index = 0U; index < 12U; ++index) {
    compound[24U + index] = static_cast<std::uint8_t>(0xA0U + index);
  }
  constexpr std::size_t data_offset = 36U;
  std::memcpy(&compound[data_offset], &data_message[header_size],
              builder.size() - header_size);
  const std::size_t compound_size =
      data_offset + builder.size() - header_size;

  openrtdds::rtps::DataMessageView view{};
  const auto error = openrtdds::rtps::parse_data_message(
      compound.data(), compound_size, view);
  if (error != openrtdds::rtps::RtpsError::none) {
    std::cerr << openrtdds::rtps::to_string(error) << '\n';
    return 1;
  }

  std::cout << "sequence=" << view.sequence_number
            << " payload_bytes=" << view.payload_size
            << " destination="
            << (view.has_destination ? "directed" : "unknown") << '\n';
  return (view.sequence_number == 7U) && view.has_destination ? 0 : 1;
}
