#pragma once

#include <cstddef>
#include <cstdint>

#include "rmw/rmw.h"
#include "rmw_openrtdds_cpp/qos.hpp"
#include "rmw_openrtdds_cpp/type_support.hpp"
#include "rmw_openrtdds_cpp/visibility_control.h"

namespace rmw_openrtdds_cpp {

// Requirements: ORT-RMW-045, ORT-RMW-046
struct PublisherSampleMetadata final {
  std::uint64_t sequence_number{0U};
  std::uint64_t source_timestamp_ns{0U};
  std::size_t serialized_size{0U};
};

// Read-only diagnostic view used by qualification tests and the example.
// Packet pointers remain owned by the publisher and are valid only until the
// next publish call or publisher destruction.
struct PublisherSnapshot final {
  rmw_gid_t gid{};
  DdsNames names{};
  rmw_qos_profile_t actual_qos{};
  std::size_t maximum_serialized_sample_size{0U};
  std::size_t history_size{0U};
  std::size_t history_capacity{0U};
  std::uint64_t next_sequence_number{0U};
  PublisherSampleMetadata last_sample{};
  const std::uint8_t* sedp_announcement{nullptr};
  std::size_t sedp_announcement_size{0U};
  const std::uint8_t* last_data_message{nullptr};
  std::size_t last_data_message_size{0U};
};

// Captures one internally consistent metadata snapshot. This helper is not an
// rmw entry point and does not transfer ownership of packet storage.
[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC bool inspect_publisher(
    const rmw_publisher_t* publisher, PublisherSnapshot& snapshot) noexcept;

}  // namespace rmw_openrtdds_cpp
