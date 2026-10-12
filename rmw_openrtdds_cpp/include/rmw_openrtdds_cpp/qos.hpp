#pragma once

#include <cstddef>
#include <cstdint>

#include "rmw/types.h"
#include "rmw_openrtdds_cpp/type_support.hpp"
#include "rmw_openrtdds_cpp/visibility_control.h"

namespace rmw_openrtdds_cpp {

// Requirements: ORT-RMW-043, ORT-RMW-044
enum class EndpointRole : std::uint8_t {
  publisher = 0,
  subscription,
};

enum class QosError : std::uint8_t {
  none = 0,
  invalid_argument,
  invalid_policy,
  unsupported_best_available,
  unsupported_history,
  unsupported_durability,
  unsupported_deadline,
  unsupported_lifespan,
  unsupported_liveliness,
  unsupported_lease_duration,
  depth_out_of_range,
  sample_size_out_of_range,
  history_bytes_out_of_range,
  arithmetic_overflow,
};

struct QosLimits final {
  std::size_t default_history_depth{10U};
  std::size_t maximum_history_depth{1024U};
  std::size_t maximum_sample_size{64U * 1024U};
  std::size_t maximum_history_bytes{64U * 1024U * 1024U};
};

struct NormalizedQos final {
  rmw_qos_profile_t profile{};
};

struct EndpointResourcePlan final {
  EndpointRole role{EndpointRole::publisher};
  NormalizedQos qos{};
  std::size_t maximum_serialized_sample_size{0U};
  std::size_t history_sample_slots{0U};
  std::size_t history_storage_bytes{0U};
  std::size_t reliability_repair_slots{0U};
};

[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC const char* to_string(
    QosError error) noexcept;

// Resolves only deterministic defaults. Unsupported or discovery-dependent
// policies fail explicitly, no allocation occurs, and output is unchanged on
// failure.
[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC bool normalize_qos(
    const rmw_qos_profile_t& input, const QosLimits& limits,
    NormalizedQos& output, QosError& error) noexcept;

// Produces the exact sample-slot and byte reservations consumed by future
// endpoint creation. Type analysis must already have established a finite
// maximum serialized size. Output is unchanged on failure.
[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC bool plan_endpoint_resources(
    const rmw_qos_profile_t& input, const TypeSupportInfo& type,
    EndpointRole role, const QosLimits& limits, EndpointResourcePlan& output,
    QosError& error) noexcept;

// Applies the DDS requested/offered relation to profiles already accepted by
// normalize_qos().
[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC bool qos_compatible(
    const NormalizedQos& publisher,
    const NormalizedQos& subscription) noexcept;

}  // namespace rmw_openrtdds_cpp
