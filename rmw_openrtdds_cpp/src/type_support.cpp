#include "rmw_openrtdds_cpp/type_support.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"

namespace rmw_openrtdds_cpp {
// Requirements: ORT-RMW-040, ORT-RMW-041
namespace {

struct AnalysisState final {
  std::size_t offset{0U};
  std::size_t maximum_alignment{1U};
  std::size_t maximum_depth{0U};
  std::size_t body_limit{0U};
  std::array<const void*, kMaximumTypeNestingDepth> stack{};
  TypeSupportError error{TypeSupportError::none};
};

[[nodiscard]] bool fail(AnalysisState& state,
                        const TypeSupportError error) noexcept {
  state.error = error;
  return false;
}

[[nodiscard]] bool append_aligned(AnalysisState& state,
                                  const std::size_t alignment,
                                  const std::size_t width) noexcept {
  if ((alignment == 0U) || ((alignment & (alignment - 1U)) != 0U)) {
    return fail(state, TypeSupportError::invalid_members);
  }
  const std::size_t remainder = state.offset % alignment;
  const std::size_t padding =
      remainder == 0U ? 0U : alignment - remainder;
  if ((padding > std::numeric_limits<std::size_t>::max() - state.offset) ||
      (width > std::numeric_limits<std::size_t>::max() -
                   (state.offset + padding))) {
    return fail(state, TypeSupportError::arithmetic_overflow);
  }
  const std::size_t next = state.offset + padding + width;
  if (next > state.body_limit) {
    return fail(state, TypeSupportError::sample_limit_exceeded);
  }
  state.offset = next;
  if (alignment > state.maximum_alignment) {
    state.maximum_alignment = alignment;
  }
  return true;
}

[[nodiscard]] bool scalar_layout(const std::uint8_t type_id,
                                 std::size_t& alignment,
                                 std::size_t& width) noexcept {
  switch (type_id) {
    case rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN:
    case rosidl_typesupport_introspection_c__ROS_TYPE_CHAR:
    case rosidl_typesupport_introspection_c__ROS_TYPE_OCTET:
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
      alignment = 1U;
      width = 1U;
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
      alignment = 2U;
      width = 2U;
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
      alignment = 4U;
      width = 4U;
      return true;
    case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE:
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
      alignment = 8U;
      width = 8U;
      return true;
    default:
      return false;
  }
}

[[nodiscard]] bool identifier_language(
    const char* identifier, TypeSupportLanguage& language) noexcept {
  if (identifier == nullptr) {
    return false;
  }
  if (std::strcmp(identifier,
                  rosidl_typesupport_introspection_c__identifier) == 0) {
    language = TypeSupportLanguage::introspection_c;
    return true;
  }
  if (std::strcmp(identifier,
                  rosidl_typesupport_introspection_cpp::
                      typesupport_identifier) == 0) {
    language = TypeSupportLanguage::introspection_cpp;
    return true;
  }
  return false;
}

template <typename Members>
[[nodiscard]] bool analyze_members(const Members* members,
                                   TypeSupportLanguage language,
                                   std::size_t depth,
                                   AnalysisState& state) noexcept;

template <typename Member>
[[nodiscard]] bool analyze_element(const Member& member,
                                   const TypeSupportLanguage language,
                                   const std::size_t depth,
                                   AnalysisState& state) noexcept {
  std::size_t alignment = 0U;
  std::size_t width = 0U;
  if (scalar_layout(member.type_id_, alignment, width)) {
    return append_aligned(state, alignment, width);
  }

  if (member.type_id_ ==
      rosidl_typesupport_introspection_c__ROS_TYPE_STRING) {
    if (member.string_upper_bound_ == 0U) {
      return fail(state, TypeSupportError::unbounded_type);
    }
    if (member.string_upper_bound_ ==
        std::numeric_limits<std::size_t>::max()) {
      return fail(state, TypeSupportError::arithmetic_overflow);
    }
    const std::size_t wire_characters = member.string_upper_bound_ + 1U;
    if (!append_aligned(state, 4U, 4U)) {
      return false;
    }
    return append_aligned(state, 1U, wire_characters);
  }

  if (member.type_id_ ==
      rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
    if (member.members_ == nullptr) {
      return fail(state, TypeSupportError::invalid_members);
    }
    TypeSupportLanguage nested_language{};
    if (!identifier_language(member.members_->typesupport_identifier,
                             nested_language) ||
        (nested_language != language) || (member.members_->data == nullptr)) {
      return fail(state, TypeSupportError::unsupported_identifier);
    }
    if (language == TypeSupportLanguage::introspection_c) {
      return analyze_members(
          static_cast<const
              rosidl_typesupport_introspection_c__MessageMembers*>(
                  member.members_->data),
          language, depth + 1U, state);
    }
    return analyze_members(
        static_cast<const
            rosidl_typesupport_introspection_cpp::MessageMembers*>(
                member.members_->data),
        language, depth + 1U, state);
  }

  return fail(state, TypeSupportError::unsupported_field);
}

template <typename Member>
[[nodiscard]] bool analyze_member(const Member& member,
                                  const TypeSupportLanguage language,
                                  const std::size_t depth,
                                  AnalysisState& state) noexcept {
  if (member.name_ == nullptr) {
    return fail(state, TypeSupportError::invalid_members);
  }
  std::size_t count = 1U;
  if (member.is_array_) {
    if (member.is_upper_bound_) {
      if (member.array_size_ == 0U) {
        return fail(state, TypeSupportError::invalid_members);
      }
      if (!append_aligned(state, 4U, 4U)) {
        return false;
      }
      count = member.array_size_;
    } else if (member.array_size_ == 0U) {
      return fail(state, TypeSupportError::unbounded_type);
    } else {
      count = member.array_size_;
    }
  }

  for (std::size_t index = 0U; index < count; ++index) {
    if (!analyze_element(member, language, depth, state)) {
      return false;
    }
  }
  return true;
}

template <typename Members>
[[nodiscard]] bool analyze_members(const Members* members,
                                   const TypeSupportLanguage language,
                                   const std::size_t depth,
                                   AnalysisState& state) noexcept {
  if ((members == nullptr) || (members->message_name_ == nullptr) ||
      (members->message_namespace_ == nullptr) ||
      ((members->member_count_ != 0U) && (members->members_ == nullptr))) {
    return fail(state, TypeSupportError::invalid_members);
  }
  if (depth > kMaximumTypeNestingDepth) {
    return fail(state, TypeSupportError::nesting_limit_exceeded);
  }
  for (std::size_t index = 0U; index + 1U < depth; ++index) {
    if (state.stack[index] == members) {
      return fail(state, TypeSupportError::recursive_type);
    }
  }
  state.stack[depth - 1U] = members;
  if (depth > state.maximum_depth) {
    state.maximum_depth = depth;
  }
  for (std::uint32_t index = 0U; index < members->member_count_; ++index) {
    if (!analyze_member(members->members_[index], language, depth, state)) {
      return false;
    }
  }
  state.stack[depth - 1U] = nullptr;
  return true;
}

[[nodiscard]] bool append_character(char*& output, std::size_t& remaining,
                                    const char value) noexcept {
  if (remaining <= 1U) {
    return false;
  }
  *output++ = value;
  --remaining;
  *output = '\0';
  return true;
}

[[nodiscard]] bool append_text(char*& output, std::size_t& remaining,
                               const char* value,
                               const bool convert_c_namespace) noexcept {
  if (value == nullptr) {
    return false;
  }
  for (std::size_t index = 0U; value[index] != '\0'; ++index) {
    if (convert_c_namespace && (value[index] == '_') &&
        (value[index + 1U] == '_')) {
      if (!append_character(output, remaining, ':') ||
          !append_character(output, remaining, ':')) {
        return false;
      }
      ++index;
    } else if (!append_character(output, remaining, value[index])) {
      return false;
    }
  }
  return true;
}

template <typename Members>
[[nodiscard]] bool build_type_name(
    const Members* members, const bool convert_c_namespace,
    std::array<char, kDdsNameCapacity>& output) noexcept {
  if ((members == nullptr) || (members->message_namespace_ == nullptr) ||
      (members->message_name_ == nullptr) ||
      (members->message_name_[0] == '\0')) {
    return false;
  }
  std::array<char, kDdsNameCapacity> candidate{};
  char* cursor = candidate.data();
  std::size_t remaining = candidate.size();
  if (members->message_namespace_[0] != '\0') {
    if (!append_text(cursor, remaining, members->message_namespace_,
                     convert_c_namespace) ||
        !append_text(cursor, remaining, "::", false)) {
      return false;
    }
  }
  if (!append_text(cursor, remaining, "dds_::", false) ||
      !append_text(cursor, remaining, members->message_name_, false) ||
      !append_character(cursor, remaining, '_')) {
    return false;
  }
  output = candidate;
  return true;
}

[[nodiscard]] bool derive_type_name(
    const rosidl_message_type_support_t* type_support,
    TypeSupportLanguage& language,
    std::array<char, kDdsNameCapacity>& output,
    TypeSupportError& error) noexcept {
  if ((type_support == nullptr) || (type_support->data == nullptr)) {
    error = TypeSupportError::invalid_argument;
    return false;
  }
  if (!identifier_language(type_support->typesupport_identifier, language)) {
    error = TypeSupportError::unsupported_identifier;
    return false;
  }
  bool success = false;
  if (language == TypeSupportLanguage::introspection_c) {
    const auto* members = static_cast<const
        rosidl_typesupport_introspection_c__MessageMembers*>(
        type_support->data);
    if ((members->message_namespace_ == nullptr) ||
        (members->message_name_ == nullptr)) {
      error = TypeSupportError::invalid_members;
      return false;
    }
    if (members->message_name_[0] == '\0') {
      error = TypeSupportError::invalid_name;
      return false;
    }
    success = build_type_name(members, true, output);
  } else {
    const auto* members = static_cast<const
        rosidl_typesupport_introspection_cpp::MessageMembers*>(
        type_support->data);
    if ((members->message_namespace_ == nullptr) ||
        (members->message_name_ == nullptr)) {
      error = TypeSupportError::invalid_members;
      return false;
    }
    if (members->message_name_[0] == '\0') {
      error = TypeSupportError::invalid_name;
      return false;
    }
    success = build_type_name(members, false, output);
  }
  if (!success) {
    error = TypeSupportError::name_limit_exceeded;
    return false;
  }
  return true;
}

}  // namespace

const char* to_string(const TypeSupportError error) noexcept {
  switch (error) {
    case TypeSupportError::none:
      return "none";
    case TypeSupportError::invalid_argument:
      return "invalid_argument";
    case TypeSupportError::unsupported_identifier:
      return "unsupported_identifier";
    case TypeSupportError::invalid_members:
      return "invalid_members";
    case TypeSupportError::unsupported_field:
      return "unsupported_field";
    case TypeSupportError::unbounded_type:
      return "unbounded_type";
    case TypeSupportError::recursive_type:
      return "recursive_type";
    case TypeSupportError::nesting_limit_exceeded:
      return "nesting_limit_exceeded";
    case TypeSupportError::arithmetic_overflow:
      return "arithmetic_overflow";
    case TypeSupportError::sample_limit_exceeded:
      return "sample_limit_exceeded";
    case TypeSupportError::invalid_name:
      return "invalid_name";
    case TypeSupportError::name_limit_exceeded:
      return "name_limit_exceeded";
  }
  return "unknown";
}

bool analyze_type_support(
    const rosidl_message_type_support_t* type_support,
    const std::size_t maximum_sample_size, TypeSupportInfo& output,
    TypeSupportError& error) noexcept {
  if ((type_support == nullptr) || (type_support->data == nullptr) ||
      (maximum_sample_size < kXcdr1EncapsulationSize)) {
    error = TypeSupportError::invalid_argument;
    return false;
  }

  TypeSupportInfo candidate{};
  if (!derive_type_name(type_support, candidate.language,
                        candidate.dds_type_name, error)) {
    return false;
  }

  AnalysisState state{};
  state.body_limit = maximum_sample_size - kXcdr1EncapsulationSize;
  const bool success =
      candidate.language == TypeSupportLanguage::introspection_c
          ? analyze_members(
                static_cast<const
                    rosidl_typesupport_introspection_c__MessageMembers*>(
                        type_support->data),
                candidate.language, 1U, state)
          : analyze_members(
                static_cast<const
                    rosidl_typesupport_introspection_cpp::MessageMembers*>(
                        type_support->data),
                candidate.language, 1U, state);
  if (!success) {
    error = state.error;
    return false;
  }
  candidate.maximum_serialized_size =
      state.offset + kXcdr1EncapsulationSize;
  candidate.maximum_alignment = state.maximum_alignment;
  candidate.maximum_nesting_depth = state.maximum_depth;
  output = candidate;
  error = TypeSupportError::none;
  return true;
}

bool map_ros_to_dds_names(
    const rosidl_message_type_support_t* type_support,
    const RosChannel channel, const char* ros_name,
    const bool avoid_ros_namespace_conventions, DdsNames& output,
    TypeSupportError& error) noexcept {
  if ((ros_name == nullptr) || (ros_name[0] == '\0')) {
    error = TypeSupportError::invalid_name;
    return false;
  }

  const char* prefix = nullptr;
  const char* suffix = "";
  switch (channel) {
    case RosChannel::topic:
      prefix = "rt";
      break;
    case RosChannel::service_request:
      prefix = "rq";
      suffix = "Request";
      break;
    case RosChannel::service_response:
      prefix = "rr";
      suffix = "Reply";
      break;
    default:
      error = TypeSupportError::invalid_argument;
      return false;
  }

  DdsNames candidate{};
  TypeSupportLanguage language{};
  if (!derive_type_name(type_support, language, candidate.type_name, error)) {
    return false;
  }
  static_cast<void>(language);

  char* cursor = candidate.topic_name.data();
  std::size_t remaining = candidate.topic_name.size();
  if ((!avoid_ros_namespace_conventions &&
       !append_text(cursor, remaining, prefix, false)) ||
      !append_text(cursor, remaining, ros_name, false) ||
      !append_text(cursor, remaining, suffix, false)) {
    error = TypeSupportError::name_limit_exceeded;
    return false;
  }

  output = candidate;
  error = TypeSupportError::none;
  return true;
}

}  // namespace rmw_openrtdds_cpp
