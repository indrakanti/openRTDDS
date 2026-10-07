#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "rmw_openrtdds_cpp/type_support.hpp"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"

// Verifies: ORT-RMW-040, ORT-RMW-041
namespace {

using rmw_openrtdds_cpp::DdsNames;
using rmw_openrtdds_cpp::RosChannel;
using rmw_openrtdds_cpp::TypeSupportError;
using rmw_openrtdds_cpp::TypeSupportInfo;
using rmw_openrtdds_cpp::TypeSupportLanguage;

constexpr std::size_t kSampleLimit = 65'507U;

struct CFixture final {
  std::array<rosidl_typesupport_introspection_c__MessageMember, 5U> fields{};
  rosidl_typesupport_introspection_c__MessageMembers members{};
  rosidl_message_type_support_t handle{};

  CFixture() {
    fields[0].name_ = "count";
    fields[0].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_UINT32;

    fields[1].name_ = "stamp";
    fields[1].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE;

    fields[2].name_ = "frame";
    fields[2].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_STRING;
    fields[2].string_upper_bound_ = 16U;

    fields[3].name_ = "flags";
    fields[3].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_UINT8;
    fields[3].is_array_ = true;
    fields[3].array_size_ = 4U;

    fields[4].name_ = "samples";
    fields[4].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_INT16;
    fields[4].is_array_ = true;
    fields[4].array_size_ = 3U;
    fields[4].is_upper_bound_ = true;

    members.message_namespace_ = "demo_msgs__msg";
    members.message_name_ = "Bounded";
    members.member_count_ = static_cast<std::uint32_t>(fields.size());
    members.members_ = fields.data();

    handle.typesupport_identifier =
        rosidl_typesupport_introspection_c__identifier;
    handle.data = &members;
  }
};

struct CppFixture final {
  std::array<rosidl_typesupport_introspection_cpp::MessageMember, 5U> fields{};
  rosidl_typesupport_introspection_cpp::MessageMembers members{};
  rosidl_message_type_support_t handle{};

  CppFixture() {
    fields[0].name_ = "count";
    fields[0].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_UINT32;

    fields[1].name_ = "stamp";
    fields[1].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_DOUBLE;

    fields[2].name_ = "frame";
    fields[2].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_STRING;
    fields[2].string_upper_bound_ = 16U;

    fields[3].name_ = "flags";
    fields[3].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_UINT8;
    fields[3].is_array_ = true;
    fields[3].array_size_ = 4U;

    fields[4].name_ = "samples";
    fields[4].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_INT16;
    fields[4].is_array_ = true;
    fields[4].array_size_ = 3U;
    fields[4].is_upper_bound_ = true;

    members.message_namespace_ = "demo_msgs::msg";
    members.message_name_ = "Bounded";
    members.member_count_ = static_cast<std::uint32_t>(fields.size());
    members.members_ = fields.data();

    handle.typesupport_identifier =
        rosidl_typesupport_introspection_cpp::typesupport_identifier;
    handle.data = &members;
  }
};

[[nodiscard]] int test_bounded_c_and_cpp() {
  CFixture c_fixture{};
  CppFixture cpp_fixture{};
  TypeSupportError error = TypeSupportError::invalid_argument;
  TypeSupportInfo c_info{};
  if (!rmw_openrtdds_cpp::analyze_type_support(
          &c_fixture.handle, kSampleLimit, c_info, error)) {
    return 1;
  }
  if ((error != TypeSupportError::none) ||
      (c_info.language != TypeSupportLanguage::introspection_c) ||
      (c_info.maximum_serialized_size != 58U) ||
      (c_info.maximum_alignment != 8U) ||
      (c_info.maximum_nesting_depth != 1U) ||
      (std::strcmp(c_info.dds_type_name.data(),
                   "demo_msgs::msg::dds_::Bounded_") != 0)) {
    return 2;
  }

  TypeSupportInfo cpp_info{};
  if (!rmw_openrtdds_cpp::analyze_type_support(
          &cpp_fixture.handle, kSampleLimit, cpp_info, error)) {
    return 3;
  }
  if ((cpp_info.language != TypeSupportLanguage::introspection_cpp) ||
      (cpp_info.maximum_serialized_size !=
       c_info.maximum_serialized_size) ||
      (std::strcmp(cpp_info.dds_type_name.data(),
                   c_info.dds_type_name.data()) != 0)) {
    return 4;
  }
  return 0;
}

[[nodiscard]] int test_nested_and_rejections() {
  rosidl_typesupport_introspection_c__MessageMember child_field{};
  child_field.name_ = "value";
  child_field.type_id_ =
      rosidl_typesupport_introspection_c__ROS_TYPE_UINT32;
  rosidl_typesupport_introspection_c__MessageMembers child_members{};
  child_members.message_namespace_ = "demo_msgs__msg";
  child_members.message_name_ = "Child";
  child_members.member_count_ = 1U;
  child_members.members_ = &child_field;
  rosidl_message_type_support_t child_handle{};
  child_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  child_handle.data = &child_members;

  std::array<rosidl_typesupport_introspection_c__MessageMember, 2U>
      parent_fields{};
  parent_fields[0].name_ = "tag";
  parent_fields[0].type_id_ =
      rosidl_typesupport_introspection_c__ROS_TYPE_UINT8;
  parent_fields[1].name_ = "child";
  parent_fields[1].type_id_ =
      rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE;
  parent_fields[1].members_ = &child_handle;
  rosidl_typesupport_introspection_c__MessageMembers parent_members{};
  parent_members.message_namespace_ = "demo_msgs__msg";
  parent_members.message_name_ = "Parent";
  parent_members.member_count_ = 2U;
  parent_members.members_ = parent_fields.data();
  rosidl_message_type_support_t parent_handle{};
  parent_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  parent_handle.data = &parent_members;

  TypeSupportError error{};
  TypeSupportInfo info{};
  if (!rmw_openrtdds_cpp::analyze_type_support(
          &parent_handle, kSampleLimit, info, error) ||
      (info.maximum_serialized_size != 12U) ||
      (info.maximum_nesting_depth != 2U)) {
    return 10;
  }

  CFixture unbounded{};
  unbounded.fields[2].string_upper_bound_ = 0U;
  TypeSupportInfo unchanged{};
  unchanged.maximum_serialized_size = 777U;
  if (rmw_openrtdds_cpp::analyze_type_support(
          &unbounded.handle, kSampleLimit, unchanged, error) ||
      (error != TypeSupportError::unbounded_type) ||
      (unchanged.maximum_serialized_size != 777U)) {
    return 11;
  }

  CFixture unbounded_sequence{};
  unbounded_sequence.fields[4].array_size_ = 0U;
  unbounded_sequence.fields[4].is_upper_bound_ = false;
  if (rmw_openrtdds_cpp::analyze_type_support(
          &unbounded_sequence.handle, kSampleLimit, unchanged, error) ||
      (error != TypeSupportError::unbounded_type)) {
    return 12;
  }

  CFixture unsupported{};
  unsupported.fields[0].type_id_ =
      rosidl_typesupport_introspection_c__ROS_TYPE_LONG_DOUBLE;
  if (rmw_openrtdds_cpp::analyze_type_support(
          &unsupported.handle, kSampleLimit, unchanged, error) ||
      (error != TypeSupportError::unsupported_field)) {
    return 13;
  }

  CFixture over_limit{};
  if (rmw_openrtdds_cpp::analyze_type_support(
          &over_limit.handle, 57U, unchanged, error) ||
      (error != TypeSupportError::sample_limit_exceeded)) {
    return 14;
  }

  rosidl_typesupport_introspection_c__MessageMember recursive_field{};
  rosidl_typesupport_introspection_c__MessageMembers recursive_members{};
  rosidl_message_type_support_t recursive_handle{};
  recursive_field.name_ = "self";
  recursive_field.type_id_ =
      rosidl_typesupport_introspection_c__ROS_TYPE_MESSAGE;
  recursive_field.members_ = &recursive_handle;
  recursive_members.message_namespace_ = "demo_msgs__msg";
  recursive_members.message_name_ = "Recursive";
  recursive_members.member_count_ = 1U;
  recursive_members.members_ = &recursive_field;
  recursive_handle.typesupport_identifier =
      rosidl_typesupport_introspection_c__identifier;
  recursive_handle.data = &recursive_members;
  if (rmw_openrtdds_cpp::analyze_type_support(
          &recursive_handle, kSampleLimit, unchanged, error) ||
      (error != TypeSupportError::recursive_type)) {
    return 15;
  }

  CFixture malformed{};
  malformed.members.members_ = nullptr;
  if (rmw_openrtdds_cpp::analyze_type_support(
          &malformed.handle, kSampleLimit, unchanged, error) ||
      (error != TypeSupportError::invalid_members)) {
    return 16;
  }

  CFixture unsupported_identifier{};
  unsupported_identifier.handle.typesupport_identifier =
      "rosidl_typesupport_cpp";
  if (rmw_openrtdds_cpp::analyze_type_support(
          &unsupported_identifier.handle, kSampleLimit, unchanged, error) ||
      (error != TypeSupportError::unsupported_identifier)) {
    return 17;
  }

  if (rmw_openrtdds_cpp::analyze_type_support(
          nullptr, kSampleLimit, unchanged, error) ||
      (error != TypeSupportError::invalid_argument)) {
    return 18;
  }
  return 0;
}

[[nodiscard]] int test_name_mapping() {
  CFixture fixture{};
  TypeSupportError error{};
  DdsNames names{};
  if (!rmw_openrtdds_cpp::map_ros_to_dds_names(
          &fixture.handle, RosChannel::topic, "/camera/image", false,
          names, error) ||
      (std::strcmp(names.topic_name.data(), "rt/camera/image") != 0) ||
      (std::strcmp(names.type_name.data(),
                   "demo_msgs::msg::dds_::Bounded_") != 0)) {
    return 20;
  }
  if (!rmw_openrtdds_cpp::map_ros_to_dds_names(
          &fixture.handle, RosChannel::service_request, "/add_two_ints",
          false, names, error) ||
      (std::strcmp(names.topic_name.data(),
                   "rq/add_two_intsRequest") != 0)) {
    return 21;
  }
  if (!rmw_openrtdds_cpp::map_ros_to_dds_names(
          &fixture.handle, RosChannel::service_response, "/add_two_ints",
          false, names, error) ||
      (std::strcmp(names.topic_name.data(),
                   "rr/add_two_intsReply") != 0)) {
    return 22;
  }
  if (!rmw_openrtdds_cpp::map_ros_to_dds_names(
          &fixture.handle, RosChannel::service_request, "/add_two_ints",
          true, names, error) ||
      (std::strcmp(names.topic_name.data(),
                   "/add_two_intsRequest") != 0)) {
    return 23;
  }

  std::array<char, rmw_openrtdds_cpp::kDdsNameCapacity> long_name{};
  long_name.fill('a');
  long_name.front() = '/';
  long_name.back() = '\0';
  std::strcpy(names.topic_name.data(), "unchanged");
  if (rmw_openrtdds_cpp::map_ros_to_dds_names(
          &fixture.handle, RosChannel::topic, long_name.data(), false,
          names, error) ||
      (error != TypeSupportError::name_limit_exceeded) ||
      (std::strcmp(names.topic_name.data(), "unchanged") != 0)) {
    return 24;
  }
  if (!rmw_openrtdds_cpp::map_ros_to_dds_names(
          &fixture.handle, RosChannel::topic, long_name.data(), true,
          names, error) ||
      (std::strlen(names.topic_name.data()) != 255U)) {
    return 25;
  }

  CFixture empty_type_name{};
  empty_type_name.members.message_name_ = "";
  std::strcpy(names.type_name.data(), "unchanged");
  if (rmw_openrtdds_cpp::map_ros_to_dds_names(
          &empty_type_name.handle, RosChannel::topic, "/camera/image", false,
          names, error) ||
      (error != TypeSupportError::invalid_name) ||
      (std::strcmp(names.type_name.data(), "unchanged") != 0)) {
    return 26;
  }
  return 0;
}

}  // namespace

int main() {
  int result = test_bounded_c_and_cpp();
  if (result != 0) {
    return result;
  }
  result = test_nested_and_rejections();
  if (result != 0) {
    return result;
  }
  return test_name_mapping();
}
