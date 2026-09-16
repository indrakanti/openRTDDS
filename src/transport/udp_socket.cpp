#include "openrtdds/transport/udp_socket.hpp"

#include <cerrno>
#include <cstring>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace openrtdds::transport {
namespace {

constexpr std::size_t maximum_udp_payload_size = 65'507U;

[[nodiscard]] sockaddr_in to_native(const UdpEndpoint& endpoint) noexcept {
  sockaddr_in native{};
  native.sin_family = AF_INET;
  native.sin_port = htons(endpoint.port);
  static_assert(sizeof(native.sin_addr.s_addr) == 4U,
                "OpenRTDDS requires a 32-bit IPv4 address");
  std::memcpy(&native.sin_addr.s_addr, endpoint.address.octets.data(), 4U);
  return native;
}

[[nodiscard]] UdpEndpoint from_native(const sockaddr_in& native) noexcept {
  UdpEndpoint endpoint{};
  std::memcpy(endpoint.address.octets.data(), &native.sin_addr.s_addr, 4U);
  endpoint.port = ntohs(native.sin_port);
  return endpoint;
}

[[nodiscard]] UdpResult receive_error(const int native_error) noexcept {
  if ((native_error == EAGAIN) || (native_error == EWOULDBLOCK)) {
    return {UdpError::would_block, native_error, 0U};
  }
  return {UdpError::receive_error, native_error, 0U};
}

[[nodiscard]] UdpResult send_error(const int native_error) noexcept {
  if ((native_error == EAGAIN) || (native_error == EWOULDBLOCK)) {
    return {UdpError::would_block, native_error, 0U};
  }
  return {UdpError::send_error, native_error, 0U};
}

}  // namespace

const char* to_string(const UdpError error) noexcept {
  switch (error) {
    case UdpError::none:
      return "none";
    case UdpError::invalid_argument:
      return "invalid argument";
    case UdpError::not_open:
      return "socket is not open";
    case UdpError::socket_error:
      return "socket creation failed";
    case UdpError::bind_error:
      return "socket bind failed";
    case UdpError::endpoint_error:
      return "socket endpoint query failed";
    case UdpError::send_error:
      return "UDP send failed";
    case UdpError::receive_error:
      return "UDP receive failed";
    case UdpError::would_block:
      return "operation would block";
    case UdpError::truncated:
      return "UDP datagram was truncated";
    case UdpError::message_too_large:
      return "UDP payload exceeds protocol maximum";
  }
  return "unknown UDP error";
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : descriptor_(other.descriptor_) {
  other.descriptor_ = -1;
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
  if (this != &other) {
    close();
    descriptor_ = other.descriptor_;
    other.descriptor_ = -1;
  }
  return *this;
}

UdpSocket::~UdpSocket() noexcept { close(); }

UdpResult UdpSocket::open() noexcept {
  if (is_open()) {
    return {};
  }
  descriptor_ =
      ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (descriptor_ < 0) {
    const int native_error = errno;
    descriptor_ = -1;
    return {UdpError::socket_error, native_error, 0U};
  }
  return {};
}

UdpResult UdpSocket::bind(const UdpEndpoint& local) noexcept {
  if (!is_open()) {
    return {UdpError::not_open, 0, 0U};
  }
  const sockaddr_in native = to_native(local);
  if (::bind(descriptor_, reinterpret_cast<const sockaddr*>(&native),
             sizeof(native)) != 0) {
    return {UdpError::bind_error, errno, 0U};
  }
  return {};
}

UdpResult UdpSocket::local_endpoint(UdpEndpoint& local) const noexcept {
  if (!is_open()) {
    return {UdpError::not_open, 0, 0U};
  }
  sockaddr_in native{};
  socklen_t native_size = sizeof(native);
  if (::getsockname(descriptor_, reinterpret_cast<sockaddr*>(&native),
                    &native_size) != 0) {
    return {UdpError::endpoint_error, errno, 0U};
  }
  if ((native_size != sizeof(native)) || (native.sin_family != AF_INET)) {
    return {UdpError::endpoint_error, 0, 0U};
  }
  local = from_native(native);
  return {};
}

UdpResult UdpSocket::send_to(const UdpEndpoint& remote,
                             const std::uint8_t* const data,
                             const std::size_t size) noexcept {
  if (!is_open()) {
    return {UdpError::not_open, 0, 0U};
  }
  if (((data == nullptr) && (size != 0U)) || (remote.port == 0U)) {
    return {UdpError::invalid_argument, 0, 0U};
  }
  if (size > maximum_udp_payload_size) {
    return {UdpError::message_too_large, 0, 0U};
  }

  const sockaddr_in native = to_native(remote);
  const ssize_t sent =
      ::sendto(descriptor_, data, size, MSG_DONTWAIT | MSG_NOSIGNAL,
               reinterpret_cast<const sockaddr*>(&native), sizeof(native));
  if (sent < 0) {
    return send_error(errno);
  }
  if (static_cast<std::size_t>(sent) != size) {
    return {UdpError::send_error, 0, static_cast<std::size_t>(sent)};
  }
  return {UdpError::none, 0, size};
}

UdpResult UdpSocket::receive_from(std::uint8_t* const buffer,
                                  const std::size_t capacity,
                                  UdpEndpoint& remote) noexcept {
  if (!is_open()) {
    return {UdpError::not_open, 0, 0U};
  }
  if ((buffer == nullptr) && (capacity != 0U)) {
    return {UdpError::invalid_argument, 0, 0U};
  }

  sockaddr_in native{};
  socklen_t native_size = sizeof(native);
  const ssize_t received =
      ::recvfrom(descriptor_, buffer, capacity, MSG_DONTWAIT | MSG_TRUNC,
                 reinterpret_cast<sockaddr*>(&native), &native_size);
  if (received < 0) {
    return receive_error(errno);
  }
  if ((native_size != sizeof(native)) || (native.sin_family != AF_INET)) {
    return {UdpError::receive_error, 0, 0U};
  }
  remote = from_native(native);
  const std::size_t received_size = static_cast<std::size_t>(received);
  if (received_size > capacity) {
    return {UdpError::truncated, 0, received_size};
  }
  return {UdpError::none, 0, received_size};
}

void UdpSocket::close() noexcept {
  if (descriptor_ >= 0) {
    static_cast<void>(::close(descriptor_));
    descriptor_ = -1;
  }
}

}  // namespace openrtdds::transport
