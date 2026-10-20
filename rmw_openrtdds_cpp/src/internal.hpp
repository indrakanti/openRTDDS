#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "openrtdds/rmw/foundation.hpp"
#include "rmw/rmw.h"

namespace rmw_openrtdds_cpp::detail {

// Requirements: ORT-RMW-030, ORT-RMW-031, ORT-RMW-032, ORT-RMW-033
// Requirements: ORT-RMW-045, ORT-RMW-046, ORT-RMW-047, ORT-RMW-048
constexpr std::size_t kNodeCapacity = 8U;
constexpr std::size_t kGuardConditionCapacity = 16U;
constexpr std::size_t kPublisherCapacity = 8U;
constexpr std::size_t kSubscriptionCapacity = 8U;
constexpr std::size_t kWaitSetCapacity = 8U;
constexpr std::size_t kWaitConditionCapacity = 32U;

using Foundation = openrtdds::rmw::AdapterContext<
    kNodeCapacity, kGuardConditionCapacity>;

struct NodeData final {
  openrtdds::rmw::NodeHandle handle{};
  std::size_t publisher_count{0U};
  std::size_t subscription_count{0U};
};

struct GuardConditionData final {
  openrtdds::rmw::GuardConditionHandle handle{};
  rmw_context_t* context{nullptr};
  std::uint64_t last_observed_generation{0U};
};

struct PublisherSlot final {
  void* data{nullptr};
  std::uint32_t generation{1U};
  bool occupied{false};
};

struct SubscriptionSlot final {
  void* data{nullptr};
  std::uint32_t generation{1U};
  bool occupied{false};
};

struct WaitSetSlot final {
  void* data{nullptr};
  std::uint32_t generation{1U};
  bool occupied{false};
};

[[nodiscard]] inline bool identifier_matches(
    const char* const identifier) noexcept {
  return (identifier != nullptr) &&
         (std::strcmp(identifier,
                      openrtdds::rmw::kImplementationIdentifier) == 0);
}

}  // namespace rmw_openrtdds_cpp::detail

// This is the implementation-owned definition promised by rmw/init.h.
struct rmw_context_impl_s final {
  rmw_openrtdds_cpp::detail::Foundation foundation{};
  rmw_guard_condition_t* graph_guard_condition{nullptr};
  std::array<rmw_openrtdds_cpp::detail::PublisherSlot,
             rmw_openrtdds_cpp::detail::kPublisherCapacity>
      publishers{};
  std::mutex publisher_registry_mutex{};
  std::array<rmw_openrtdds_cpp::detail::SubscriptionSlot,
             rmw_openrtdds_cpp::detail::kSubscriptionCapacity>
      subscriptions{};
  std::mutex subscription_registry_mutex{};
  std::array<rmw_openrtdds_cpp::detail::WaitSetSlot,
             rmw_openrtdds_cpp::detail::kWaitSetCapacity>
      wait_sets{};
  std::mutex wait_set_registry_mutex{};
  std::mutex wait_mutex{};
  std::condition_variable wait_condition{};
  std::uint64_t wait_generation{1U};
  std::atomic<bool> shutdown{false};
};

namespace rmw_openrtdds_cpp::detail {

[[nodiscard]] inline bool context_is_valid(
    const rmw_context_t* const context) noexcept {
  return (context != nullptr) &&
         identifier_matches(context->implementation_identifier) &&
         (context->impl != nullptr);
}

[[nodiscard]] inline bool context_has_publishers(
    const rmw_context_t* const context) noexcept {
  if (!context_is_valid(context)) {
    return false;
  }
  for (const auto& slot : context->impl->publishers) {
    if (slot.occupied) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool context_has_subscriptions(
    const rmw_context_t* const context) noexcept {
  if (!context_is_valid(context)) {
    return false;
  }
  for (const auto& slot : context->impl->subscriptions) {
    if (slot.occupied) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool context_has_wait_sets(
    const rmw_context_t* const context) noexcept {
  if (!context_is_valid(context)) {
    return false;
  }
  for (const auto& slot : context->impl->wait_sets) {
    if (slot.occupied) {
      return true;
    }
  }
  return false;
}

inline void notify_waiters(rmw_context_t* const context) noexcept {
  if (!context_is_valid(context)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(context->impl->wait_mutex);
    ++context->impl->wait_generation;
    if (context->impl->wait_generation == 0U) {
      context->impl->wait_generation = 1U;
    }
  }
  context->impl->wait_condition.notify_all();
}

[[nodiscard]] bool subscription_wait_data_valid(
    const void* data, const rmw_context_t* context) noexcept;

[[nodiscard]] bool subscription_wait_ready(const void* data) noexcept;

}  // namespace rmw_openrtdds_cpp::detail
