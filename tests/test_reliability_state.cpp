#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "openrtdds/rtps/reliability_state.hpp"
#include "test_support.hpp"

// Verifies: ORT-REL-001, ORT-REL-004, ORT-REL-005, ORT-REL-006

namespace {

using openrtdds::rtps::AckNackView;
using openrtdds::rtps::HeartbeatConfig;
using openrtdds::rtps::HeartbeatView;
using openrtdds::rtps::ReliabilityAction;
using openrtdds::rtps::ReliabilityActionBuffer;
using openrtdds::rtps::ReliabilityActionKind;
using openrtdds::rtps::ReliabilityError;
using openrtdds::rtps::ReliableReader;
using openrtdds::rtps::ReliableWriter;
using openrtdds::rtps::ReliableWriterConfig;
using openrtdds::rtps::RepairFailure;

template <std::size_t Capacity>
[[nodiscard]] const ReliabilityAction* find_action(
    const ReliabilityActionBuffer<Capacity>& actions,
    const ReliabilityActionKind kind,
    const std::uint64_t sequence_number) noexcept {
  for (std::size_t index = 0U; index < actions.size(); ++index) {
    if ((actions[index].kind == kind) &&
        (actions[index].sequence_number == sequence_number)) {
      return &actions[index];
    }
  }
  return nullptr;
}

[[nodiscard]] AckNackView make_acknack(
    const std::uint64_t base, const std::uint32_t bits,
    const std::int32_t count) {
  AckNackView acknack{};
  CHECK(acknack.reader_state.reset(base, bits));
  acknack.count = count;
  return acknack;
}

[[nodiscard]] HeartbeatView make_heartbeat(
    const std::uint64_t first, const std::uint64_t last,
    const std::int32_t count, const bool final_flag = false) {
  HeartbeatView heartbeat{};
  heartbeat.first_sequence_number = first;
  heartbeat.last_sequence_number = last;
  heartbeat.count = count;
  heartbeat.final_flag = final_flag;
  return heartbeat;
}

[[nodiscard]] ReliableWriterConfig make_writer_config(
    const std::uint8_t attempts, const std::uint64_t window_ns) {
  ReliableWriterConfig config{};
  config.max_repair_attempts = attempts;
  config.repair_window_ns = window_ns;
  return config;
}

void test_writer_history_and_repairs() {
  using Writer = ReliableWriter<2U, 4U>;
  Writer writer(make_writer_config(2U, 100U));
  ReliabilityActionBuffer<8U> actions{};
  const std::array<std::uint8_t, 2U> first{{1U, 2U}};
  const std::array<std::uint8_t, 1U> second{{3U}};

  CHECK(writer.write(first.data(), first.size(), 1U, 10U, actions) ==
        ReliabilityError::none);
  CHECK(actions.size() == 1U);
  CHECK(actions[0U].kind == ReliabilityActionKind::send_data);
  CHECK(actions[0U].sequence_number == 1U);
  CHECK(actions[0U].data_size == first.size());
  CHECK(actions[0U].data[1U] == 2U);

  actions.clear();
  CHECK(writer.write(second.data(), second.size(), 2U, 11U, actions) ==
        ReliabilityError::none);
  CHECK(writer.full());
  actions.clear();
  CHECK(writer.write(second.data(), second.size(), 3U, 12U, actions) ==
        ReliabilityError::history_full);
  CHECK(writer.size() == 2U);
  CHECK(actions.size() == 0U);

  CHECK(writer.write(second.data(), second.size(), 2U, 12U, actions) ==
        ReliabilityError::non_monotonic_sequence);

  AckNackView acknack = make_acknack(2U, 1U, 1);
  CHECK(acknack.reader_state.set(0U));
  AckNackView wrong_peer = acknack;
  wrong_peer.reader_id.value[3U] = 0x04U;
  CHECK(writer.on_acknack(wrong_peer, 20U, actions) ==
        ReliabilityError::unexpected_peer);
  CHECK(writer.size() == 2U);
  CHECK(writer.on_acknack(acknack, 20U, actions) == ReliabilityError::none);
  CHECK(writer.size() == 1U);
  CHECK(actions.size() == 2U);
  CHECK(find_action(actions, ReliabilityActionKind::sample_delivered, 1U) !=
        nullptr);
  const ReliabilityAction* repair =
      find_action(actions, ReliabilityActionKind::retransmit_data, 2U);
  CHECK(repair != nullptr);
  CHECK(repair->data_size == second.size());
  CHECK(repair->data[0U] == 3U);
  CHECK(writer.repair_attempts(0U) == 1U);

  actions.clear();
  CHECK(writer.on_acknack(acknack, 21U, actions) ==
        ReliabilityError::stale_control);
  CHECK(actions.size() == 0U);
  CHECK(writer.repair_attempts(0U) == 1U);

  acknack.count = 2;
  CHECK(writer.on_acknack(acknack, 22U, actions) == ReliabilityError::none);
  CHECK(actions.size() == 1U);
  CHECK(actions[0U].kind == ReliabilityActionKind::retransmit_data);
  CHECK(writer.repair_attempts(0U) == 2U);

  actions.clear();
  acknack.count = 3;
  CHECK(writer.on_acknack(acknack, 23U, actions) == ReliabilityError::none);
  CHECK(writer.empty());
  CHECK(actions.size() == 1U);
  CHECK(actions[0U].kind == ReliabilityActionKind::sample_failed);
  CHECK(actions[0U].failure == RepairFailure::repair_limit_exceeded);
}

void test_writer_window_and_transactional_capacity() {
  using Writer = ReliableWriter<3U, 4U>;
  const std::array<std::uint8_t, 1U> payload{{9U}};
  Writer writer(make_writer_config(2U, 50U));
  ReliabilityActionBuffer<4U> setup{};
  CHECK(writer.write(payload.data(), payload.size(), 1U, 100U, setup) ==
        ReliabilityError::none);
  CHECK(writer.write(payload.data(), payload.size(), 2U, 110U, setup) ==
        ReliabilityError::none);

  AckNackView acknack = make_acknack(3U, 0U, 1);
  ReliabilityActionBuffer<1U> too_small{};
  CHECK(writer.on_acknack(acknack, 120U, too_small) ==
        ReliabilityError::action_capacity_exceeded);
  CHECK(writer.size() == 2U);
  CHECK(too_small.size() == 0U);

  ReliabilityActionBuffer<4U> actions{};
  CHECK(writer.on_timer(99U, actions) == ReliabilityError::time_regression);
  CHECK(writer.size() == 2U);
  CHECK(writer.on_timer(150U, actions) == ReliabilityError::none);
  CHECK(writer.size() == 1U);
  CHECK(actions.size() == 1U);
  CHECK(actions[0U].sequence_number == 1U);
  CHECK(actions[0U].failure == RepairFailure::repair_window_expired);

  actions.clear();
  AckNackView missing = make_acknack(2U, 1U, 2);
  CHECK(missing.reader_state.set(0U));
  CHECK(writer.on_acknack(missing, 160U, actions) ==
        ReliabilityError::none);
  CHECK(writer.empty());
  CHECK(actions.size() == 1U);
  CHECK(actions[0U].failure == RepairFailure::repair_window_expired);
}

void test_heartbeat_and_control_count_wrap() {
  using Writer = ReliableWriter<2U, 2U>;
  Writer writer{};
  HeartbeatConfig heartbeat{};
  CHECK(writer.fill_heartbeat(heartbeat) == ReliabilityError::none);
  CHECK(heartbeat.first_sequence_number == 1U);
  CHECK(heartbeat.last_sequence_number == 0U);
  CHECK(heartbeat.count == 1);

  const std::array<std::uint8_t, 1U> payload{{1U}};
  ReliabilityActionBuffer<2U> actions{};
  CHECK(writer.write(payload.data(), payload.size(), 8U, 1U, actions) ==
        ReliabilityError::none);
  CHECK(writer.fill_heartbeat(heartbeat) == ReliabilityError::none);
  CHECK(heartbeat.first_sequence_number == 8U);
  CHECK(heartbeat.last_sequence_number == 8U);
  CHECK(heartbeat.count == 2);

  const auto maximum = std::numeric_limits<std::int32_t>::max();
  const auto minimum = std::numeric_limits<std::int32_t>::min();
  CHECK(openrtdds::rtps::next_control_count(maximum) == minimum);
  CHECK(openrtdds::rtps::control_count_is_newer(minimum, maximum));
  CHECK(!openrtdds::rtps::control_count_is_newer(maximum, minimum));
  CHECK(!openrtdds::rtps::control_count_is_newer(7, 7));
}

void test_reader_window_and_acknack() {
  ReliableReader<4U> reader{};
  ReliabilityActionBuffer<8U> actions{};

  CHECK(reader.on_data(2U, actions) == ReliabilityError::none);
  CHECK(reader.next_expected_sequence() == 1U);
  CHECK(reader.on_data(2U, actions) == ReliabilityError::duplicate_data);

  actions.clear();
  const HeartbeatView heartbeat = make_heartbeat(1U, 3U, 1);
  HeartbeatView wrong_peer = heartbeat;
  wrong_peer.writer_id.value[3U] = 0x03U;
  CHECK(reader.on_heartbeat(wrong_peer, actions) ==
        ReliabilityError::unexpected_peer);
  CHECK(actions.size() == 0U);
  CHECK(reader.on_heartbeat(heartbeat, actions) == ReliabilityError::none);
  CHECK(actions.size() == 1U);
  CHECK(actions[0U].kind == ReliabilityActionKind::send_acknack);
  CHECK(actions[0U].reader_state.bitmap_base() == 1U);
  CHECK(actions[0U].reader_state.num_bits() == 3U);
  CHECK(actions[0U].reader_state.test(0U));
  CHECK(!actions[0U].reader_state.test(1U));
  CHECK(actions[0U].reader_state.test(2U));
  CHECK(!actions[0U].final_flag);

  actions.clear();
  CHECK(reader.on_heartbeat(heartbeat, actions) ==
        ReliabilityError::stale_control);
  CHECK(actions.size() == 0U);

  CHECK(reader.on_data(1U, actions) == ReliabilityError::none);
  CHECK(reader.next_expected_sequence() == 3U);
  CHECK(reader.on_data(3U, actions) == ReliabilityError::none);
  CHECK(reader.next_expected_sequence() == 4U);
  CHECK(reader.on_data(3U, actions) == ReliabilityError::stale_data);
  CHECK(reader.on_data(8U, actions) ==
        ReliabilityError::receive_window_exceeded);

  actions.clear();
  CHECK(reader.on_heartbeat(make_heartbeat(1U, 3U, 2), actions) ==
        ReliabilityError::none);
  CHECK(actions.size() == 1U);
  CHECK(actions[0U].reader_state.bitmap_base() == 4U);
  CHECK(actions[0U].reader_state.num_bits() == 0U);
  CHECK(actions[0U].final_flag);

  actions.clear();
  CHECK(reader.on_heartbeat(make_heartbeat(1U, 3U, 3, true), actions) ==
        ReliabilityError::none);
  CHECK(actions.size() == 0U);
  CHECK(reader.on_heartbeat(make_heartbeat(6U, 7U, 4), actions) ==
        ReliabilityError::gap_not_repairable);
}

void test_reader_action_capacity_is_transactional() {
  ReliableReader<4U> reader{};
  ReliabilityActionBuffer<1U> actions{};
  ReliabilityAction occupied{};
  CHECK(actions.push(occupied));
  CHECK(reader.on_data(1U, actions) ==
        ReliabilityError::action_capacity_exceeded);
  CHECK(reader.next_expected_sequence() == 1U);

  actions.clear();
  CHECK(reader.on_data(1U, actions) == ReliabilityError::none);
  CHECK(reader.next_expected_sequence() == 2U);

  CHECK(reader.on_heartbeat(make_heartbeat(1U, 2U, 1), actions) ==
        ReliabilityError::action_capacity_exceeded);
  actions.clear();
  CHECK(reader.on_heartbeat(make_heartbeat(1U, 2U, 1), actions) ==
        ReliabilityError::none);
}

void test_fixed_loss_and_repair_simulation() {
  using Writer = ReliableWriter<4U, 8U>;
  Writer writer(make_writer_config(2U, 1'000U));
  ReliableReader<8U> reader{};
  ReliabilityActionBuffer<8U> writer_actions{};
  ReliabilityActionBuffer<8U> reader_actions{};
  const std::array<std::uint8_t, 1U> payload{{0xAAU}};

  CHECK(writer.write(payload.data(), payload.size(), 1U, 10U,
                     writer_actions) == ReliabilityError::none);
  writer_actions.clear();
  CHECK(writer.write(payload.data(), payload.size(), 2U, 20U,
                     writer_actions) == ReliabilityError::none);
  writer_actions.clear();
  CHECK(writer.write(payload.data(), payload.size(), 3U, 30U,
                     writer_actions) == ReliabilityError::none);
  writer_actions.clear();

  CHECK(reader.on_data(1U, reader_actions) == ReliabilityError::none);
  CHECK(reader.on_data(3U, reader_actions) == ReliabilityError::none);
  reader_actions.clear();

  HeartbeatConfig heartbeat_config{};
  CHECK(writer.fill_heartbeat(heartbeat_config) == ReliabilityError::none);
  HeartbeatView heartbeat{};
  heartbeat.first_sequence_number = heartbeat_config.first_sequence_number;
  heartbeat.last_sequence_number = heartbeat_config.last_sequence_number;
  heartbeat.count = heartbeat_config.count;
  CHECK(reader.on_heartbeat(heartbeat, reader_actions) ==
        ReliabilityError::none);
  CHECK(reader_actions.size() == 1U);

  AckNackView acknack{};
  acknack.reader_state = reader_actions[0U].reader_state;
  acknack.count = reader_actions[0U].control_count;
  CHECK(writer.on_acknack(acknack, 40U, writer_actions) ==
        ReliabilityError::none);
  CHECK(writer_actions.size() == 3U);
  CHECK(find_action(writer_actions, ReliabilityActionKind::sample_delivered,
                    1U) != nullptr);
  CHECK(find_action(writer_actions, ReliabilityActionKind::retransmit_data,
                    2U) != nullptr);
  CHECK(find_action(writer_actions, ReliabilityActionKind::sample_delivered,
                    3U) != nullptr);

  CHECK(reader.on_data(2U, reader_actions) == ReliabilityError::none);
  CHECK(reader.next_expected_sequence() == 4U);
  reader_actions.clear();
  writer_actions.clear();

  CHECK(writer.fill_heartbeat(heartbeat_config) == ReliabilityError::none);
  heartbeat.first_sequence_number = heartbeat_config.first_sequence_number;
  heartbeat.last_sequence_number = heartbeat_config.last_sequence_number;
  heartbeat.count = heartbeat_config.count;
  CHECK(reader.on_heartbeat(heartbeat, reader_actions) ==
        ReliabilityError::none);
  CHECK(reader_actions.size() == 1U);
  acknack.reader_state = reader_actions[0U].reader_state;
  acknack.count = reader_actions[0U].control_count;
  CHECK(writer.on_acknack(acknack, 50U, writer_actions) ==
        ReliabilityError::none);
  CHECK(writer.empty());
  CHECK(writer_actions.size() == 1U);
  CHECK(writer_actions[0U].kind == ReliabilityActionKind::sample_delivered);
  CHECK(writer_actions[0U].sequence_number == 2U);
}

void test_static_storage_properties() {
  using Writer = ReliableWriter<4U, 32U>;
  using Reader = ReliableReader<16U>;
  static_assert(std::is_nothrow_destructible<Writer>::value,
                "writer destruction must not throw");
  static_assert(std::is_nothrow_destructible<Reader>::value,
                "reader destruction must not throw");
  CHECK(sizeof(Writer) < 512U);
  CHECK(sizeof(Reader) < 128U);
}

}  // namespace

void test_reliability_state() {
  test_writer_history_and_repairs();
  test_writer_window_and_transactional_capacity();
  test_heartbeat_and_control_count_wrap();
  test_reader_window_and_acknack();
  test_reader_action_capacity_is_transactional();
  test_fixed_loss_and_repair_simulation();
  test_static_storage_properties();
}
