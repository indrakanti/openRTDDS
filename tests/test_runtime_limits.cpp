#include "openrtdds/core/runtime_limits.hpp"
#include "test_support.hpp"

// Verifies: ORT-CORE-001

void test_runtime_limits() {
  openrtdds::core::RuntimeLimits limits{};
  CHECK(limits.validate() == openrtdds::core::LimitError::none);

  limits.max_participants = 0U;
  CHECK(limits.validate() ==
        openrtdds::core::LimitError::zero_participants);

  limits = {};
  limits.max_topics = 0U;
  CHECK(limits.validate() == openrtdds::core::LimitError::zero_topics);

  limits = {};
  limits.max_writers = 0U;
  CHECK(limits.validate() == openrtdds::core::LimitError::zero_endpoints);

  limits = {};
  limits.max_readers = 0U;
  CHECK(limits.validate() == openrtdds::core::LimitError::zero_endpoints);

  limits = {};
  limits.max_samples = 0U;
  CHECK(limits.validate() == openrtdds::core::LimitError::zero_samples);

  limits = {};
  limits.history_depth = limits.max_samples + 1U;
  CHECK(limits.validate() ==
        openrtdds::core::LimitError::invalid_history_depth);

  limits = {};
  limits.max_payload_bytes = 0U;
  CHECK(limits.validate() == openrtdds::core::LimitError::zero_payload);

  limits = {};
  limits.history_depth = 0U;
  CHECK(limits.validate() ==
        openrtdds::core::LimitError::invalid_history_depth);
}
