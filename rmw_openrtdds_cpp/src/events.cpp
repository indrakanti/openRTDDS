#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>

#include "internal.hpp"

#include "rcutils/allocator.h"
#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/events_statuses/matched.h"

namespace rmw_openrtdds_cpp {
namespace {

// Requirements: ORT-RMW-009, ORT-RMW-013, ORT-RMW-015, ORT-RMW-022
enum class EventSource : std::uint8_t { publisher, subscription };

struct EventData final {
  rmw_context_t* context{nullptr};
  void* source{nullptr};
  EventSource source_kind{EventSource::publisher};
  rmw_event_type_t type{RMW_EVENT_INVALID};
  std::size_t slot_index{0U};
  std::uint32_t slot_generation{0U};
  std::size_t last_total_count{0U};
  std::size_t last_current_count{0U};
  std::uint64_t last_generation{0U};
  rmw_event_callback_t callback{nullptr};
  const void* callback_user_data{nullptr};
  mutable std::mutex mutex{};
};

void release_slot(detail::EventSlot& slot) noexcept {
  slot.data = nullptr;
  slot.occupied = false;
  ++slot.generation;
  if (slot.generation == 0U) {
    slot.generation = 1U;
  }
}

[[nodiscard]] bool valid_event_data(const void* const opaque,
                                    const rmw_context_t* const context,
                                    EventData*& data) noexcept {
  if ((opaque == nullptr) || !detail::context_is_valid(context)) {
    return false;
  }
  auto* candidate = const_cast<EventData*>(
      static_cast<const EventData*>(opaque));
  if ((candidate->context != context) ||
      (candidate->slot_index >= detail::kEventCapacity)) {
    return false;
  }
  const auto& slot = context->impl->events[candidate->slot_index];
  if (!slot.occupied || (slot.data != candidate) ||
      (slot.generation != candidate->slot_generation)) {
    return false;
  }
  data = candidate;
  return true;
}

[[nodiscard]] bool snapshot(const EventData& data,
                            detail::MatchedStatusSnapshot& value) noexcept {
  if (data.source_kind == EventSource::publisher) {
    return detail::publisher_matched_status(data.source, data.context, value);
  }
  return detail::subscription_matched_status(data.source, data.context, value);
}

rmw_ret_t initialize_event(rmw_event_t* const event,
                           rmw_context_t* const context, void* const source,
                           const EventSource source_kind,
                           const rmw_event_type_t type,
                           const detail::MatchedStatusSnapshot& initial) {
  static_cast<void>(initial);
  if ((event->implementation_identifier != nullptr) ||
      (event->data != nullptr)) {
    RMW_SET_ERROR_MSG("event must be zero initialized");
    return RMW_RET_INVALID_ARGUMENT;
  }
  std::lock_guard<std::mutex> registry_lock(
      context->impl->event_registry_mutex);
  std::size_t slot_index = detail::kEventCapacity;
  for (std::size_t index = 0U; index < detail::kEventCapacity; ++index) {
    if (!context->impl->events[index].occupied) {
      slot_index = index;
      context->impl->events[index].occupied = true;
      break;
    }
  }
  if (slot_index == detail::kEventCapacity) {
    RMW_SET_ERROR_MSG("OpenRTDDS event pool is exhausted");
    return RMW_RET_BAD_ALLOC;
  }

  auto& slot = context->impl->events[slot_index];
  auto& allocator = context->options.allocator;
  void* storage =
      allocator.zero_allocate(1U, sizeof(EventData), allocator.state);
  auto* data = storage == nullptr ? nullptr : new (storage) EventData{};
  if (data == nullptr) {
    release_slot(slot);
    RMW_SET_ERROR_MSG("unable to allocate bounded event storage");
    return RMW_RET_BAD_ALLOC;
  }
  data->context = context;
  data->source = source;
  data->source_kind = source_kind;
  data->type = type;
  data->slot_index = slot_index;
  data->slot_generation = slot.generation;
  data->last_total_count = 0U;
  data->last_current_count = 0U;
  data->last_generation = 0U;
  slot.data = data;
  event->implementation_identifier =
      openrtdds::rmw::kImplementationIdentifier;
  event->data = data;
  event->event_type = type;
  return RMW_RET_OK;
}

}  // namespace

namespace detail {

bool event_wait_data_valid(const void* const opaque,
                           const rmw_context_t* const context) noexcept {
  EventData* data = nullptr;
  return valid_event_data(opaque, context, data);
}

bool event_wait_ready(const void* const opaque) noexcept {
  auto* candidate = const_cast<EventData*>(
      static_cast<const EventData*>(opaque));
  if ((candidate == nullptr) ||
      !context_is_valid(candidate->context)) {
    return false;
  }
  EventData* data = nullptr;
  if (!valid_event_data(opaque, candidate->context, data)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  MatchedStatusSnapshot value{};
  return snapshot(*data, value) &&
         (value.generation != data->last_generation);
}

void notify_event_source(rmw_context_t* const context,
                         const void* const source,
                         const std::size_t number_of_events) noexcept {
  if (!context_is_valid(context) || (source == nullptr) ||
      (number_of_events == 0U)) {
    return;
  }
  struct Callback final {
    rmw_event_callback_t function{nullptr};
    const void* user_data{nullptr};
  };
  std::array<Callback, kEventCapacity> callbacks{};
  std::size_t callback_count = 0U;
  {
    std::lock_guard<std::mutex> registry_lock(
        context->impl->event_registry_mutex);
    for (const auto& slot : context->impl->events) {
      if (!slot.occupied || (slot.data == nullptr)) {
        continue;
      }
      auto* data = static_cast<EventData*>(slot.data);
      if (data->source != source) {
        continue;
      }
      std::lock_guard<std::mutex> event_lock(data->mutex);
      if (data->callback != nullptr) {
        callbacks[callback_count++] =
            {data->callback, data->callback_user_data};
      }
    }
  }
  for (std::size_t index = 0U; index < callback_count; ++index) {
    callbacks[index].function(callbacks[index].user_data, number_of_events);
  }
}

}  // namespace detail
}  // namespace rmw_openrtdds_cpp

extern "C" {

rmw_ret_t rmw_publisher_event_init(rmw_event_t* event,
                                   const rmw_publisher_t* publisher,
                                   rmw_event_type_t event_type) {
  using namespace rmw_openrtdds_cpp;
  if ((event == nullptr) || (publisher == nullptr)) {
    RMW_SET_ERROR_MSG("publisher event initialization requires non-null arguments");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (event_type != RMW_EVENT_PUBLICATION_MATCHED) {
    RMW_SET_ERROR_MSG("publisher event type is unsupported");
    return RMW_RET_UNSUPPORTED;
  }
  rmw_context_t* context = nullptr;
  void* source = nullptr;
  detail::MatchedStatusSnapshot initial{};
  if (!detail::publisher_event_source(
          publisher, context, source, initial)) {
    RMW_SET_ERROR_MSG("invalid OpenRTDDS publisher event source");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return initialize_event(event, context, source, EventSource::publisher,
                          event_type, initial);
}

rmw_ret_t rmw_subscription_event_init(rmw_event_t* event,
                                      const rmw_subscription_t* subscription,
                                      rmw_event_type_t event_type) {
  using namespace rmw_openrtdds_cpp;
  if ((event == nullptr) || (subscription == nullptr)) {
    RMW_SET_ERROR_MSG(
        "subscription event initialization requires non-null arguments");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (event_type != RMW_EVENT_SUBSCRIPTION_MATCHED) {
    RMW_SET_ERROR_MSG("subscription event type is unsupported");
    return RMW_RET_UNSUPPORTED;
  }
  rmw_context_t* context = nullptr;
  void* source = nullptr;
  detail::MatchedStatusSnapshot initial{};
  if (!detail::subscription_event_source(
          subscription, context, source, initial)) {
    RMW_SET_ERROR_MSG("invalid OpenRTDDS subscription event source");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return initialize_event(event, context, source, EventSource::subscription,
                          event_type, initial);
}

rmw_ret_t rmw_take_event(const rmw_event_t* event_handle, void* event_info,
                         bool* taken) {
  using namespace rmw_openrtdds_cpp;
  if ((event_handle == nullptr) || (event_info == nullptr) ||
      (taken == nullptr)) {
    RMW_SET_ERROR_MSG("event take requires non-null arguments");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!detail::identifier_matches(event_handle->implementation_identifier) ||
      (event_handle->data == nullptr)) {
    RMW_SET_ERROR_MSG("invalid OpenRTDDS event handle");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto* candidate = static_cast<EventData*>(event_handle->data);
  EventData* data = nullptr;
  if ((event_handle->event_type != candidate->type) ||
      !valid_event_data(candidate, candidate->context, data)) {
    RMW_SET_ERROR_MSG("invalid or stale OpenRTDDS event handle");
    return RMW_RET_INVALID_ARGUMENT;
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  detail::MatchedStatusSnapshot value{};
  if (!snapshot(*data, value)) {
    RMW_SET_ERROR_MSG("event source is no longer valid");
    return RMW_RET_INVALID_ARGUMENT;
  }
  *taken = value.generation != data->last_generation;
  if (!*taken) {
    return RMW_RET_OK;
  }
  auto* status = static_cast<rmw_matched_status_t*>(event_info);
  status->total_count = value.total_count;
  status->total_count_change = value.total_count - data->last_total_count;
  status->current_count = value.current_count;
  status->current_count_change =
      static_cast<std::int32_t>(value.current_count) -
      static_cast<std::int32_t>(data->last_current_count);
  data->last_total_count = value.total_count;
  data->last_current_count = value.current_count;
  data->last_generation = value.generation;
  return RMW_RET_OK;
}

rmw_ret_t rmw_event_fini(rmw_event_t* event) {
  using namespace rmw_openrtdds_cpp;
  if (event == nullptr) {
    RMW_SET_ERROR_MSG("event is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if ((event->implementation_identifier != nullptr) &&
      !detail::identifier_matches(event->implementation_identifier)) {
    RMW_SET_ERROR_MSG("event belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if ((event->data == nullptr) ||
      !detail::identifier_matches(event->implementation_identifier)) {
    RMW_SET_ERROR_MSG("invalid OpenRTDDS event");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto* candidate = static_cast<EventData*>(event->data);
  rmw_context_t* const context = candidate->context;
  EventData* data = nullptr;
  if (!valid_event_data(candidate, context, data)) {
    RMW_SET_ERROR_MSG("invalid or stale OpenRTDDS event");
    return RMW_RET_INVALID_ARGUMENT;
  }
  std::lock_guard<std::mutex> registry_lock(
      context->impl->event_registry_mutex);
  auto& slot = context->impl->events[data->slot_index];
  auto& allocator = context->options.allocator;
  release_slot(slot);
  data->~EventData();
  allocator.deallocate(data, allocator.state);
  *event = rmw_get_zero_initialized_event();
  return RMW_RET_OK;
}

rmw_ret_t rmw_event_set_callback(rmw_event_t* event,
                                 rmw_event_callback_t callback,
                                 const void* user_data) {
  using namespace rmw_openrtdds_cpp;
  if ((event == nullptr) ||
      !detail::identifier_matches(event->implementation_identifier) ||
      (event->data == nullptr)) {
    RMW_SET_ERROR_MSG("invalid OpenRTDDS event callback handle");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto* candidate = static_cast<EventData*>(event->data);
  EventData* data = nullptr;
  if (!valid_event_data(candidate, candidate->context, data)) {
    RMW_SET_ERROR_MSG("invalid or stale OpenRTDDS event callback handle");
    return RMW_RET_INVALID_ARGUMENT;
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  data->callback = callback;
  data->callback_user_data = callback == nullptr ? nullptr : user_data;
  return RMW_RET_OK;
}

bool rmw_event_type_is_supported(const rmw_event_type_t event_type) {
  return (event_type == RMW_EVENT_PUBLICATION_MATCHED) ||
         (event_type == RMW_EVENT_SUBSCRIPTION_MATCHED);
}

}  // extern "C"
