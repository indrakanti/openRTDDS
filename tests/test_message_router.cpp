#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/rtps/message_router.hpp"
#include "openrtdds/rtps/reliability_messages.hpp"
#include "test_support.hpp"

// Verifies: ORT-ROUTE-001, ORT-ROUTE-002, ORT-ROUTE-003,
// Verifies: ORT-ROUTE-004, ORT-ROUTE-005, ORT-ROUTE-006

namespace {

using openrtdds::rtps::DataMessageBuilder;
using openrtdds::rtps::DataMessageConfig;
using openrtdds::rtps::DataMessageView;
using openrtdds::rtps::AckNackConfig;
using openrtdds::rtps::AckNackView;
using openrtdds::rtps::HeartbeatConfig;
using openrtdds::rtps::HeartbeatView;
using openrtdds::rtps::MessageRouteError;
using openrtdds::rtps::ReliabilityMessageBuilder;
using openrtdds::rtps::ReliabilityMessageError;
using openrtdds::rtps::RoutedSubmessageView;
using openrtdds::serialization::ByteOrder;

constexpr std::size_t rtps_header_size = 20U;

void write_u16_le(std::uint8_t* const output, const std::uint16_t value) {
  output[0] = static_cast<std::uint8_t>(value & 0xFFU);
  output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32_le(std::uint8_t* const output, const std::uint32_t value) {
  for (std::size_t index = 0U; index < 4U; ++index) {
    output[index] =
        static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
  }
}

std::size_t append_submessage(std::uint8_t* const output,
                              const std::size_t offset,
                              const std::uint8_t id,
                              const std::uint8_t flags,
                              const std::uint8_t* const content,
                              const std::size_t content_size) {
  output[offset] = id;
  output[offset + 1U] = flags;
  write_u16_le(&output[offset + 2U],
               static_cast<std::uint16_t>(content_size));
  if (content_size != 0U) {
    std::memcpy(&output[offset + 4U], content, content_size);
  }
  return offset + 4U + content_size;
}

DataMessageConfig data_config() {
  DataMessageConfig config{};
  config.version = {2U, 5U};
  config.vendor_id.value = {{0x01U, 0x23U}};
  config.guid_prefix.value = {{0U, 1U, 2U, 3U, 4U, 5U,
                               6U, 7U, 8U, 9U, 10U, 11U}};
  config.reader_id.value = {{0U, 0U, 1U, 4U}};
  config.writer_id.value = {{0U, 0U, 2U, 3U}};
  config.sequence_number = 42U;
  config.submessage_byte_order = ByteOrder::little_endian;
  return config;
}

HeartbeatConfig heartbeat_config() {
  HeartbeatConfig config{};
  config.header.version = {2U, 5U};
  config.header.vendor_id.value = {{0x01U, 0x23U}};
  config.header.guid_prefix.value = {{0U, 1U, 2U, 3U, 4U, 5U,
                                      6U, 7U, 8U, 9U, 10U, 11U}};
  config.header.submessage_byte_order = ByteOrder::little_endian;
  config.reader_id.value = {{0U, 0U, 1U, 4U}};
  config.writer_id.value = {{0U, 0U, 2U, 3U}};
  config.first_sequence_number = 40U;
  config.last_sequence_number = 42U;
  config.count = 3;
  return config;
}

void test_compound_data_and_context() {
  const std::array<std::uint8_t, 8U> payload{{0U, 1U, 0U, 0U,
                                              0x78U, 0x56U, 0x34U, 0x12U}};
  std::array<std::uint8_t, 64U> plain{};
  DataMessageBuilder builder(plain.data(), plain.size());
  CHECK(builder.build(data_config(), payload.data(), payload.size()));

  std::array<std::uint8_t, 160U> compound{};
  std::memcpy(compound.data(), plain.data(), rtps_header_size);
  std::size_t offset = rtps_header_size;

  const std::array<std::uint8_t, 4U> unknown{{0xDEU, 0xADU, 0xBEU, 0xEFU}};
  offset = append_submessage(compound.data(), offset, 0x80U, 0x01U,
                             unknown.data(), unknown.size());

  const std::array<std::uint8_t, 12U> destination{{
      0xA0U, 0xA1U, 0xA2U, 0xA3U, 0xA4U, 0xA5U,
      0xA6U, 0xA7U, 0xA8U, 0xA9U, 0xAAU, 0xABU}};
  offset = append_submessage(
      compound.data(), offset,
      openrtdds::rtps::submessage_id_info_destination, 0x01U,
      destination.data(), destination.size());

  std::array<std::uint8_t, 8U> timestamp{};
  write_u32_le(timestamp.data(), 123U);
  write_u32_le(&timestamp[4], 0x80000000U);
  offset = append_submessage(
      compound.data(), offset,
      openrtdds::rtps::submessage_id_info_timestamp, 0x01U,
      timestamp.data(), timestamp.size());

  offset = append_submessage(compound.data(), offset,
                             openrtdds::rtps::submessage_id_pad, 0x01U,
                             nullptr, 0U);
  const std::size_t data_offset = offset;
  std::memcpy(&compound[offset], &plain[rtps_header_size],
              builder.size() - rtps_header_size);
  offset += builder.size() - rtps_header_size;

  RoutedSubmessageView routed{};
  CHECK(openrtdds::rtps::find_submessage(
            compound.data(), offset, openrtdds::rtps::submessage_id_data,
            0U, routed) == MessageRouteError::none);
  CHECK(routed.offset == data_offset);
  CHECK(routed.has_destination);
  CHECK(routed.destination_guid_prefix.value == destination);
  CHECK(routed.has_source_timestamp);
  CHECK(routed.source_timestamp.seconds == 123);
  CHECK(routed.source_timestamp.fraction == 0x80000000U);

  DataMessageView view{};
  CHECK(openrtdds::rtps::parse_data_message(compound.data(), offset, view) ==
        openrtdds::rtps::RtpsError::none);
  CHECK(view.sequence_number == 42U);
  CHECK(view.has_destination);
  CHECK(view.destination_guid_prefix.value == destination);
  CHECK(view.has_source_timestamp);
  CHECK(view.source_timestamp_seconds == 123);
  CHECK(view.payload_size == payload.size());
  CHECK(std::memcmp(view.serialized_payload, payload.data(), payload.size()) ==
        0);
}

void test_info_source_and_occurrence_selection() {
  const std::array<std::uint8_t, 4U> payload{{0U, 1U, 0U, 0U}};
  std::array<std::uint8_t, 64U> plain{};
  DataMessageBuilder builder(plain.data(), plain.size());
  CHECK(builder.build(data_config(), payload.data(), payload.size()));

  std::array<std::uint8_t, 160U> compound{};
  std::memcpy(compound.data(), plain.data(), rtps_header_size);
  std::size_t offset = rtps_header_size;
  std::array<std::uint8_t, 20U> info_source{};
  info_source[4] = 2U;
  info_source[5] = 5U;
  info_source[6] = 0xCAU;
  info_source[7] = 0xFEU;
  for (std::size_t index = 0U; index < 12U; ++index) {
    info_source[index + 8U] = static_cast<std::uint8_t>(0xB0U + index);
  }
  offset = append_submessage(
      compound.data(), offset, openrtdds::rtps::submessage_id_info_source,
      0x01U, info_source.data(), info_source.size());
  const std::size_t first_data_offset = offset;
  std::memcpy(&compound[offset], &plain[rtps_header_size],
              builder.size() - rtps_header_size);
  offset += builder.size() - rtps_header_size;
  const std::size_t second_data_offset = offset;
  std::memcpy(&compound[offset], &plain[rtps_header_size],
              builder.size() - rtps_header_size);
  offset += builder.size() - rtps_header_size;

  RoutedSubmessageView first{};
  RoutedSubmessageView second{};
  CHECK(openrtdds::rtps::find_submessage(
            compound.data(), offset, openrtdds::rtps::submessage_id_data,
            0U, first) == MessageRouteError::none);
  CHECK(openrtdds::rtps::find_submessage(
            compound.data(), offset, openrtdds::rtps::submessage_id_data,
            1U, second) == MessageRouteError::none);
  CHECK(first.offset == first_data_offset);
  CHECK(second.offset == second_data_offset);
  CHECK(first.vendor_id.value[0] == 0xCAU);
  CHECK(first.source_guid_prefix.value[0] == 0xB0U);
  CHECK(first.source_guid_prefix.value[11] == 0xBBU);
}

void test_compound_reliability_message() {
  std::array<std::uint8_t, 80U> plain{};
  ReliabilityMessageBuilder builder(plain.data(), plain.size());
  CHECK(builder.build_heartbeat(heartbeat_config()));

  std::array<std::uint8_t, 96U> compound{};
  std::memcpy(compound.data(), plain.data(), rtps_header_size);
  std::size_t offset = rtps_header_size;
  const std::array<std::uint8_t, 12U> destination{{
      1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U}};
  offset = append_submessage(
      compound.data(), offset,
      openrtdds::rtps::submessage_id_info_destination, 0x01U,
      destination.data(), destination.size());
  std::memcpy(&compound[offset], &plain[rtps_header_size],
              builder.size() - rtps_header_size);
  offset += builder.size() - rtps_header_size;

  HeartbeatView view{};
  CHECK(openrtdds::rtps::parse_heartbeat_message(
            compound.data(), offset, view) == ReliabilityMessageError::none);
  CHECK(view.first_sequence_number == 40U);
  CHECK(view.last_sequence_number == 42U);
  CHECK(view.count == 3);

  AckNackConfig ack{};
  ack.header = heartbeat_config().header;
  ack.reader_id.value = {{0U, 0U, 1U, 4U}};
  ack.writer_id.value = {{0U, 0U, 2U, 3U}};
  CHECK(ack.reader_state.reset(40U, 3U));
  CHECK(ack.reader_state.set(1U));
  ack.count = 4;
  CHECK(builder.build_acknack(ack));

  std::memcpy(compound.data(), plain.data(), rtps_header_size);
  offset = rtps_header_size;
  offset = append_submessage(
      compound.data(), offset,
      openrtdds::rtps::submessage_id_info_destination, 0x01U,
      destination.data(), destination.size());
  std::memcpy(&compound[offset], &plain[rtps_header_size],
              builder.size() - rtps_header_size);
  offset += builder.size() - rtps_header_size;

  AckNackView ack_view{};
  CHECK(openrtdds::rtps::parse_acknack_message(
            compound.data(), offset, ack_view) ==
        ReliabilityMessageError::none);
  CHECK(ack_view.reader_state.bitmap_base() == 40U);
  CHECK(ack_view.reader_state.num_bits() == 3U);
  CHECK(ack_view.reader_state.test(1U));
  CHECK(ack_view.count == 4);
}

void test_malformed_messages_and_atomic_output() {
  const std::array<std::uint8_t, 4U> payload{{0U, 1U, 0U, 0U}};
  std::array<std::uint8_t, 64U> plain{};
  DataMessageBuilder builder(plain.data(), plain.size());
  CHECK(builder.build(data_config(), payload.data(), payload.size()));

  std::array<std::uint8_t, 96U> malformed{};
  std::memcpy(malformed.data(), plain.data(), rtps_header_size);
  malformed[20] = openrtdds::rtps::submessage_id_info_destination;
  malformed[21] = 0x01U;
  write_u16_le(&malformed[22], 13U);
  DataMessageView view{};
  view.sequence_number = 777U;
  CHECK(openrtdds::rtps::parse_data_message(malformed.data(), 37U, view) ==
        openrtdds::rtps::RtpsError::invalid_submessage);
  CHECK(view.sequence_number == 777U);

  RoutedSubmessageView routed{};
  routed.offset = 999U;
  CHECK(openrtdds::rtps::find_submessage(
            plain.data(), builder.size() - 1U,
            openrtdds::rtps::submessage_id_data, 0U, routed) ==
        MessageRouteError::truncated);
  CHECK(routed.offset == 999U);
  CHECK(openrtdds::rtps::find_submessage(
            plain.data(), builder.size(),
            openrtdds::rtps::submessage_id_acknack, 0U, routed) ==
        MessageRouteError::submessage_not_found);
  CHECK(routed.offset == 999U);
}

}  // namespace

void test_message_router() {
  test_compound_data_and_context();
  test_info_source_and_occurrence_selection();
  test_compound_reliability_message();
  test_malformed_messages_and_atomic_output();
}
