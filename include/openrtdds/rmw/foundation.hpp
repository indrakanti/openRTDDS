#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace openrtdds::rmw {

// Requirements: ORT-RMW-023, ORT-RMW-024, ORT-RMW-025, ORT-RMW-026
// Requirements: ORT-RMW-027, ORT-RMW-028

inline constexpr char kImplementationIdentifier[] = "rmw_openrtdds_cpp";
inline constexpr char kRosDistribution[] = "jazzy";
inline constexpr char kRmwBaselineVersion[] = "7.3.4";
inline constexpr std::uint32_t kMaxPortableDomainId = 232U;

enum class AdapterError : std::uint8_t {
  none = 0,
  invalid_argument,
  incorrect_implementation,
  invalid_limits,
  invalid_domain,
  invalid_state,
  resource_exhausted,
  name_too_long,
  stale_handle,
  unsupported,
};

[[nodiscard]] const char* to_string(AdapterError error) noexcept;

enum class ContextState : std::uint8_t {
  zero = 0,
  initialized,
  active,
  shutting_down,
  finalizable,
};

struct AdapterLimits final {
  std::size_t max_nodes{8U};
  std::size_t max_guard_conditions{16U};

  [[nodiscard]] constexpr AdapterError validate(
      const std::size_t node_capacity,
      const std::size_t guard_condition_capacity) const noexcept {
    if ((max_nodes == 0U) || (max_guard_conditions == 0U)) {
      return AdapterError::invalid_limits;
    }
    if ((max_nodes > node_capacity) ||
        (max_guard_conditions > guard_condition_capacity)) {
      return AdapterError::invalid_limits;
    }
    return AdapterError::none;
  }
};

struct AdapterConfig final {
  AdapterLimits limits{};
  std::uint32_t domain_id{0U};
  const char* implementation_identifier{kImplementationIdentifier};
};

struct NodeHandle final {
  std::uint16_t slot{0U};
  std::uint32_t slot_generation{0U};
  std::uint32_t context_generation{0U};
};

struct GuardConditionHandle final {
  std::uint16_t slot{0U};
  std::uint32_t slot_generation{0U};
  std::uint32_t context_generation{0U};
};

template <std::size_t NodeCapacity, std::size_t GuardConditionCapacity,
          std::size_t MaxNodeNameBytes = 64U,
          std::size_t MaxNamespaceBytes = 128U>
class AdapterContext final {
  static_assert(NodeCapacity > 0U, "node capacity must be nonzero");
  static_assert(GuardConditionCapacity > 0U,
                "guard-condition capacity must be nonzero");
  static_assert(NodeCapacity <= 65535U, "node capacity exceeds handle slot");
  static_assert(GuardConditionCapacity <= 65535U,
                "guard-condition capacity exceeds handle slot");
  static_assert(MaxNodeNameBytes > 1U, "node name bound is too small");
  static_assert(MaxNamespaceBytes > 1U, "namespace bound is too small");

 public:
  AdapterContext() noexcept = default;
  AdapterContext(const AdapterContext&) = delete;
  AdapterContext& operator=(const AdapterContext&) = delete;

  [[nodiscard]] AdapterError initialize(const AdapterConfig& config) noexcept {
    if (state_ != ContextState::zero) {
      return AdapterError::invalid_state;
    }
    if (!identifier_matches(config.implementation_identifier)) {
      return config.implementation_identifier == nullptr
                 ? AdapterError::invalid_argument
                 : AdapterError::incorrect_implementation;
    }
    const AdapterError limits_error =
        config.limits.validate(NodeCapacity, GuardConditionCapacity);
    if (limits_error != AdapterError::none) {
      return limits_error;
    }
    if (config.domain_id > kMaxPortableDomainId) {
      return AdapterError::invalid_domain;
    }

    limits_ = config.limits;
    domain_id_ = config.domain_id;
    context_generation_ = next_generation(context_generation_);
    wake_generation_.store(0U, std::memory_order_relaxed);
    state_ = ContextState::initialized;
    return AdapterError::none;
  }

  [[nodiscard]] AdapterError shutdown() noexcept {
    if ((state_ != ContextState::initialized) &&
        (state_ != ContextState::active)) {
      return AdapterError::invalid_state;
    }
    state_ = (node_count_ == 0U) && (guard_condition_count_ == 0U)
                 ? ContextState::finalizable
                 : ContextState::shutting_down;
    advance_atomic(wake_generation_);
    return AdapterError::none;
  }

  [[nodiscard]] AdapterError finalize() noexcept {
    if (state_ != ContextState::finalizable) {
      return AdapterError::invalid_state;
    }
    limits_ = AdapterLimits{};
    domain_id_ = 0U;
    wake_generation_.store(0U, std::memory_order_relaxed);
    state_ = ContextState::zero;
    return AdapterError::none;
  }

  [[nodiscard]] AdapterError create_node(const char* name,
                                         const char* name_space,
                                         NodeHandle& output) noexcept {
    if ((state_ != ContextState::initialized) &&
        (state_ != ContextState::active)) {
      return AdapterError::invalid_state;
    }
    if ((name == nullptr) || (name_space == nullptr) || (name[0] == '\0') ||
        (name_space[0] == '\0')) {
      return AdapterError::invalid_argument;
    }
    const std::size_t name_length = bounded_length(name, MaxNodeNameBytes);
    const std::size_t namespace_length =
        bounded_length(name_space, MaxNamespaceBytes);
    if ((name_length >= MaxNodeNameBytes) ||
        (namespace_length >= MaxNamespaceBytes)) {
      return AdapterError::name_too_long;
    }
    if (node_count_ >= limits_.max_nodes) {
      return AdapterError::resource_exhausted;
    }

    for (std::size_t index = 0U; index < NodeCapacity; ++index) {
      NodeSlot& slot = nodes_[index];
      if (!slot.used) {
        copy_bounded(name, name_length, slot.name);
        copy_bounded(name_space, namespace_length, slot.name_space);
        slot.generation = next_generation(slot.generation);
        slot.used = true;
        ++node_count_;
        output = NodeHandle{static_cast<std::uint16_t>(index), slot.generation,
                            context_generation_};
        state_ = ContextState::active;
        return AdapterError::none;
      }
    }
    return AdapterError::resource_exhausted;
  }

  [[nodiscard]] AdapterError destroy_node(const NodeHandle handle) noexcept {
    if ((state_ != ContextState::active) &&
        (state_ != ContextState::shutting_down)) {
      return AdapterError::invalid_state;
    }
    if (!valid_node_handle(handle)) {
      return AdapterError::stale_handle;
    }
    NodeSlot& slot = nodes_[handle.slot];
    slot.used = false;
    slot.name[0] = '\0';
    slot.name_space[0] = '\0';
    --node_count_;
    update_state_after_release();
    return AdapterError::none;
  }

  [[nodiscard]] AdapterError create_guard_condition(
      GuardConditionHandle& output) noexcept {
    if ((state_ != ContextState::initialized) &&
        (state_ != ContextState::active)) {
      return AdapterError::invalid_state;
    }
    if (guard_condition_count_ >= limits_.max_guard_conditions) {
      return AdapterError::resource_exhausted;
    }

    for (std::size_t index = 0U; index < GuardConditionCapacity; ++index) {
      GuardConditionSlot& slot = guard_conditions_[index];
      if (!slot.used) {
        slot.generation = next_generation(slot.generation);
        slot.trigger_generation.store(0U, std::memory_order_relaxed);
        slot.used = true;
        ++guard_condition_count_;
        output = GuardConditionHandle{static_cast<std::uint16_t>(index),
                                      slot.generation, context_generation_};
        state_ = ContextState::active;
        return AdapterError::none;
      }
    }
    return AdapterError::resource_exhausted;
  }

  [[nodiscard]] AdapterError destroy_guard_condition(
      const GuardConditionHandle handle) noexcept {
    if ((state_ != ContextState::active) &&
        (state_ != ContextState::shutting_down)) {
      return AdapterError::invalid_state;
    }
    if (!valid_guard_condition_handle(handle)) {
      return AdapterError::stale_handle;
    }
    GuardConditionSlot& slot = guard_conditions_[handle.slot];
    slot.used = false;
    slot.trigger_generation.store(0U, std::memory_order_relaxed);
    --guard_condition_count_;
    update_state_after_release();
    return AdapterError::none;
  }

  [[nodiscard]] AdapterError trigger_guard_condition(
      const GuardConditionHandle handle) noexcept {
    if ((state_ != ContextState::initialized) &&
        (state_ != ContextState::active)) {
      return AdapterError::invalid_state;
    }
    if (!valid_guard_condition_handle(handle)) {
      return AdapterError::stale_handle;
    }
    advance_atomic(guard_conditions_[handle.slot].trigger_generation);
    advance_atomic(wake_generation_);
    return AdapterError::none;
  }

  [[nodiscard]] AdapterError observe_guard_condition(
      const GuardConditionHandle handle, const std::uint64_t last_observed,
      std::uint64_t& current, bool& ready) const noexcept {
    if ((state_ != ContextState::initialized) &&
        (state_ != ContextState::active)) {
      return AdapterError::invalid_state;
    }
    if (!valid_guard_condition_handle(handle)) {
      return AdapterError::stale_handle;
    }
    const std::uint64_t generation = guard_conditions_[handle.slot]
                                         .trigger_generation.load(
                                             std::memory_order_acquire);
    current = generation;
    ready = generation != last_observed;
    return AdapterError::none;
  }

  [[nodiscard]] ContextState state() const noexcept { return state_; }
  [[nodiscard]] std::uint32_t context_generation() const noexcept {
    return context_generation_;
  }
  [[nodiscard]] std::uint32_t domain_id() const noexcept { return domain_id_; }
  [[nodiscard]] std::size_t node_count() const noexcept { return node_count_; }
  [[nodiscard]] std::size_t guard_condition_count() const noexcept {
    return guard_condition_count_;
  }
  [[nodiscard]] std::uint64_t wake_generation() const noexcept {
    return wake_generation_.load(std::memory_order_acquire);
  }

 private:
  struct NodeSlot final {
    std::array<char, MaxNodeNameBytes> name{};
    std::array<char, MaxNamespaceBytes> name_space{};
    std::uint32_t generation{0U};
    bool used{false};
  };

  struct GuardConditionSlot final {
    std::atomic<std::uint64_t> trigger_generation{0U};
    std::uint32_t generation{0U};
    bool used{false};
  };

  [[nodiscard]] static constexpr std::uint32_t next_generation(
      const std::uint32_t current) noexcept {
    return current == std::numeric_limits<std::uint32_t>::max() ? 1U
                                                                : current + 1U;
  }

  static void advance_atomic(std::atomic<std::uint64_t>& value) noexcept {
    std::uint64_t current = value.load(std::memory_order_relaxed);
    for (;;) {
      const std::uint64_t next =
          current == std::numeric_limits<std::uint64_t>::max() ? 1U
                                                               : current + 1U;
      if (value.compare_exchange_weak(current, next, std::memory_order_release,
                                      std::memory_order_relaxed)) {
        return;
      }
    }
  }

  [[nodiscard]] static bool identifier_matches(const char* candidate) noexcept {
    if (candidate == nullptr) {
      return false;
    }
    for (std::size_t index = 0U; index < sizeof(kImplementationIdentifier);
         ++index) {
      if (candidate[index] != kImplementationIdentifier[index]) {
        return false;
      }
      if (kImplementationIdentifier[index] == '\0') {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] static std::size_t bounded_length(
      const char* value, const std::size_t capacity) noexcept {
    std::size_t length = 0U;
    while ((length < capacity) && (value[length] != '\0')) {
      ++length;
    }
    return length;
  }

  template <std::size_t Capacity>
  static void copy_bounded(const char* source, const std::size_t length,
                           std::array<char, Capacity>& destination) noexcept {
    for (std::size_t index = 0U; index < length; ++index) {
      destination[index] = source[index];
    }
    destination[length] = '\0';
  }

  [[nodiscard]] bool valid_node_handle(const NodeHandle handle) const noexcept {
    if ((handle.context_generation != context_generation_) ||
        (handle.slot >= NodeCapacity)) {
      return false;
    }
    const NodeSlot& slot = nodes_[handle.slot];
    return slot.used && (slot.generation == handle.slot_generation);
  }

  [[nodiscard]] bool valid_guard_condition_handle(
      const GuardConditionHandle handle) const noexcept {
    if ((handle.context_generation != context_generation_) ||
        (handle.slot >= GuardConditionCapacity)) {
      return false;
    }
    const GuardConditionSlot& slot = guard_conditions_[handle.slot];
    return slot.used && (slot.generation == handle.slot_generation);
  }

  void update_state_after_release() noexcept {
    if ((node_count_ == 0U) && (guard_condition_count_ == 0U)) {
      state_ = state_ == ContextState::shutting_down
                   ? ContextState::finalizable
                   : ContextState::initialized;
    }
  }

  std::array<NodeSlot, NodeCapacity> nodes_{};
  std::array<GuardConditionSlot, GuardConditionCapacity> guard_conditions_{};
  AdapterLimits limits_{};
  std::atomic<std::uint64_t> wake_generation_{0U};
  std::size_t node_count_{0U};
  std::size_t guard_condition_count_{0U};
  std::uint32_t context_generation_{0U};
  std::uint32_t domain_id_{0U};
  ContextState state_{ContextState::zero};
};

}  // namespace openrtdds::rmw
