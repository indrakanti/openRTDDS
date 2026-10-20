#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

#include "openrtdds/rtps/sedp.hpp"
#include "openrtdds/rtps/reliability_state.hpp"
#include "openrtdds/rtps/spdp.hpp"
#include "openrtdds/transport/udp_socket.hpp"
#include "rmw/types.h"

namespace rmw_openrtdds_cpp::detail {

struct NetworkEndpointView;

// Requirements: ORT-RMW-004, ORT-RMW-005, ORT-RMW-016, ORT-RMW-051
// Requirements: ORT-RMW-053, ORT-RMW-054
class NetworkRuntime final {
 public:
  NetworkRuntime() noexcept = default;
  NetworkRuntime(const NetworkRuntime&) = delete;
  NetworkRuntime& operator=(const NetworkRuntime&) = delete;
  ~NetworkRuntime() noexcept;

  [[nodiscard]] bool initialize(rmw_context_t* context) noexcept;
  [[nodiscard]] bool start() noexcept;
  void notify() noexcept;
  void shutdown() noexcept;
  void drop_next_reliable_data_for_test(
      const std::uint32_t count) noexcept {
    test_drop_reliable_data_.store(count);
  }

 private:
  static constexpr std::size_t kRemoteParticipantCapacity = 16U;
  static constexpr std::size_t kRemoteEndpointCapacity = 32U;
  static constexpr std::size_t kReliableLinkCapacity = 32U;
  static constexpr std::size_t kMaximumDatagramSize = 65'507U;
  static constexpr std::size_t kControlDatagramSize = 512U;

  struct RemoteParticipant final {
    openrtdds::rtps::SpdpParticipantData data{};
    std::uint64_t last_seen_ns{0U};
    bool occupied{false};
  };

  struct RemoteEndpoint final {
    openrtdds::rtps::SedpEndpointData data{};
    std::uint64_t last_seen_ns{0U};
    bool occupied{false};
  };

  using ReliableWriterState =
      openrtdds::rtps::ReliableWriter<1U, kMaximumDatagramSize>;
  using ReliableReaderState = openrtdds::rtps::ReliableReader<256U>;

  struct ReliableWriterLink final {
    ReliableWriterState state{};
    openrtdds::rtps::GuidPrefix remote_prefix{};
    openrtdds::rtps::EntityId remote_reader{};
    openrtdds::rtps::EntityId local_writer{};
    openrtdds::transport::UdpEndpoint destination{};
    std::uint64_t last_queued_sequence{0U};
    std::uint64_t next_heartbeat_ns{0U};
    std::size_t local_slot{0U};
    std::uint32_t local_generation{0U};
    bool occupied{false};
  };

  struct ReliableReaderLink final {
    ReliableReaderState state{};
    openrtdds::rtps::GuidPrefix remote_prefix{};
    openrtdds::rtps::EntityId remote_writer{};
    openrtdds::rtps::EntityId local_reader{};
    openrtdds::transport::UdpEndpoint destination{};
    std::size_t local_slot{0U};
    std::uint32_t local_generation{0U};
    bool initialized{false};
    bool occupied{false};
  };

  void run() noexcept;
  void announce(std::uint64_t now_ns) noexcept;
  void receive_discovery(std::uint64_t now_ns) noexcept;
  void receive_user_data(std::uint64_t now_ns) noexcept;
  void send_pending_samples(std::uint64_t now_ns) noexcept;
  void service_reliable_writers(std::uint64_t now_ns) noexcept;
  void handle_data(const std::uint8_t* packet, std::size_t packet_size,
                   std::uint64_t received_timestamp_ns) noexcept;
  void handle_heartbeat(const std::uint8_t* packet,
                        std::size_t packet_size) noexcept;
  void handle_acknack(const std::uint8_t* packet, std::size_t packet_size,
                      std::uint64_t now_ns) noexcept;
  void prune_reliable_links() noexcept;
  void expire_remote_state(std::uint64_t now_ns) noexcept;
  void recompute_matches() noexcept;

  [[nodiscard]] bool configure_sockets() noexcept;
  [[nodiscard]] bool build_spdp() noexcept;
  [[nodiscard]] RemoteParticipant* find_participant(
      const openrtdds::rtps::GuidPrefix& prefix) noexcept;
  [[nodiscard]] const RemoteEndpoint* find_remote_writer(
      const openrtdds::rtps::GuidPrefix& prefix,
      const openrtdds::rtps::EntityId& entity) const noexcept;
  [[nodiscard]] const RemoteEndpoint* find_remote_reader(
      const openrtdds::rtps::GuidPrefix& prefix,
      const openrtdds::rtps::EntityId& entity) const noexcept;
  [[nodiscard]] ReliableWriterLink* writer_link(
      std::size_t local_slot, std::uint32_t local_generation,
      const NetworkEndpointView& local,
      const RemoteEndpoint& remote) noexcept;
  [[nodiscard]] ReliableReaderLink* reader_link(
      std::size_t local_slot, std::uint32_t local_generation,
      const NetworkEndpointView& local, const RemoteEndpoint& remote,
      std::uint64_t initial_sequence) noexcept;
  [[nodiscard]] bool send_heartbeat(ReliableWriterLink& link) noexcept;
  [[nodiscard]] bool send_acknack(
      const ReliableReaderLink& link,
      const openrtdds::rtps::ReliabilityAction& action) noexcept;

  rmw_context_t* context_{nullptr};
  openrtdds::transport::UdpSocket multicast_socket_{};
  openrtdds::transport::UdpSocket metadata_socket_{};
  openrtdds::transport::UdpSocket user_socket_{};
  openrtdds::transport::UdpEndpoint spdp_destination_{};
  std::array<std::uint8_t, 2'048U> spdp_{};
  std::size_t spdp_size_{0U};
  std::array<std::uint8_t, kMaximumDatagramSize> receive_buffer_{};
  std::array<std::uint8_t, kMaximumDatagramSize> transmit_buffer_{};
  std::array<std::uint8_t, kControlDatagramSize> control_buffer_{};
  std::array<std::uint8_t, kControlDatagramSize + 16U>
      directed_control_buffer_{};
  std::array<RemoteParticipant, kRemoteParticipantCapacity> participants_{};
  std::array<RemoteEndpoint, kRemoteEndpointCapacity> endpoints_{};
  std::array<ReliableWriterLink, kReliableLinkCapacity> reliable_writers_{};
  std::array<ReliableReaderLink, kReliableLinkCapacity> reliable_readers_{};
  std::array<std::uint64_t, 8U> last_sent_sequence_{};
  std::array<std::uint32_t, 8U> publisher_generations_{};
  std::thread worker_{};
  std::mutex wake_mutex_{};
  std::condition_variable wake_condition_{};
  std::atomic<bool> stop_{false};
  std::atomic<bool> started_{false};
  std::atomic<std::uint32_t> test_drop_reliable_data_{0U};
  std::uint64_t wake_generation_{1U};
};

}  // namespace rmw_openrtdds_cpp::detail
