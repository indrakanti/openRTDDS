#pragma once

#include <cstddef>
#include <cstdint>

namespace openrtdds::os::linux_rt {

enum class RealtimeError : std::uint8_t {
  none = 0,
  invalid_argument,
  permission_denied,
  resource_limit,
  operating_system_error,
};

struct RealtimeResult final {
  RealtimeError error{RealtimeError::none};
  int native_error{0};

  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == RealtimeError::none;
  }
};

// Lock current and future process mappings to avoid demand paging at runtime.
[[nodiscard]] RealtimeResult lock_process_memory() noexcept;

// Pin the calling thread to a zero-based logical CPU.
[[nodiscard]] RealtimeResult set_current_thread_affinity(
    std::size_t cpu_index) noexcept;

// Set SCHED_FIFO on the calling thread. Valid priorities are determined by
// sched_get_priority_min/max(SCHED_FIFO), normally 1..99.
[[nodiscard]] RealtimeResult set_current_thread_fifo(int priority) noexcept;

// Use CLOCK_MONOTONIC for interval/deadline measurements.
[[nodiscard]] std::uint64_t monotonic_time_ns() noexcept;

[[nodiscard]] const char* to_string(RealtimeError error) noexcept;

}  // namespace openrtdds::os::linux_rt
