#pragma once

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "openrtdds/rmw/foundation.hpp"
#include "openrtdds/rmw/graph_cache.hpp"
#include "openrtdds/rtps/sedp.hpp"
#include "rmw/rmw.h"
#include "rmw_openrtdds_cpp/type_support.hpp"

namespace rmw_openrtdds_cpp::detail {

class NetworkRuntime;

// Requirements: ORT-RMW-030, ORT-RMW-031, ORT-RMW-032, ORT-RMW-033
// Requirements: ORT-RMW-045, ORT-RMW-046, ORT-RMW-047, ORT-RMW-048
constexpr std::size_t kNodeCapacity = 8U;
constexpr std::size_t kGuardConditionCapacity = 16U;
constexpr std::size_t kPublisherCapacity = 8U;
constexpr std::size_t kSubscriptionCapacity = 8U;
constexpr std::size_t kWaitSetCapacity = 8U;
constexpr std::size_t kEventCapacity = 16U;
constexpr std::size_t kWaitConditionCapacity = 32U;
constexpr std::size_t kServiceCorrelationSize =
    RMW_GID_STORAGE_SIZE + sizeof(std::int64_t);
constexpr std::size_t kServiceOutstandingCapacity = 32U;

using Foundation = openrtdds::rmw::AdapterContext<
    kNodeCapacity, kGuardConditionCapacity>;

struct NodeData final {
  openrtdds::rmw::NodeHandle handle{};
  std::uint64_t graph_id{0U};
  std::size_t publisher_count{0U};
  std::size_t subscription_count{0U};
};

struct GuardConditionData final {
  openrtdds::rmw::GuardConditionHandle handle{};
  rmw_context_t* context{nullptr};
  std::uint64_t last_observed_generation{0U};
};

struct NetworkEndpointView final {
  openrtdds::rtps::LocalEndpointDescriptor descriptor{};
  openrtdds::rtps::EntityId entity_id{};
  const std::uint8_t* sedp{nullptr};
  std::size_t sedp_size{0U};
};

struct NetworkSample final {
  std::uint64_t sequence_number{0U};
  std::size_t packet_size{0U};
};

struct PublisherSlot final {
  void* data{nullptr};
  std::uint32_t generation{1U};
  bool occupied{false};
};

struct SubscriptionSlot final {
  void* data{nullptr};
  std::uint32_t generation{1U};
  bool occupied{false};
};

struct WaitSetSlot final {
  void* data{nullptr};
  std::uint32_t generation{1U};
  bool occupied{false};
};

struct EventSlot final {
  void* data{nullptr};
  std::uint32_t generation{1U};
  bool occupied{false};
};

struct MatchedStatusSnapshot final {
  std::size_t total_count{0U};
  std::size_t current_count{0U};
  std::uint64_t generation{0U};
};

[[nodiscard]] inline bool identifier_matches(
    const char* const identifier) noexcept {
  return (identifier != nullptr) &&
         (std::strcmp(identifier,
                      openrtdds::rmw::kImplementationIdentifier) == 0);
}

}  // namespace rmw_openrtdds_cpp::detail

// This is the implementation-owned definition promised by rmw/init.h.
struct rmw_context_impl_s final {
  rmw_openrtdds_cpp::detail::Foundation foundation{};
  openrtdds::rmw::GraphCache graph{};
  std::mutex graph_mutex{};
  rmw_openrtdds_cpp::detail::NetworkRuntime* network{nullptr};
  openrtdds::rtps::GuidPrefix participant_prefix{};
  std::uint32_t participant_id{0U};
  std::uint16_t metadata_port{0U};
  std::uint16_t user_port{0U};
  rmw_guard_condition_t* graph_guard_condition{nullptr};
  std::array<rmw_openrtdds_cpp::detail::PublisherSlot,
             rmw_openrtdds_cpp::detail::kPublisherCapacity>
      publishers{};
  std::mutex publisher_registry_mutex{};
  std::array<rmw_openrtdds_cpp::detail::SubscriptionSlot,
             rmw_openrtdds_cpp::detail::kSubscriptionCapacity>
      subscriptions{};
  std::mutex subscription_registry_mutex{};
  std::array<rmw_openrtdds_cpp::detail::WaitSetSlot,
             rmw_openrtdds_cpp::detail::kWaitSetCapacity>
      wait_sets{};
  std::mutex wait_set_registry_mutex{};
  std::array<rmw_openrtdds_cpp::detail::EventSlot,
             rmw_openrtdds_cpp::detail::kEventCapacity>
      events{};
  std::mutex event_registry_mutex{};
  std::mutex wait_mutex{};
  std::condition_variable wait_condition{};
  std::uint64_t wait_generation{1U};
  std::atomic<bool> shutdown{false};
};

namespace rmw_openrtdds_cpp::detail {

[[nodiscard]] inline bool context_is_valid(
    const rmw_context_t* const context) noexcept {
  return (context != nullptr) &&
         identifier_matches(context->implementation_identifier) &&
         (context->impl != nullptr);
}

[[nodiscard]] inline bool context_has_publishers(
    const rmw_context_t* const context) noexcept {
  if (!context_is_valid(context)) {
    return false;
  }
  for (const auto& slot : context->impl->publishers) {
    if (slot.occupied) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool context_has_subscriptions(
    const rmw_context_t* const context) noexcept {
  if (!context_is_valid(context)) {
    return false;
  }
  for (const auto& slot : context->impl->subscriptions) {
    if (slot.occupied) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool context_has_wait_sets(
    const rmw_context_t* const context) noexcept {
  if (!context_is_valid(context)) {
    return false;
  }
  for (const auto& slot : context->impl->wait_sets) {
    if (slot.occupied) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool context_has_events(
    const rmw_context_t* const context) noexcept {
  if (!context_is_valid(context)) {
    return false;
  }
  for (const auto& slot : context->impl->events) {
    if (slot.occupied) {
      return true;
    }
  }
  return false;
}

inline void notify_waiters(rmw_context_t* const context) noexcept {
  if (!context_is_valid(context)) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(context->impl->wait_mutex);
    ++context->impl->wait_generation;
    if (context->impl->wait_generation == 0U) {
      context->impl->wait_generation = 1U;
    }
  }
  context->impl->wait_condition.notify_all();
}

[[nodiscard]] bool subscription_wait_data_valid(
    const void* data, const rmw_context_t* context) noexcept;

[[nodiscard]] bool subscription_wait_ready(const void* data) noexcept;

[[nodiscard]] bool publisher_network_view(
    void* data, NetworkEndpointView& view) noexcept;

[[nodiscard]] bool publisher_network_copy_next(
    void* data, std::uint64_t after_sequence, std::uint8_t* packet,
    std::size_t capacity, NetworkSample& sample) noexcept;

[[nodiscard]] std::size_t publisher_network_set_match_count(
    void* data, std::size_t count) noexcept;

[[nodiscard]] bool publisher_event_source(
    const rmw_publisher_t* publisher, rmw_context_t*& context,
    void*& source, MatchedStatusSnapshot& snapshot) noexcept;

[[nodiscard]] bool publisher_matched_status(
    const void* source, const rmw_context_t* context,
    MatchedStatusSnapshot& snapshot) noexcept;

[[nodiscard]] bool subscription_network_view(
    void* data, NetworkEndpointView& view) noexcept;

[[nodiscard]] bool subscription_network_receive(
    void* data, const std::uint8_t* packet, std::size_t packet_size,
    std::uint64_t received_timestamp_ns) noexcept;

[[nodiscard]] std::size_t subscription_network_set_match_count(
    void* data, std::size_t count) noexcept;

[[nodiscard]] bool subscription_event_source(
    const rmw_subscription_t* subscription, rmw_context_t*& context,
    void*& source, MatchedStatusSnapshot& snapshot) noexcept;

[[nodiscard]] bool subscription_matched_status(
    const void* source, const rmw_context_t* context,
    MatchedStatusSnapshot& snapshot) noexcept;

[[nodiscard]] bool event_wait_data_valid(
    const void* data, const rmw_context_t* context) noexcept;

[[nodiscard]] bool event_wait_ready(const void* data) noexcept;

void notify_event_source(rmw_context_t* context, const void* source,
                         std::size_t number_of_events) noexcept;

void notify_network(rmw_context_t* context) noexcept;

void notify_graph_change(rmw_context_t* context) noexcept;

[[nodiscard]] rmw_publisher_t* create_publisher_endpoint(
    const rmw_node_t* node,
    const rosidl_message_type_support_t* type_support,
    const char* topic_name,
    const rmw_qos_profile_t* qos_profile,
    const rmw_publisher_options_t* publisher_options,
    RosChannel channel, bool graph_visible) noexcept;

[[nodiscard]] rmw_subscription_t* create_subscription_endpoint(
    const rmw_node_t* node,
    const rosidl_message_type_support_t* type_support,
    const char* topic_name,
    const rmw_qos_profile_t* qos_profile,
    const rmw_subscription_options_t* subscription_options,
    RosChannel channel, bool graph_visible) noexcept;

[[nodiscard]] rmw_ret_t publish_service_sample(
    const rmw_publisher_t* publisher, const rmw_request_id_t& request_id,
    const void* ros_message) noexcept;

[[nodiscard]] rmw_ret_t take_service_sample(
    const rmw_subscription_t* subscription, rmw_service_info_t* service_info,
    void* ros_message, bool* taken,
    bool (*accept)(const rmw_request_id_t&, void*) = nullptr,
    void* accept_context = nullptr) noexcept;

[[nodiscard]] bool client_wait_data_valid(
    const void* data, const rmw_context_t* context) noexcept;
[[nodiscard]] bool client_wait_ready(const void* data) noexcept;
[[nodiscard]] bool service_wait_data_valid(
    const void* data, const rmw_context_t* context) noexcept;
[[nodiscard]] bool service_wait_ready(const void* data) noexcept;

}  // namespace rmw_openrtdds_cpp::detail
