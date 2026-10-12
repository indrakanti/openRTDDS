#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "rmw_openrtdds_cpp/xcdr1_codec.hpp"
#include "rosidl_runtime_c/string.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"

// Verifies: ORT-RMW-042
namespace {

using rmw_openrtdds_cpp::CodecByteOrder;
using rmw_openrtdds_cpp::CodecError;
using rmw_openrtdds_cpp::CodecResult;
using rmw_openrtdds_cpp::TypeSupportError;

struct CChild final {
  std::uint32_t value;
};

struct CInt16Sequence final {
  std::int16_t* data;
  std::size_t size;
  std::size_t capacity;
};

struct CMessage final {
  std::uint32_t count;
  double stamp;
  rosidl_runtime_c__String frame;
  std::uint8_t flags[3];
  CInt16Sequence samples;
  CChild child;
};

[[nodiscard]] std::size_t flags_size(const void*) { return 3U; }
[[nodiscard]] const void* flags_const(const void* field,
                                      const std::size_t index) {
  return &static_cast<const std::uint8_t*>(field)[index];
}
[[nodiscard]] void* flags_mutable(void* field, const std::size_t index) {
  return &static_cast<std::uint8_t*>(field)[index];
}
[[nodiscard]] std::size_t sequence_size(const void* field) {
  return static_cast<const CInt16Sequence*>(field)->size;
}
[[nodiscard]] const void* sequence_const(const void* field,
                                         const std::size_t index) {
  return &static_cast<const CInt16Sequence*>(field)->data[index];
}
[[nodiscard]] void* sequence_mutable(void* field,
                                     const std::size_t index) {
  return &static_cast<CInt16Sequence*>(field)->data[index];
}

struct CFixture final {
  rosidl_typesupport_introspection_c__MessageMember child_field{};
  rosidl_typesupport_introspection_c__MessageMembers child_members{};
  rosidl_message_type_support_t child_handle{};
  std::array<rosidl_typesupport_introspection_c__MessageMember, 6U> fields{};
  rosidl_typesupport_introspection_c__MessageMembers members{};
  rosidl_message_type_support_t handle{};

  CFixture() {
    child_field.name_ = "value";
    child_field.type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_UINT32;
    child_field.offset_ = offsetof(CChild, value);
    child_members.message_namespace_ = "codec_msgs__msg";
    child_members.message_name_ = "Child";
    child_members.member_count_ = 1U;
    child_members.size_of_ = sizeof(CChild);
    child_members.members_ = &child_field;
    child_handle.typesupport_identifier =
        rosidl_typesupport_introspection_c__identifier;
    child_handle.data = &child_members;

    fields[0].name_ = "count";
    fields[0].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_UINT32;
    fields[0].offset_ = offsetof(CMessage, count);
    fields[1].name_ = "stamp";
    fields[1].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE;
    fields[1].offset_ = offsetof(CMessage, stamp);
    fields[2].name_ = "frame";
    fields[2].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_STRING;
    fields[2].string_upper_bound_ = 5U;
    fields[2].offset_ = offsetof(CMessage, frame);
    fields[3].name_ = "flags";
    fields[3].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_UINT8;
    fields[3].is_array_ = true;
    fields[3].array_size_ = 3U;
    fields[3].offset_ = offsetof(CMessage, flags);
    fields[3].size_function = flags_size;
    fields[3].get_const_function = flags_const;
    fields[3].get_function = flags_mutable;
    fields[4].name_ = "samples";
    fields[4].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_INT16;
    fields[4].is_array_ = true;
    fields[4].array_size_ = 2U;
    fields[4].is_upper_bound_ = true;
    fields[4].offset_ = offsetof(CMessage, samples);
    fields[4].size_function = sequence_size;
    fields[4].get_const_function = sequence_const;
    fields[4].get_function = sequence_mutable;
    fields[5].name_ = "child";
    fields[5].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE;
    fields[5].members_ = &child_handle;
    fields[5].offset_ = offsetof(CMessage, child);

    members.message_namespace_ = "codec_msgs__msg";
    members.message_name_ = "Bounded";
    members.member_count_ = static_cast<std::uint32_t>(fields.size());
    members.size_of_ = sizeof(CMessage);
    members.members_ = fields.data();
    handle.typesupport_identifier =
        rosidl_typesupport_introspection_c__identifier;
    handle.data = &members;
  }
};

struct CMessageStorage final {
  std::array<char, 6U> frame{};
  std::array<std::int16_t, 2U> samples{};
  CMessage message{};

  CMessageStorage() {
    message.frame.data = frame.data();
    message.frame.capacity = frame.size();
    message.samples.data = samples.data();
    message.samples.size = samples.size();
    message.samples.capacity = samples.size();
  }
};

struct CppMessage final {
  std::uint32_t count{0U};
  std::string frame{};
  std::vector<bool> enabled{};
};

[[nodiscard]] std::size_t enabled_size(const void* field) {
  return static_cast<const std::vector<bool>*>(field)->size();
}
void enabled_fetch(const void* field, const std::size_t index, void* value) {
  *static_cast<bool*>(value) =
      (*static_cast<const std::vector<bool>*>(field))[index];
}
void enabled_assign(void* field, const std::size_t index, const void* value) {
  (*static_cast<std::vector<bool>*>(field))[index] =
      *static_cast<const bool*>(value);
}

struct CppFixture final {
  std::array<rosidl_typesupport_introspection_cpp::MessageMember, 3U> fields{};
  rosidl_typesupport_introspection_cpp::MessageMembers members{};
  rosidl_message_type_support_t handle{};

  explicit CppFixture(CppMessage& sample) {
    const auto* base = reinterpret_cast<const std::uint8_t*>(&sample);
    fields[0].name_ = "count";
    fields[0].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_UINT32;
    fields[0].offset_ = static_cast<std::uint32_t>(
        reinterpret_cast<const std::uint8_t*>(&sample.count) - base);
    fields[1].name_ = "frame";
    fields[1].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_STRING;
    fields[1].string_upper_bound_ = 8U;
    fields[1].offset_ = static_cast<std::uint32_t>(
        reinterpret_cast<const std::uint8_t*>(&sample.frame) - base);
    fields[2].name_ = "enabled";
    fields[2].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN;
    fields[2].is_array_ = true;
    fields[2].array_size_ = 3U;
    fields[2].is_upper_bound_ = true;
    fields[2].offset_ = static_cast<std::uint32_t>(
        reinterpret_cast<const std::uint8_t*>(&sample.enabled) - base);
    fields[2].size_function = enabled_size;
    fields[2].fetch_function = enabled_fetch;
    fields[2].assign_function = enabled_assign;

    members.message_namespace_ = "codec_msgs::msg";
    members.message_name_ = "CppBounded";
    members.member_count_ = static_cast<std::uint32_t>(fields.size());
    members.size_of_ = sizeof(CppMessage);
    members.members_ = fields.data();
    handle.typesupport_identifier =
        rosidl_typesupport_introspection_cpp::typesupport_identifier;
    handle.data = &members;
  }
};

[[nodiscard]] int test_c_round_trip(const CodecByteOrder byte_order) {
  CFixture fixture{};
  CMessageStorage source{};
  source.message.count = 7U;
  source.message.stamp = 12.5;
  std::memcpy(source.frame.data(), "map", 4U);
  source.message.frame.size = 3U;
  source.message.flags[0] = 1U;
  source.message.flags[1] = 2U;
  source.message.flags[2] = 3U;
  source.samples = {{-2, 9}};
  source.message.child.value = 42U;

  std::array<std::uint8_t, 48U> buffer{};
  CodecResult result{};
  if (!rmw_openrtdds_cpp::serialize_xcdr1(
          &fixture.handle, &source.message, buffer.data(), buffer.size(),
          byte_order, result) ||
      (result.error != CodecError::none) ||
      (result.bytes_processed != 44U)) {
    return 1;
  }

  CMessageStorage destination{};
  destination.message.count = 999U;
  if (!rmw_openrtdds_cpp::deserialize_xcdr1(
          &fixture.handle, buffer.data(), result.bytes_processed,
          buffer.size(), &destination.message, result) ||
      (destination.message.count != 7U) ||
      (destination.message.stamp != 12.5) ||
      (destination.message.frame.size != 3U) ||
      (std::strcmp(destination.message.frame.data, "map") != 0) ||
      (destination.message.flags[0] != 1U) ||
      (destination.message.flags[2] != 3U) ||
      (destination.samples[0] != -2) || (destination.samples[1] != 9) ||
      (destination.message.child.value != 42U)) {
    return 2;
  }
  return 0;
}

[[nodiscard]] int test_cpp_vector_bool_round_trip() {
  CppMessage source{};
  source.count = 11U;
  source.frame = "base";
  source.enabled = {true, false, true};
  CppFixture fixture{source};
  std::array<std::uint8_t, 64U> buffer{};
  CodecResult result{};
  if (!rmw_openrtdds_cpp::serialize_xcdr1(
          &fixture.handle, &source, buffer.data(), buffer.size(),
          CodecByteOrder::little_endian, result)) {
    return 10;
  }

  CppMessage destination{};
  destination.frame.reserve(8U);
  destination.enabled.resize(3U);
  if (!rmw_openrtdds_cpp::deserialize_xcdr1(
          &fixture.handle, buffer.data(), result.bytes_processed,
          buffer.size(), &destination, result) ||
      (destination.count != source.count) ||
      (destination.frame != source.frame) ||
      (destination.enabled != source.enabled)) {
    return 11;
  }

  destination.count = 888U;
  buffer[result.bytes_processed - 1U] = 2U;
  if (rmw_openrtdds_cpp::deserialize_xcdr1(
          &fixture.handle, buffer.data(), result.bytes_processed,
          buffer.size(), &destination, result) ||
      (result.error != CodecError::invalid_wire_data) ||
      (destination.count != 888U)) {
    return 12;
  }
  return 0;
}

[[nodiscard]] int test_atomic_rejections() {
  CFixture fixture{};
  CMessageStorage source{};
  source.message.count = 3U;
  std::memcpy(source.frame.data(), "odom", 5U);
  source.message.frame.size = 4U;
  source.samples = {{4, 5}};
  std::array<std::uint8_t, 48U> buffer{};
  CodecResult result{};
  if (!rmw_openrtdds_cpp::serialize_xcdr1(
          &fixture.handle, &source.message, buffer.data(), buffer.size(),
          CodecByteOrder::little_endian, result)) {
    return 20;
  }
  const std::size_t encoded_size = result.bytes_processed;

  CMessageStorage destination{};
  destination.message.count = 777U;
  destination.message.samples.size = 1U;
  if (rmw_openrtdds_cpp::deserialize_xcdr1(
          &fixture.handle, buffer.data(), encoded_size, buffer.size(),
          &destination.message, result) ||
      (result.error != CodecError::destination_size_mismatch) ||
      (destination.message.count != 777U)) {
    return 21;
  }

  destination.message.samples.size = 2U;
  destination.message.frame.capacity = 2U;
  if (rmw_openrtdds_cpp::deserialize_xcdr1(
          &fixture.handle, buffer.data(), encoded_size, buffer.size(),
          &destination.message, result) ||
      (result.error != CodecError::invalid_message_storage) ||
      (destination.message.count != 777U)) {
    return 22;
  }
  destination.message.frame.capacity = destination.frame.size();

  if (rmw_openrtdds_cpp::deserialize_xcdr1(
          &fixture.handle, buffer.data(), encoded_size - 1U, buffer.size(),
          &destination.message, result) ||
      (result.error != CodecError::input_truncated) ||
      (destination.message.count != 777U)) {
    return 23;
  }

  buffer[encoded_size] = 0xAAU;
  if (rmw_openrtdds_cpp::deserialize_xcdr1(
          &fixture.handle, buffer.data(), encoded_size + 1U, buffer.size(),
          &destination.message, result) ||
      (result.error != CodecError::trailing_data) ||
      (destination.message.count != 777U)) {
    return 24;
  }

  CMessageStorage over_bound{};
  std::memcpy(over_bound.frame.data(), "12345", 6U);
  over_bound.message.frame.size = 6U;
  if (rmw_openrtdds_cpp::serialize_xcdr1(
          &fixture.handle, &over_bound.message, buffer.data(), buffer.size(),
          CodecByteOrder::little_endian, result) ||
      (result.error != CodecError::invalid_message_storage)) {
    return 25;
  }

  CMessageStorage excessive_sequence{};
  std::array<std::int16_t, 3U> excessive_values{{1, 2, 3}};
  excessive_sequence.message.samples.data = excessive_values.data();
  excessive_sequence.message.samples.size = excessive_values.size();
  excessive_sequence.message.samples.capacity = excessive_values.size();
  if (rmw_openrtdds_cpp::serialize_xcdr1(
          &fixture.handle, &excessive_sequence.message, buffer.data(),
          buffer.size(), CodecByteOrder::little_endian, result) ||
      (result.error != CodecError::field_bound_exceeded)) {
    return 26;
  }

  if (rmw_openrtdds_cpp::serialize_xcdr1(
          &fixture.handle, &source.message, buffer.data(), buffer.size() - 1U,
          CodecByteOrder::little_endian, result) ||
      (result.error != CodecError::type_support_rejected) ||
      (result.type_support_error !=
       TypeSupportError::sample_limit_exceeded)) {
    return 27;
  }
  return 0;
}

}  // namespace

int main() {
  int result = test_c_round_trip(CodecByteOrder::little_endian);
  if (result != 0) {
    return result;
  }
  result = test_c_round_trip(CodecByteOrder::big_endian);
  if (result != 0) {
    return result;
  }
  result = test_cpp_vector_bool_round_trip();
  if (result != 0) {
    return result;
  }
  return test_atomic_rejections();
}
