#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "openrtdds/dds/static_entities.hpp"
#include "openrtdds/transport/udp_socket.hpp"
#include "test_support.hpp"

// Verifies: ORT-DDS-001, ORT-DDS-002, ORT-DDS-003, ORT-DDS-004,
// Verifies: ORT-DDS-005, ORT-DDS-006

namespace {

struct VehicleState final {
  std::uint64_t timestamp_ns{0U};
  float velocity_mps{0.0F};
  std::int32_t mode{0};
};

struct VehicleStateTypeSupport final {
  static constexpr std::uint32_t type_id = 0x56535431U;

  [[nodiscard]] static bool serialize(
      const VehicleState& sample,
      openrtdds::serialization::CdrWriter& writer) noexcept {
    return writer.write_uint64(sample.timestamp_ns) &&
           writer.write_float32(sample.velocity_mps) &&
           writer.write_int32(sample.mode);
  }

  [[nodiscard]] static bool deserialize(
      openrtdds::serialization::CdrReader& reader,
      VehicleState& sample) noexcept {
    return reader.read_uint64(sample.timestamp_ns) &&
           reader.read_float32(sample.velocity_mps) &&
           reader.read_int32(sample.mode);
  }
};

using Topic =
    openrtdds::dds::Topic<VehicleState, VehicleStateTypeSupport>;
using Writer = openrtdds::dds::DataWriter<VehicleState,
                                         VehicleStateTypeSupport, 2U, 32U>;
using Reader =
    openrtdds::dds::DataReader<VehicleState, VehicleStateTypeSupport, 8U>;

[[nodiscard]] openrtdds::dds::DomainParticipantConfig participant_config(
    const std::uint8_t prefix_seed) {
  openrtdds::dds::DomainParticipantConfig config{};
  config.domain_id = 7U;
  config.version = {2U, 5U};
  config.vendor_id.value = {{0x12U, 0x34U}};
  for (std::size_t index = 0U; index < config.guid_prefix.value.size();
       ++index) {
    config.guid_prefix.value[index] =
        static_cast<std::uint8_t>(prefix_seed + index);
  }
  return config;
}

[[nodiscard]] openrtdds::dds::StaticEndpointConfig endpoint_config(
    const openrtdds::rtps::GuidPrefix& remote_prefix) {
  openrtdds::dds::StaticEndpointConfig config{};
  config.reader_id.value = {{0x00U, 0x00U, 0x01U, 0x04U}};
  config.writer_id.value = {{0x00U, 0x00U, 0x02U, 0x03U}};
  config.remote_guid_prefix = remote_prefix;
  return config;
}

struct EntityFixture final {
  openrtdds::dds::DomainParticipant writer_participant{
      participant_config(0x10U)};
  openrtdds::dds::DomainParticipant reader_participant{
      participant_config(0x40U)};
  openrtdds::dds::Publisher publisher{writer_participant};
  openrtdds::dds::Subscriber subscriber{reader_participant};
  Topic topic{{0x1001U, VehicleStateTypeSupport::type_id}};
  openrtdds::dds::StaticEndpointConfig writer_endpoint{
      endpoint_config(reader_participant.config().guid_prefix)};
  openrtdds::dds::StaticEndpointConfig reader_endpoint{
      endpoint_config(writer_participant.config().guid_prefix)};
};

void test_entity_validation() {
  using openrtdds::dds::DdsError;
  openrtdds::dds::DomainParticipantConfig zero_guid{};
  openrtdds::dds::DomainParticipant invalid_participant(zero_guid);
  CHECK(!invalid_participant.valid());

  auto invalid_version = participant_config(1U);
  invalid_version.version = {3U, 0U};
  openrtdds::dds::DomainParticipant version_participant(invalid_version);
  CHECK(!version_participant.valid());

  Topic valid_topic{{1U, VehicleStateTypeSupport::type_id}};
  Topic invalid_topic{{1U, 0xDEADBEEFU}};
  CHECK(valid_topic.valid());
  CHECK(!invalid_topic.valid());

  EntityFixture fixture{};
  Writer invalid_writer(fixture.publisher, invalid_topic,
                        fixture.writer_endpoint);
  std::array<std::uint8_t, 128U> datagram{};
  const VehicleState sample{1U, 2.0F, 3};
  CHECK(invalid_writer.write(sample, 1U, datagram.data(), datagram.size())
            .error == DdsError::invalid_topic);

  auto invalid_endpoint = fixture.writer_endpoint;
  invalid_endpoint.writer_id = {};
  Writer endpoint_writer(fixture.publisher, fixture.topic, invalid_endpoint);
  CHECK(endpoint_writer.write(sample, 1U, datagram.data(), datagram.size())
            .error == DdsError::invalid_endpoint);
  openrtdds::rtps::ReliabilityActionBuffer<1U> actions{};
  CHECK(endpoint_writer.on_timer(1U, actions).error ==
        DdsError::invalid_endpoint);
}

void test_typed_data_and_identity_validation() {
  using openrtdds::dds::DdsError;
  using openrtdds::rtps::ReliabilityError;
  EntityFixture fixture{};
  Writer writer(fixture.publisher, fixture.topic, fixture.writer_endpoint);
  Reader reader(fixture.subscriber, fixture.topic, fixture.reader_endpoint);
  CHECK(writer.valid());
  CHECK(reader.valid());

  const VehicleState sent{123456789U, 13.5F, 4};
  std::array<std::uint8_t, 128U> datagram{};
  const auto write_result =
      writer.write(sent, 1'000U, datagram.data(), datagram.size());
  CHECK(write_result.ok());
  CHECK(write_result.sequence_number == 1U);
  CHECK(write_result.bytes != 0U);
  CHECK(writer.history_size() == 1U);

  VehicleState received{99U, 99.0F, 99};
  std::array<std::uint8_t, 128U> wrong = datagram;
  wrong[8U] ^= 0xFFU;
  CHECK(reader.take(wrong.data(), write_result.bytes, received).error ==
        DdsError::unexpected_participant);
  CHECK(received.timestamp_ns == 99U);

  wrong = datagram;
  wrong[31U] ^= 0x01U;
  CHECK(reader.take(wrong.data(), write_result.bytes, received).error ==
        DdsError::unexpected_endpoint);
  CHECK(received.timestamp_ns == 99U);

  const auto take_result =
      reader.take(datagram.data(), write_result.bytes, received);
  CHECK(take_result.ok());
  CHECK(take_result.sequence_number == 1U);
  CHECK(received.timestamp_ns == sent.timestamp_ns);
  CHECK(received.velocity_mps == sent.velocity_mps);
  CHECK(received.mode == sent.mode);
  CHECK(reader.next_expected_sequence() == 2U);

  const auto duplicate =
      reader.take(datagram.data(), write_result.bytes, received);
  CHECK(duplicate.error == DdsError::reliability_state_failed);
  CHECK(duplicate.reliability_error == ReliabilityError::stale_data);
}

void test_reliability_control_and_repair() {
  using openrtdds::rtps::ReliabilityActionBuffer;
  using openrtdds::rtps::ReliabilityActionKind;
  EntityFixture fixture{};
  Writer writer(fixture.publisher, fixture.topic, fixture.writer_endpoint);
  Reader reader(fixture.subscriber, fixture.topic, fixture.reader_endpoint);
  std::array<std::uint8_t, 128U> data{};
  std::array<std::uint8_t, 128U> heartbeat{};
  std::array<std::uint8_t, 128U> acknack{};

  const VehicleState first{1U, 1.0F, 1};
  auto result = writer.write(first, 10U, data.data(), data.size());
  CHECK(result.ok());
  VehicleState received{};
  CHECK(reader.take(data.data(), result.bytes, received).ok());

  result = writer.build_heartbeat(heartbeat.data(), heartbeat.size());
  CHECK(result.ok());
  const auto ack_result = reader.on_heartbeat(
      heartbeat.data(), result.bytes, acknack.data(), acknack.size());
  CHECK(ack_result.ok());
  CHECK(ack_result.bytes != 0U);
  ReliabilityActionBuffer<4U> writer_actions{};
  CHECK(writer.on_acknack(acknack.data(), ack_result.bytes, 20U,
                          writer_actions)
            .ok());
  CHECK(writer_actions.size() == 1U);
  CHECK(writer_actions[0U].kind == ReliabilityActionKind::sample_delivered);
  CHECK(writer.history_size() == 0U);

  writer_actions.clear();
  const VehicleState second{2U, 2.0F, 2};
  result = writer.write(second, 30U, data.data(), data.size());
  CHECK(result.ok());
  // Drop DATA(2); the HEARTBEAT must produce a missing-bit ACKNACK.
  result = writer.build_heartbeat(heartbeat.data(), heartbeat.size());
  CHECK(result.ok());
  const auto missing_ack = reader.on_heartbeat(
      heartbeat.data(), result.bytes, acknack.data(), acknack.size());
  CHECK(missing_ack.ok());
  CHECK(missing_ack.bytes != 0U);
  CHECK(writer.on_acknack(acknack.data(), missing_ack.bytes, 40U,
                          writer_actions)
            .ok());
  CHECK(writer_actions.size() == 1U);
  CHECK(writer_actions[0U].kind == ReliabilityActionKind::retransmit_data);
  CHECK(writer_actions[0U].sequence_number == 2U);

  const auto repair = writer.build_data_action(
      writer_actions[0U], data.data(), data.size());
  CHECK(repair.ok());
  CHECK(repair.sequence_number == 2U);
  CHECK(reader.take(data.data(), repair.bytes, received).ok());
  CHECK(received.timestamp_ns == second.timestamp_ns);
  CHECK(reader.next_expected_sequence() == 3U);
}

void test_history_bound_and_serialization_failure() {
  using openrtdds::dds::DdsError;
  using openrtdds::rtps::ReliabilityError;
  EntityFixture fixture{};
  Writer writer(fixture.publisher, fixture.topic, fixture.writer_endpoint);
  std::array<std::uint8_t, 128U> datagram{};
  CHECK(writer.write({1U, 1.0F, 1}, 1U, datagram.data(), datagram.size()).ok());
  CHECK(writer.write({2U, 2.0F, 2}, 2U, datagram.data(), datagram.size()).ok());
  const auto full =
      writer.write({3U, 3.0F, 3}, 3U, datagram.data(), datagram.size());
  CHECK(full.error == DdsError::reliability_state_failed);
  CHECK(full.reliability_error == ReliabilityError::history_full);
  CHECK(writer.history_size() == 2U);

  using SmallWriter = openrtdds::dds::DataWriter<
      VehicleState, VehicleStateTypeSupport, 1U, 8U>;
  SmallWriter small(fixture.publisher, fixture.topic, fixture.writer_endpoint);
  const auto serialization =
      small.write({1U, 1.0F, 1}, 1U, datagram.data(), datagram.size());
  CHECK(serialization.error == DdsError::serialization_failed);
  CHECK(serialization.cdr_error ==
        openrtdds::serialization::CdrError::overflow);
}

void test_udp_datagram_boundary() {
  using openrtdds::transport::Ipv4Address;
  using openrtdds::transport::UdpEndpoint;
  using openrtdds::transport::UdpSocket;
  EntityFixture fixture{};
  Writer writer(fixture.publisher, fixture.topic, fixture.writer_endpoint);
  Reader reader(fixture.subscriber, fixture.topic, fixture.reader_endpoint);

  UdpSocket receiver_socket;
  CHECK(receiver_socket.open().ok());
  CHECK(receiver_socket.bind({Ipv4Address::loopback(), 0U}).ok());
  UdpEndpoint receiver_endpoint{};
  CHECK(receiver_socket.local_endpoint(receiver_endpoint).ok());

  UdpSocket sender_socket;
  CHECK(sender_socket.open().ok());
  std::array<std::uint8_t, 128U> send_buffer{};
  const VehicleState sent{77U, 7.0F, 7};
  const auto write_result = writer.write(
      sent, 100U, send_buffer.data(), send_buffer.size());
  CHECK(write_result.ok());
  CHECK(sender_socket.send_to(receiver_endpoint, send_buffer.data(),
                              write_result.bytes)
            .ok());

  std::array<std::uint8_t, 128U> receive_buffer{};
  UdpEndpoint remote{};
  const auto udp_result = receiver_socket.receive_from(
      receive_buffer.data(), receive_buffer.size(), remote);
  CHECK(udp_result.ok());
  CHECK(udp_result.bytes == write_result.bytes);
  VehicleState received{};
  CHECK(reader.take(receive_buffer.data(), udp_result.bytes, received).ok());
  CHECK(received.timestamp_ns == sent.timestamp_ns);
  CHECK(received.velocity_mps == sent.velocity_mps);
  CHECK(received.mode == sent.mode);
}

void test_static_storage_properties() {
  static_assert(std::is_nothrow_destructible<Writer>::value,
                "static DDS writer destruction must not throw");
  static_assert(std::is_nothrow_destructible<Reader>::value,
                "static DDS reader destruction must not throw");
  CHECK(sizeof(Writer) < 512U);
  CHECK(sizeof(Reader) < 256U);
}

}  // namespace

void test_static_dds() {
  test_entity_validation();
  test_typed_data_and_identity_validation();
  test_reliability_control_and_repair();
  test_history_bound_and_serialization_failure();
  test_udp_datagram_boundary();
  test_static_storage_properties();
}
