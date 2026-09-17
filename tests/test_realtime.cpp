#include <cstdint>
#include <limits>

#include "openrtdds/os/linux/realtime.hpp"
#include "test_support.hpp"

// Verifies: ORT-LNX-002, ORT-LNX-003, ORT-LNX-004

void test_realtime() {
  using openrtdds::os::linux_rt::RealtimeError;

  const std::uint64_t first =
      openrtdds::os::linux_rt::monotonic_time_ns();
  const std::uint64_t second =
      openrtdds::os::linux_rt::monotonic_time_ns();
  CHECK(first != 0U);
  CHECK(second >= first);

  const auto affinity_result =
      openrtdds::os::linux_rt::set_current_thread_affinity(
          std::numeric_limits<std::size_t>::max());
  CHECK(affinity_result.error == RealtimeError::invalid_argument);

  const auto fifo_result =
      openrtdds::os::linux_rt::set_current_thread_fifo(0);
  CHECK(fifo_result.error == RealtimeError::invalid_argument);
}
