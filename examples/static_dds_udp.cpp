#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "openrtdds/dds/static_entities.hpp"
#include "openrtdds/transport/udp_socket.hpp"

// Demonstrates: ORT-DDS-001, ORT-DDS-002, ORT-DDS-003, ORT-DDS-004,
// Demonstrates: ORT-DDS-005, ORT-DDS-006

namespace {

struct VehicleState final {
  std::uint64_t timestamp_ns{0U};
  float velocity_mps{0.0F};
};

struct VehicleStateTypeSupport final {
  static constexpr std::uint32_t type_id = 0x56535431U;

  [[nodiscard]] static bool serialize(
      const VehicleState& sample,
      openrtdds::serialization::CdrWriter& writer) noexcept {
    return writer.write_uint64(sample.timestamp_ns) &&
           writer.write_float32(sample.velocity_mps);
  }

  [[nodiscard]] static bool deserialize(
      openrtdds::serialization::CdrReader& reader,
      VehicleState& sample) noexcept {
    return reader.read_uint64(sample.timestamp_ns) &&
           reader.read_float32(sample.velocity_mps);
  }
};

[[nodiscard]] openrtdds::dds::DomainParticipantConfig participant_config(
    const std::uint8_t seed) {
  openrtdds::dds::DomainParticipantConfig config{};
  config.domain_id = 7U;
  config.vendor_id.value = {{0x12U, 0x34U}};
  for (std::size_t index = 0U; index < config.guid_prefix.value.size();
       ++index) {
    config.guid_prefix.value[index] =
        static_cast<std::uint8_t>(seed + index);
  }
  return config;
}

[[nodiscard]] openrtdds::dds::StaticEndpointConfig endpoint_config(
    const openrtdds::rtps::GuidPrefix& remote) {
  openrtdds::dds::StaticEndpointConfig endpoint{};
  endpoint.reader_id.value = {{0U, 0U, 1U, 4U}};
  endpoint.writer_id.value = {{0U, 0U, 2U, 3U}};
  endpoint.remote_guid_prefix = remote;
  return endpoint;
}

}  // namespace

int main() {
  using Topic =
      openrtdds::dds::Topic<VehicleState, VehicleStateTypeSupport>;
  using Writer = openrtdds::dds::DataWriter<
      VehicleState, VehicleStateTypeSupport, 4U, 32U>;
  using Reader = openrtdds::dds::DataReader<
      VehicleState, VehicleStateTypeSupport, 8U>;
  using openrtdds::transport::Ipv4Address;
  using openrtdds::transport::UdpEndpoint;
  using openrtdds::transport::UdpSocket;

  const openrtdds::dds::DomainParticipant writer_participant(
      participant_config(0x10U));
  const openrtdds::dds::DomainParticipant reader_participant(
      participant_config(0x40U));
  const openrtdds::dds::Publisher publisher(writer_participant);
  const openrtdds::dds::Subscriber subscriber(reader_participant);
  const Topic topic({0x1001U, VehicleStateTypeSupport::type_id});
  Writer writer(publisher, topic,
                endpoint_config(reader_participant.config().guid_prefix));
  Reader reader(subscriber, topic,
                endpoint_config(writer_participant.config().guid_prefix));
  if (!writer.valid() || !reader.valid()) {
    return 1;
  }

  UdpSocket reader_socket;
  UdpSocket writer_socket;
  if (!reader_socket.open().ok() ||
      !reader_socket.bind({Ipv4Address::loopback(), 0U}).ok() ||
      !writer_socket.open().ok()) {
    return 2;
  }
  UdpEndpoint reader_endpoint{};
  if (!reader_socket.local_endpoint(reader_endpoint).ok()) {
    return 3;
  }

  std::array<std::uint8_t, 256U> writer_datagram{};
  std::array<std::uint8_t, 256U> reader_datagram{};
  const VehicleState sent{1'000'000U, 12.5F};
  const auto write_result = writer.write(
      sent, 1'000'000U, writer_datagram.data(), writer_datagram.size());
  if (!write_result.ok() ||
      !writer_socket.send_to(reader_endpoint, writer_datagram.data(),
                             write_result.bytes)
           .ok()) {
    return 4;
  }

  UdpEndpoint writer_endpoint{};
  const auto receive_data = reader_socket.receive_from(
      reader_datagram.data(), reader_datagram.size(), writer_endpoint);
  VehicleState received{};
  if (!receive_data.ok() ||
      !reader.take(reader_datagram.data(), receive_data.bytes, received).ok()) {
    return 5;
  }

  const auto heartbeat = writer.build_heartbeat(
      writer_datagram.data(), writer_datagram.size());
  if (!heartbeat.ok() ||
      !writer_socket.send_to(reader_endpoint, writer_datagram.data(),
                             heartbeat.bytes)
           .ok()) {
    return 6;
  }
  const auto receive_heartbeat = reader_socket.receive_from(
      reader_datagram.data(), reader_datagram.size(), writer_endpoint);
  if (!receive_heartbeat.ok()) {
    return 7;
  }
  const auto acknack = reader.on_heartbeat(
      reader_datagram.data(), receive_heartbeat.bytes,
      writer_datagram.data(), writer_datagram.size());
  if (!acknack.ok() || (acknack.bytes == 0U) ||
      !reader_socket.send_to(writer_endpoint, writer_datagram.data(),
                             acknack.bytes)
           .ok()) {
    return 8;
  }

  const auto receive_acknack = writer_socket.receive_from(
      reader_datagram.data(), reader_datagram.size(), writer_endpoint);
  openrtdds::rtps::ReliabilityActionBuffer<4U> actions{};
  if (!receive_acknack.ok() ||
      !writer.on_acknack(reader_datagram.data(), receive_acknack.bytes,
                         2'000'000U, actions)
           .ok() ||
      (writer.history_size() != 0U)) {
    return 9;
  }

  std::cout << "static DDS/UDP sample sequence="
            << write_result.sequence_number
            << " velocity=" << received.velocity_mps << '\n';
  return 0;
}
