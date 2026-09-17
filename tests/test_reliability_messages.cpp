#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "openrtdds/rtps/reliability_messages.hpp"
#include "test_support.hpp"

// Verifies: ORT-REL-002, ORT-REL-003

namespace {

using openrtdds::rtps::AckNackConfig;
using openrtdds::rtps::AckNackView;
using openrtdds::rtps::HeartbeatConfig;
using openrtdds::rtps::HeartbeatView;
using openrtdds::rtps::ReliabilityMessageBuilder;
using openrtdds::rtps::ReliabilityMessageError;
using openrtdds::rtps::ReliabilityMessageHeader;
using openrtdds::rtps::SequenceNumberSet;
using openrtdds::serialization::ByteOrder;

[[nodiscard]] ReliabilityMessageHeader make_header(
    const ByteOrder byte_order) {
  ReliabilityMessageHeader header{};
  header.version = {2U, 5U};
  header.vendor_id.value = {{0x12U, 0x34U}};
  for (std::size_t index = 0U; index < header.guid_prefix.value.size();
       ++index) {
    header.guid_prefix.value[index] = static_cast<std::uint8_t>(index);
  }
  header.submessage_byte_order = byte_order;
  return header;
}

[[nodiscard]] HeartbeatConfig make_heartbeat(const ByteOrder byte_order) {
  HeartbeatConfig config{};
  config.header = make_header(byte_order);
  config.reader_id.value = {{0x00U, 0x00U, 0x01U, 0x04U}};
  config.writer_id.value = {{0x00U, 0x00U, 0x02U, 0x03U}};
  config.first_sequence_number = 1U;
  config.last_sequence_number = 3U;
  config.count = 1;
  return config;
}

[[nodiscard]] AckNackConfig make_acknack(const ByteOrder byte_order) {
  AckNackConfig config{};
  config.header = make_header(byte_order);
  config.reader_id.value = {{0x00U, 0x00U, 0x01U, 0x04U}};
  config.writer_id.value = {{0x00U, 0x00U, 0x02U, 0x03U}};
  return config;
}

void test_sequence_number_set() {
  SequenceNumberSet set{};
  CHECK(set.reset(5U, 35U));
  CHECK(set.set(0U));
  CHECK(set.set(2U));
  CHECK(set.set(34U));
  CHECK(!set.set(35U));
  CHECK(set.test(0U));
  CHECK(!set.test(1U));
  CHECK(set.test(2U));
  CHECK(set.test(34U));
  CHECK(!set.test(35U));
  CHECK(set.word_count() == 2U);
  CHECK(set.word(0U) == 0xA0000000U);
  CHECK(set.word(1U) == 0x20000000U);
  CHECK(!set.reset(0U, 1U));
  CHECK(!set.reset(1U, 257U));
  CHECK(!set.reset(
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
      2U));
}

void test_little_endian_heartbeat_golden() {
  std::array<std::uint8_t, 64U> message{};
  ReliabilityMessageBuilder builder(message.data(), message.size());
  HeartbeatConfig config = make_heartbeat(ByteOrder::little_endian);
  config.liveliness_flag = true;
  CHECK(builder.build_heartbeat(config));
  CHECK(builder.size() == 52U);

  const std::array<std::uint8_t, 52U> expected{{
      0x52U, 0x54U, 0x50U, 0x53U,
      0x02U, 0x05U, 0x12U, 0x34U,
      0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U,
      0x06U, 0x07U, 0x08U, 0x09U, 0x0AU, 0x0BU,
      0x07U, 0x05U, 0x1CU, 0x00U,
      0x00U, 0x00U, 0x01U, 0x04U,
      0x00U, 0x00U, 0x02U, 0x03U,
      0x00U, 0x00U, 0x00U, 0x00U,
      0x01U, 0x00U, 0x00U, 0x00U,
      0x00U, 0x00U, 0x00U, 0x00U,
      0x03U, 0x00U, 0x00U, 0x00U,
      0x01U, 0x00U, 0x00U, 0x00U,
  }};
  CHECK(std::memcmp(message.data(), expected.data(), expected.size()) == 0);

  HeartbeatView view{};
  CHECK(openrtdds::rtps::parse_heartbeat_message(
            message.data(), builder.size(), view) ==
        ReliabilityMessageError::none);
  CHECK(view.header.version.major == 2U);
  CHECK(view.header.version.minor == 5U);
  CHECK(view.reader_id == config.reader_id);
  CHECK(view.writer_id == config.writer_id);
  CHECK(view.first_sequence_number == 1U);
  CHECK(view.last_sequence_number == 3U);
  CHECK(view.count == 1);
  CHECK(!view.final_flag);
  CHECK(view.liveliness_flag);
}

void test_big_endian_empty_heartbeat() {
  std::array<std::uint8_t, 64U> message{};
  ReliabilityMessageBuilder builder(message.data(), message.size());
  HeartbeatConfig config = make_heartbeat(ByteOrder::big_endian);
  config.first_sequence_number = 11U;
  config.last_sequence_number = 10U;
  config.count = 0x01020304;
  config.final_flag = true;
  CHECK(builder.build_heartbeat(config));
  CHECK(message[21] == 0x02U);
  CHECK(message[22] == 0x00U);
  CHECK(message[23] == 0x1CU);
  CHECK(message[39] == 0x0BU);
  CHECK(message[47] == 0x0AU);
  CHECK(message[48] == 0x01U);
  CHECK(message[51] == 0x04U);

  HeartbeatView view{};
  CHECK(openrtdds::rtps::parse_heartbeat_message(
            message.data(), builder.size(), view) ==
        ReliabilityMessageError::none);
  CHECK(view.first_sequence_number == 11U);
  CHECK(view.last_sequence_number == 10U);
  CHECK(view.count == 0x01020304);
  CHECK(view.final_flag);
  CHECK(!view.liveliness_flag);
  CHECK(view.header.submessage_byte_order == ByteOrder::big_endian);
}

void test_little_endian_acknack_golden() {
  std::array<std::uint8_t, 80U> message{};
  ReliabilityMessageBuilder builder(message.data(), message.size());
  AckNackConfig config = make_acknack(ByteOrder::little_endian);
  CHECK(config.reader_state.reset(5U, 35U));
  CHECK(config.reader_state.set(0U));
  CHECK(config.reader_state.set(2U));
  CHECK(config.reader_state.set(34U));
  config.count = 7;
  config.final_flag = true;
  CHECK(builder.build_acknack(config));
  CHECK(builder.size() == 56U);

  const std::array<std::uint8_t, 56U> expected{{
      0x52U, 0x54U, 0x50U, 0x53U,
      0x02U, 0x05U, 0x12U, 0x34U,
      0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U,
      0x06U, 0x07U, 0x08U, 0x09U, 0x0AU, 0x0BU,
      0x06U, 0x03U, 0x20U, 0x00U,
      0x00U, 0x00U, 0x01U, 0x04U,
      0x00U, 0x00U, 0x02U, 0x03U,
      0x00U, 0x00U, 0x00U, 0x00U,
      0x05U, 0x00U, 0x00U, 0x00U,
      0x23U, 0x00U, 0x00U, 0x00U,
      0x00U, 0x00U, 0x00U, 0xA0U,
      0x00U, 0x00U, 0x00U, 0x20U,
      0x07U, 0x00U, 0x00U, 0x00U,
  }};
  CHECK(std::memcmp(message.data(), expected.data(), expected.size()) == 0);

  AckNackView view{};
  CHECK(openrtdds::rtps::parse_acknack_message(
            message.data(), builder.size(), view) ==
        ReliabilityMessageError::none);
  CHECK(view.reader_id == config.reader_id);
  CHECK(view.writer_id == config.writer_id);
  CHECK(view.reader_state.bitmap_base() == 5U);
  CHECK(view.reader_state.num_bits() == 35U);
  CHECK(view.reader_state.test(0U));
  CHECK(view.reader_state.test(2U));
  CHECK(view.reader_state.test(34U));
  CHECK(view.count == 7);
  CHECK(view.final_flag);
}

void test_acknack_bitmap_boundaries() {
  std::array<std::uint8_t, 80U> message{};
  ReliabilityMessageBuilder builder(message.data(), message.size());
  AckNackConfig config = make_acknack(ByteOrder::big_endian);
  CHECK(config.reader_state.reset(1U, 0U));
  config.count = -1;
  CHECK(builder.build_acknack(config));
  CHECK(builder.size() == 48U);

  AckNackView view{};
  CHECK(openrtdds::rtps::parse_acknack_message(
            message.data(), builder.size(), view) ==
        ReliabilityMessageError::none);
  CHECK(view.reader_state.num_bits() == 0U);
  CHECK(view.count == -1);

  CHECK(config.reader_state.reset(9U, 256U));
  CHECK(config.reader_state.set(0U));
  CHECK(config.reader_state.set(31U));
  CHECK(config.reader_state.set(32U));
  CHECK(config.reader_state.set(255U));
  CHECK(builder.build_acknack(config));
  CHECK(builder.size() == 80U);
  CHECK(openrtdds::rtps::parse_acknack_message(
            message.data(), builder.size(), view) ==
        ReliabilityMessageError::none);
  CHECK(view.reader_state.test(0U));
  CHECK(view.reader_state.test(31U));
  CHECK(view.reader_state.test(32U));
  CHECK(view.reader_state.test(255U));
}

void test_zero_length_final_submessages() {
  std::array<std::uint8_t, 80U> message{};
  ReliabilityMessageBuilder builder(message.data(), message.size());
  CHECK(builder.build_heartbeat(make_heartbeat(ByteOrder::little_endian)));
  message[22] = 0U;
  message[23] = 0U;
  HeartbeatView heartbeat{};
  CHECK(openrtdds::rtps::parse_heartbeat_message(
            message.data(), builder.size(), heartbeat) ==
        ReliabilityMessageError::none);

  AckNackConfig ack = make_acknack(ByteOrder::little_endian);
  CHECK(ack.reader_state.reset(1U, 1U));
  CHECK(builder.build_acknack(ack));
  message[22] = 0U;
  message[23] = 0U;
  AckNackView ack_view{};
  CHECK(openrtdds::rtps::parse_acknack_message(
            message.data(), builder.size(), ack_view) ==
        ReliabilityMessageError::none);
}

void test_builder_rejections() {
  std::array<std::uint8_t, 80U> message{};
  ReliabilityMessageBuilder builder(message.data(), message.size());
  HeartbeatConfig heartbeat = make_heartbeat(ByteOrder::little_endian);

  heartbeat.first_sequence_number = 0U;
  CHECK(!builder.build_heartbeat(heartbeat));
  CHECK(builder.error() == ReliabilityMessageError::invalid_sequence_number);
  CHECK(builder.size() == 0U);

  heartbeat = make_heartbeat(ByteOrder::little_endian);
  heartbeat.first_sequence_number = 4U;
  heartbeat.last_sequence_number = 2U;
  CHECK(!builder.build_heartbeat(heartbeat));
  CHECK(builder.error() == ReliabilityMessageError::invalid_sequence_number);

  std::array<std::uint8_t, 51U> small{};
  ReliabilityMessageBuilder small_builder(small.data(), small.size());
  CHECK(!small_builder.build_heartbeat(
      make_heartbeat(ByteOrder::little_endian)));
  CHECK(small_builder.error() == ReliabilityMessageError::buffer_overflow);
}

void test_parser_rejections_and_atomic_output() {
  std::array<std::uint8_t, 80U> message{};
  ReliabilityMessageBuilder builder(message.data(), message.size());
  CHECK(builder.build_heartbeat(make_heartbeat(ByteOrder::little_endian)));
  const std::size_t heartbeat_size = builder.size();

  HeartbeatView heartbeat{};
  heartbeat.count = 99;
  std::array<std::uint8_t, 80U> malformed = message;
  malformed[21] |= 0x08U;
  CHECK(openrtdds::rtps::parse_heartbeat_message(
            malformed.data(), heartbeat_size, heartbeat) ==
        ReliabilityMessageError::unsupported_feature);
  CHECK(heartbeat.count == 99);

  malformed = message;
  malformed[36] = 0U;
  malformed[37] = 0U;
  malformed[38] = 0U;
  malformed[39] = 0U;
  CHECK(openrtdds::rtps::parse_heartbeat_message(
            malformed.data(), heartbeat_size, heartbeat) ==
        ReliabilityMessageError::invalid_sequence_number);
  CHECK(heartbeat.count == 99);

  malformed = message;
  malformed[44] = 0U;
  malformed[45] = 0U;
  malformed[46] = 0U;
  malformed[47] = 0U;
  malformed[36] = 3U;
  CHECK(openrtdds::rtps::parse_heartbeat_message(
            malformed.data(), heartbeat_size, heartbeat) ==
        ReliabilityMessageError::invalid_sequence_number);

  CHECK(openrtdds::rtps::parse_heartbeat_message(
            message.data(), heartbeat_size - 1U, heartbeat) ==
        ReliabilityMessageError::truncated);

  malformed = message;
  malformed[52] = 0x01U;
  CHECK(openrtdds::rtps::parse_heartbeat_message(
            malformed.data(), heartbeat_size + 1U, heartbeat) ==
        ReliabilityMessageError::invalid_submessage);

  AckNackConfig ack = make_acknack(ByteOrder::little_endian);
  CHECK(ack.reader_state.reset(1U, 1U));
  CHECK(builder.build_acknack(ack));
  const std::size_t ack_size = builder.size();
  AckNackView ack_view{};
  ack_view.count = 77;

  malformed = message;
  malformed[40] = 0x01U;
  malformed[41] = 0x01U;
  malformed[42] = 0U;
  malformed[43] = 0U;
  CHECK(openrtdds::rtps::parse_acknack_message(
            malformed.data(), ack_size, ack_view) ==
        ReliabilityMessageError::bitmap_bound_exceeded);
  CHECK(ack_view.count == 77);

  malformed = message;
  malformed[36] = 0U;
  malformed[37] = 0U;
  malformed[38] = 0U;
  malformed[39] = 0U;
  CHECK(openrtdds::rtps::parse_acknack_message(
            malformed.data(), ack_size, ack_view) ==
        ReliabilityMessageError::invalid_sequence_number);

  malformed = message;
  malformed[20] = 0x07U;
  CHECK(openrtdds::rtps::parse_acknack_message(
            malformed.data(), ack_size, ack_view) ==
        ReliabilityMessageError::unsupported_submessage);

  malformed = message;
  malformed[22] = 0xFFU;
  malformed[23] = 0x7FU;
  CHECK(openrtdds::rtps::parse_acknack_message(
            malformed.data(), ack_size, ack_view) ==
        ReliabilityMessageError::truncated);
}

}  // namespace

void test_reliability_messages() {
  test_sequence_number_set();
  test_little_endian_heartbeat_golden();
  test_big_endian_empty_heartbeat();
  test_little_endian_acknack_golden();
  test_acknack_bitmap_boundaries();
  test_zero_length_final_submessages();
  test_builder_rejections();
  test_parser_rejections_and_atomic_output();
}
