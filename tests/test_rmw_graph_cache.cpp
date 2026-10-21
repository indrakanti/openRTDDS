#include <array>
#include <cstring>

#include "openrtdds/rmw/graph_cache.hpp"
#include "test_support.hpp"

// Verifies: ORT-RMW-010, ORT-RMW-020, ORT-RMW-055, ORT-RMW-056

namespace {

using openrtdds::rmw::GraphCache;
using openrtdds::rmw::GraphEndpointKind;
using openrtdds::rmw::GraphError;
using openrtdds::rmw::GraphSnapshot;
using openrtdds::rmw::GraphUpdate;
using openrtdds::rtps::EntityId;
using openrtdds::rtps::GuidPrefix;

constexpr GuidPrefix kLocal{{{1U, 2U, 3U, 4U, 5U, 6U,
                               7U, 8U, 9U, 10U, 11U, 12U}}};
constexpr GuidPrefix kRemote{{{12U, 11U, 10U, 9U, 8U, 7U,
                                6U, 5U, 4U, 3U, 2U, 1U}}};
constexpr EntityId kWriter{{0U, 0U, 1U, 0x03U}};
constexpr EntityId kReader{{0U, 0U, 1U, 0x04U}};

void test_local_lifecycle_and_snapshot() {
  GraphCache cache;
  CHECK(cache.add_local_node(kLocal, 7U, "talker", "/demo", "/") ==
        GraphError::none);
  CHECK(cache.add_local_node(kLocal, 7U, "duplicate", "/", "/") ==
        GraphError::duplicate_record);
  CHECK(cache.add_local_endpoint(
            kLocal, kWriter, 7U, GraphEndpointKind::publisher,
            "/graph_demo", "std_msgs/msg/UInt32", "rt/graph_demo",
            "std_msgs::msg::dds_::UInt32_", true) == GraphError::none);
  CHECK(cache.remove_local_node(7U) == GraphError::invalid_record);

  GraphSnapshot snapshot{};
  CHECK(cache.snapshot(snapshot) == GraphError::none);
  CHECK(snapshot.node_count == 1U);
  CHECK(snapshot.endpoint_count == 1U);
  CHECK(std::strcmp(snapshot.nodes[0].name.data(), "talker") == 0);
  CHECK(std::strcmp(snapshot.endpoints[0].ros_topic_name.data(),
                    "/graph_demo") == 0);

  CHECK(cache.remove_local_endpoint(kWriter) == GraphError::none);
  CHECK(cache.remove_local_node(7U) == GraphError::none);
  CHECK(cache.snapshot(snapshot) == GraphError::none);
  CHECK(snapshot.node_count == 0U);
  CHECK(snapshot.endpoint_count == 0U);
}

void test_wire_round_trip_and_expiry() {
  GraphCache source;
  CHECK(source.add_local_node(kLocal, 17U, "subject", "/graph", "/safe") ==
        GraphError::none);
  CHECK(source.add_local_endpoint(
            kLocal, kWriter, 17U, GraphEndpointKind::publisher,
            "/graph_demo", "std_msgs/msg/UInt32", "rt/graph_demo",
            "std_msgs::msg::dds_::UInt32_", true) == GraphError::none);
  CHECK(source.add_local_endpoint(
            kLocal, kReader, 17U, GraphEndpointKind::subscription,
            "/graph_demo", "std_msgs/msg/UInt32", "rt/graph_demo",
            "std_msgs::msg::dds_::UInt32_", true) == GraphError::none);

  std::array<std::uint8_t, openrtdds::rmw::graph_wire_capacity> wire{};
  std::size_t wire_size = 0U;
  std::uint64_t generation = 0U;
  CHECK(source.serialize_local(wire.data(), wire.size(), wire_size,
                               generation) == GraphError::none);
  CHECK(wire_size > 24U);
  CHECK(generation != 0U);

  GraphCache destination;
  GraphError error = GraphError::none;
  CHECK(destination.apply_remote(kRemote, wire.data(), wire_size,
                                 generation + 1U, error) ==
        GraphUpdate::rejected);
  CHECK(error == GraphError::malformed_snapshot);
  CHECK(destination.apply_remote(kRemote, wire.data(), wire_size, generation,
                                 error) ==
        GraphUpdate::changed);
  CHECK(error == GraphError::none);
  GraphSnapshot snapshot{};
  CHECK(destination.snapshot(snapshot) == GraphError::none);
  CHECK(snapshot.node_count == 1U);
  CHECK(snapshot.endpoint_count == 2U);
  CHECK(!snapshot.nodes[0].local);
  CHECK(snapshot.nodes[0].participant_prefix.value == kRemote.value);

  std::array<std::uint8_t, openrtdds::rmw::graph_wire_capacity>
      destination_wire{};
  std::size_t destination_wire_size = 0U;
  std::uint64_t destination_generation = 0U;
  CHECK(destination.serialize_local(
            destination_wire.data(), destination_wire.size(),
            destination_wire_size, destination_generation) ==
        GraphError::none);
  CHECK(destination_generation == 1U);

  CHECK(destination.apply_remote(kRemote, wire.data(), wire_size, generation,
                                 error) ==
        GraphUpdate::unchanged);
  CHECK(error == GraphError::stale_snapshot);
  CHECK(destination.remove_remote_participant(kRemote) ==
        GraphUpdate::changed);
  CHECK(destination.snapshot(snapshot) == GraphError::none);
  CHECK(snapshot.node_count == 0U);
  CHECK(snapshot.endpoint_count == 0U);
}

void test_malformed_snapshot_is_atomic() {
  GraphCache source;
  CHECK(source.add_local_node(kLocal, 1U, "node", "/", "/") ==
        GraphError::none);
  std::array<std::uint8_t, openrtdds::rmw::graph_wire_capacity> wire{};
  std::size_t wire_size = 0U;
  std::uint64_t generation = 0U;
  CHECK(source.serialize_local(wire.data(), wire.size(), wire_size,
                               generation) == GraphError::none);

  GraphCache destination;
  GraphError error = GraphError::none;
  CHECK(destination.apply_remote(kRemote, wire.data(), wire_size - 1U,
                                 generation, error) ==
        GraphUpdate::rejected);
  CHECK(error == GraphError::malformed_snapshot);
  GraphSnapshot snapshot{};
  CHECK(destination.snapshot(snapshot) == GraphError::none);
  CHECK(snapshot.node_count == 0U);

  wire[4] = 'X';
  CHECK(destination.apply_remote(kRemote, wire.data(), wire_size, generation,
                                 error) ==
        GraphUpdate::rejected);
  CHECK(destination.snapshot(snapshot) == GraphError::none);
  CHECK(snapshot.node_count == 0U);
}

void test_error_names() {
  CHECK(std::strcmp(openrtdds::rmw::to_string(GraphError::none), "none") ==
        0);
  CHECK(std::strcmp(openrtdds::rmw::to_string(GraphError::malformed_snapshot),
                    "malformed_snapshot") == 0);
  CHECK(std::strcmp(openrtdds::rmw::to_string(
                        static_cast<GraphError>(255U)),
                    "unknown") == 0);
}

}  // namespace

void test_rmw_graph_cache() {
  test_local_lifecycle_and_snapshot();
  test_wire_round_trip_and_expiry();
  test_malformed_snapshot_is_atomic();
  test_error_names();
}
