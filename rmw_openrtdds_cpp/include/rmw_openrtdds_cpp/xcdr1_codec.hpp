#pragma once

#include <cstddef>
#include <cstdint>

#include "rmw_openrtdds_cpp/type_support.hpp"
#include "rmw_openrtdds_cpp/visibility_control.h"
#include "rosidl_runtime_c/message_type_support_struct.h"

namespace rmw_openrtdds_cpp {

// Requirements: ORT-RMW-042
enum class CodecByteOrder : std::uint8_t {
  big_endian = 0,
  little_endian,
};

enum class CodecError : std::uint8_t {
  none = 0,
  invalid_argument,
  type_support_rejected,
  invalid_message_storage,
  callback_missing,
  field_bound_exceeded,
  destination_size_mismatch,
  output_too_small,
  input_truncated,
  unsupported_encoding,
  invalid_wire_data,
  trailing_data,
  cdr_failure,
};

struct CodecResult final {
  CodecError error{CodecError::none};
  TypeSupportError type_support_error{TypeSupportError::none};
  std::size_t bytes_processed{0U};
};

[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC const char* to_string(
    CodecError error) noexcept;

// Serializes one direct Jazzy C/C++ introspection message. The buffer must be
// large enough for the schema's analyzed maximum, not merely the current
// sample. No allocation or schema mutation occurs. The result is replaced on
// every call; the buffer may contain a partial payload after failure.
[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC bool serialize_xcdr1(
    const rosidl_message_type_support_t* type_support,
    const void* ros_message, std::uint8_t* buffer, std::size_t capacity,
    CodecByteOrder byte_order, CodecResult& result) noexcept;

// Deserializes one exact XCDR1 payload into pre-sized caller-owned storage.
// Bounded sequences must already have the encoded length, and C/C++ strings
// must have sufficient existing capacity. A validation pass checks the full
// payload and destination bounds before the apply pass, so malformed input or
// insufficient storage does not partially update the message. No resize
// callback or heap allocation is used.
[[nodiscard]] RMW_OPENRTDDS_CPP_PUBLIC bool deserialize_xcdr1(
    const rosidl_message_type_support_t* type_support,
    const std::uint8_t* buffer, std::size_t size,
    std::size_t maximum_sample_size, void* ros_message,
    CodecResult& result) noexcept;

}  // namespace rmw_openrtdds_cpp
