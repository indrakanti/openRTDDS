#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "openrtdds/rtps/reliability_state.hpp"

// Demonstrates: ORT-REL-001, ORT-REL-006

int main() {
  using openrtdds::rtps::AckNackView;
  using openrtdds::rtps::HeartbeatConfig;
  using openrtdds::rtps::HeartbeatView;
  using openrtdds::rtps::ReliabilityActionBuffer;
  using openrtdds::rtps::ReliabilityActionKind;
  using openrtdds::rtps::ReliabilityError;
  using openrtdds::rtps::ReliableReader;
  using openrtdds::rtps::ReliableWriter;

  ReliableWriter<4U, 16U> writer{};
  ReliableReader<8U> reader{};
  ReliabilityActionBuffer<8U> writer_actions{};
  ReliabilityActionBuffer<8U> reader_actions{};
  const std::array<std::uint8_t, 1U> payload{{0x42U}};

  for (std::uint64_t sequence = 1U; sequence <= 3U; ++sequence) {
    if (writer.write(payload.data(), payload.size(), sequence,
                     sequence * 1'000U, writer_actions) !=
        ReliabilityError::none) {
      return 1;
    }
    writer_actions.clear();
  }

  // Simulate DATA(2) loss while DATA(1) and DATA(3) arrive.
  if ((reader.on_data(1U, reader_actions) != ReliabilityError::none) ||
      (reader.on_data(3U, reader_actions) != ReliabilityError::none)) {
    return 2;
  }
  reader_actions.clear();

  HeartbeatConfig heartbeat_config{};
  if (writer.fill_heartbeat(heartbeat_config) != ReliabilityError::none) {
    return 3;
  }
  HeartbeatView heartbeat{};
  heartbeat.first_sequence_number = heartbeat_config.first_sequence_number;
  heartbeat.last_sequence_number = heartbeat_config.last_sequence_number;
  heartbeat.count = heartbeat_config.count;
  if (reader.on_heartbeat(heartbeat, reader_actions) !=
      ReliabilityError::none) {
    return 4;
  }

  AckNackView acknack{};
  acknack.reader_state = reader_actions[0U].reader_state;
  acknack.count = reader_actions[0U].control_count;
  if (writer.on_acknack(acknack, 4'000U, writer_actions) !=
      ReliabilityError::none) {
    return 5;
  }

  bool repaired_two = false;
  for (std::size_t index = 0U; index < writer_actions.size(); ++index) {
    if ((writer_actions[index].kind ==
         ReliabilityActionKind::retransmit_data) &&
        (writer_actions[index].sequence_number == 2U)) {
      repaired_two = true;
      if (reader.on_data(2U, reader_actions) != ReliabilityError::none) {
        return 6;
      }
    }
  }
  if (!repaired_two || (reader.next_expected_sequence() != 4U)) {
    return 7;
  }

  std::cout << "bounded repair completed; reader expects sequence "
            << reader.next_expected_sequence() << '\n';
  return 0;
}
