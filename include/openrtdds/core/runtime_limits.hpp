#pragma once

#include <cstddef>
#include <cstdint>

namespace openrtdds::core {

// Requirements: ORT-CORE-001

enum class LimitError : std::uint8_t {
  none = 0,
  zero_participants,
  zero_topics,
  zero_endpoints,
  zero_samples,
  zero_payload,
  invalid_history_depth,
};

struct RuntimeLimits final {
  std::size_t max_participants{16U};
  std::size_t max_topics{128U};
  std::size_t max_writers{256U};
  std::size_t max_readers{256U};
  std::size_t max_samples{1024U};
  std::size_t max_payload_bytes{64U * 1024U};
  std::size_t history_depth{4U};

  [[nodiscard]] constexpr LimitError validate() const noexcept {
    if (max_participants == 0U) {
      return LimitError::zero_participants;
    }
    if (max_topics == 0U) {
      return LimitError::zero_topics;
    }
    if ((max_writers == 0U) || (max_readers == 0U)) {
      return LimitError::zero_endpoints;
    }
    if (max_samples == 0U) {
      return LimitError::zero_samples;
    }
    if (max_payload_bytes == 0U) {
      return LimitError::zero_payload;
    }
    if ((history_depth == 0U) || (history_depth > max_samples)) {
      return LimitError::invalid_history_depth;
    }
    return LimitError::none;
  }
};

[[nodiscard]] const char* to_string(LimitError error) noexcept;

}  // namespace openrtdds::core
