#include "openrtdds/rmw/graph_cache.hpp"

#include <cstring>
#include <limits>

namespace openrtdds::rmw {
namespace {

constexpr std::array<std::uint8_t, 4U> kEncapsulation{{0U, 1U, 0U, 0U}};
constexpr std::array<std::uint8_t, 4U> kMagic{{'O', 'R', 'T', 'G'}};
constexpr std::uint8_t kVersion = 1U;
constexpr std::size_t kHeaderSize = 24U;
constexpr std::size_t kNodeHeaderSize = 16U;
constexpr std::size_t kEndpointHeaderSize = 24U;

[[nodiscard]] bool nonzero_prefix(const rtps::GuidPrefix& prefix) noexcept {
  for (const auto value : prefix.value) {
    if (value != 0U) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool valid_entity(const rtps::EntityId& entity) noexcept {
  return (entity.value[0] | entity.value[1] | entity.value[2] |
          entity.value[3]) != 0U;
}

template <std::size_t Capacity>
[[nodiscard]] bool copy_text(const char* const input,
                             std::array<char, Capacity>& output,
                             std::size_t& output_size,
                             const bool empty_allowed) noexcept {
  if (input == nullptr) {
    return false;
  }
  std::size_t size = 0U;
  while ((size < Capacity) && (input[size] != '\0')) {
    ++size;
  }
  if ((size >= Capacity) || (!empty_allowed && (size == 0U))) {
    return false;
  }
  std::array<char, Capacity> candidate{};
  std::memcpy(candidate.data(), input, size);
  output = candidate;
  output_size = size;
  return true;
}

void write_u16(std::uint8_t* const output,
               const std::uint16_t value) noexcept {
  output[0] = static_cast<std::uint8_t>(value & 0xFFU);
  output[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u64(std::uint8_t* const output,
               const std::uint64_t value) noexcept {
  for (std::size_t index = 0U; index < 8U; ++index) {
    output[index] =
        static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
  }
}

[[nodiscard]] std::uint16_t read_u16(
    const std::uint8_t* const input) noexcept {
  return static_cast<std::uint16_t>(input[0]) |
         (static_cast<std::uint16_t>(input[1]) << 8U);
}

[[nodiscard]] std::uint64_t read_u64(
    const std::uint8_t* const input) noexcept {
  std::uint64_t value = 0U;
  for (std::size_t index = 0U; index < 8U; ++index) {
    value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
  }
  return value;
}

[[nodiscard]] bool reserve(const std::size_t amount,
                           const std::size_t capacity,
                           std::size_t& position) noexcept {
  if ((position > capacity) || (amount > (capacity - position))) {
    return false;
  }
  position += amount;
  return true;
}

template <std::size_t Capacity>
[[nodiscard]] bool read_text(const std::uint8_t* const payload,
                             const std::size_t payload_size,
                             std::size_t& position,
                             const std::size_t size,
                             std::array<char, Capacity>& output,
                             const bool empty_allowed) noexcept {
  if ((size >= Capacity) || (!empty_allowed && (size == 0U)) ||
      (position > payload_size) || (size > (payload_size - position))) {
    return false;
  }
  std::array<char, Capacity> candidate{};
  std::memcpy(candidate.data(), &payload[position], size);
  position += size;
  output = candidate;
  return true;
}

[[nodiscard]] bool node_exists(
    const std::array<GraphNodeRecord, graph_local_node_capacity>& nodes,
    const std::size_t count, const std::uint64_t node_id) noexcept {
  for (std::size_t index = 0U; index < count; ++index) {
    if (nodes[index].node_id == node_id) {
      return true;
    }
  }
  return false;
}

}  // namespace

const char* to_string(const GraphError error) noexcept {
  switch (error) {
    case GraphError::none: return "none";
    case GraphError::invalid_argument: return "invalid_argument";
    case GraphError::invalid_record: return "invalid_record";
    case GraphError::capacity_exceeded: return "capacity_exceeded";
    case GraphError::duplicate_record: return "duplicate_record";
    case GraphError::record_not_found: return "record_not_found";
    case GraphError::stale_snapshot: return "stale_snapshot";
    case GraphError::malformed_snapshot: return "malformed_snapshot";
    case GraphError::buffer_overflow: return "buffer_overflow";
  }
  return "unknown";
}

void GraphCache::advance_generation(const bool local_change) noexcept {
  generation_ = generation_ >=
                        static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max())
                    ? 1U
                    : generation_ + 1U;
  if (local_change) {
    local_generation_ = local_generation_ >=
                                static_cast<std::uint64_t>(
                                    std::numeric_limits<std::int64_t>::max())
                            ? 1U
                            : local_generation_ + 1U;
  }
}

GraphError GraphCache::add_local_node(
    const rtps::GuidPrefix& participant_prefix, const std::uint64_t node_id,
    const char* const name, const char* const name_space,
    const char* const enclave) noexcept {
  if (!nonzero_prefix(participant_prefix) || (node_id == 0U)) {
    return GraphError::invalid_argument;
  }
  for (const auto& node : nodes_) {
    if (node.occupied && node.local && (node.node_id == node_id)) {
      return GraphError::duplicate_record;
    }
  }
  GraphNodeRecord candidate{};
  candidate.participant_prefix = participant_prefix;
  candidate.node_id = node_id;
  candidate.local = true;
  candidate.occupied = true;
  if (!copy_text(name, candidate.name, candidate.name_size, false) ||
      !copy_text(name_space, candidate.name_space,
                 candidate.namespace_size, false) ||
      !copy_text(enclave, candidate.enclave, candidate.enclave_size, false)) {
    return GraphError::invalid_record;
  }
  for (auto& node : nodes_) {
    if (!node.occupied) {
      node = candidate;
      advance_generation(true);
      return GraphError::none;
    }
  }
  return GraphError::capacity_exceeded;
}

GraphError GraphCache::remove_local_node(const std::uint64_t node_id) noexcept {
  for (const auto& endpoint : endpoints_) {
    if (endpoint.occupied && endpoint.local &&
        (endpoint.node_id == node_id)) {
      return GraphError::invalid_record;
    }
  }
  for (auto& node : nodes_) {
    if (node.occupied && node.local && (node.node_id == node_id)) {
      node = {};
      advance_generation(true);
      return GraphError::none;
    }
  }
  return GraphError::record_not_found;
}

GraphError GraphCache::add_local_endpoint(
    const rtps::GuidPrefix& participant_prefix,
    const rtps::EntityId& entity_id, const std::uint64_t node_id,
    const GraphEndpointKind kind, const char* const ros_topic_name,
    const char* const ros_type_name, const char* const dds_topic_name,
    const char* const dds_type_name,
    const bool ros_namespace_conventions) noexcept {
  if (!nonzero_prefix(participant_prefix) || !valid_entity(entity_id) ||
      (node_id == 0U) ||
      ((kind != GraphEndpointKind::publisher) &&
       (kind != GraphEndpointKind::subscription))) {
    return GraphError::invalid_argument;
  }
  bool owner_exists = false;
  for (const auto& node : nodes_) {
    if (node.occupied && node.local && (node.node_id == node_id) &&
        (node.participant_prefix.value == participant_prefix.value)) {
      owner_exists = true;
      break;
    }
  }
  if (!owner_exists) {
    return GraphError::record_not_found;
  }
  for (const auto& endpoint : endpoints_) {
    if (endpoint.occupied && endpoint.local &&
        (endpoint.entity_id == entity_id)) {
      return GraphError::duplicate_record;
    }
  }
  GraphEndpointRecord candidate{};
  candidate.participant_prefix = participant_prefix;
  candidate.entity_id = entity_id;
  candidate.node_id = node_id;
  candidate.kind = kind;
  candidate.ros_namespace_conventions = ros_namespace_conventions;
  candidate.local = true;
  candidate.occupied = true;
  if (!copy_text(ros_topic_name, candidate.ros_topic_name,
                 candidate.ros_topic_name_size, false) ||
      !copy_text(ros_type_name, candidate.ros_type_name,
                 candidate.ros_type_name_size, false) ||
      !copy_text(dds_topic_name, candidate.dds_topic_name,
                 candidate.dds_topic_name_size, false) ||
      !copy_text(dds_type_name, candidate.dds_type_name,
                 candidate.dds_type_name_size, false)) {
    return GraphError::invalid_record;
  }
  for (auto& endpoint : endpoints_) {
    if (!endpoint.occupied) {
      endpoint = candidate;
      advance_generation(true);
      return GraphError::none;
    }
  }
  return GraphError::capacity_exceeded;
}

GraphError GraphCache::remove_local_endpoint(
    const rtps::EntityId& entity_id) noexcept {
  for (auto& endpoint : endpoints_) {
    if (endpoint.occupied && endpoint.local &&
        (endpoint.entity_id == entity_id)) {
      endpoint = {};
      advance_generation(true);
      return GraphError::none;
    }
  }
  return GraphError::record_not_found;
}

GraphError GraphCache::snapshot(GraphSnapshot& output) const noexcept {
  GraphSnapshot candidate{};
  for (const auto& node : nodes_) {
    if (node.occupied) {
      candidate.nodes[candidate.node_count++] = node;
    }
  }
  for (const auto& endpoint : endpoints_) {
    if (endpoint.occupied) {
      candidate.endpoints[candidate.endpoint_count++] = endpoint;
    }
  }
  candidate.generation = generation_;
  output = candidate;
  return GraphError::none;
}

GraphError GraphCache::serialize_local(
    std::uint8_t* const output, const std::size_t capacity,
    std::size_t& output_size, std::uint64_t& generation) const noexcept {
  output_size = 0U;
  generation = local_generation_;
  if ((output == nullptr) || (capacity < kHeaderSize)) {
    return GraphError::invalid_argument;
  }
  std::uint16_t node_count = 0U;
  std::uint16_t endpoint_count = 0U;
  for (const auto& node : nodes_) {
    if (node.occupied && node.local) {
      ++node_count;
    }
  }
  for (const auto& endpoint : endpoints_) {
    if (endpoint.occupied && endpoint.local) {
      ++endpoint_count;
    }
  }
  if ((node_count > graph_local_node_capacity) ||
      (endpoint_count > graph_local_endpoint_capacity)) {
    return GraphError::capacity_exceeded;
  }

  std::memcpy(output, kEncapsulation.data(), kEncapsulation.size());
  std::memcpy(&output[4], kMagic.data(), kMagic.size());
  output[8] = kVersion;
  output[9] = 0U;
  write_u16(&output[10], node_count);
  write_u16(&output[12], endpoint_count);
  output[14] = 0U;
  output[15] = 0U;
  write_u64(&output[16], local_generation_);
  std::size_t position = kHeaderSize;

  for (const auto& node : nodes_) {
    if (!node.occupied || !node.local) {
      continue;
    }
    const std::size_t record_size = kNodeHeaderSize + node.name_size +
                                    node.namespace_size + node.enclave_size;
    const std::size_t start = position;
    if (!reserve(record_size, capacity, position)) {
      output_size = 0U;
      return GraphError::buffer_overflow;
    }
    write_u64(&output[start], node.node_id);
    write_u16(&output[start + 8U],
              static_cast<std::uint16_t>(node.name_size));
    write_u16(&output[start + 10U],
              static_cast<std::uint16_t>(node.namespace_size));
    write_u16(&output[start + 12U],
              static_cast<std::uint16_t>(node.enclave_size));
    output[start + 14U] = 0U;
    output[start + 15U] = 0U;
    std::size_t cursor = start + kNodeHeaderSize;
    std::memcpy(&output[cursor], node.name.data(), node.name_size);
    cursor += node.name_size;
    std::memcpy(&output[cursor], node.name_space.data(), node.namespace_size);
    cursor += node.namespace_size;
    std::memcpy(&output[cursor], node.enclave.data(), node.enclave_size);
  }

  for (const auto& endpoint : endpoints_) {
    if (!endpoint.occupied || !endpoint.local) {
      continue;
    }
    const std::size_t record_size =
        kEndpointHeaderSize + endpoint.ros_topic_name_size +
        endpoint.ros_type_name_size + endpoint.dds_topic_name_size +
        endpoint.dds_type_name_size;
    const std::size_t start = position;
    if (!reserve(record_size, capacity, position)) {
      output_size = 0U;
      return GraphError::buffer_overflow;
    }
    write_u64(&output[start], endpoint.node_id);
    std::memcpy(&output[start + 8U], endpoint.entity_id.value.data(), 4U);
    output[start + 12U] = static_cast<std::uint8_t>(endpoint.kind);
    output[start + 13U] = endpoint.ros_namespace_conventions ? 1U : 0U;
    output[start + 14U] = 0U;
    output[start + 15U] = 0U;
    write_u16(&output[start + 16U],
              static_cast<std::uint16_t>(endpoint.ros_topic_name_size));
    write_u16(&output[start + 18U],
              static_cast<std::uint16_t>(endpoint.ros_type_name_size));
    write_u16(&output[start + 20U],
              static_cast<std::uint16_t>(endpoint.dds_topic_name_size));
    write_u16(&output[start + 22U],
              static_cast<std::uint16_t>(endpoint.dds_type_name_size));
    std::size_t cursor = start + kEndpointHeaderSize;
    std::memcpy(&output[cursor], endpoint.ros_topic_name.data(),
                endpoint.ros_topic_name_size);
    cursor += endpoint.ros_topic_name_size;
    std::memcpy(&output[cursor], endpoint.ros_type_name.data(),
                endpoint.ros_type_name_size);
    cursor += endpoint.ros_type_name_size;
    std::memcpy(&output[cursor], endpoint.dds_topic_name.data(),
                endpoint.dds_topic_name_size);
    cursor += endpoint.dds_topic_name_size;
    std::memcpy(&output[cursor], endpoint.dds_type_name.data(),
                endpoint.dds_type_name_size);
  }

  output_size = position;
  return GraphError::none;
}

GraphUpdate GraphCache::apply_remote(
    const rtps::GuidPrefix& participant_prefix,
    const std::uint8_t* const payload, const std::size_t payload_size,
    const std::uint64_t rtps_sequence_number,
    GraphError& error) noexcept {
  error = GraphError::malformed_snapshot;
  if (!nonzero_prefix(participant_prefix) || (payload == nullptr) ||
      (payload_size < kHeaderSize) ||
      (std::memcmp(payload, kEncapsulation.data(), 4U) != 0) ||
      (std::memcmp(&payload[4], kMagic.data(), 4U) != 0) ||
      (payload[8] != kVersion) || (payload[9] != 0U) ||
      (payload[14] != 0U) || (payload[15] != 0U)) {
    return GraphUpdate::rejected;
  }
  const std::size_t node_count = read_u16(&payload[10]);
  const std::size_t endpoint_count = read_u16(&payload[12]);
  const std::uint64_t remote_generation = read_u64(&payload[16]);
  if ((node_count > graph_local_node_capacity) ||
      (endpoint_count > graph_local_endpoint_capacity) ||
      (remote_generation == 0U) ||
      (remote_generation > static_cast<std::uint64_t>(
                               std::numeric_limits<std::int64_t>::max())) ||
      (remote_generation != rtps_sequence_number)) {
    return GraphUpdate::rejected;
  }

  std::array<GraphNodeRecord, graph_local_node_capacity> incoming_nodes{};
  std::array<GraphEndpointRecord, graph_local_endpoint_capacity>
      incoming_endpoints{};
  std::size_t position = kHeaderSize;
  for (std::size_t index = 0U; index < node_count; ++index) {
    if ((position > payload_size) ||
        (kNodeHeaderSize > (payload_size - position))) {
      return GraphUpdate::rejected;
    }
    const std::size_t start = position;
    GraphNodeRecord candidate{};
    candidate.participant_prefix = participant_prefix;
    candidate.node_id = read_u64(&payload[start]);
    const std::size_t name_size = read_u16(&payload[start + 8U]);
    const std::size_t namespace_size = read_u16(&payload[start + 10U]);
    const std::size_t enclave_size = read_u16(&payload[start + 12U]);
    if ((candidate.node_id == 0U) || (payload[start + 14U] != 0U) ||
        (payload[start + 15U] != 0U)) {
      return GraphUpdate::rejected;
    }
    position += kNodeHeaderSize;
    if (!read_text(payload, payload_size, position, name_size,
                   candidate.name, false) ||
        !read_text(payload, payload_size, position, namespace_size,
                   candidate.name_space, false) ||
        !read_text(payload, payload_size, position, enclave_size,
                   candidate.enclave, false)) {
      return GraphUpdate::rejected;
    }
    candidate.name_size = name_size;
    candidate.namespace_size = namespace_size;
    candidate.enclave_size = enclave_size;
    candidate.occupied = true;
    for (std::size_t previous = 0U; previous < index; ++previous) {
      if (incoming_nodes[previous].node_id == candidate.node_id) {
        error = GraphError::duplicate_record;
        return GraphUpdate::rejected;
      }
    }
    incoming_nodes[index] = candidate;
  }

  for (std::size_t index = 0U; index < endpoint_count; ++index) {
    if ((position > payload_size) ||
        (kEndpointHeaderSize > (payload_size - position))) {
      return GraphUpdate::rejected;
    }
    const std::size_t start = position;
    GraphEndpointRecord candidate{};
    candidate.participant_prefix = participant_prefix;
    candidate.node_id = read_u64(&payload[start]);
    std::memcpy(candidate.entity_id.value.data(), &payload[start + 8U], 4U);
    candidate.kind = static_cast<GraphEndpointKind>(payload[start + 12U]);
    candidate.ros_namespace_conventions = payload[start + 13U] == 1U;
    const std::size_t ros_topic_size = read_u16(&payload[start + 16U]);
    const std::size_t ros_type_size = read_u16(&payload[start + 18U]);
    const std::size_t dds_topic_size = read_u16(&payload[start + 20U]);
    const std::size_t dds_type_size = read_u16(&payload[start + 22U]);
    if (!valid_entity(candidate.entity_id) ||
        !node_exists(incoming_nodes, node_count, candidate.node_id) ||
        ((candidate.kind != GraphEndpointKind::publisher) &&
         (candidate.kind != GraphEndpointKind::subscription)) ||
        (payload[start + 13U] > 1U) || (payload[start + 14U] != 0U) ||
        (payload[start + 15U] != 0U)) {
      return GraphUpdate::rejected;
    }
    position += kEndpointHeaderSize;
    if (!read_text(payload, payload_size, position, ros_topic_size,
                   candidate.ros_topic_name, false) ||
        !read_text(payload, payload_size, position, ros_type_size,
                   candidate.ros_type_name, false) ||
        !read_text(payload, payload_size, position, dds_topic_size,
                   candidate.dds_topic_name, false) ||
        !read_text(payload, payload_size, position, dds_type_size,
                   candidate.dds_type_name, false)) {
      return GraphUpdate::rejected;
    }
    candidate.ros_topic_name_size = ros_topic_size;
    candidate.ros_type_name_size = ros_type_size;
    candidate.dds_topic_name_size = dds_topic_size;
    candidate.dds_type_name_size = dds_type_size;
    candidate.occupied = true;
    for (std::size_t previous = 0U; previous < index; ++previous) {
      if (incoming_endpoints[previous].entity_id == candidate.entity_id) {
        error = GraphError::duplicate_record;
        return GraphUpdate::rejected;
      }
    }
    incoming_endpoints[index] = candidate;
  }
  if (position != payload_size) {
    return GraphUpdate::rejected;
  }

  RemoteGeneration* generation_entry = nullptr;
  RemoteGeneration* available_generation = nullptr;
  for (auto& candidate : remote_generations_) {
    if (candidate.occupied &&
        (candidate.participant_prefix.value == participant_prefix.value)) {
      generation_entry = &candidate;
      break;
    }
    if (!candidate.occupied && (available_generation == nullptr)) {
      available_generation = &candidate;
    }
  }
  if ((generation_entry != nullptr) &&
      (remote_generation <= generation_entry->generation)) {
    error = GraphError::stale_snapshot;
    return GraphUpdate::unchanged;
  }
  if ((generation_entry == nullptr) && (available_generation == nullptr)) {
    error = GraphError::capacity_exceeded;
    return GraphUpdate::rejected;
  }

  std::size_t remaining_nodes = 0U;
  for (const auto& node : nodes_) {
    if (node.occupied &&
        (node.local ||
         (node.participant_prefix.value != participant_prefix.value))) {
      ++remaining_nodes;
    }
  }
  std::size_t remaining_endpoints = 0U;
  for (const auto& endpoint : endpoints_) {
    if (endpoint.occupied &&
        (endpoint.local ||
         (endpoint.participant_prefix.value != participant_prefix.value))) {
      ++remaining_endpoints;
    }
  }
  if ((node_count > (graph_node_capacity - remaining_nodes)) ||
      (endpoint_count > (graph_endpoint_capacity - remaining_endpoints))) {
    error = GraphError::capacity_exceeded;
    return GraphUpdate::rejected;
  }

  for (auto& node : nodes_) {
    if (node.occupied && !node.local &&
        (node.participant_prefix.value == participant_prefix.value)) {
      node = {};
    }
  }
  for (auto& endpoint : endpoints_) {
    if (endpoint.occupied && !endpoint.local &&
        (endpoint.participant_prefix.value == participant_prefix.value)) {
      endpoint = {};
    }
  }
  for (std::size_t index = 0U; index < node_count; ++index) {
    for (auto& destination : nodes_) {
      if (!destination.occupied) {
        destination = incoming_nodes[index];
        break;
      }
    }
  }
  for (std::size_t index = 0U; index < endpoint_count; ++index) {
    for (auto& destination : endpoints_) {
      if (!destination.occupied) {
        destination = incoming_endpoints[index];
        break;
      }
    }
  }
  if (generation_entry == nullptr) {
    generation_entry = available_generation;
    generation_entry->participant_prefix = participant_prefix;
    generation_entry->occupied = true;
  }
  generation_entry->generation = remote_generation;
  advance_generation(false);
  error = GraphError::none;
  return GraphUpdate::changed;
}

GraphUpdate GraphCache::remove_remote_participant(
    const rtps::GuidPrefix& participant_prefix) noexcept {
  bool changed = false;
  for (auto& node : nodes_) {
    if (node.occupied && !node.local &&
        (node.participant_prefix.value == participant_prefix.value)) {
      node = {};
      changed = true;
    }
  }
  for (auto& endpoint : endpoints_) {
    if (endpoint.occupied && !endpoint.local &&
        (endpoint.participant_prefix.value == participant_prefix.value)) {
      endpoint = {};
      changed = true;
    }
  }
  for (auto& generation : remote_generations_) {
    if (generation.occupied &&
        (generation.participant_prefix.value == participant_prefix.value)) {
      generation = {};
      break;
    }
  }
  if (changed) {
    advance_generation(false);
    return GraphUpdate::changed;
  }
  return GraphUpdate::unchanged;
}

}  // namespace openrtdds::rmw
