#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "openrtdds/rtps/types.hpp"

namespace openrtdds::rmw {

// Requirements: ORT-RMW-010, ORT-RMW-020, ORT-RMW-055, ORT-RMW-056
constexpr std::size_t graph_local_node_capacity = 8U;
constexpr std::size_t graph_local_endpoint_capacity = 16U;
constexpr std::size_t graph_remote_participant_capacity = 16U;
constexpr std::size_t graph_node_capacity = 136U;
constexpr std::size_t graph_endpoint_capacity = 48U;
constexpr std::size_t graph_node_name_capacity = 64U;
constexpr std::size_t graph_namespace_capacity = 128U;
constexpr std::size_t graph_enclave_capacity = 256U;
constexpr std::size_t graph_topic_name_capacity = 256U;
constexpr std::size_t graph_type_name_capacity = 256U;
constexpr std::size_t graph_wire_capacity = 32'768U;

enum class GraphEndpointKind : std::uint8_t {
  publisher = 1U,
  subscription = 2U,
};

enum class GraphError : std::uint8_t {
  none = 0U,
  invalid_argument,
  invalid_record,
  capacity_exceeded,
  duplicate_record,
  record_not_found,
  stale_snapshot,
  malformed_snapshot,
  buffer_overflow,
};

enum class GraphUpdate : std::uint8_t {
  unchanged = 0U,
  changed,
  rejected,
};

struct GraphNodeRecord final {
  rtps::GuidPrefix participant_prefix{};
  std::uint64_t node_id{0U};
  std::array<char, graph_node_name_capacity> name{};
  std::array<char, graph_namespace_capacity> name_space{};
  std::array<char, graph_enclave_capacity> enclave{};
  std::size_t name_size{0U};
  std::size_t namespace_size{0U};
  std::size_t enclave_size{0U};
  bool local{false};
  bool occupied{false};
};

struct GraphEndpointRecord final {
  rtps::GuidPrefix participant_prefix{};
  rtps::EntityId entity_id{};
  std::uint64_t node_id{0U};
  GraphEndpointKind kind{GraphEndpointKind::publisher};
  std::array<char, graph_topic_name_capacity> ros_topic_name{};
  std::array<char, graph_type_name_capacity> ros_type_name{};
  std::array<char, graph_topic_name_capacity> dds_topic_name{};
  std::array<char, graph_type_name_capacity> dds_type_name{};
  std::size_t ros_topic_name_size{0U};
  std::size_t ros_type_name_size{0U};
  std::size_t dds_topic_name_size{0U};
  std::size_t dds_type_name_size{0U};
  bool ros_namespace_conventions{true};
  bool local{false};
  bool occupied{false};
};

struct GraphSnapshot final {
  std::array<GraphNodeRecord, graph_node_capacity> nodes{};
  std::array<GraphEndpointRecord, graph_endpoint_capacity> endpoints{};
  std::size_t node_count{0U};
  std::size_t endpoint_count{0U};
  std::uint64_t generation{0U};
};

class GraphCache final {
 public:
  GraphCache() noexcept = default;
  GraphCache(const GraphCache&) = delete;
  GraphCache& operator=(const GraphCache&) = delete;

  [[nodiscard]] GraphError add_local_node(
      const rtps::GuidPrefix& participant_prefix, std::uint64_t node_id,
      const char* name, const char* name_space, const char* enclave) noexcept;
  [[nodiscard]] GraphError remove_local_node(std::uint64_t node_id) noexcept;

  [[nodiscard]] GraphError add_local_endpoint(
      const rtps::GuidPrefix& participant_prefix,
      const rtps::EntityId& entity_id, std::uint64_t node_id,
      GraphEndpointKind kind, const char* ros_topic_name,
      const char* ros_type_name, const char* dds_topic_name,
      const char* dds_type_name,
      bool ros_namespace_conventions) noexcept;
  [[nodiscard]] GraphError remove_local_endpoint(
      const rtps::EntityId& entity_id) noexcept;

  [[nodiscard]] GraphError snapshot(GraphSnapshot& output) const noexcept;
  [[nodiscard]] GraphError serialize_local(
      std::uint8_t* output, std::size_t capacity, std::size_t& output_size,
      std::uint64_t& generation) const noexcept;
  [[nodiscard]] GraphUpdate apply_remote(
      const rtps::GuidPrefix& participant_prefix,
      const std::uint8_t* payload, std::size_t payload_size,
      std::uint64_t rtps_sequence_number,
      GraphError& error) noexcept;
  [[nodiscard]] GraphUpdate remove_remote_participant(
      const rtps::GuidPrefix& participant_prefix) noexcept;

  [[nodiscard]] std::uint64_t generation() const noexcept {
    return generation_;
  }

 private:
  struct RemoteGeneration final {
    rtps::GuidPrefix participant_prefix{};
    std::uint64_t generation{0U};
    bool occupied{false};
  };

  void advance_generation(bool local_change) noexcept;

  std::array<GraphNodeRecord, graph_node_capacity> nodes_{};
  std::array<GraphEndpointRecord, graph_endpoint_capacity> endpoints_{};
  std::array<RemoteGeneration, graph_remote_participant_capacity>
      remote_generations_{};
  std::uint64_t generation_{1U};
  std::uint64_t local_generation_{1U};
};

[[nodiscard]] const char* to_string(GraphError error) noexcept;

}  // namespace openrtdds::rmw
