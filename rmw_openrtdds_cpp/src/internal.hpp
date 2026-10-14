#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "openrtdds/rmw/foundation.hpp"
#include "rmw/rmw.h"

namespace rmw_openrtdds_cpp::detail {

// Requirements: ORT-RMW-030, ORT-RMW-031, ORT-RMW-032, ORT-RMW-033
// Requirements: ORT-RMW-045, ORT-RMW-046
constexpr std::size_t kNodeCapacity = 8U;
constexpr std::size_t kGuardConditionCapacity = 16U;
constexpr std::size_t kPublisherCapacity = 8U;

using Foundation = openrtdds::rmw::AdapterContext<
    kNodeCapacity, kGuardConditionCapacity>;

struct NodeData final {
  openrtdds::rmw::NodeHandle handle{};
  std::size_t publisher_count{0U};
};

struct GuardConditionData final {
  openrtdds::rmw::GuardConditionHandle handle{};
};

struct PublisherSlot final {
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
  bool shutdown{false};
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

}  // namespace rmw_openrtdds_cpp::detail
