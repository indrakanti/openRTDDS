#include "rmw_openrtdds_cpp/qos.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "rmw/error_handling.h"
#include "rmw/qos_profiles.h"

namespace rmw_openrtdds_cpp {
// Requirements: ORT-RMW-043, ORT-RMW-044
namespace {

constexpr rmw_time_t kUnspecified{0U, 0U};
constexpr rmw_time_t kInfinite{9223372036ULL, 854775807ULL};
constexpr rmw_time_t kBestAvailable{9223372036ULL, 854775806ULL};

[[nodiscard]] bool time_equal(const rmw_time_t& left,
                              const rmw_time_t& right) noexcept {
  return (left.sec == right.sec) && (left.nsec == right.nsec);
}

[[nodiscard]] bool valid_time(const rmw_time_t& value) noexcept {
  return value.nsec < 1000000000ULL;
}

[[nodiscard]] bool deterministic_infinite(const rmw_time_t& value) noexcept {
  return time_equal(value, kUnspecified) || time_equal(value, kInfinite);
}

[[nodiscard]] bool fail(QosError& error, const QosError value) noexcept {
  error = value;
  return false;
}

[[nodiscard]] bool normalize_profile(const rmw_qos_profile_t& input,
                                     const QosLimits& limits,
                                     rmw_qos_profile_t& profile,
                                     QosError& error) noexcept {
  if ((limits.default_history_depth == 0U) ||
      (limits.maximum_history_depth == 0U) ||
      (limits.default_history_depth > limits.maximum_history_depth) ||
      (limits.maximum_sample_size == 0U) ||
      (limits.maximum_history_bytes == 0U)) {
    return fail(error, QosError::invalid_argument);
  }

  profile = input;
  switch (input.history) {
    case RMW_QOS_POLICY_HISTORY_SYSTEM_DEFAULT:
      profile.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
      profile.depth = limits.default_history_depth;
      break;
    case RMW_QOS_POLICY_HISTORY_KEEP_LAST:
      break;
    case RMW_QOS_POLICY_HISTORY_KEEP_ALL:
      return fail(error, QosError::unsupported_history);
    case RMW_QOS_POLICY_HISTORY_UNKNOWN:
    default:
      return fail(error, QosError::invalid_policy);
  }
  if ((profile.depth == 0U) ||
      (profile.depth > limits.maximum_history_depth)) {
    return fail(error, QosError::depth_out_of_range);
  }

  switch (input.reliability) {
    case RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT:
      profile.reliability = RMW_QOS_POLICY_RELIABILITY_RELIABLE;
      break;
    case RMW_QOS_POLICY_RELIABILITY_RELIABLE:
    case RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT:
      break;
    case RMW_QOS_POLICY_RELIABILITY_BEST_AVAILABLE:
      return fail(error, QosError::unsupported_best_available);
    case RMW_QOS_POLICY_RELIABILITY_UNKNOWN:
    default:
      return fail(error, QosError::invalid_policy);
  }

  switch (input.durability) {
    case RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT:
      profile.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
      break;
    case RMW_QOS_POLICY_DURABILITY_VOLATILE:
      break;
    case RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL:
      return fail(error, QosError::unsupported_durability);
    case RMW_QOS_POLICY_DURABILITY_BEST_AVAILABLE:
      return fail(error, QosError::unsupported_best_available);
    case RMW_QOS_POLICY_DURABILITY_UNKNOWN:
    default:
      return fail(error, QosError::invalid_policy);
  }

  if (!valid_time(input.deadline)) {
    return fail(error, QosError::invalid_argument);
  }
  if (time_equal(input.deadline, kBestAvailable)) {
    return fail(error, QosError::unsupported_best_available);
  }
  if (!deterministic_infinite(input.deadline)) {
    return fail(error, QosError::unsupported_deadline);
  }
  profile.deadline = kInfinite;

  if (!valid_time(input.lifespan)) {
    return fail(error, QosError::invalid_argument);
  }
  if (!deterministic_infinite(input.lifespan)) {
    return fail(error, QosError::unsupported_lifespan);
  }
  profile.lifespan = kInfinite;

  switch (input.liveliness) {
    case RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT:
      profile.liveliness = RMW_QOS_POLICY_LIVELINESS_AUTOMATIC;
      break;
    case RMW_QOS_POLICY_LIVELINESS_AUTOMATIC:
      break;
    case RMW_QOS_POLICY_LIVELINESS_BEST_AVAILABLE:
      return fail(error, QosError::unsupported_best_available);
    // Jazzy assigns value 2 to the deprecated MANUAL_BY_NODE enumerator.
    // Compare by value so warnings-as-errors builds do not name that symbol.
    case static_cast<rmw_qos_liveliness_policy_t>(2):
    case RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC:
      return fail(error, QosError::unsupported_liveliness);
    case RMW_QOS_POLICY_LIVELINESS_UNKNOWN:
    default:
      return fail(error, QosError::invalid_policy);
  }

  if (!valid_time(input.liveliness_lease_duration)) {
    return fail(error, QosError::invalid_argument);
  }
  if (time_equal(input.liveliness_lease_duration, kBestAvailable)) {
    return fail(error, QosError::unsupported_best_available);
  }
  if (!deterministic_infinite(input.liveliness_lease_duration)) {
    return fail(error, QosError::unsupported_lease_duration);
  }
  profile.liveliness_lease_duration = kInfinite;
  return true;
}

}  // namespace

const char* to_string(const QosError error) noexcept {
  switch (error) {
    case QosError::none:
      return "none";
    case QosError::invalid_argument:
      return "invalid_argument";
    case QosError::invalid_policy:
      return "invalid_policy";
    case QosError::unsupported_best_available:
      return "unsupported_best_available";
    case QosError::unsupported_history:
      return "unsupported_history";
    case QosError::unsupported_durability:
      return "unsupported_durability";
    case QosError::unsupported_deadline:
      return "unsupported_deadline";
    case QosError::unsupported_lifespan:
      return "unsupported_lifespan";
    case QosError::unsupported_liveliness:
      return "unsupported_liveliness";
    case QosError::unsupported_lease_duration:
      return "unsupported_lease_duration";
    case QosError::depth_out_of_range:
      return "depth_out_of_range";
    case QosError::sample_size_out_of_range:
      return "sample_size_out_of_range";
    case QosError::history_bytes_out_of_range:
      return "history_bytes_out_of_range";
    case QosError::arithmetic_overflow:
      return "arithmetic_overflow";
  }
  return "unknown";
}

bool normalize_qos(const rmw_qos_profile_t& input, const QosLimits& limits,
                   NormalizedQos& output, QosError& error) noexcept {
  rmw_qos_profile_t profile{};
  if (!normalize_profile(input, limits, profile, error)) {
    return false;
  }
  output.profile = profile;
  error = QosError::none;
  return true;
}

bool plan_endpoint_resources(const rmw_qos_profile_t& input,
                             const TypeSupportInfo& type,
                             const EndpointRole role, const QosLimits& limits,
                             EndpointResourcePlan& output,
                             QosError& error) noexcept {
  if ((role != EndpointRole::publisher) &&
      (role != EndpointRole::subscription)) {
    return fail(error, QosError::invalid_argument);
  }
  NormalizedQos normalized{};
  if (!normalize_qos(input, limits, normalized, error)) {
    return false;
  }
  if ((type.maximum_serialized_size == 0U) ||
      (type.maximum_serialized_size > limits.maximum_sample_size)) {
    return fail(error, QosError::sample_size_out_of_range);
  }
  if (normalized.profile.depth >
      std::numeric_limits<std::size_t>::max() /
          type.maximum_serialized_size) {
    return fail(error, QosError::arithmetic_overflow);
  }
  const std::size_t history_bytes =
      normalized.profile.depth * type.maximum_serialized_size;
  if (history_bytes > limits.maximum_history_bytes) {
    return fail(error, QosError::history_bytes_out_of_range);
  }

  EndpointResourcePlan candidate{};
  candidate.role = role;
  candidate.qos = normalized;
  candidate.maximum_serialized_sample_size = type.maximum_serialized_size;
  candidate.history_sample_slots = normalized.profile.depth;
  candidate.history_storage_bytes = history_bytes;
  candidate.reliability_repair_slots =
      (role == EndpointRole::publisher &&
       normalized.profile.reliability ==
           RMW_QOS_POLICY_RELIABILITY_RELIABLE)
          ? normalized.profile.depth
          : 0U;
  output = candidate;
  error = QosError::none;
  return true;
}

bool qos_compatible(const NormalizedQos& publisher,
                    const NormalizedQos& subscription) noexcept {
  if ((publisher.profile.reliability ==
       RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT) &&
      (subscription.profile.reliability ==
       RMW_QOS_POLICY_RELIABILITY_RELIABLE)) {
    return false;
  }
  if ((publisher.profile.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE) &&
      (subscription.profile.durability ==
       RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL)) {
    return false;
  }
  return true;
}

}  // namespace rmw_openrtdds_cpp

namespace {

struct CompatibilityFindings final {
  bool reliability_error{false};
  bool durability_error{false};
  bool deadline_error{false};
  bool liveliness_error{false};
  bool lease_error{false};
  bool reliability_warning{false};
  bool durability_warning{false};
  bool deadline_warning{false};
  bool liveliness_warning{false};
  bool lease_warning{false};
};

[[nodiscard]] bool ambiguous_reliability(
    const rmw_qos_reliability_policy_t value) noexcept {
  return value == RMW_QOS_POLICY_RELIABILITY_SYSTEM_DEFAULT ||
         value == RMW_QOS_POLICY_RELIABILITY_UNKNOWN ||
         value == RMW_QOS_POLICY_RELIABILITY_BEST_AVAILABLE;
}

[[nodiscard]] bool ambiguous_durability(
    const rmw_qos_durability_policy_t value) noexcept {
  return value == RMW_QOS_POLICY_DURABILITY_SYSTEM_DEFAULT ||
         value == RMW_QOS_POLICY_DURABILITY_UNKNOWN ||
         value == RMW_QOS_POLICY_DURABILITY_BEST_AVAILABLE;
}

[[nodiscard]] bool ambiguous_liveliness(
    const rmw_qos_liveliness_policy_t value) noexcept {
  return value == RMW_QOS_POLICY_LIVELINESS_SYSTEM_DEFAULT ||
         value == RMW_QOS_POLICY_LIVELINESS_UNKNOWN ||
         value == RMW_QOS_POLICY_LIVELINESS_BEST_AVAILABLE;
}

[[nodiscard]] bool ambiguous_duration(const rmw_time_t& value) noexcept {
  constexpr rmw_time_t best_available{9223372036ULL, 854775806ULL};
  return ((value.sec == best_available.sec) &&
          (value.nsec == best_available.nsec));
}

[[nodiscard]] bool unspecified_duration(const rmw_time_t& value) noexcept {
  return (value.sec == 0U) && (value.nsec == 0U);
}

[[nodiscard]] int compare_time(const rmw_time_t& left,
                               const rmw_time_t& right) noexcept {
  if (left.sec != right.sec) {
    return left.sec < right.sec ? -1 : 1;
  }
  if (left.nsec != right.nsec) {
    return left.nsec < right.nsec ? -1 : 1;
  }
  return 0;
}

void append_reason(char* reason, const std::size_t capacity,
                   std::size_t& used, const char* message) noexcept {
  if ((reason == nullptr) || (capacity == 0U)) {
    return;
  }
  if (used != 0U) {
    if (used + 1U < capacity) {
      reason[used++] = ';';
      reason[used] = '\0';
    } else {
      return;
    }
  }
  while ((*message != '\0') && (used + 1U < capacity)) {
    reason[used++] = *message++;
  }
  reason[used] = '\0';
}

}  // namespace

extern "C" rmw_ret_t rmw_qos_profile_check_compatible(
    const rmw_qos_profile_t publisher_profile,
    const rmw_qos_profile_t subscription_profile,
    rmw_qos_compatibility_type_t* compatibility, char* reason,
    const std::size_t reason_size) {
  // Requirements: ORT-RMW-043
  if (compatibility == nullptr) {
    RMW_SET_ERROR_MSG("compatibility output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if ((reason == nullptr) && (reason_size != 0U)) {
    RMW_SET_ERROR_MSG("reason is null while reason_size is nonzero");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if ((publisher_profile.deadline.nsec >= 1000000000ULL) ||
      (subscription_profile.deadline.nsec >= 1000000000ULL) ||
      (publisher_profile.liveliness_lease_duration.nsec >= 1000000000ULL) ||
      (subscription_profile.liveliness_lease_duration.nsec >= 1000000000ULL)) {
    RMW_SET_ERROR_MSG("QoS duration nanoseconds must be less than one second");
    return RMW_RET_INVALID_ARGUMENT;
  }

  CompatibilityFindings findings{};
  const bool publisher_reliability_ambiguous =
      ambiguous_reliability(publisher_profile.reliability);
  const bool subscription_reliability_ambiguous =
      ambiguous_reliability(subscription_profile.reliability);
  findings.reliability_warning =
      (publisher_reliability_ambiguous &&
       subscription_reliability_ambiguous) ||
      (publisher_reliability_ambiguous &&
       subscription_profile.reliability ==
           RMW_QOS_POLICY_RELIABILITY_RELIABLE) ||
      (publisher_profile.reliability ==
           RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT &&
       subscription_reliability_ambiguous);
  const bool publisher_durability_ambiguous =
      ambiguous_durability(publisher_profile.durability);
  const bool subscription_durability_ambiguous =
      ambiguous_durability(subscription_profile.durability);
  findings.durability_warning =
      (publisher_durability_ambiguous &&
       subscription_durability_ambiguous) ||
      (publisher_durability_ambiguous &&
       subscription_profile.durability ==
           RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL) ||
      (publisher_profile.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE &&
       subscription_durability_ambiguous);
  findings.deadline_warning = ambiguous_duration(publisher_profile.deadline) ||
                              ambiguous_duration(subscription_profile.deadline);
  const bool publisher_liveliness_ambiguous =
      ambiguous_liveliness(publisher_profile.liveliness);
  const bool subscription_liveliness_ambiguous =
      ambiguous_liveliness(subscription_profile.liveliness);
  findings.liveliness_warning =
      (publisher_liveliness_ambiguous &&
       subscription_liveliness_ambiguous) ||
      (publisher_liveliness_ambiguous &&
       subscription_profile.liveliness ==
           RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC) ||
      (publisher_profile.liveliness ==
           RMW_QOS_POLICY_LIVELINESS_AUTOMATIC &&
       subscription_liveliness_ambiguous);
  findings.lease_warning =
      ambiguous_duration(publisher_profile.liveliness_lease_duration) ||
      ambiguous_duration(subscription_profile.liveliness_lease_duration);

  if (!findings.reliability_warning) {
    findings.reliability_error =
        publisher_profile.reliability ==
            RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT &&
        subscription_profile.reliability ==
            RMW_QOS_POLICY_RELIABILITY_RELIABLE;
  }
  if (!findings.durability_warning) {
    findings.durability_error =
        publisher_profile.durability == RMW_QOS_POLICY_DURABILITY_VOLATILE &&
        subscription_profile.durability ==
            RMW_QOS_POLICY_DURABILITY_TRANSIENT_LOCAL;
  }
  if (!findings.deadline_warning) {
    const bool publisher_unspecified =
        unspecified_duration(publisher_profile.deadline);
    const bool subscription_unspecified =
        unspecified_duration(subscription_profile.deadline);
    findings.deadline_error =
        (publisher_unspecified && !subscription_unspecified) ||
        (!publisher_unspecified && !subscription_unspecified &&
         compare_time(publisher_profile.deadline,
                      subscription_profile.deadline) > 0);
  }
  if (!findings.liveliness_warning) {
    findings.liveliness_error =
        publisher_profile.liveliness ==
            RMW_QOS_POLICY_LIVELINESS_AUTOMATIC &&
        subscription_profile.liveliness ==
            RMW_QOS_POLICY_LIVELINESS_MANUAL_BY_TOPIC;
  }
  if (!findings.lease_warning) {
    const bool publisher_unspecified =
        unspecified_duration(publisher_profile.liveliness_lease_duration);
    const bool subscription_unspecified =
        unspecified_duration(subscription_profile.liveliness_lease_duration);
    findings.lease_error =
        (publisher_unspecified && !subscription_unspecified) ||
        (!publisher_unspecified && !subscription_unspecified &&
         compare_time(publisher_profile.liveliness_lease_duration,
                      subscription_profile.liveliness_lease_duration) > 0);
  }

  const bool has_error = findings.reliability_error ||
                         findings.durability_error || findings.deadline_error ||
                         findings.liveliness_error || findings.lease_error;
  const bool has_warning = findings.reliability_warning ||
                           findings.durability_warning ||
                           findings.deadline_warning ||
                           findings.liveliness_warning || findings.lease_warning;
  rmw_qos_compatibility_type_t result = RMW_QOS_COMPATIBILITY_OK;
  if (has_error) {
    result = RMW_QOS_COMPATIBILITY_ERROR;
  } else if (has_warning) {
    result = RMW_QOS_COMPATIBILITY_WARNING;
  }

  std::size_t used = 0U;
  if ((reason != nullptr) && (reason_size != 0U)) {
    reason[0] = '\0';
  }
  if (findings.reliability_error) {
    append_reason(reason, reason_size, used,
                  "publisher reliability does not satisfy subscription");
  }
  if (findings.durability_error) {
    append_reason(reason, reason_size, used,
                  "publisher durability does not satisfy subscription");
  }
  if (findings.deadline_error) {
    append_reason(reason, reason_size, used,
                  "publisher deadline does not satisfy subscription");
  }
  if (findings.liveliness_error) {
    append_reason(reason, reason_size, used,
                  "publisher liveliness does not satisfy subscription");
  }
  if (findings.lease_error) {
    append_reason(reason, reason_size, used,
                  "publisher lease duration does not satisfy subscription");
  }
  if (findings.reliability_warning) {
    append_reason(reason, reason_size, used,
                  "reliability contains system-default, unknown, or best-available");
  }
  if (findings.durability_warning) {
    append_reason(reason, reason_size, used,
                  "durability contains system-default, unknown, or best-available");
  }
  if (findings.deadline_warning) {
    append_reason(reason, reason_size, used,
                  "deadline contains best-available");
  }
  if (findings.liveliness_warning) {
    append_reason(reason, reason_size, used,
                  "liveliness contains system-default, unknown, or best-available");
  }
  if (findings.lease_warning) {
    append_reason(reason, reason_size, used,
                  "lease duration contains best-available");
  }

  *compatibility = result;
  return RMW_RET_OK;
}
