#include <array>
#include <cstdint>
#include <iostream>

#include "openrtdds/rtps/reliability_messages.hpp"

// Demonstrates: ORT-REL-002, ORT-REL-003

int main() {
  using openrtdds::rtps::AckNackConfig;
  using openrtdds::rtps::AckNackView;
  using openrtdds::rtps::HeartbeatConfig;
  using openrtdds::rtps::HeartbeatView;
  using openrtdds::rtps::ReliabilityMessageBuilder;
  using openrtdds::rtps::ReliabilityMessageError;

  std::array<std::uint8_t, 80U> datagram{};
  ReliabilityMessageBuilder builder(datagram.data(), datagram.size());

  HeartbeatConfig heartbeat{};
  heartbeat.header.vendor_id.value = {{0x01U, 0x23U}};
  heartbeat.writer_id.value = {{0x00U, 0x00U, 0x02U, 0x03U}};
  heartbeat.first_sequence_number = 1U;
  heartbeat.last_sequence_number = 4U;
  heartbeat.count = 1;
  if (!builder.build_heartbeat(heartbeat)) {
    return 1;
  }

  HeartbeatView heartbeat_view{};
  if (openrtdds::rtps::parse_heartbeat_message(
          builder.data(), builder.size(), heartbeat_view) !=
      ReliabilityMessageError::none) {
    return 2;
  }

  AckNackConfig acknack{};
  acknack.header = heartbeat.header;
  acknack.reader_id.value = {{0x00U, 0x00U, 0x01U, 0x04U}};
  acknack.writer_id = heartbeat.writer_id;
  if (!acknack.reader_state.reset(1U, 4U) ||
      !acknack.reader_state.set(1U) ||
      !acknack.reader_state.set(3U) ||
      !builder.build_acknack(acknack)) {
    return 3;
  }

  AckNackView acknack_view{};
  if (openrtdds::rtps::parse_acknack_message(
          builder.data(), builder.size(), acknack_view) !=
      ReliabilityMessageError::none) {
    return 4;
  }

  std::cout << "HEARTBEAT [" << heartbeat_view.first_sequence_number << ", "
            << heartbeat_view.last_sequence_number << "]; ACKNACK missing: "
            << (acknack_view.reader_state.bitmap_base() + 1U) << ", "
            << (acknack_view.reader_state.bitmap_base() + 3U) << '\n';
  return 0;
}
