#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "openrtdds/core/keep_last_history.hpp"
#include "openrtdds/rtps/reliability_messages.hpp"

namespace openrtdds::rtps {

// Requirements: ORT-REL-001, ORT-REL-004, ORT-REL-005, ORT-REL-006

enum class ReliabilityError : std::uint8_t {
  none = 0,
  invalid_configuration,
  invalid_argument,
  unexpected_peer,
  invalid_sequence_number,
  non_monotonic_sequence,
  payload_too_large,
  history_full,
  action_capacity_exceeded,
  stale_control,
  duplicate_data,
  stale_data,
  receive_window_exceeded,
  time_regression,
  gap_not_repairable,
};

[[nodiscard]] const char* to_string(ReliabilityError error) noexcept;

enum class ReliabilityActionKind : std::uint8_t {
  none = 0,
  send_data,
  retransmit_data,
  send_acknack,
  sample_received,
  sample_delivered,
  sample_failed,
};

enum class RepairFailure : std::uint8_t {
  none = 0,
  repair_limit_exceeded,
  repair_window_expired,
};

struct ReliabilityAction final {
  ReliabilityActionKind kind{ReliabilityActionKind::none};
  std::uint64_t sequence_number{0U};
  EntityId reader_id{};
  EntityId writer_id{};
  const std::uint8_t* data{nullptr};
  std::size_t data_size{0U};
  SequenceNumberSet reader_state{};
  std::int32_t control_count{0};
  RepairFailure failure{RepairFailure::none};
  bool final_flag{false};
};

template <std::size_t Capacity>
class ReliabilityActionBuffer final {
  static_assert(Capacity > 0U, "action capacity must be nonzero");

 public:
  [[nodiscard]] bool push(const ReliabilityAction& action) noexcept {
    if (size_ == Capacity) {
      return false;
    }
    actions_[size_] = action;
    ++size_;
    return true;
  }

  void clear() noexcept { size_ = 0U; }

  [[nodiscard]] const ReliabilityAction& operator[](
      const std::size_t index) const noexcept {
    return actions_[index];
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr std::size_t capacity() const noexcept {
    return Capacity;
  }
  [[nodiscard]] std::size_t remaining() const noexcept {
    return Capacity - size_;
  }

 private:
  std::array<ReliabilityAction, Capacity> actions_{};
  std::size_t size_{0U};
};

struct ReliableWriterConfig final {
  EntityId reader_id{};
  EntityId writer_id{};
  std::uint8_t max_repair_attempts{2U};
  std::uint64_t repair_window_ns{100'000'000U};
};

struct ReliableReaderConfig final {
  EntityId reader_id{};
  EntityId writer_id{};
  std::uint64_t initial_sequence_number{1U};
};

[[nodiscard]] bool control_count_is_newer(std::int32_t candidate,
                                          std::int32_t reference) noexcept;
[[nodiscard]] std::int32_t next_control_count(std::int32_t count) noexcept;

template <std::size_t HistoryDepth, std::size_t MaxPayloadBytes>
class ReliableWriter final {
  static_assert(HistoryDepth > 0U, "writer history must be nonzero");
  static_assert(MaxPayloadBytes > 0U, "payload capacity must be nonzero");

 public:
  explicit ReliableWriter(const ReliableWriterConfig config = {}) noexcept
      : config_(config), valid_(config.repair_window_ns != 0U) {}

  template <std::size_t ActionCapacity>
  [[nodiscard]] ReliabilityError write(
      const std::uint8_t* const payload, const std::size_t payload_size,
      const std::uint64_t sequence_number, const std::uint64_t now_ns,
      ReliabilityActionBuffer<ActionCapacity>& actions) noexcept {
    if (!valid_) {
      return ReliabilityError::invalid_configuration;
    }
    if ((payload == nullptr) && (payload_size != 0U)) {
      return ReliabilityError::invalid_argument;
    }
    if (payload_size > MaxPayloadBytes) {
      return ReliabilityError::payload_too_large;
    }
    if (!valid_state_sequence(sequence_number)) {
      return ReliabilityError::invalid_sequence_number;
    }
    if ((last_sequence_number_ != 0U) &&
        (sequence_number <= last_sequence_number_)) {
      return ReliabilityError::non_monotonic_sequence;
    }
    if (size_ == HistoryDepth) {
      return ReliabilityError::history_full;
    }
    if (actions.remaining() == 0U) {
      return ReliabilityError::action_capacity_exceeded;
    }

    Record& record = records_[size_];
    const core::HistoryError history_error = record.sample.assign(
        payload, payload_size,
        core::SampleMetadata{sequence_number, now_ns});
    if (history_error == core::HistoryError::invalid_argument) {
      return ReliabilityError::invalid_argument;
    }
    if (history_error == core::HistoryError::payload_too_large) {
      return ReliabilityError::payload_too_large;
    }
    record.first_send_ns = now_ns;
    record.repair_attempts = 0U;
    ++size_;
    last_sequence_number_ = sequence_number;

    ReliabilityAction action{};
    action.kind = ReliabilityActionKind::send_data;
    action.sequence_number = sequence_number;
    action.data = record.sample.data();
    action.data_size = record.sample.size();
    static_cast<void>(actions.push(action));
    return ReliabilityError::none;
  }

  template <std::size_t ActionCapacity>
  [[nodiscard]] ReliabilityError on_acknack(
      const AckNackView& acknack, const std::uint64_t now_ns,
      ReliabilityActionBuffer<ActionCapacity>& actions) noexcept {
    if (!valid_) {
      return ReliabilityError::invalid_configuration;
    }
    if ((acknack.reader_id != config_.reader_id) ||
        (acknack.writer_id != config_.writer_id)) {
      return ReliabilityError::unexpected_peer;
    }
    if (acknack_count_initialized_ &&
        !control_count_is_newer(acknack.count, last_acknack_count_)) {
      return ReliabilityError::stale_control;
    }

    std::array<Decision, HistoryDepth> decisions{};
    std::size_t required_actions = 0U;
    for (std::size_t index = 0U; index < size_; ++index) {
      const Record& record = records_[index];
      const std::uint64_t sequence_number =
          record.sample.metadata().sequence_number;
      Decision decision = Decision::keep;
      if (sequence_number < acknack.reader_state.bitmap_base()) {
        decision = Decision::deliver;
      } else {
        const std::uint64_t offset =
            sequence_number - acknack.reader_state.bitmap_base();
        if (offset < acknack.reader_state.num_bits()) {
          if (!acknack.reader_state.test(static_cast<std::uint32_t>(offset))) {
            decision = Decision::deliver;
          } else {
            if (now_ns < record.first_send_ns) {
              return ReliabilityError::time_regression;
            }
            const std::uint64_t elapsed = now_ns - record.first_send_ns;
            if (elapsed >= config_.repair_window_ns) {
              decision = Decision::fail_window;
            } else if (record.repair_attempts >=
                       config_.max_repair_attempts) {
              decision = Decision::fail_attempts;
            } else {
              decision = Decision::repair;
            }
          }
        }
      }
      decisions[index] = decision;
      if (decision != Decision::keep) {
        ++required_actions;
      }
    }

    if (required_actions > actions.remaining()) {
      return ReliabilityError::action_capacity_exceeded;
    }

    std::size_t destination = 0U;
    for (std::size_t index = 0U; index < size_; ++index) {
      const Decision decision = decisions[index];
      const std::uint64_t sequence_number =
          records_[index].sample.metadata().sequence_number;
      if ((decision == Decision::keep) || (decision == Decision::repair)) {
        if (destination != index) {
          records_[destination] = records_[index];
        }
        Record& retained = records_[destination];
        if (decision == Decision::repair) {
          ++retained.repair_attempts;
          ReliabilityAction action{};
          action.kind = ReliabilityActionKind::retransmit_data;
          action.sequence_number = sequence_number;
          action.data = retained.sample.data();
          action.data_size = retained.sample.size();
          static_cast<void>(actions.push(action));
        }
        ++destination;
      } else {
        ReliabilityAction action{};
        action.sequence_number = sequence_number;
        if (decision == Decision::deliver) {
          action.kind = ReliabilityActionKind::sample_delivered;
        } else {
          action.kind = ReliabilityActionKind::sample_failed;
          action.failure = decision == Decision::fail_window
                               ? RepairFailure::repair_window_expired
                               : RepairFailure::repair_limit_exceeded;
        }
        static_cast<void>(actions.push(action));
      }
    }
    size_ = destination;
    last_acknack_count_ = acknack.count;
    acknack_count_initialized_ = true;
    return ReliabilityError::none;
  }

  template <std::size_t ActionCapacity>
  [[nodiscard]] ReliabilityError on_timer(
      const std::uint64_t now_ns,
      ReliabilityActionBuffer<ActionCapacity>& actions) noexcept {
    if (!valid_) {
      return ReliabilityError::invalid_configuration;
    }

    std::size_t expired = 0U;
    for (std::size_t index = 0U; index < size_; ++index) {
      if (now_ns < records_[index].first_send_ns) {
        return ReliabilityError::time_regression;
      }
      if ((now_ns - records_[index].first_send_ns) >=
          config_.repair_window_ns) {
        ++expired;
      }
    }
    if (expired > actions.remaining()) {
      return ReliabilityError::action_capacity_exceeded;
    }

    std::size_t destination = 0U;
    for (std::size_t index = 0U; index < size_; ++index) {
      const bool is_expired =
          (now_ns - records_[index].first_send_ns) >=
          config_.repair_window_ns;
      if (is_expired) {
        ReliabilityAction action{};
        action.kind = ReliabilityActionKind::sample_failed;
        action.sequence_number =
            records_[index].sample.metadata().sequence_number;
        action.failure = RepairFailure::repair_window_expired;
        static_cast<void>(actions.push(action));
      } else {
        if (destination != index) {
          records_[destination] = records_[index];
        }
        ++destination;
      }
    }
    size_ = destination;
    return ReliabilityError::none;
  }

  [[nodiscard]] ReliabilityError fill_heartbeat(
      HeartbeatConfig& heartbeat) noexcept {
    if (!valid_) {
      return ReliabilityError::invalid_configuration;
    }
    heartbeat.first_sequence_number =
        size_ == 0U
            ? last_sequence_number_ + 1U
            : records_[0U].sample.metadata().sequence_number;
    heartbeat.last_sequence_number = last_sequence_number_;
    heartbeat.reader_id = config_.reader_id;
    heartbeat.writer_id = config_.writer_id;
    heartbeat_count_ = next_control_count(heartbeat_count_);
    heartbeat.count = heartbeat_count_;
    return ReliabilityError::none;
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] constexpr std::size_t capacity() const noexcept {
    return HistoryDepth;
  }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
  [[nodiscard]] bool full() const noexcept { return size_ == HistoryDepth; }
  [[nodiscard]] std::uint64_t last_sequence_number() const noexcept {
    return last_sequence_number_;
  }
  [[nodiscard]] std::uint8_t repair_attempts(
      const std::size_t index) const noexcept {
    return index < size_ ? records_[index].repair_attempts : 0U;
  }

 private:
  using Sample = core::BoundedSample<MaxPayloadBytes>;

  struct Record final {
    Sample sample{};
    std::uint64_t first_send_ns{0U};
    std::uint8_t repair_attempts{0U};
  };

  enum class Decision : std::uint8_t {
    keep,
    deliver,
    repair,
    fail_window,
    fail_attempts,
  };

  [[nodiscard]] static bool valid_state_sequence(
      const std::uint64_t sequence_number) noexcept {
    return (sequence_number != 0U) &&
           (sequence_number < static_cast<std::uint64_t>(
                                  std::numeric_limits<std::int64_t>::max()));
  }

  ReliableWriterConfig config_{};
  std::array<Record, HistoryDepth> records_{};
  std::size_t size_{0U};
  std::uint64_t last_sequence_number_{0U};
  std::int32_t heartbeat_count_{0};
  std::int32_t last_acknack_count_{0};
  bool valid_{false};
  bool acknack_count_initialized_{false};
};

template <std::size_t WindowBits>
class ReliableReader final {
  static_assert(WindowBits > 0U, "reader window must be nonzero");
  static_assert(WindowBits <= SequenceNumberSet::maximum_bits,
                "reader window cannot exceed ACKNACK bitmap bound");

 public:
  explicit ReliableReader(const ReliableReaderConfig config = {}) noexcept
      : config_(config),
        base_(config.initial_sequence_number),
        valid_(valid_state_sequence(config.initial_sequence_number)) {}

  template <std::size_t ActionCapacity>
  [[nodiscard]] ReliabilityError on_data(
      const std::uint64_t sequence_number,
      ReliabilityActionBuffer<ActionCapacity>& actions) noexcept {
    if (!valid_) {
      return ReliabilityError::invalid_configuration;
    }
    if (!valid_state_sequence(sequence_number)) {
      return ReliabilityError::invalid_sequence_number;
    }
    if (sequence_number < base_) {
      return ReliabilityError::stale_data;
    }
    const std::uint64_t offset = sequence_number - base_;
    if (offset >= WindowBits) {
      return ReliabilityError::receive_window_exceeded;
    }
    const std::size_t slot =
        (base_index_ + static_cast<std::size_t>(offset)) % WindowBits;
    if (received_[slot]) {
      return ReliabilityError::duplicate_data;
    }
    if (actions.remaining() == 0U) {
      return ReliabilityError::action_capacity_exceeded;
    }

    received_[slot] = true;
    ReliabilityAction action{};
    action.kind = ReliabilityActionKind::sample_received;
    action.sequence_number = sequence_number;
    static_cast<void>(actions.push(action));

    while (received_[base_index_]) {
      received_[base_index_] = false;
      base_index_ = (base_index_ + 1U) % WindowBits;
      ++base_;
    }
    return ReliabilityError::none;
  }

  template <std::size_t ActionCapacity>
  [[nodiscard]] ReliabilityError on_heartbeat(
      const HeartbeatView& heartbeat,
      ReliabilityActionBuffer<ActionCapacity>& actions) noexcept {
    if (!valid_) {
      return ReliabilityError::invalid_configuration;
    }
    if (!valid_heartbeat_range(heartbeat)) {
      return ReliabilityError::invalid_sequence_number;
    }
    if ((heartbeat.reader_id != config_.reader_id) ||
        (heartbeat.writer_id != config_.writer_id)) {
      return ReliabilityError::unexpected_peer;
    }
    if (heartbeat_count_initialized_ &&
        !control_count_is_newer(heartbeat.count, last_heartbeat_count_)) {
      return ReliabilityError::stale_control;
    }
    if ((heartbeat.last_sequence_number >= base_) &&
        (heartbeat.first_sequence_number > base_)) {
      return ReliabilityError::gap_not_repairable;
    }

    const bool writer_covers_base = heartbeat.last_sequence_number >= base_;
    const bool response_required = writer_covers_base || !heartbeat.final_flag;
    if (response_required && (actions.remaining() == 0U)) {
      return ReliabilityError::action_capacity_exceeded;
    }

    if (response_required) {
      ReliabilityAction action{};
      action.kind = ReliabilityActionKind::send_acknack;
      action.reader_id = config_.reader_id;
      action.writer_id = config_.writer_id;
      action.control_count = next_control_count(acknack_count_);
      if (writer_covers_base) {
        const std::uint64_t available =
            heartbeat.last_sequence_number - base_ + 1U;
        const std::uint32_t num_bits = static_cast<std::uint32_t>(
            available < WindowBits ? available : WindowBits);
        static_cast<void>(action.reader_state.reset(base_, num_bits));
        for (std::uint32_t offset = 0U; offset < num_bits; ++offset) {
          const std::size_t slot =
              (base_index_ + static_cast<std::size_t>(offset)) % WindowBits;
          if (!received_[slot]) {
            static_cast<void>(action.reader_state.set(offset));
          }
        }
        action.final_flag = false;
      } else {
        static_cast<void>(action.reader_state.reset(base_, 0U));
        action.final_flag = true;
      }
      static_cast<void>(actions.push(action));
      acknack_count_ = action.control_count;
    }

    last_heartbeat_count_ = heartbeat.count;
    heartbeat_count_initialized_ = true;
    return ReliabilityError::none;
  }

  [[nodiscard]] std::uint64_t next_expected_sequence() const noexcept {
    return base_;
  }
  [[nodiscard]] constexpr std::size_t window_size() const noexcept {
    return WindowBits;
  }

 private:
  [[nodiscard]] static bool valid_state_sequence(
      const std::uint64_t sequence_number) noexcept {
    return (sequence_number != 0U) &&
           (sequence_number < static_cast<std::uint64_t>(
                                  std::numeric_limits<std::int64_t>::max()));
  }

  [[nodiscard]] static bool valid_heartbeat_range(
      const HeartbeatView& heartbeat) noexcept {
    return (heartbeat.first_sequence_number != 0U) &&
           (heartbeat.first_sequence_number <=
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) &&
           (heartbeat.last_sequence_number <=
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) &&
           (heartbeat.last_sequence_number >=
            (heartbeat.first_sequence_number - 1U));
  }

  ReliableReaderConfig config_{};
  std::array<bool, WindowBits> received_{};
  std::uint64_t base_{1U};
  std::size_t base_index_{0U};
  std::int32_t acknack_count_{0};
  std::int32_t last_heartbeat_count_{0};
  bool valid_{false};
  bool heartbeat_count_initialized_{false};
};

}  // namespace openrtdds::rtps
