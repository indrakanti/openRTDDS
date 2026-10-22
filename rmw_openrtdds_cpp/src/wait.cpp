#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <new>

#include "internal.hpp"

#include "rcutils/allocator.h"
#include "rmw/error_handling.h"
#include "rmw/rmw.h"
#include "rmw/time.h"

namespace rmw_openrtdds_cpp {
namespace {

// Requirements: ORT-RMW-009, ORT-RMW-016, ORT-RMW-049, ORT-RMW-050
struct WaitSetData final {
  rmw_context_t* context{nullptr};
  std::size_t slot_index{0U};
  std::uint32_t slot_generation{0U};
  std::size_t maximum_conditions{0U};
  std::atomic<bool> waiting{false};
};

class WaitingClaim final {
 public:
  explicit WaitingClaim(std::atomic<bool>& waiting) noexcept
      : waiting_(waiting) {}
  ~WaitingClaim() { waiting_.store(false, std::memory_order_release); }

  WaitingClaim(const WaitingClaim&) = delete;
  WaitingClaim& operator=(const WaitingClaim&) = delete;

 private:
  std::atomic<bool>& waiting_;
};

void release_slot(detail::WaitSetSlot& slot) noexcept {
  slot.data = nullptr;
  slot.occupied = false;
  ++slot.generation;
  if (slot.generation == 0U) {
    slot.generation = 1U;
  }
}

[[nodiscard]] bool valid_wait_set(rmw_wait_set_t* const wait_set,
                                  WaitSetData*& data) noexcept {
  if ((wait_set == nullptr) || (wait_set->data == nullptr) ||
      !detail::identifier_matches(wait_set->implementation_identifier)) {
    return false;
  }
  auto* candidate = static_cast<WaitSetData*>(wait_set->data);
  if (!detail::context_is_valid(candidate->context) ||
      (candidate->slot_index >= detail::kWaitSetCapacity)) {
    return false;
  }
  auto& slot = candidate->context->impl->wait_sets[candidate->slot_index];
  if (!slot.occupied || (slot.data != candidate) ||
      (slot.generation != candidate->slot_generation)) {
    return false;
  }
  data = candidate;
  return true;
}

[[nodiscard]] bool valid_pointer_array(const std::size_t count,
                                       void* const* const values) noexcept {
  if ((count != 0U) && (values == nullptr)) {
    return false;
  }
  for (std::size_t index = 0U; index < count; ++index) {
    if (values[index] == nullptr) {
      return false;
    }
  }
  return true;
}

void clear_entries(rmw_subscriptions_t* const subscriptions,
                   rmw_guard_conditions_t* const guards,
                   rmw_services_t* const services,
                   rmw_clients_t* const clients,
                   rmw_events_t* const events) noexcept {
  if ((subscriptions != nullptr) && (subscriptions->subscribers != nullptr)) {
    for (std::size_t index = 0U; index < subscriptions->subscriber_count;
         ++index) {
      subscriptions->subscribers[index] = nullptr;
    }
  }
  if ((guards != nullptr) && (guards->guard_conditions != nullptr)) {
    for (std::size_t index = 0U; index < guards->guard_condition_count;
         ++index) {
      guards->guard_conditions[index] = nullptr;
    }
  }
  if ((services != nullptr) && (services->services != nullptr)) {
    for (std::size_t index = 0U; index < services->service_count; ++index) {
      services->services[index] = nullptr;
    }
  }
  if ((clients != nullptr) && (clients->clients != nullptr)) {
    for (std::size_t index = 0U; index < clients->client_count; ++index) {
      clients->clients[index] = nullptr;
    }
  }
  if ((events != nullptr) && (events->events != nullptr)) {
    for (std::size_t index = 0U; index < events->event_count; ++index) {
      events->events[index] = nullptr;
    }
  }
}

}  // namespace
}  // namespace rmw_openrtdds_cpp

extern "C" {

// Requirements: ORT-RMW-038, ORT-RMW-049, ORT-RMW-050
rmw_wait_set_t* rmw_create_wait_set(rmw_context_t* context,
                                    size_t max_conditions) {
  using namespace rmw_openrtdds_cpp;
  if (!detail::context_is_valid(context) || context->impl->shutdown.load()) {
    RMW_SET_ERROR_MSG("wait-set creation requires an active context");
    return nullptr;
  }
  const std::size_t bounded_maximum =
      max_conditions == 0U ? detail::kWaitConditionCapacity : max_conditions;
  if (bounded_maximum > detail::kWaitConditionCapacity) {
    RMW_SET_ERROR_MSG("wait-set condition capacity exceeds deterministic limit");
    return nullptr;
  }

  std::lock_guard<std::mutex> registry_lock(
      context->impl->wait_set_registry_mutex);
  std::size_t slot_index = detail::kWaitSetCapacity;
  for (std::size_t index = 0U; index < detail::kWaitSetCapacity; ++index) {
    if (!context->impl->wait_sets[index].occupied) {
      slot_index = index;
      context->impl->wait_sets[index].occupied = true;
      break;
    }
  }
  if (slot_index == detail::kWaitSetCapacity) {
    RMW_SET_ERROR_MSG("OpenRTDDS wait-set pool is exhausted");
    return nullptr;
  }

  auto& slot = context->impl->wait_sets[slot_index];
  auto& allocator = context->options.allocator;
  auto* wait_set = static_cast<rmw_wait_set_t*>(allocator.zero_allocate(
      1U, sizeof(rmw_wait_set_t), allocator.state));
  void* storage =
      allocator.zero_allocate(1U, sizeof(WaitSetData), allocator.state);
  auto* data = storage == nullptr ? nullptr : new (storage) WaitSetData{};
  if ((wait_set == nullptr) || (data == nullptr)) {
    if (data != nullptr) {
      data->~WaitSetData();
      allocator.deallocate(data, allocator.state);
    }
    if (wait_set != nullptr) {
      allocator.deallocate(wait_set, allocator.state);
    }
    release_slot(slot);
    RMW_SET_ERROR_MSG("unable to allocate bounded wait-set storage");
    return nullptr;
  }

  data->context = context;
  data->slot_index = slot_index;
  data->slot_generation = slot.generation;
  data->maximum_conditions = bounded_maximum;
  wait_set->implementation_identifier =
      openrtdds::rmw::kImplementationIdentifier;
  wait_set->guard_conditions = nullptr;
  wait_set->data = data;
  slot.data = data;
  return wait_set;
}

rmw_ret_t rmw_destroy_wait_set(rmw_wait_set_t* wait_set) {
  using namespace rmw_openrtdds_cpp;
  if ((wait_set != nullptr) &&
      (wait_set->implementation_identifier != nullptr) &&
      !detail::identifier_matches(wait_set->implementation_identifier)) {
    RMW_SET_ERROR_MSG("wait set belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  WaitSetData* data = nullptr;
  if (!valid_wait_set(wait_set, data)) {
    RMW_SET_ERROR_MSG("invalid OpenRTDDS wait set");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (data->waiting.load(std::memory_order_acquire)) {
    RMW_SET_ERROR_MSG("cannot destroy a wait set while rmw_wait is active");
    return RMW_RET_ERROR;
  }

  rmw_context_t* const context = data->context;
  std::lock_guard<std::mutex> registry_lock(
      context->impl->wait_set_registry_mutex);
  auto& slot = context->impl->wait_sets[data->slot_index];
  auto& allocator = context->options.allocator;
  release_slot(slot);
  data->~WaitSetData();
  allocator.deallocate(data, allocator.state);
  allocator.deallocate(wait_set, allocator.state);
  return RMW_RET_OK;
}

rmw_ret_t rmw_wait(rmw_subscriptions_t* subscriptions,
                   rmw_guard_conditions_t* guard_conditions,
                   rmw_services_t* services, rmw_clients_t* clients,
                   rmw_events_t* events, rmw_wait_set_t* wait_set,
                   const rmw_time_t* wait_timeout) {
  using namespace rmw_openrtdds_cpp;
  WaitSetData* data = nullptr;
  if (!valid_wait_set(wait_set, data)) {
    RMW_SET_ERROR_MSG("invalid OpenRTDDS wait set");
    return RMW_RET_INVALID_ARGUMENT;
  }
  bool expected = false;
  if (!data->waiting.compare_exchange_strong(
          expected, true, std::memory_order_acq_rel)) {
    RMW_SET_ERROR_MSG("concurrent waits on one wait set are unsupported");
    return RMW_RET_ERROR;
  }
  WaitingClaim claim(data->waiting);

  const std::size_t subscription_count =
      subscriptions == nullptr ? 0U : subscriptions->subscriber_count;
  const std::size_t guard_count =
      guard_conditions == nullptr ? 0U
                                  : guard_conditions->guard_condition_count;
  const std::size_t service_count =
      services == nullptr ? 0U : services->service_count;
  const std::size_t client_count =
      clients == nullptr ? 0U : clients->client_count;
  const std::size_t event_count =
      events == nullptr ? 0U : events->event_count;
  if (!valid_pointer_array(
          subscription_count,
          subscriptions == nullptr ? nullptr : subscriptions->subscribers) ||
      !valid_pointer_array(
          guard_count, guard_conditions == nullptr
                           ? nullptr
                           : guard_conditions->guard_conditions) ||
      !valid_pointer_array(
          service_count, services == nullptr ? nullptr : services->services) ||
      !valid_pointer_array(
          client_count, clients == nullptr ? nullptr : clients->clients) ||
      !valid_pointer_array(
          event_count, events == nullptr ? nullptr : events->events) ||
      (subscription_count > data->maximum_conditions) ||
      (guard_count > (data->maximum_conditions - subscription_count)) ||
      (service_count >
       (data->maximum_conditions - subscription_count - guard_count)) ||
      (client_count >
       (data->maximum_conditions - subscription_count - guard_count -
        service_count)) ||
      (event_count >
       (data->maximum_conditions - subscription_count - guard_count -
        service_count - client_count))) {
    RMW_SET_ERROR_MSG("invalid or over-capacity wait condition array");
    return RMW_RET_INVALID_ARGUMENT;
  }

  rmw_context_t* const context = data->context;
  for (std::size_t index = 0U; index < subscription_count; ++index) {
    if (!detail::subscription_wait_data_valid(
            subscriptions->subscribers[index], context)) {
      RMW_SET_ERROR_MSG("wait set contains an invalid subscription");
      return RMW_RET_INVALID_ARGUMENT;
    }
  }
  for (std::size_t index = 0U; index < guard_count; ++index) {
    const auto* guard = static_cast<const detail::GuardConditionData*>(
        guard_conditions->guard_conditions[index]);
    if ((guard == nullptr) || (guard->context != context)) {
      RMW_SET_ERROR_MSG("wait set contains an invalid guard condition");
      return RMW_RET_INVALID_ARGUMENT;
    }
  }
  for (std::size_t index = 0U; index < service_count; ++index) {
    if (!detail::service_wait_data_valid(services->services[index], context)) {
      RMW_SET_ERROR_MSG("wait set contains an invalid service");
      return RMW_RET_INVALID_ARGUMENT;
    }
  }
  for (std::size_t index = 0U; index < client_count; ++index) {
    if (!detail::client_wait_data_valid(clients->clients[index], context)) {
      RMW_SET_ERROR_MSG("wait set contains an invalid client");
      return RMW_RET_INVALID_ARGUMENT;
    }
  }
  for (std::size_t index = 0U; index < event_count; ++index) {
    if (!detail::event_wait_data_valid(events->events[index], context)) {
      RMW_SET_ERROR_MSG("wait set contains an invalid event");
      return RMW_RET_INVALID_ARGUMENT;
    }
  }

  const bool finite_timeout = wait_timeout != nullptr;
  const rmw_duration_t timeout_ns =
      finite_timeout ? rmw_time_total_nsec(*wait_timeout) : 0;
  const auto deadline =
      !finite_timeout || (timeout_ns == std::numeric_limits<rmw_duration_t>::max())
          ? std::chrono::steady_clock::time_point::max()
          : std::chrono::steady_clock::now() +
                std::chrono::nanoseconds(timeout_ns);

  std::array<bool, detail::kWaitConditionCapacity> subscription_ready{};
  std::array<bool, detail::kWaitConditionCapacity> guard_ready{};
  std::array<bool, detail::kWaitConditionCapacity> service_ready{};
  std::array<bool, detail::kWaitConditionCapacity> client_ready{};
  std::array<bool, detail::kWaitConditionCapacity> event_ready{};
  std::array<std::uint64_t, detail::kWaitConditionCapacity>
      guard_generations{};
  std::unique_lock<std::mutex> lock(context->impl->wait_mutex);
  for (;;) {
    if (context->impl->shutdown.load()) {
      clear_entries(subscriptions, guard_conditions, services, clients,
                    events);
      RMW_SET_ERROR_MSG("wait interrupted because the context was shutdown");
      return RMW_RET_ERROR;
    }

    bool any_ready = false;
    for (std::size_t index = 0U; index < subscription_count; ++index) {
      subscription_ready[index] = detail::subscription_wait_ready(
          subscriptions->subscribers[index]);
      any_ready = any_ready || subscription_ready[index];
    }
    for (std::size_t index = 0U; index < guard_count; ++index) {
      auto* guard = static_cast<detail::GuardConditionData*>(
          guard_conditions->guard_conditions[index]);
      bool ready = false;
      const auto error = context->impl->foundation.observe_guard_condition(
          guard->handle, guard->last_observed_generation,
          guard_generations[index], ready);
      if (error != openrtdds::rmw::AdapterError::none) {
        clear_entries(subscriptions, guard_conditions, services, clients,
                      events);
        RMW_SET_ERROR_MSG("wait could not observe a guard condition");
        return RMW_RET_ERROR;
      }
      guard_ready[index] = ready;
      any_ready = any_ready || ready;
    }
    for (std::size_t index = 0U; index < service_count; ++index) {
      service_ready[index] =
          detail::service_wait_ready(services->services[index]);
      any_ready = any_ready || service_ready[index];
    }
    for (std::size_t index = 0U; index < client_count; ++index) {
      client_ready[index] = detail::client_wait_ready(clients->clients[index]);
      any_ready = any_ready || client_ready[index];
    }
    for (std::size_t index = 0U; index < event_count; ++index) {
      event_ready[index] = detail::event_wait_ready(events->events[index]);
      any_ready = any_ready || event_ready[index];
    }

    if (any_ready) {
      for (std::size_t index = 0U; index < subscription_count; ++index) {
        if (!subscription_ready[index]) {
          subscriptions->subscribers[index] = nullptr;
        }
      }
      for (std::size_t index = 0U; index < guard_count; ++index) {
        auto* guard = static_cast<detail::GuardConditionData*>(
            guard_conditions->guard_conditions[index]);
        if (guard_ready[index]) {
          guard->last_observed_generation = guard_generations[index];
        } else {
          guard_conditions->guard_conditions[index] = nullptr;
        }
      }
      for (std::size_t index = 0U; index < service_count; ++index) {
        if (!service_ready[index]) {
          services->services[index] = nullptr;
        }
      }
      for (std::size_t index = 0U; index < client_count; ++index) {
        if (!client_ready[index]) {
          clients->clients[index] = nullptr;
        }
      }
      for (std::size_t index = 0U; index < event_count; ++index) {
        if (!event_ready[index]) {
          events->events[index] = nullptr;
        }
      }
      return RMW_RET_OK;
    }

    if (finite_timeout &&
        ((timeout_ns == 0) ||
         (std::chrono::steady_clock::now() >= deadline))) {
      clear_entries(subscriptions, guard_conditions, services, clients,
                    events);
      return RMW_RET_TIMEOUT;
    }
    const std::uint64_t observed_generation = context->impl->wait_generation;
    if (finite_timeout) {
      static_cast<void>(context->impl->wait_condition.wait_until(
          lock, deadline, [context, observed_generation] {
            return context->impl->shutdown.load() ||
                   (context->impl->wait_generation != observed_generation);
          }));
    } else {
      context->impl->wait_condition.wait(
          lock, [context, observed_generation] {
            return context->impl->shutdown.load() ||
                   (context->impl->wait_generation != observed_generation);
          });
    }
  }
}

}  // extern "C"
