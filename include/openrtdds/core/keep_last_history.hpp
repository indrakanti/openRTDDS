#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace openrtdds::core {

// Requirements: ORT-HIST-001, ORT-HIST-002, ORT-HIST-003, ORT-HIST-004

enum class HistoryError : std::uint8_t {
  none = 0,
  invalid_argument,
  payload_too_large,
};

struct SampleMetadata final {
  std::uint64_t sequence_number{0U};
  std::uint64_t source_timestamp_ns{0U};
};

template <std::size_t MaxPayloadBytes>
class BoundedSample final {
  static_assert(MaxPayloadBytes > 0U,
                "BoundedSample payload capacity must be nonzero");

 public:
  [[nodiscard]] HistoryError assign(const std::uint8_t* payload,
                                    const std::size_t payload_size,
                                    const SampleMetadata metadata) noexcept {
    if ((payload == nullptr) && (payload_size != 0U)) {
      return HistoryError::invalid_argument;
    }
    if (payload_size > MaxPayloadBytes) {
      return HistoryError::payload_too_large;
    }
    if (payload_size != 0U) {
      std::memcpy(payload_.data(), payload, payload_size);
    }
    size_ = payload_size;
    metadata_ = metadata;
    return HistoryError::none;
  }

  [[nodiscard]] const std::uint8_t* data() const noexcept {
    return payload_.data();
  }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr std::size_t capacity() const noexcept {
    return MaxPayloadBytes;
  }
  [[nodiscard]] const SampleMetadata& metadata() const noexcept {
    return metadata_;
  }

 private:
  std::array<std::uint8_t, MaxPayloadBytes> payload_{};
  std::size_t size_{0U};
  SampleMetadata metadata_{};
};

// Fixed-capacity DDS KEEP_LAST history. When full, a successful push replaces
// the oldest sample. Invalid inputs never modify history state.
template <std::size_t Depth, std::size_t MaxPayloadBytes>
class KeepLastHistory final {
  static_assert(Depth > 0U, "KEEP_LAST depth must be nonzero");

 public:
  using Sample = BoundedSample<MaxPayloadBytes>;

  [[nodiscard]] HistoryError push(const std::uint8_t* payload,
                                  const std::size_t payload_size,
                                  const SampleMetadata metadata) noexcept {
    if ((payload == nullptr) && (payload_size != 0U)) {
      return HistoryError::invalid_argument;
    }
    if (payload_size > MaxPayloadBytes) {
      return HistoryError::payload_too_large;
    }

    const HistoryError result =
        samples_[write_index_].assign(payload, payload_size, metadata);
    if (result != HistoryError::none) {
      return result;
    }

    if (size_ == Depth) {
      oldest_index_ = increment(oldest_index_);
    } else {
      ++size_;
    }
    write_index_ = increment(write_index_);
    return HistoryError::none;
  }

  [[nodiscard]] const Sample* oldest() const noexcept {
    return empty() ? nullptr : &samples_[oldest_index_];
  }

  [[nodiscard]] const Sample* newest() const noexcept {
    if (empty()) {
      return nullptr;
    }
    const std::size_t newest_index =
        write_index_ == 0U ? Depth - 1U : write_index_ - 1U;
    return &samples_[newest_index];
  }

  [[nodiscard]] bool pop_oldest() noexcept {
    if (empty()) {
      return false;
    }
    oldest_index_ = increment(oldest_index_);
    --size_;
    return true;
  }

  void clear() noexcept {
    size_ = 0U;
    oldest_index_ = 0U;
    write_index_ = 0U;
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr std::size_t capacity() const noexcept {
    return Depth;
  }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
  [[nodiscard]] bool full() const noexcept { return size_ == Depth; }

 private:
  [[nodiscard]] static constexpr std::size_t increment(
      const std::size_t index) noexcept {
    return (index + 1U) % Depth;
  }

  std::array<Sample, Depth> samples_{};
  std::size_t size_{0U};
  std::size_t oldest_index_{0U};
  std::size_t write_index_{0U};
};

}  // namespace openrtdds::core
