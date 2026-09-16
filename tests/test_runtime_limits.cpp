#include "openrtdds/core/runtime_limits.hpp"
#include "test_support.hpp"

void test_runtime_limits() {
  openrtdds::core::RuntimeLimits limits{};
  CHECK(limits.validate() == openrtdds::core::LimitError::none);

  limits.history_depth = limits.max_samples + 1U;
  CHECK(limits.validate() ==
        openrtdds::core::LimitError::invalid_history_depth);

  limits.history_depth = 1U;
  limits.max_payload_bytes = 0U;
  CHECK(limits.validate() == openrtdds::core::LimitError::zero_payload);
}

