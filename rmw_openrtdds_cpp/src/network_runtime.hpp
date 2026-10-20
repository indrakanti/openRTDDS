#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>

#include "openrtdds/rtps/sedp.hpp"
#include "openrtdds/rtps/spdp.hpp"
#include "openrtdds/transport/udp_socket.hpp"
#include "rmw/types.h"

namespace rmw_openrtdds_cpp::detail {

// Requirements: ORT-RMW-004, ORT-RMW-005, ORT-RMW-016, ORT-RMW-051
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

 private:
  static constexpr std::size_t kRemoteParticipantCapacity = 16U;
  static constexpr std::size_t kRemoteEndpointCapacity = 32U;
  static constexpr std::size_t kMaximumDatagramSize = 65'507U;

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

  void run() noexcept;
  void announce(std::uint64_t now_ns) noexcept;
  void receive_discovery(std::uint64_t now_ns) noexcept;
  void receive_user_data(std::uint64_t now_ns) noexcept;
  void send_pending_samples() noexcept;
  void expire_remote_state(std::uint64_t now_ns) noexcept;
  void recompute_matches() noexcept;

  [[nodiscard]] bool configure_sockets() noexcept;
  [[nodiscard]] bool build_spdp() noexcept;
  [[nodiscard]] RemoteParticipant* find_participant(
      const openrtdds::rtps::GuidPrefix& prefix) noexcept;
  [[nodiscard]] const RemoteEndpoint* find_remote_writer(
      const openrtdds::rtps::GuidPrefix& prefix,
      const openrtdds::rtps::EntityId& entity) const noexcept;

  rmw_context_t* context_{nullptr};
  openrtdds::transport::UdpSocket multicast_socket_{};
  openrtdds::transport::UdpSocket metadata_socket_{};
  openrtdds::transport::UdpSocket user_socket_{};
  openrtdds::transport::UdpEndpoint spdp_destination_{};
  std::array<std::uint8_t, 2'048U> spdp_{};
  std::size_t spdp_size_{0U};
  std::array<std::uint8_t, kMaximumDatagramSize> receive_buffer_{};
  std::array<std::uint8_t, kMaximumDatagramSize> transmit_buffer_{};
  std::array<RemoteParticipant, kRemoteParticipantCapacity> participants_{};
  std::array<RemoteEndpoint, kRemoteEndpointCapacity> endpoints_{};
  std::array<std::uint64_t, 8U> last_sent_sequence_{};
  std::array<std::uint32_t, 8U> publisher_generations_{};
  std::thread worker_{};
  std::mutex wake_mutex_{};
  std::condition_variable wake_condition_{};
  std::atomic<bool> stop_{false};
  std::atomic<bool> started_{false};
  std::uint64_t wake_generation_{1U};
};

}  // namespace rmw_openrtdds_cpp::detail
