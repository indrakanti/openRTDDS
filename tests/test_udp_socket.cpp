#include <array>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include "openrtdds/rtps/data_message.hpp"
#include "openrtdds/serialization/cdr.hpp"
#include "openrtdds/transport/udp_socket.hpp"
#include "test_support.hpp"

// Verifies: ORT-UDP-001, ORT-UDP-002, ORT-UDP-003, ORT-UDP-004,
// Verifies: ORT-UDP-005

void test_udp_socket() {
  using openrtdds::transport::Ipv4Address;
  using openrtdds::transport::UdpEndpoint;
  using openrtdds::transport::UdpError;
  using openrtdds::transport::UdpSocket;

  static_assert(!std::is_copy_constructible_v<UdpSocket>);
  static_assert(!std::is_copy_assignable_v<UdpSocket>);
  static_assert(std::is_nothrow_move_constructible_v<UdpSocket>);
  static_assert(std::is_nothrow_move_assignable_v<UdpSocket>);

  UdpSocket unopened;
  std::array<std::uint8_t, 1U> scratch{};
  UdpEndpoint scratch_endpoint{};
  CHECK(unopened.receive_from(scratch.data(), scratch.size(), scratch_endpoint)
            .error == UdpError::not_open);

  UdpSocket receiver;
  CHECK(receiver.open().ok());
  CHECK(receiver.bind({Ipv4Address::loopback(), 0U}).ok());

  UdpEndpoint receiver_endpoint{};
  CHECK(receiver.local_endpoint(receiver_endpoint).ok());
  CHECK(receiver_endpoint.address == Ipv4Address::loopback());
  CHECK(receiver_endpoint.port != 0U);

  UdpSocket sender;
  CHECK(sender.open().ok());
  UdpSocket moved_sender = std::move(sender);
  CHECK(!sender.is_open());
  CHECK(moved_sender.is_open());
  CHECK(moved_sender.send_to({Ipv4Address::loopback(), 0U}, scratch.data(),
                             scratch.size())
            .error == UdpError::invalid_argument);

  std::array<std::uint8_t, 16U> payload{};
  openrtdds::serialization::CdrWriter cdr(payload.data(), payload.size());
  CHECK(cdr.begin(openrtdds::serialization::ByteOrder::little_endian));
  CHECK(cdr.write_uint32(0xAABBCCDDU));

  openrtdds::rtps::DataMessageConfig config{};
  config.vendor_id.value = {{0x12U, 0x34U}};
  config.guid_prefix.value = {{0U, 1U, 2U, 3U, 4U, 5U,
                               6U, 7U, 8U, 9U, 10U, 11U}};
  config.reader_id.value = {{0U, 0U, 1U, 4U}};
  config.writer_id.value = {{0U, 0U, 2U, 3U}};
  config.sequence_number = 7U;

  std::array<std::uint8_t, 128U> sent{};
  openrtdds::rtps::DataMessageBuilder builder(sent.data(), sent.size());
  CHECK(builder.build(config, payload.data(), cdr.size()));
  const auto send_result =
      moved_sender.send_to(receiver_endpoint, builder.data(), builder.size());
  CHECK(send_result.ok());
  CHECK(send_result.bytes == builder.size());

  std::array<std::uint8_t, 128U> received{};
  UdpEndpoint sender_endpoint{};
  const auto receive_result = receiver.receive_from(
      received.data(), received.size(), sender_endpoint);
  CHECK(receive_result.ok());
  CHECK(receive_result.bytes == builder.size());
  CHECK(sender_endpoint.address == Ipv4Address::loopback());
  CHECK(sender_endpoint.port != 0U);
  CHECK(std::memcmp(sent.data(), received.data(), builder.size()) == 0);

  openrtdds::rtps::DataMessageView view{};
  CHECK(openrtdds::rtps::parse_data_message(
            received.data(), receive_result.bytes, view) ==
        openrtdds::rtps::RtpsError::none);
  CHECK(view.sequence_number == 7U);
  CHECK(view.payload_size == cdr.size());

  const auto empty_result = receiver.receive_from(
      received.data(), received.size(), sender_endpoint);
  CHECK(empty_result.error == UdpError::would_block);

  CHECK(moved_sender.send_to(receiver_endpoint, builder.data(), builder.size())
            .ok());
  std::array<std::uint8_t, 16U> too_small{};
  const auto truncated_result = receiver.receive_from(
      too_small.data(), too_small.size(), sender_endpoint);
  CHECK(truncated_result.error == UdpError::truncated);
  CHECK(truncated_result.bytes == builder.size());

  std::array<std::uint8_t, 65'508U> oversized{};
  const auto oversized_result = moved_sender.send_to(
      receiver_endpoint, oversized.data(), oversized.size());
  CHECK(oversized_result.error == UdpError::message_too_large);
}
