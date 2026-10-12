#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "rmw_openrtdds_cpp/visibility_control.h"
#include "rosidl_runtime_c/message_type_support_struct.h"

namespace rmw_openrtdds_cpp {

// Requirements: ORT-RMW-040, ORT-RMW-041
constexpr std::size_t kDdsNameCapacity = 256U;
constexpr std::size_t kMaximumTypeNestingDepth = 16U;
constexpr std::size_t kXcdr1EncapsulationSize = 4U;

enum class TypeSupportLanguage : std::uint8_t {
  introspection_c = 0,
  introspection_cpp,
};

enum class TypeSupportError : std::uint8_t {
  none = 0,
  invalid_argument,
  unsupported_identifier,
  invalid_members,
  unsupported_field,
  unbounded_type,
  recursive_type,
  nesting_limit_exceeded,
  arithmetic_overflow,
  sample_limit_exceeded,
  invalid_name,
  name_limit_exceeded,
};

enum class RosChannel : std::uint8_t {
  topic = 0,
  service_request,
  service_response,
};

struct TypeSupportInfo final {
  TypeSupportLanguage language{TypeSupportLanguage::introspection_c};
  std::size_t maximum_serialized_size{0U};
  std::size_t maximum_alignment{1U};
  std::size_t maximum_nesting_depth{0U};
  std::array<char, kDdsNameCapacity> dds_type_name{};
};

struct DdsNames final {
  std::array<char, kDdsNameCapacity> topic_name{};
  std::array<char, kDdsNameCapacity> type_name{};
};

[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC const char* to_string(
    TypeSupportError error) noexcept;

// Computes the maximum XCDR1 PLAIN_CDR wire size, including the four-byte
// encapsulation header. The initial profile accepts bounded strings,
// fixed arrays, bounded sequences, supported scalar kinds, and nested messages.
// It never allocates and leaves output unchanged on failure.
[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC bool analyze_type_support(
    const rosidl_message_type_support_t* type_support,
    std::size_t maximum_sample_size, TypeSupportInfo& output,
    TypeSupportError& error) noexcept;

// Applies the pinned Jazzy ROS-over-DDS convention. The topic output is
// prefixed with rt/rq/rr unless literal DDS naming is requested; service
// channels retain the Request/Reply suffix in either mode. Type identity is
// derived from the introspection namespace and message name. No allocation
// occurs and output is unchanged on failure.
[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC bool map_ros_to_dds_names(
    const rosidl_message_type_support_t* type_support, RosChannel channel,
    const char* ros_name, bool avoid_ros_namespace_conventions,
    DdsNames& output, TypeSupportError& error) noexcept;

}  // namespace rmw_openrtdds_cpp
