#include "rmw_openrtdds_cpp/xcdr1_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>

#include "openrtdds/serialization/cdr.hpp"
#include "rosidl_runtime_c/string.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"

namespace rmw_openrtdds_cpp {
// Requirements: ORT-RMW-042
namespace {

using openrtdds::serialization::ByteOrder;
using openrtdds::serialization::CdrError;
using openrtdds::serialization::CdrReader;
using openrtdds::serialization::CdrWriter;
using CMembers = rosidl_typesupport_introspection_c__MessageMembers;
using CMember = rosidl_typesupport_introspection_c__MessageMember;
using CppMembers = rosidl_typesupport_introspection_cpp::MessageMembers;
using CppMember = rosidl_typesupport_introspection_cpp::MessageMember;

[[nodiscard]] bool fail(CodecResult& result, const CodecError error,
                        const std::size_t bytes = 0U) noexcept {
  result.error = error;
  result.bytes_processed = bytes;
  return false;
}

[[nodiscard]] CodecError map_cdr_error(const CdrError error) noexcept {
  switch (error) {
    case CdrError::overflow:
      return CodecError::output_too_small;
    case CdrError::underflow:
      return CodecError::input_truncated;
    case CdrError::unsupported_encoding:
      return CodecError::unsupported_encoding;
    case CdrError::invalid_data:
      return CodecError::invalid_wire_data;
    case CdrError::bound_exceeded:
      return CodecError::field_bound_exceeded;
    case CdrError::invalid_argument:
    case CdrError::not_initialized:
    case CdrError::none:
      return CodecError::cdr_failure;
  }
  return CodecError::cdr_failure;
}

[[nodiscard]] bool writer_failure(CdrWriter& writer,
                                  CodecResult& result) noexcept {
  return fail(result, map_cdr_error(writer.error()), writer.size());
}

[[nodiscard]] bool reader_failure(CdrReader& reader,
                                  CodecResult& result) noexcept {
  return fail(result, map_cdr_error(reader.error()), reader.position());
}

template <typename T>
[[nodiscard]] T load_value(const void* const source) noexcept {
  T value{};
  std::memcpy(&value, source, sizeof(value));
  return value;
}

template <typename T>
void store_value(void* const destination, const T& value) noexcept {
  std::memcpy(destination, &value, sizeof(value));
}

[[nodiscard]] bool write_scalar(const std::uint8_t type_id,
                                const void* const value,
                                CdrWriter& writer,
                                CodecResult& result) noexcept {
  bool success = false;
  switch (type_id) {
    case rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN:
      success = writer.write_bool(load_value<bool>(value));
      break;
    case rosidl_typesupport_introspection_c__ROS_TYPE_CHAR:
    case rosidl_typesupport_introspection_c__ROS_TYPE_OCTET:
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT8:
      success = writer.write_uint8(load_value<std::uint8_t>(value));
      break;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16:
      success = writer.write_uint16(load_value<std::uint16_t>(value));
      break;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT16:
      success = writer.write_int16(load_value<std::int16_t>(value));
      break;
    case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT:
      success = writer.write_float32(load_value<float>(value));
      break;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32:
      success = writer.write_uint32(load_value<std::uint32_t>(value));
      break;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT32:
      success = writer.write_int32(load_value<std::int32_t>(value));
      break;
    case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE:
      success = writer.write_float64(load_value<double>(value));
      break;
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64:
      success = writer.write_uint64(load_value<std::uint64_t>(value));
      break;
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT64:
      success = writer.write_int64(load_value<std::int64_t>(value));
      break;
    default:
      return fail(result, CodecError::type_support_rejected, writer.size());
  }
  return success ? true : writer_failure(writer, result);
}

[[nodiscard]] bool read_scalar(const std::uint8_t type_id,
                               CdrReader& reader, void* const destination,
                               const bool apply,
                               CodecResult& result) noexcept {
  bool success = false;
  switch (type_id) {
    case rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN: {
      bool value = false;
      success = reader.read_bool(value);
      if (success && apply) {
        store_value(destination, value);
      }
      break;
    }
    case rosidl_typesupport_introspection_c__ROS_TYPE_CHAR:
    case rosidl_typesupport_introspection_c__ROS_TYPE_OCTET:
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT8:
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT8: {
      std::uint8_t value = 0U;
      success = reader.read_uint8(value);
      if (success && apply) {
        store_value(destination, value);
      }
      break;
    }
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT16: {
      std::uint16_t value = 0U;
      success = reader.read_uint16(value);
      if (success && apply) {
        store_value(destination, value);
      }
      break;
    }
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT16: {
      std::int16_t value = 0;
      success = reader.read_int16(value);
      if (success && apply) {
        store_value(destination, value);
      }
      break;
    }
    case rosidl_typesupport_introspection_c__ROS_TYPE_FLOAT: {
      float value = 0.0F;
      success = reader.read_float32(value);
      if (success && apply) {
        store_value(destination, value);
      }
      break;
    }
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT32: {
      std::uint32_t value = 0U;
      success = reader.read_uint32(value);
      if (success && apply) {
        store_value(destination, value);
      }
      break;
    }
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT32: {
      std::int32_t value = 0;
      success = reader.read_int32(value);
      if (success && apply) {
        store_value(destination, value);
      }
      break;
    }
    case rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE: {
      double value = 0.0;
      success = reader.read_float64(value);
      if (success && apply) {
        store_value(destination, value);
      }
      break;
    }
    case rosidl_typesupport_introspection_c__ROS_TYPE_UINT64: {
      std::uint64_t value = 0U;
      success = reader.read_uint64(value);
      if (success && apply) {
        store_value(destination, value);
      }
      break;
    }
    case rosidl_typesupport_introspection_c__ROS_TYPE_INT64: {
      std::int64_t value = 0;
      success = reader.read_int64(value);
      if (success && apply) {
        store_value(destination, value);
      }
      break;
    }
    default:
      return fail(result, CodecError::type_support_rejected,
                  reader.position());
  }
  return success ? true : reader_failure(reader, result);
}

template <typename Members>
[[nodiscard]] const Members* nested_members(
    const typename std::conditional_t<std::is_same_v<Members, CMembers>,
                                      CMember, CppMember>& member) noexcept {
  return static_cast<const Members*>(member.members_->data);
}

template <typename Members>
[[nodiscard]] bool serialize_members(const Members* members,
                                     const void* message,
                                     CdrWriter& writer,
                                     CodecResult& result) noexcept;

template <typename Members, typename Member>
[[nodiscard]] bool serialize_value(const Member& member,
                                   const void* value, CdrWriter& writer,
                                   CodecResult& result) noexcept {
  if (member.type_id_ ==
      rosidl_typesupport_introspection_c__ROS_TYPE_STRING) {
    const char* data = nullptr;
    std::size_t length = 0U;
    if constexpr (std::is_same_v<Members, CMembers>) {
      const auto* string = static_cast<const rosidl_runtime_c__String*>(value);
      data = string->data;
      length = string->size;
      if (((data == nullptr) && (length != 0U)) ||
          ((data != nullptr) && (string->capacity <= length)) ||
          ((data != nullptr) && (data[length] != '\0'))) {
        return fail(result, CodecError::invalid_message_storage,
                    writer.size());
      }
    } else {
      const auto* string = static_cast<const std::string*>(value);
      data = string->data();
      length = string->size();
    }
    if (length > member.string_upper_bound_) {
      return fail(result, CodecError::field_bound_exceeded, writer.size());
    }
    if (!writer.write_string(data, length, member.string_upper_bound_)) {
      return writer_failure(writer, result);
    }
    return true;
  }

  if (member.type_id_ ==
      rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
    return serialize_members(nested_members<Members>(member), value, writer,
                             result);
  }
  return write_scalar(member.type_id_, value, writer, result);
}

template <typename Members>
[[nodiscard]] bool serialize_members(const Members* const members,
                                     const void* const message,
                                     CdrWriter& writer,
                                     CodecResult& result) noexcept {
  for (std::uint32_t index = 0U; index < members->member_count_; ++index) {
    const auto& member = members->members_[index];
    const auto* field = static_cast<const std::uint8_t*>(message) +
                        member.offset_;
    if (!member.is_array_) {
      if (!serialize_value<Members>(member, field, writer, result)) {
        return false;
      }
      continue;
    }
    if ((member.size_function == nullptr) ||
        ((member.get_const_function == nullptr) &&
         ((member.type_id_ !=
           rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN) ||
          (member.fetch_function == nullptr)))) {
      return fail(result, CodecError::callback_missing, writer.size());
    }
    const std::size_t count = member.size_function(field);
    if ((member.is_upper_bound_ && (count > member.array_size_)) ||
        (!member.is_upper_bound_ && (count != member.array_size_)) ||
        (count > std::numeric_limits<std::uint32_t>::max())) {
      return fail(result, CodecError::field_bound_exceeded, writer.size());
    }
    if (member.is_upper_bound_ &&
        !writer.write_uint32(static_cast<std::uint32_t>(count))) {
      return writer_failure(writer, result);
    }
    for (std::size_t element_index = 0U; element_index < count;
         ++element_index) {
      bool boolean = false;
      const void* element = nullptr;
      if (member.get_const_function != nullptr) {
        element = member.get_const_function(field, element_index);
      } else {
        member.fetch_function(field, element_index, &boolean);
        element = &boolean;
      }
      if ((element == nullptr) ||
          !serialize_value<Members>(member, element, writer, result)) {
        return element != nullptr
                   ? false
                   : fail(result, CodecError::invalid_message_storage,
                          writer.size());
      }
    }
  }
  return true;
}

template <typename Members>
[[nodiscard]] bool process_members(const Members* members, void* message,
                                   CdrReader& reader, bool apply,
                                   CodecResult& result) noexcept;

template <typename Members, typename Member>
[[nodiscard]] bool process_string(const Member& member, void* value,
                                  CdrReader& reader, const bool apply,
                                  CodecResult& result) noexcept {
  const char* data = nullptr;
  std::size_t length = 0U;
  if (!reader.read_string_view(data, member.string_upper_bound_, length)) {
    return reader_failure(reader, result);
  }

  if constexpr (std::is_same_v<Members, CMembers>) {
    auto* string = static_cast<rosidl_runtime_c__String*>(value);
    if ((string->data == nullptr) || (string->capacity <= length)) {
      return fail(result, CodecError::invalid_message_storage,
                  reader.position());
    }
    if (apply) {
      if (length != 0U) {
        std::memcpy(string->data, data, length);
      }
      string->data[length] = '\0';
      string->size = length;
    }
  } else {
    auto* string = static_cast<std::string*>(value);
    if (length > string->capacity()) {
      return fail(result, CodecError::invalid_message_storage,
                  reader.position());
    }
    if (apply) {
      try {
        string->resize(length);
      } catch (...) {
        return fail(result, CodecError::invalid_message_storage,
                    reader.position());
      }
      if (length != 0U) {
        std::memcpy(string->data(), data, length);
      }
    }
  }
  return true;
}

template <typename Members, typename Member>
[[nodiscard]] bool process_value(const Member& member, void* value,
                                 CdrReader& reader, const bool apply,
                                 CodecResult& result) noexcept {
  if (member.type_id_ ==
      rosidl_typesupport_introspection_c__ROS_TYPE_STRING) {
    return process_string<Members>(member, value, reader, apply, result);
  }
  if (member.type_id_ ==
      rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE) {
    return process_members(nested_members<Members>(member), value, reader,
                           apply, result);
  }
  return read_scalar(member.type_id_, reader, value, apply, result);
}

template <typename Members>
[[nodiscard]] bool process_members(const Members* const members,
                                   void* const message, CdrReader& reader,
                                   const bool apply,
                                   CodecResult& result) noexcept {
  for (std::uint32_t index = 0U; index < members->member_count_; ++index) {
    const auto& member = members->members_[index];
    auto* field = static_cast<std::uint8_t*>(message) + member.offset_;
    if (!member.is_array_) {
      if (!process_value<Members>(member, field, reader, apply, result)) {
        return false;
      }
      continue;
    }
    if (member.size_function == nullptr) {
      return fail(result, CodecError::callback_missing, reader.position());
    }
    std::size_t count = member.array_size_;
    if (member.is_upper_bound_) {
      std::uint32_t encoded_count = 0U;
      if (!reader.read_uint32(encoded_count)) {
        return reader_failure(reader, result);
      }
      count = encoded_count;
      if (count > member.array_size_) {
        return fail(result, CodecError::field_bound_exceeded,
                    reader.position());
      }
    }
    if (member.size_function(field) != count) {
      return fail(result, CodecError::destination_size_mismatch,
                  reader.position());
    }
    const bool direct = apply ? member.get_function != nullptr
                              : member.get_const_function != nullptr;
    if (!direct &&
        ((member.type_id_ !=
          rosidl_typesupport_introspection_c__ROS_TYPE_BOOLEAN) ||
         (member.assign_function == nullptr))) {
      return fail(result, CodecError::callback_missing, reader.position());
    }

    for (std::size_t element_index = 0U; element_index < count;
         ++element_index) {
      bool boolean = false;
      void* element = nullptr;
      if (apply && (member.get_function != nullptr)) {
        element = member.get_function(field, element_index);
      } else if (!apply && (member.get_const_function != nullptr)) {
        element = const_cast<void*>(
            member.get_const_function(field, element_index));
      } else {
        element = &boolean;
      }
      if ((element == nullptr) ||
          !process_value<Members>(member, element, reader, apply, result)) {
        return element != nullptr
                   ? false
                   : fail(result, CodecError::invalid_message_storage,
                          reader.position());
      }
      if (apply && (member.get_function == nullptr)) {
        member.assign_function(field, element_index, &boolean);
      }
    }
  }
  return true;
}

template <typename Members>
[[nodiscard]] bool serialize_for_language(const void* data,
                                          const void* message,
                                          CdrWriter& writer,
                                          CodecResult& result) noexcept {
  return serialize_members(static_cast<const Members*>(data), message, writer,
                           result);
}

template <typename Members>
[[nodiscard]] bool process_for_language(const void* data, void* message,
                                        CdrReader& reader, const bool apply,
                                        CodecResult& result) noexcept {
  return process_members(static_cast<const Members*>(data), message, reader,
                         apply, result);
}

}  // namespace

const char* to_string(const CodecError error) noexcept {
  switch (error) {
    case CodecError::none:
      return "none";
    case CodecError::invalid_argument:
      return "invalid_argument";
    case CodecError::type_support_rejected:
      return "type_support_rejected";
    case CodecError::invalid_message_storage:
      return "invalid_message_storage";
    case CodecError::callback_missing:
      return "callback_missing";
    case CodecError::field_bound_exceeded:
      return "field_bound_exceeded";
    case CodecError::destination_size_mismatch:
      return "destination_size_mismatch";
    case CodecError::output_too_small:
      return "output_too_small";
    case CodecError::input_truncated:
      return "input_truncated";
    case CodecError::unsupported_encoding:
      return "unsupported_encoding";
    case CodecError::invalid_wire_data:
      return "invalid_wire_data";
    case CodecError::trailing_data:
      return "trailing_data";
    case CodecError::cdr_failure:
      return "cdr_failure";
  }
  return "unknown";
}

bool serialize_xcdr1(const rosidl_message_type_support_t* const type_support,
                     const void* const ros_message,
                     std::uint8_t* const buffer,
                     const std::size_t capacity,
                     const CodecByteOrder byte_order,
                     CodecResult& result) noexcept {
  result = {};
  if ((type_support == nullptr) || (ros_message == nullptr) ||
      (buffer == nullptr) ||
      ((byte_order != CodecByteOrder::big_endian) &&
       (byte_order != CodecByteOrder::little_endian))) {
    return fail(result, CodecError::invalid_argument);
  }

  TypeSupportInfo info{};
  TypeSupportError type_error{};
  if (!analyze_type_support(type_support, capacity, info, type_error)) {
    result.type_support_error = type_error;
    return fail(result, CodecError::type_support_rejected);
  }

  CdrWriter writer(buffer, capacity);
  const ByteOrder cdr_order = byte_order == CodecByteOrder::little_endian
                                  ? ByteOrder::little_endian
                                  : ByteOrder::big_endian;
  if (!writer.begin(cdr_order)) {
    return writer_failure(writer, result);
  }
  const bool success =
      info.language == TypeSupportLanguage::introspection_c
          ? serialize_for_language<CMembers>(type_support->data, ros_message,
                                             writer, result)
          : serialize_for_language<CppMembers>(type_support->data, ros_message,
                                               writer, result);
  if (!success) {
    return false;
  }
  result.bytes_processed = writer.size();
  return true;
}

bool deserialize_xcdr1(
    const rosidl_message_type_support_t* const type_support,
    const std::uint8_t* const buffer, const std::size_t size,
    const std::size_t maximum_sample_size, void* const ros_message,
    CodecResult& result) noexcept {
  result = {};
  if ((type_support == nullptr) || (buffer == nullptr) ||
      (ros_message == nullptr) || (size > maximum_sample_size)) {
    return fail(result, CodecError::invalid_argument);
  }

  TypeSupportInfo info{};
  TypeSupportError type_error{};
  if (!analyze_type_support(type_support, maximum_sample_size, info,
                            type_error)) {
    result.type_support_error = type_error;
    return fail(result, CodecError::type_support_rejected);
  }

  CdrReader validator(buffer, size);
  if (!validator.begin()) {
    return reader_failure(validator, result);
  }
  const bool valid =
      info.language == TypeSupportLanguage::introspection_c
          ? process_for_language<CMembers>(type_support->data, ros_message,
                                           validator, false, result)
          : process_for_language<CppMembers>(type_support->data, ros_message,
                                             validator, false, result);
  if (!valid) {
    return false;
  }
  if (validator.remaining() != 0U) {
    return fail(result, CodecError::trailing_data, validator.position());
  }

  CdrReader reader(buffer, size);
  if (!reader.begin()) {
    return reader_failure(reader, result);
  }
  const bool applied =
      info.language == TypeSupportLanguage::introspection_c
          ? process_for_language<CMembers>(type_support->data, ros_message,
                                           reader, true, result)
          : process_for_language<CppMembers>(type_support->data, ros_message,
                                             reader, true, result);
  if (!applied) {
    return false;
  }
  result.bytes_processed = reader.position();
  return true;
}

}  // namespace rmw_openrtdds_cpp
