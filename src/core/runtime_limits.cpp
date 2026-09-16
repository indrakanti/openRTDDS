#include "openrtdds/core/runtime_limits.hpp"

namespace openrtdds::core {

const char* to_string(const LimitError error) noexcept {
  switch (error) {
    case LimitError::none:
      return "none";
    case LimitError::zero_participants:
      return "max_participants must be nonzero";
    case LimitError::zero_topics:
      return "max_topics must be nonzero";
    case LimitError::zero_endpoints:
      return "max_writers and max_readers must be nonzero";
    case LimitError::zero_samples:
      return "max_samples must be nonzero";
    case LimitError::zero_payload:
      return "max_payload_bytes must be nonzero";
    case LimitError::invalid_history_depth:
      return "history_depth must be within max_samples";
  }
  return "unknown limit error";
}

}  // namespace openrtdds::core

