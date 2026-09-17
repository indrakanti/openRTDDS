#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/serialization/cdr.hpp"
#include "test_support.hpp"

// Verifies: ORT-RTPS-001, ORT-RTPS-002, ORT-RTPS-003, ORT-RTPS-004,
// Verifies: ORT-RTPS-005, ORT-RTPS-006, ORT-RTPS-007

namespace {

using openrtdds::rtps::DataMessageBuilder;
using openrtdds::rtps::DataMessageConfig;
using openrtdds::rtps::DataMessageView;
using openrtdds::rtps::RtpsError;
using openrtdds::serialization::ByteOrder;
using openrtdds::serialization::CdrWriter;

[[nodiscard]] DataMessageConfig make_config(const ByteOrder byte_order) {
  DataMessageConfig config{};
  config.version = {2U, 5U};
  config.vendor_id.value = {{0x12U, 0x34U}};
  for (std::size_t index = 0U; index < config.guid_prefix.value.size();
       ++index) {
    config.guid_prefix.value[index] = static_cast<std::uint8_t>(index);
  }
  config.reader_id.value = {{0x00U, 0x00U, 0x01U, 0x04U}};
  config.writer_id.value = {{0x00U, 0x00U, 0x02U, 0x03U}};
  config.sequence_number = 1U;
  config.submessage_byte_order = byte_order;
  return config;
}

void test_little_endian_golden_message() {
  std::array<std::uint8_t, 16U> payload{};
  CdrWriter cdr(payload.data(), payload.size());
  CHECK(cdr.begin(ByteOrder::little_endian));
  CHECK(cdr.write_uint32(0x11223344U));
  CHECK(cdr.size() == 8U);

  std::array<std::uint8_t, 64U> message{};
  DataMessageBuilder builder(message.data(), message.size());
  const DataMessageConfig config = make_config(ByteOrder::little_endian);
  CHECK(builder.build(config, payload.data(), cdr.size()));
  CHECK(builder.size() == 52U);

  const std::array<std::uint8_t, 52U> expected{{
      0x52U, 0x54U, 0x50U, 0x53U,  // RTPS
      0x02U, 0x05U, 0x12U, 0x34U,
      0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U,
      0x06U, 0x07U, 0x08U, 0x09U, 0x0AU, 0x0BU,
      0x15U, 0x05U, 0x1CU, 0x00U,  // DATA, E|D, length 28
      0x00U, 0x00U, 0x10U, 0x00U,
      0x00U, 0x00U, 0x01U, 0x04U,
      0x00U, 0x00U, 0x02U, 0x03U,
      0x00U, 0x00U, 0x00U, 0x00U,  // sequence high
      0x01U, 0x00U, 0x00U, 0x00U,  // sequence low
      0x00U, 0x01U, 0x00U, 0x00U,  // CDR_LE
      0x44U, 0x33U, 0x22U, 0x11U,
  }};
  CHECK(std::memcmp(message.data(), expected.data(), expected.size()) == 0);

  DataMessageView view{};
  CHECK(openrtdds::rtps::parse_data_message(message.data(), builder.size(),
                                            view) == RtpsError::none);
  CHECK(view.version.major == 2U);
  CHECK(view.version.minor == 5U);
  CHECK(view.reader_id == config.reader_id);
  CHECK(view.writer_id == config.writer_id);
  CHECK(view.sequence_number == 1U);
  CHECK(view.submessage_byte_order == ByteOrder::little_endian);
  CHECK(view.payload_size == cdr.size());
  CHECK(std::memcmp(view.serialized_payload, payload.data(), cdr.size()) == 0);
}

void test_big_endian_sequence_number() {
  const std::array<std::uint8_t, 4U> payload{{0x00U, 0x00U, 0x00U, 0x00U}};
  std::array<std::uint8_t, 64U> message{};
  DataMessageBuilder builder(message.data(), message.size());
  DataMessageConfig config = make_config(ByteOrder::big_endian);
  config.sequence_number = 0x0000000100000002ULL;
  CHECK(builder.build(config, payload.data(), payload.size()));
  CHECK(message[21] == 0x04U);
  CHECK(message[22] == 0x00U);
  CHECK(message[23] == 0x18U);
  CHECK(message[36] == 0x00U);
  CHECK(message[39] == 0x01U);
  CHECK(message[40] == 0x00U);
  CHECK(message[43] == 0x02U);

  DataMessageView view{};
  CHECK(openrtdds::rtps::parse_data_message(message.data(), builder.size(),
                                            view) == RtpsError::none);
  CHECK(view.sequence_number == config.sequence_number);
  CHECK(view.submessage_byte_order == ByteOrder::big_endian);
}

void test_last_submessage_zero_length() {
  const std::array<std::uint8_t, 4U> payload{{0x00U, 0x01U, 0x00U, 0x00U}};
  std::array<std::uint8_t, 64U> message{};
  DataMessageBuilder builder(message.data(), message.size());
  CHECK(builder.build(make_config(ByteOrder::little_endian), payload.data(),
                      payload.size()));
  message[22] = 0U;
  message[23] = 0U;

  DataMessageView view{};
  CHECK(openrtdds::rtps::parse_data_message(message.data(), builder.size(),
                                            view) == RtpsError::none);
  CHECK(view.payload_size == payload.size());
}

void test_builder_and_parser_rejections() {
  const std::array<std::uint8_t, 4U> payload{{0x00U, 0x01U, 0x00U, 0x00U}};
  std::array<std::uint8_t, 64U> message{};
  DataMessageBuilder builder(message.data(), message.size());
  DataMessageConfig config = make_config(ByteOrder::little_endian);

  config.sequence_number = 0U;
  CHECK(!builder.build(config, payload.data(), payload.size()));
  CHECK(builder.error() == RtpsError::invalid_sequence_number);
  CHECK(builder.size() == 0U);

  config.sequence_number = 1U;
  CHECK(builder.build(config, payload.data(), payload.size()));
  DataMessageView view{};
  view.sequence_number = 99U;

  std::array<std::uint8_t, 64U> malformed = message;
  malformed[0] = 0U;
  CHECK(openrtdds::rtps::parse_data_message(malformed.data(), builder.size(),
                                            view) ==
        RtpsError::invalid_protocol);
  CHECK(view.sequence_number == 99U);

  malformed = message;
  malformed[21] |= 0x02U;
  CHECK(openrtdds::rtps::parse_data_message(malformed.data(), builder.size(),
                                            view) ==
        RtpsError::unsupported_feature);

  malformed = message;
  malformed[22] = 0xFFU;
  malformed[23] = 0x7FU;
  CHECK(openrtdds::rtps::parse_data_message(malformed.data(), builder.size(),
                                            view) == RtpsError::truncated);

  malformed = message;
  malformed[26] = 0x0FU;
  malformed[27] = 0x00U;
  CHECK(openrtdds::rtps::parse_data_message(malformed.data(), builder.size(),
                                            view) ==
        RtpsError::invalid_submessage);

  std::array<std::uint8_t, 65'508U> oversized_payload{};
  oversized_payload[1] = 1U;
  CHECK(!builder.build(config, oversized_payload.data(),
                       oversized_payload.size()));
  CHECK(builder.error() == RtpsError::message_too_large);
}

}  // namespace

void test_rtps_data_message() {
  test_little_endian_golden_message();
  test_big_endian_sequence_number();
  test_last_submessage_zero_length();
  test_builder_and_parser_rejections();
}
