#include <array>
#include <cstddef>
#include <cstring>
#include <mutex>

#include "internal.hpp"
#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rcutils/types/string_array.h"
#include "rmw/error_handling.h"
#include "rmw/get_node_info_and_types.h"
#include "rmw/get_topic_names_and_types.h"
#include "rmw/names_and_types.h"
#include "rmw/validate_full_topic_name.h"
#include "rmw/validate_namespace.h"
#include "rmw/validate_node_name.h"

namespace rmw_openrtdds_cpp::detail {
namespace {

using openrtdds::rmw::GraphEndpointKind;
using openrtdds::rmw::GraphEndpointRecord;
using openrtdds::rmw::GraphSnapshot;

struct TopicAggregate final {
  const char* name{nullptr};
  std::array<const char*, openrtdds::rmw::graph_endpoint_capacity> types{};
  std::size_t type_count{0U};
};

[[nodiscard]] bool valid_graph_node(const rmw_node_t* const node,
                                    rmw_context_t*& context) noexcept {
  if ((node == nullptr) || (node->context == nullptr) ||
      (node->data == nullptr) ||
      !identifier_matches(node->implementation_identifier) ||
      !context_is_valid(node->context)) {
    return false;
  }
  context = node->context;
  return true;
}

[[nodiscard]] rmw_ret_t invalid_graph_node_error(
    const rmw_node_t* const node) noexcept {
  if ((node != nullptr) && (node->implementation_identifier != nullptr) &&
      !identifier_matches(node->implementation_identifier)) {
    RMW_SET_ERROR_MSG("node belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  RMW_SET_ERROR_MSG("invalid OpenRTDDS node");
  return RMW_RET_INVALID_ARGUMENT;
}

[[nodiscard]] bool zero_string_array(
    const rcutils_string_array_t* const array) noexcept {
  return (array != nullptr) && (array->data == nullptr) &&
         (array->size == 0U);
}

void cleanup_names_and_types(rmw_names_and_types_t& value) noexcept {
  [[maybe_unused]] const rmw_ret_t cleanup_result =
      rmw_names_and_types_fini(&value);
}

void cleanup_string_array(rcutils_string_array_t& value) noexcept {
  [[maybe_unused]] const rcutils_ret_t cleanup_result =
      rcutils_string_array_fini(&value);
}

[[nodiscard]] bool snapshot_graph(rmw_context_t& context,
                                  GraphSnapshot& snapshot) noexcept {
  std::lock_guard<std::mutex> graph_lock(context.impl->graph_mutex);
  return context.impl->graph.snapshot(snapshot) ==
         openrtdds::rmw::GraphError::none;
}

[[nodiscard]] bool node_matches(
    const openrtdds::rmw::GraphNodeRecord& node, const char* const name,
    const char* const name_space) noexcept {
  return node.occupied && (std::strcmp(node.name.data(), name) == 0) &&
         (std::strcmp(node.name_space.data(), name_space) == 0);
}

[[nodiscard]] bool endpoint_owned_by_selected_node(
    const GraphEndpointRecord& endpoint, const GraphSnapshot& snapshot,
    const char* const name, const char* const name_space) noexcept {
  for (std::size_t index = 0U; index < snapshot.node_count; ++index) {
    const auto& node = snapshot.nodes[index];
    if (node_matches(node, name, name_space) &&
        (node.node_id == endpoint.node_id) &&
        (node.participant_prefix.value ==
         endpoint.participant_prefix.value)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool graph_has_node(const GraphSnapshot& snapshot,
                                  const char* const name,
                                  const char* const name_space) noexcept {
  for (std::size_t index = 0U; index < snapshot.node_count; ++index) {
    if (node_matches(snapshot.nodes[index], name, name_space)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool same_text(const char* const left,
                             const char* const right) noexcept {
  return (left != nullptr) && (right != nullptr) &&
         (std::strcmp(left, right) == 0);
}

[[nodiscard]] std::size_t aggregate_topics(
    const GraphSnapshot& snapshot, const bool no_demangle,
    const GraphEndpointKind* const kind, const char* const node_name,
    const char* const node_namespace,
    std::array<TopicAggregate,
               openrtdds::rmw::graph_endpoint_capacity>& topics) noexcept {
  std::size_t topic_count = 0U;
  for (std::size_t index = 0U; index < snapshot.endpoint_count; ++index) {
    const auto& endpoint = snapshot.endpoints[index];
    if (!endpoint.occupied || ((kind != nullptr) &&
                               (endpoint.kind != *kind)) ||
        ((node_name != nullptr) &&
         !endpoint_owned_by_selected_node(endpoint, snapshot, node_name,
                                          node_namespace))) {
      continue;
    }
    const bool use_dds = no_demangle || !endpoint.ros_namespace_conventions;
    const char* const topic_name =
        use_dds ? endpoint.dds_topic_name.data()
                : endpoint.ros_topic_name.data();
    const char* const type_name =
        use_dds ? endpoint.dds_type_name.data()
                : endpoint.ros_type_name.data();

    std::size_t topic_index = topic_count;
    for (std::size_t candidate = 0U; candidate < topic_count; ++candidate) {
      if (same_text(topics[candidate].name, topic_name)) {
        topic_index = candidate;
        break;
      }
    }
    if (topic_index == topic_count) {
      topics[topic_count].name = topic_name;
      ++topic_count;
    }
    auto& topic = topics[topic_index];
    bool known_type = false;
    for (std::size_t candidate = 0U; candidate < topic.type_count;
         ++candidate) {
      if (same_text(topic.types[candidate], type_name)) {
        known_type = true;
        break;
      }
    }
    if (!known_type && (topic.type_count < topic.types.size())) {
      topic.types[topic.type_count++] = type_name;
    }
  }
  return topic_count;
}

[[nodiscard]] rmw_ret_t fill_names_and_types(
    const std::array<TopicAggregate,
                     openrtdds::rmw::graph_endpoint_capacity>& topics,
    const std::size_t topic_count, rcutils_allocator_t* const allocator,
    rmw_names_and_types_t* const output) noexcept {
  rmw_names_and_types_t candidate = rmw_get_zero_initialized_names_and_types();
  rmw_ret_t result = rmw_names_and_types_init(
      &candidate, topic_count, allocator);
  if (result != RMW_RET_OK) {
    return result;
  }
  for (std::size_t index = 0U; index < topic_count; ++index) {
    candidate.names.data[index] = rcutils_strdup(topics[index].name,
                                                  *allocator);
    if (candidate.names.data[index] == nullptr) {
      cleanup_names_and_types(candidate);
      RMW_SET_ERROR_MSG("unable to allocate graph topic name");
      return RMW_RET_BAD_ALLOC;
    }
    if (rcutils_string_array_init(&candidate.types[index],
                                  topics[index].type_count,
                                  allocator) != RCUTILS_RET_OK) {
      cleanup_names_and_types(candidate);
      RMW_SET_ERROR_MSG("unable to allocate graph type array");
      return RMW_RET_BAD_ALLOC;
    }
    for (std::size_t type_index = 0U;
         type_index < topics[index].type_count; ++type_index) {
      candidate.types[index].data[type_index] =
          rcutils_strdup(topics[index].types[type_index], *allocator);
      if (candidate.types[index].data[type_index] == nullptr) {
        cleanup_names_and_types(candidate);
        RMW_SET_ERROR_MSG("unable to allocate graph type name");
        return RMW_RET_BAD_ALLOC;
      }
    }
  }
  *output = candidate;
  return RMW_RET_OK;
}

[[nodiscard]] rmw_ret_t names_and_types_query(
    const rmw_node_t* const node, rcutils_allocator_t* const allocator,
    const bool no_demangle, const GraphEndpointKind* const kind,
    const char* const node_name, const char* const node_namespace,
    rmw_names_and_types_t* const output) noexcept {
  rmw_context_t* context = nullptr;
  if (!valid_graph_node(node, context)) {
    return invalid_graph_node_error(node);
  }
  if ((allocator == nullptr) || !rcutils_allocator_is_valid(allocator) ||
      (output == nullptr)) {
    RMW_SET_ERROR_MSG("graph query requires a valid allocator and output");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (rmw_names_and_types_check_zero(output) != RMW_RET_OK) {
    return RMW_RET_INVALID_ARGUMENT;
  }
  GraphSnapshot snapshot{};
  if (!snapshot_graph(*context, snapshot)) {
    RMW_SET_ERROR_MSG("unable to snapshot OpenRTDDS graph");
    return RMW_RET_ERROR;
  }
  if ((node_name != nullptr) &&
      !graph_has_node(snapshot, node_name, node_namespace)) {
    RMW_SET_ERROR_MSG("requested node does not exist in graph");
    return RMW_RET_NODE_NAME_NON_EXISTENT;
  }
  std::array<TopicAggregate, openrtdds::rmw::graph_endpoint_capacity>
      topics{};
  const std::size_t topic_count = aggregate_topics(
      snapshot, no_demangle, kind, node_name, node_namespace, topics);
  return fill_names_and_types(topics, topic_count, allocator, output);
}

[[nodiscard]] rmw_ret_t validate_remote_node_name(
    const char* const node_name, const char* const node_namespace) noexcept {
  if ((node_name == nullptr) || (node_namespace == nullptr)) {
    RMW_SET_ERROR_MSG("node name and namespace are required");
    return RMW_RET_INVALID_ARGUMENT;
  }
  int validation = RMW_NODE_NAME_VALID;
  if ((rmw_validate_node_name(node_name, &validation, nullptr) != RMW_RET_OK) ||
      (validation != RMW_NODE_NAME_VALID)) {
    RMW_SET_ERROR_MSG("invalid graph node name");
    return RMW_RET_INVALID_ARGUMENT;
  }
  validation = RMW_NAMESPACE_VALID;
  if ((rmw_validate_namespace(node_namespace, &validation, nullptr) !=
       RMW_RET_OK) ||
      (validation != RMW_NAMESPACE_VALID)) {
    RMW_SET_ERROR_MSG("invalid graph node namespace");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return RMW_RET_OK;
}

[[nodiscard]] rmw_ret_t fill_node_arrays(
    const GraphSnapshot& snapshot, rcutils_allocator_t allocator,
    rcutils_string_array_t* const node_names,
    rcutils_string_array_t* const node_namespaces,
    rcutils_string_array_t* const enclaves) noexcept {
  rcutils_string_array_t names = rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t namespaces =
      rcutils_get_zero_initialized_string_array();
  rcutils_string_array_t enclave_values =
      rcutils_get_zero_initialized_string_array();
  if ((rcutils_string_array_init(&names, snapshot.node_count, &allocator) !=
       RCUTILS_RET_OK) ||
      (rcutils_string_array_init(&namespaces, snapshot.node_count,
                                 &allocator) != RCUTILS_RET_OK) ||
      ((enclaves != nullptr) &&
       (rcutils_string_array_init(&enclave_values, snapshot.node_count,
                                  &allocator) != RCUTILS_RET_OK))) {
    cleanup_string_array(enclave_values);
    cleanup_string_array(namespaces);
    cleanup_string_array(names);
    RMW_SET_ERROR_MSG("unable to allocate graph node arrays");
    return RMW_RET_BAD_ALLOC;
  }
  for (std::size_t index = 0U; index < snapshot.node_count; ++index) {
    names.data[index] =
        rcutils_strdup(snapshot.nodes[index].name.data(), allocator);
    namespaces.data[index] =
        rcutils_strdup(snapshot.nodes[index].name_space.data(), allocator);
    if (enclaves != nullptr) {
      enclave_values.data[index] =
          rcutils_strdup(snapshot.nodes[index].enclave.data(), allocator);
    }
    if ((names.data[index] == nullptr) ||
        (namespaces.data[index] == nullptr) ||
        ((enclaves != nullptr) &&
         (enclave_values.data[index] == nullptr))) {
      cleanup_string_array(enclave_values);
      cleanup_string_array(namespaces);
      cleanup_string_array(names);
      RMW_SET_ERROR_MSG("unable to allocate graph node string");
      return RMW_RET_BAD_ALLOC;
    }
  }
  *node_names = names;
  *node_namespaces = namespaces;
  if (enclaves != nullptr) {
    *enclaves = enclave_values;
  }
  return RMW_RET_OK;
}

[[nodiscard]] rmw_ret_t node_names_query(
    const rmw_node_t* const node,
    rcutils_string_array_t* const node_names,
    rcutils_string_array_t* const node_namespaces,
    rcutils_string_array_t* const enclaves) noexcept {
  rmw_context_t* context = nullptr;
  if (!valid_graph_node(node, context)) {
    return invalid_graph_node_error(node);
  }
  if (!zero_string_array(node_names) ||
      !zero_string_array(node_namespaces) ||
      ((enclaves != nullptr) && !zero_string_array(enclaves))) {
    RMW_SET_ERROR_MSG("graph node outputs must be zero initialized");
    return RMW_RET_INVALID_ARGUMENT;
  }
  GraphSnapshot snapshot{};
  if (!snapshot_graph(*context, snapshot)) {
    RMW_SET_ERROR_MSG("unable to snapshot OpenRTDDS graph");
    return RMW_RET_ERROR;
  }
  return fill_node_arrays(snapshot, context->options.allocator, node_names,
                          node_namespaces, enclaves);
}

}  // namespace

void notify_graph_change(rmw_context_t* const context) noexcept {
  if (!context_is_valid(context)) {
    return;
  }
  rmw_guard_condition_t* const guard = context->impl->graph_guard_condition;
  if ((guard != nullptr) && (guard->data != nullptr)) {
    const auto* const data = static_cast<const GuardConditionData*>(guard->data);
    {
      std::lock_guard<std::mutex> wait_lock(context->impl->wait_mutex);
      if (context->impl->foundation.trigger_guard_condition(data->handle) ==
          openrtdds::rmw::AdapterError::none) {
        ++context->impl->wait_generation;
        if (context->impl->wait_generation == 0U) {
          context->impl->wait_generation = 1U;
        }
      }
    }
    context->impl->wait_condition.notify_all();
  }
  notify_network(context);
}

}  // namespace rmw_openrtdds_cpp::detail

extern "C" {

// Requirements: ORT-RMW-010, ORT-RMW-020, ORT-RMW-055, ORT-RMW-056
rmw_ret_t rmw_get_topic_names_and_types(
    const rmw_node_t* node, rcutils_allocator_t* allocator,
    bool no_demangle, rmw_names_and_types_t* topic_names_and_types) {
  return rmw_openrtdds_cpp::detail::names_and_types_query(
      node, allocator, no_demangle, nullptr, nullptr, nullptr,
      topic_names_and_types);
}

rmw_ret_t rmw_get_publisher_names_and_types_by_node(
    const rmw_node_t* node, rcutils_allocator_t* allocator,
    const char* node_name, const char* node_namespace, bool no_demangle,
    rmw_names_and_types_t* topic_names_and_types) {
  const rmw_ret_t validation =
      rmw_openrtdds_cpp::detail::validate_remote_node_name(
          node_name, node_namespace);
  if (validation != RMW_RET_OK) {
    return validation;
  }
  constexpr auto kind = openrtdds::rmw::GraphEndpointKind::publisher;
  return rmw_openrtdds_cpp::detail::names_and_types_query(
      node, allocator, no_demangle, &kind, node_name, node_namespace,
      topic_names_and_types);
}

rmw_ret_t rmw_get_subscriber_names_and_types_by_node(
    const rmw_node_t* node, rcutils_allocator_t* allocator,
    const char* node_name, const char* node_namespace, bool no_demangle,
    rmw_names_and_types_t* topic_names_and_types) {
  const rmw_ret_t validation =
      rmw_openrtdds_cpp::detail::validate_remote_node_name(
          node_name, node_namespace);
  if (validation != RMW_RET_OK) {
    return validation;
  }
  constexpr auto kind = openrtdds::rmw::GraphEndpointKind::subscription;
  return rmw_openrtdds_cpp::detail::names_and_types_query(
      node, allocator, no_demangle, &kind, node_name, node_namespace,
      topic_names_and_types);
}

rmw_ret_t rmw_get_node_names(
    const rmw_node_t* node, rcutils_string_array_t* node_names,
    rcutils_string_array_t* node_namespaces) {
  return rmw_openrtdds_cpp::detail::node_names_query(
      node, node_names, node_namespaces, nullptr);
}

rmw_ret_t rmw_get_node_names_with_enclaves(
    const rmw_node_t* node, rcutils_string_array_t* node_names,
    rcutils_string_array_t* node_namespaces,
    rcutils_string_array_t* enclaves) {
  if (enclaves == nullptr) {
    RMW_SET_ERROR_MSG("node enclave output is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return rmw_openrtdds_cpp::detail::node_names_query(
      node, node_names, node_namespaces, enclaves);
}

rmw_ret_t rmw_count_publishers(const rmw_node_t* node,
                               const char* topic_name,
                               size_t* count) {
  using namespace rmw_openrtdds_cpp::detail;
  rmw_context_t* context = nullptr;
  if (!valid_graph_node(node, context)) {
    return invalid_graph_node_error(node);
  }
  if ((topic_name == nullptr) || (count == nullptr)) {
    RMW_SET_ERROR_MSG("publisher count requires topic and output");
    return RMW_RET_INVALID_ARGUMENT;
  }
  int validation = RMW_TOPIC_VALID;
  if ((rmw_validate_full_topic_name(topic_name, &validation, nullptr) !=
       RMW_RET_OK) ||
      (validation != RMW_TOPIC_VALID)) {
    RMW_SET_ERROR_MSG("invalid topic name");
    return RMW_RET_INVALID_ARGUMENT;
  }
  openrtdds::rmw::GraphSnapshot snapshot{};
  if (!snapshot_graph(*context, snapshot)) {
    return RMW_RET_ERROR;
  }
  std::size_t result = 0U;
  for (std::size_t index = 0U; index < snapshot.endpoint_count; ++index) {
    const auto& endpoint = snapshot.endpoints[index];
    if (endpoint.occupied &&
        (endpoint.kind == openrtdds::rmw::GraphEndpointKind::publisher) &&
        (std::strcmp(endpoint.ros_topic_name.data(), topic_name) == 0)) {
      ++result;
    }
  }
  *count = result;
  return RMW_RET_OK;
}

rmw_ret_t rmw_count_subscribers(const rmw_node_t* node,
                                const char* topic_name,
                                size_t* count) {
  using namespace rmw_openrtdds_cpp::detail;
  rmw_context_t* context = nullptr;
  if (!valid_graph_node(node, context)) {
    return invalid_graph_node_error(node);
  }
  if ((topic_name == nullptr) || (count == nullptr)) {
    RMW_SET_ERROR_MSG("subscriber count requires topic and output");
    return RMW_RET_INVALID_ARGUMENT;
  }
  int validation = RMW_TOPIC_VALID;
  if ((rmw_validate_full_topic_name(topic_name, &validation, nullptr) !=
       RMW_RET_OK) ||
      (validation != RMW_TOPIC_VALID)) {
    RMW_SET_ERROR_MSG("invalid topic name");
    return RMW_RET_INVALID_ARGUMENT;
  }
  openrtdds::rmw::GraphSnapshot snapshot{};
  if (!snapshot_graph(*context, snapshot)) {
    return RMW_RET_ERROR;
  }
  std::size_t result = 0U;
  for (std::size_t index = 0U; index < snapshot.endpoint_count; ++index) {
    const auto& endpoint = snapshot.endpoints[index];
    if (endpoint.occupied &&
        (endpoint.kind == openrtdds::rmw::GraphEndpointKind::subscription) &&
        (std::strcmp(endpoint.ros_topic_name.data(), topic_name) == 0)) {
      ++result;
    }
  }
  *count = result;
  return RMW_RET_OK;
}

}  // extern "C"
