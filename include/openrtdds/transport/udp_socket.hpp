#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace openrtdds::transport {

struct Ipv4Address final {
  std::array<std::uint8_t, 4U> octets{};

  [[nodiscard]] static constexpr Ipv4Address any() noexcept {
    return {{{0U, 0U, 0U, 0U}}};
  }

  [[nodiscard]] static constexpr Ipv4Address loopback() noexcept {
    return {{{127U, 0U, 0U, 1U}}};
  }
};

[[nodiscard]] constexpr bool operator==(const Ipv4Address& left,
                                        const Ipv4Address& right) noexcept {
  return (left.octets[0] == right.octets[0]) &&
         (left.octets[1] == right.octets[1]) &&
         (left.octets[2] == right.octets[2]) &&
         (left.octets[3] == right.octets[3]);
}

struct UdpEndpoint final {
  Ipv4Address address{};
  std::uint16_t port{0U};
};

enum class UdpError : std::uint8_t {
  none = 0,
  invalid_argument,
  not_open,
  socket_error,
  bind_error,
  endpoint_error,
  send_error,
  receive_error,
  would_block,
  truncated,
  message_too_large,
};

struct UdpResult final {
  UdpError error{UdpError::none};
  int native_error{0};
  std::size_t bytes{0U};

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == UdpError::none;
  }
};

[[nodiscard]] const char* to_string(UdpError error) noexcept;

// Linux UDPv4 socket with nonblocking send/receive and close-on-exec. It owns
// only a file descriptor and performs no heap allocation.
class UdpSocket final {
 public:
  UdpSocket() noexcept = default;
  UdpSocket(const UdpSocket&) = delete;
  UdpSocket& operator=(const UdpSocket&) = delete;
  UdpSocket(UdpSocket&& other) noexcept;
  UdpSocket& operator=(UdpSocket&& other) noexcept;
  ~UdpSocket() noexcept;

  [[nodiscard]] UdpResult open() noexcept;
  [[nodiscard]] UdpResult bind(const UdpEndpoint& local) noexcept;
  [[nodiscard]] UdpResult local_endpoint(UdpEndpoint& local) const noexcept;
  [[nodiscard]] UdpResult send_to(const UdpEndpoint& remote,
                                  const std::uint8_t* data,
                                  std::size_t size) noexcept;
  [[nodiscard]] UdpResult receive_from(std::uint8_t* buffer,
                                       std::size_t capacity,
                                       UdpEndpoint& remote) noexcept;
  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return descriptor_ >= 0; }

 private:
  int descriptor_{-1};
};

}  // namespace openrtdds::transport
