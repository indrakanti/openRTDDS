#include <iostream>

#include "openrtdds/core/runtime_limits.hpp"
#include "openrtdds/os/linux/realtime.hpp"
#include "openrtdds/version.hpp"

// Demonstrates: ORT-LNX-001, ORT-LNX-004

int main() {
  const openrtdds::core::RuntimeLimits limits{};
  const auto limit_error = limits.validate();
  if (limit_error != openrtdds::core::LimitError::none) {
    std::cerr << "invalid limits: " << openrtdds::core::to_string(limit_error)
              << '\n';
    return 1;
  }

  const auto memory_result = openrtdds::os::linux_rt::lock_process_memory();
  std::cout << "OpenRTDDS " << openrtdds::version() << '\n'
            << "CLOCK_MONOTONIC(ns): "
            << openrtdds::os::linux_rt::monotonic_time_ns() << '\n'
            << "memory lock: "
            << openrtdds::os::linux_rt::to_string(memory_result.error);
  if (!memory_result.ok()) {
    std::cout << " (errno=" << memory_result.native_error << ')';
  }
  std::cout << '\n';
  return 0;
}
