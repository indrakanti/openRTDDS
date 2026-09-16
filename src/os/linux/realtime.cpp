#include "openrtdds/os/linux/realtime.hpp"

#include <cerrno>
#include <ctime>

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>

namespace openrtdds::os::linux_rt {
namespace {

[[nodiscard]] RealtimeResult translate_error(const int native_error) noexcept {
  switch (native_error) {
    case 0:
      return {};
    case EINVAL:
      return {RealtimeError::invalid_argument, native_error};
    case EPERM:
    case EACCES:
      return {RealtimeError::permission_denied, native_error};
    case ENOMEM:
    case EAGAIN:
      return {RealtimeError::resource_limit, native_error};
    default:
      return {RealtimeError::operating_system_error, native_error};
  }
}

}  // namespace

RealtimeResult lock_process_memory() noexcept {
  if (::mlockall(MCL_CURRENT | MCL_FUTURE) == 0) {
    return {};
  }
  return translate_error(errno);
}

RealtimeResult set_current_thread_affinity(const std::size_t cpu_index) noexcept {
  if (cpu_index >= static_cast<std::size_t>(CPU_SETSIZE)) {
    return {RealtimeError::invalid_argument, EINVAL};
  }

  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  CPU_SET(cpu_index, &cpu_set);
  return translate_error(
      ::pthread_setaffinity_np(::pthread_self(), sizeof(cpu_set), &cpu_set));
}

RealtimeResult set_current_thread_fifo(const int priority) noexcept {
  const int minimum = ::sched_get_priority_min(SCHED_FIFO);
  const int maximum = ::sched_get_priority_max(SCHED_FIFO);
  if ((minimum == -1) || (maximum == -1)) {
    return translate_error(errno);
  }
  if ((priority < minimum) || (priority > maximum)) {
    return {RealtimeError::invalid_argument, EINVAL};
  }

  sched_param parameters{};
  parameters.sched_priority = priority;
  return translate_error(::pthread_setschedparam(
      ::pthread_self(), SCHED_FIFO, &parameters));
}

std::uint64_t monotonic_time_ns() noexcept {
  timespec now{};
  if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0U;
  }
  constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000ULL;
  return (static_cast<std::uint64_t>(now.tv_sec) * nanoseconds_per_second) +
         static_cast<std::uint64_t>(now.tv_nsec);
}

const char* to_string(const RealtimeError error) noexcept {
  switch (error) {
    case RealtimeError::none:
      return "none";
    case RealtimeError::invalid_argument:
      return "invalid argument";
    case RealtimeError::permission_denied:
      return "permission denied";
    case RealtimeError::resource_limit:
      return "resource limit";
    case RealtimeError::operating_system_error:
      return "operating system error";
  }
  return "unknown realtime error";
}

}  // namespace openrtdds::os::linux_rt
