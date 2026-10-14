#include "rmw_openrtdds_cpp/qos.hpp"

#include <array>
#include <cstddef>
#include <cstring>
#include <limits>

#include "rmw/error_handling.h"
#include "rmw/qos_profiles.h"

// Verifies: ORT-RMW-043, ORT-RMW-044
namespace {

constexpr rmw_time_t kInfinite{9223372036ULL, 854775807ULL};

int fail(const int code) {
  rmw_reset_error();
  return code;
}

rmw_qos_profile_t explicit_profile() {
  rmw_qos_profile_t profile = rmw_qos_profile_default;
  profile.deadline = kInfinite;
  profile.lifespan = kInfinite;
  profile.liveliness = RMW_QOS_POLICY_LIVELINESS_AUTOMATIC;
  profile.liveliness_lease_duration = kInfinite;
  return profile;
}

}  // namespace

int main() {
  using rmw_openrtdds_cpp::EndpointResourcePlan;
  using rmw_openrtdds_cpp::EndpointRole;
  using rmw_openrtdds_cpp::NormalizedQos;
  using rmw_openrtdds_cpp::QosError;
  using rmw_openrtdds_cpp::QosLimits;
  using rmw_openrtdds_cpp::TypeSupportInfo;

  const QosLimits limits{};
  QosError error = QosError::invalid_argument;
  NormalizedQos normalized{};
  rmw_qos_profile_t system_default = rmw_qos_profile_system_default;
  system_default.avoid_ros_namespace_conventions = true;
  if (!rmw_openrtdds_cpp::normalize_qos(
          system_default, limits, normalized, error)) {
    return fail(1);
  }
  if ((error != QosError::none) ||
      (normalized.profile.history != RMW_QOS_POLICY_HISTORY_KEEP_LAST) ||
      (normalized.profile.depth != limits.default_history_depth) ||
      (normalized.profile.reliability !=
       RMW_QOS_POLICY_RELIABILITY_RELIABLE) ||
      (normalized.profile.durability != RMW_QOS_POLICY_DURABILITY_VOLATILE) ||
      (normalized.profile.liveliness !=
       RMW_QOS_POLICY_LIVELINESS_AUTOMATIC) ||
      !normalized.profile.avoid_ros_namespace_conventions ||
      (normalized.profile.deadline.sec != kInfinite.sec) ||
      (normalized.profile.deadline.nsec != kInfinite.nsec)) {
    return fail(2);
  }

  const NormalizedQos sentinel = normalized;
  rmw_qos_profile_t rejected = rmw_qos_profile_unknown;
  if (rmw_openrtdds_cpp::normalize_qos(
          rejected, limits, normalized, error) ||
      (error != QosError::invalid_policy) ||
      (normalized.profile.depth != sentinel.profile.depth)) {
    return fail(3);
  }
  rejected = rmw_qos_profile_default;
  rejected.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  if (rmw_openrtdds_cpp::normalize_qos(
          rejected, limits, normalized, error) ||
      (error != QosError::unsupported_durability)) {
    return fail(4);
  }
  rejected = rmw_qos_profile_default;
  rejected.deadline = rmw_time_t{0U, 1U};
  if (rmw_openrtdds_cpp::normalize_qos(
          rejected, limits, normalized, error) ||
      (error != QosError::unsupported_deadline)) {
    return fail(5);
  }
  rejected = rmw_qos_profile_best_available;
  if (rmw_openrtdds_cpp::normalize_qos(
          rejected, limits, normalized, error) ||
      (error != QosError::unsupported_best_available)) {
    return fail(6);
  }
  rejected = rmw_qos_profile_default;
  rejected.depth = 0U;
  if (rmw_openrtdds_cpp::normalize_qos(
          rejected, limits, normalized, error) ||
      (error != QosError::depth_out_of_range)) {
    return fail(7);
  }

  TypeSupportInfo type{};
  type.maximum_serialized_size = 128U;
  EndpointResourcePlan plan{};
  rmw_qos_profile_t reliable = explicit_profile();
  reliable.depth = 4U;
  if (!rmw_openrtdds_cpp::plan_endpoint_resources(
          reliable, type, EndpointRole::publisher, limits, plan, error)) {
    return fail(8);
  }
  if ((plan.history_sample_slots != 4U) ||
      (plan.history_storage_bytes != 512U) ||
      (plan.reliability_repair_slots != 4U) ||
      (plan.maximum_serialized_sample_size != 128U)) {
    return fail(9);
  }

  const EndpointResourcePlan saved_plan = plan;
  QosLimits byte_limited = limits;
  byte_limited.maximum_history_bytes = 511U;
  if (rmw_openrtdds_cpp::plan_endpoint_resources(
          reliable, type, EndpointRole::publisher, byte_limited, plan, error) ||
      (error != QosError::history_bytes_out_of_range) ||
      (plan.history_storage_bytes != saved_plan.history_storage_bytes)) {
    return fail(10);
  }

  QosLimits overflow_limits{};
  overflow_limits.default_history_depth = 2U;
  overflow_limits.maximum_history_depth =
      std::numeric_limits<std::size_t>::max();
  overflow_limits.maximum_sample_size =
      std::numeric_limits<std::size_t>::max();
  overflow_limits.maximum_history_bytes =
      std::numeric_limits<std::size_t>::max();
  type.maximum_serialized_size = std::numeric_limits<std::size_t>::max();
  reliable.depth = 2U;
  if (rmw_openrtdds_cpp::plan_endpoint_resources(
          reliable, type, EndpointRole::publisher, overflow_limits, plan,
          error) ||
      (error != QosError::arithmetic_overflow)) {
    return fail(11);
  }

  reliable = explicit_profile();
  NormalizedQos publisher{};
  NormalizedQos subscription{};
  if (!rmw_openrtdds_cpp::normalize_qos(
          reliable, limits, publisher, error) ||
      !rmw_openrtdds_cpp::normalize_qos(
          reliable, limits, subscription, error) ||
      !rmw_openrtdds_cpp::qos_compatible(publisher, subscription)) {
    return fail(12);
  }
  publisher.profile.reliability =
      RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  if (rmw_openrtdds_cpp::qos_compatible(publisher, subscription)) {
    return fail(13);
  }

  rmw_qos_compatibility_type_t compatibility = RMW_QOS_COMPATIBILITY_WARNING;
  std::array<char, 512U> reason{};
  if ((rmw_qos_profile_check_compatible(
           reliable, reliable, &compatibility, reason.data(), reason.size()) !=
       RMW_RET_OK) ||
      (compatibility != RMW_QOS_COMPATIBILITY_OK) ||
      (reason[0] != '\0')) {
    return fail(14);
  }
  rmw_qos_profile_t best_effort = reliable;
  best_effort.reliability = RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  if ((rmw_qos_profile_check_compatible(
           best_effort, reliable, &compatibility, reason.data(),
           reason.size()) != RMW_RET_OK) ||
      (compatibility != RMW_QOS_COMPATIBILITY_ERROR) ||
      (std::strstr(reason.data(), "reliability") == nullptr)) {
    return fail(15);
  }
  rmw_qos_profile_t warning_target = rmw_qos_profile_default;
  warning_target.liveliness = RMW_QOS_POLICY_LIVELINESS_AUTOMATIC;
  if ((rmw_qos_profile_check_compatible(
           rmw_qos_profile_system_default, warning_target, &compatibility,
           reason.data(), reason.size()) != RMW_RET_OK) ||
      (compatibility != RMW_QOS_COMPATIBILITY_WARNING) ||
      (std::strstr(reason.data(), "system-default") == nullptr)) {
    return fail(16);
  }

  rmw_reset_error();
  compatibility = RMW_QOS_COMPATIBILITY_ERROR;
  if ((rmw_qos_profile_check_compatible(
           reliable, reliable, nullptr, nullptr, 0U) !=
       RMW_RET_INVALID_ARGUMENT) ||
      !rmw_error_is_set() ||
      (compatibility != RMW_QOS_COMPATIBILITY_ERROR)) {
    return fail(17);
  }
  rmw_reset_error();
  std::array<char, 8U> short_reason{};
  if ((rmw_qos_profile_check_compatible(
           best_effort, reliable, &compatibility, short_reason.data(),
           short_reason.size()) != RMW_RET_OK) ||
      (short_reason.back() != '\0')) {
    return fail(18);
  }

  rejected = rmw_qos_profile_default;
  rejected.history = RMW_QOS_POLICY_HISTORY_KEEP_ALL;
  if (rmw_openrtdds_cpp::normalize_qos(
          rejected, limits, normalized, error) ||
      (error != QosError::unsupported_history)) {
    return fail(19);
  }
  rejected = rmw_qos_profile_default;
  rejected.lifespan = rmw_time_t{1U, 0U};
  if (rmw_openrtdds_cpp::normalize_qos(
          rejected, limits, normalized, error) ||
      (error != QosError::unsupported_lifespan)) {
    return fail(20);
  }
  rejected = rmw_qos_profile_default;
  rejected.liveliness = RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC;
  if (rmw_openrtdds_cpp::normalize_qos(
          rejected, limits, normalized, error) ||
      (error != QosError::unsupported_liveliness)) {
    return fail(21);
  }
  rejected = rmw_qos_profile_default;
  rejected.liveliness_lease_duration = rmw_time_t{1U, 0U};
  if (rmw_openrtdds_cpp::normalize_qos(
          rejected, limits, normalized, error) ||
      (error != QosError::unsupported_lease_duration)) {
    return fail(22);
  }
  rejected = rmw_qos_profile_default;
  rejected.deadline = rmw_time_t{0U, 1000000000U};
  if (rmw_openrtdds_cpp::normalize_qos(
          rejected, limits, normalized, error) ||
      (error != QosError::invalid_argument)) {
    return fail(23);
  }

  type.maximum_serialized_size = limits.maximum_sample_size + 1U;
  plan = saved_plan;
  reliable = explicit_profile();
  if (rmw_openrtdds_cpp::plan_endpoint_resources(
          reliable, type, EndpointRole::subscription, limits, plan, error) ||
      (error != QosError::sample_size_out_of_range) ||
      (plan.history_storage_bytes != saved_plan.history_storage_bytes)) {
    return fail(24);
  }

  rmw_qos_profile_t incompatible = reliable;
  incompatible.durability = RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  if ((rmw_qos_profile_check_compatible(
           reliable, incompatible, &compatibility, reason.data(),
           reason.size()) != RMW_RET_OK) ||
      (compatibility != RMW_QOS_COMPATIBILITY_ERROR) ||
      (std::strstr(reason.data(), "durability") == nullptr)) {
    return fail(25);
  }
  incompatible = reliable;
  incompatible.deadline = rmw_time_t{1U, 0U};
  if ((rmw_qos_profile_check_compatible(
           rmw_qos_profile_default, incompatible, &compatibility,
           reason.data(), reason.size()) != RMW_RET_OK) ||
      (compatibility != RMW_QOS_COMPATIBILITY_ERROR) ||
      (std::strstr(reason.data(), "deadline") == nullptr)) {
    return fail(26);
  }
  incompatible = reliable;
  incompatible.liveliness = RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC;
  if ((rmw_qos_profile_check_compatible(
           reliable, incompatible, &compatibility, reason.data(),
           reason.size()) != RMW_RET_OK) ||
      (compatibility != RMW_QOS_COMPATIBILITY_ERROR) ||
      (std::strstr(reason.data(), "liveliness") == nullptr)) {
    return fail(27);
  }
  incompatible = reliable;
  incompatible.liveliness_lease_duration = rmw_time_t{1U, 0U};
  if ((rmw_qos_profile_check_compatible(
           rmw_qos_profile_default, incompatible, &compatibility,
           reason.data(), reason.size()) != RMW_RET_OK) ||
      (compatibility != RMW_QOS_COMPATIBILITY_ERROR) ||
      (std::strstr(reason.data(), "lease") == nullptr)) {
    return fail(28);
  }

  rmw_qos_profile_t default_duration = rmw_qos_profile_default;
  default_duration.liveliness = RMW_QOS_POLICY_LIVELINESS_AUTOMATIC;
  if ((rmw_qos_profile_check_compatible(
           default_duration, default_duration, &compatibility,
           reason.data(), reason.size()) != RMW_RET_OK) ||
      (compatibility != RMW_QOS_COMPATIBILITY_OK) ||
      (reason[0] != '\0')) {
    return fail(29);
  }

  return 0;
}
