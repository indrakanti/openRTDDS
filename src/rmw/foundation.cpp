#include "openrtdds/rmw/foundation.hpp"

namespace openrtdds::rmw {

// Requirements: ORT-RMW-028
const char* to_string(const AdapterError error) noexcept {
  switch (error) {
    case AdapterError::none:
      return "none";
    case AdapterError::invalid_argument:
      return "invalid_argument";
    case AdapterError::incorrect_implementation:
      return "incorrect_implementation";
    case AdapterError::invalid_limits:
      return "invalid_limits";
    case AdapterError::invalid_domain:
      return "invalid_domain";
    case AdapterError::invalid_state:
      return "invalid_state";
    case AdapterError::resource_exhausted:
      return "resource_exhausted";
    case AdapterError::name_too_long:
      return "name_too_long";
    case AdapterError::stale_handle:
      return "stale_handle";
    case AdapterError::unsupported:
      return "unsupported";
  }
  return "unknown";
}

}  // namespace openrtdds::rmw
