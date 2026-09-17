#include "openrtdds/rtps/reliability_state.hpp"

#include <cstring>

namespace openrtdds::rtps {

const char* to_string(const ReliabilityError error) noexcept {
  switch (error) {
    case ReliabilityError::none:
      return "none";
    case ReliabilityError::invalid_configuration:
      return "invalid reliability configuration";
    case ReliabilityError::invalid_argument:
      return "invalid argument";
    case ReliabilityError::unexpected_peer:
      return "control message does not match configured peer";
    case ReliabilityError::invalid_sequence_number:
      return "invalid reliability sequence number";
    case ReliabilityError::non_monotonic_sequence:
      return "writer sequence number is not monotonic";
    case ReliabilityError::payload_too_large:
      return "payload exceeds reliability history capacity";
    case ReliabilityError::history_full:
      return "reliable writer history is full";
    case ReliabilityError::action_capacity_exceeded:
      return "reliability action capacity exceeded";
    case ReliabilityError::stale_control:
      return "duplicate or stale reliability control message";
    case ReliabilityError::duplicate_data:
      return "duplicate DATA sequence";
    case ReliabilityError::stale_data:
      return "stale DATA sequence";
    case ReliabilityError::receive_window_exceeded:
      return "DATA sequence exceeds bounded receive window";
    case ReliabilityError::time_regression:
      return "monotonic time regressed";
    case ReliabilityError::gap_not_repairable:
      return "writer history no longer covers reader gap";
  }
  return "unknown reliability error";
}

bool control_count_is_newer(const std::int32_t candidate,
                            const std::int32_t reference) noexcept {
  std::uint32_t candidate_bits = 0U;
  std::uint32_t reference_bits = 0U;
  std::memcpy(&candidate_bits, &candidate, sizeof(candidate_bits));
  std::memcpy(&reference_bits, &reference, sizeof(reference_bits));
  const std::uint32_t difference = candidate_bits - reference_bits;
  return (difference != 0U) && (difference < 0x80000000U);
}

std::int32_t next_control_count(const std::int32_t count) noexcept {
  std::uint32_t bits = 0U;
  std::memcpy(&bits, &count, sizeof(bits));
  ++bits;
  std::int32_t result = 0;
  std::memcpy(&result, &bits, sizeof(result));
  return result;
}

}  // namespace openrtdds::rtps
