#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/rmw.h"
#include "rosidl_typesupport_introspection_c/field_types.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_c/message_introspection.h"

// Verifies: ORT-RMW-011, ORT-RMW-021
namespace {

struct Request final {
  std::int64_t left{0};
  std::int64_t right{0};
};

struct Response final {
  std::int64_t sum{0};
};

struct Fixture final {
  std::array<rosidl_typesupport_introspection_c__MessageMember, 2U>
      request_fields{};
  rosidl_typesupport_introspection_c__MessageMember response_field{};
  rosidl_typesupport_introspection_c__MessageMembers request_members{};
  rosidl_typesupport_introspection_c__MessageMembers response_members{};
  rosidl_message_type_support_t request_handle{};
  rosidl_message_type_support_t response_handle{};
  rosidl_service_type_support_t service_handle{};

  Fixture() {
    request_fields[0].name_ = "left";
    request_fields[0].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_INT64;
    request_fields[0].offset_ = offsetof(Request, left);
    request_fields[1].name_ = "right";
    request_fields[1].type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_INT64;
    request_fields[1].offset_ = offsetof(Request, right);
    request_members.message_namespace_ = "openrtdds_test_msgs__srv";
    request_members.message_name_ = "Add_Request";
    request_members.member_count_ = request_fields.size();
    request_members.size_of_ = sizeof(Request);
    request_members.members_ = request_fields.data();
    request_handle.typesupport_identifier =
        rosidl_typesupport_introspection_c__identifier;
    request_handle.data = &request_members;

    response_field.name_ = "sum";
    response_field.type_id_ =
        rosidl_typesupport_introspection_c__ROS_TYPE_INT64;
    response_field.offset_ = offsetof(Response, sum);
    response_members.message_namespace_ = "openrtdds_test_msgs__srv";
    response_members.message_name_ = "Add_Response";
    response_members.member_count_ = 1U;
    response_members.size_of_ = sizeof(Response);
    response_members.members_ = &response_field;
    response_handle.typesupport_identifier =
        rosidl_typesupport_introspection_c__identifier;
    response_handle.data = &response_members;

    service_handle.typesupport_identifier =
        rosidl_typesupport_introspection_c__identifier;
    service_handle.request_typesupport = &request_handle;
    service_handle.response_typesupport = &response_handle;
  }
};

[[nodiscard]] int fail(const int code) {
  rmw_reset_error();
  return code;
}

template <typename Take>
[[nodiscard]] bool eventually_take(Take&& take) {
  for (std::size_t attempt = 0U; attempt < 500U; ++attempt) {
    if (take()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

}  // namespace

int main() {
  Fixture fixture{};
  const auto allocator = rcutils_get_default_allocator();
  auto options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return fail(1);
  }
  options.instance_id = 36U;
  options.domain_id = 36U;
  options.enclave = rcutils_strdup("/openrtdds_services", allocator);
  auto context = rmw_get_zero_initialized_context();
  if ((options.enclave == nullptr) ||
      (rmw_init(&options, &context) != RMW_RET_OK)) {
    return fail(2);
  }
  rmw_node_t* node = rmw_create_node(&context, "services", "/openrtdds");
  rmw_qos_profile_t qos = rmw_qos_profile_services_default;
  qos.depth = 4U;
  rmw_service_t* service = rmw_create_service(
      node, &fixture.service_handle, "/add", &qos);
  rmw_client_t* client = rmw_create_client(
      node, &fixture.service_handle, "/add", &qos);
  if ((node == nullptr) || (service == nullptr) || (client == nullptr)) {
    return fail(3);
  }

  rmw_gid_t gid{};
  if (rmw_get_gid_for_client(client, &gid) != RMW_RET_OK) {
    return fail(4);
  }
  bool available = false;
  if (!eventually_take([&] {
        return (rmw_service_server_is_available(node, client, &available) ==
                RMW_RET_OK) && available;
      })) {
    return fail(5);
  }

  Request request{20, 22};
  std::int64_t sequence = 0;
  if ((rmw_send_request(client, &request, &sequence) != RMW_RET_OK) ||
      (sequence != 1)) {
    return fail(6);
  }

  void* service_data = service->data;
  rmw_services_t services{1U, &service_data};
  rmw_wait_set_t* wait_set = rmw_create_wait_set(&context, 1U);
  const rmw_time_t timeout{2U, 0U};
  if ((wait_set == nullptr) ||
      (rmw_wait(nullptr, nullptr, &services, nullptr, nullptr, wait_set,
                &timeout) != RMW_RET_OK) ||
      (service_data != service->data)) {
    return fail(7);
  }

  Request received{};
  rmw_service_info_t request_info{};
  bool taken = false;
  if (!eventually_take([&] {
        return (rmw_take_request(service, &request_info, &received, &taken) ==
                RMW_RET_OK) && taken;
      }) || (received.left != 20) || (received.right != 22) ||
      (request_info.request_id.sequence_number != sequence) ||
      (std::memcmp(request_info.request_id.writer_guid, gid.data,
                   RMW_GID_STORAGE_SIZE) != 0)) {
    return fail(8);
  }

  Response response{42};
  rmw_request_id_t wrong = request_info.request_id;
  wrong.writer_guid[0] ^= 0xFFU;
  if ((rmw_send_response(service, &wrong, &response) != RMW_RET_OK) ||
      (rmw_send_response(service, &request_info.request_id, &response) !=
       RMW_RET_OK)) {
    return fail(9);
  }

  Response received_response{};
  rmw_service_info_t response_info{};
  taken = false;
  if (!eventually_take([&] {
        return (rmw_take_response(client, &response_info, &received_response,
                                  &taken) == RMW_RET_OK) && taken;
      }) || (received_response.sum != 42) ||
      (response_info.request_id.sequence_number != sequence)) {
    return fail(10);
  }

  std::array<std::int64_t, 3U> sequences{};
  for (std::size_t index = 0U; index < sequences.size(); ++index) {
    request = {static_cast<std::int64_t>(index), 10};
    if (rmw_send_request(client, &request, &sequences[index]) != RMW_RET_OK) {
      return fail(11);
    }
  }
  for (std::size_t index = 0U; index < sequences.size(); ++index) {
    taken = false;
    if (!eventually_take([&] {
          return (rmw_take_request(service, &request_info, &received,
                                   &taken) == RMW_RET_OK) && taken;
        })) {
      return fail(12);
    }
    response.sum = received.left + received.right;
    if (rmw_send_response(service, &request_info.request_id, &response) !=
        RMW_RET_OK) {
      return fail(13);
    }
  }
  for (std::size_t index = 0U; index < sequences.size(); ++index) {
    taken = false;
    if (!eventually_take([&] {
          return (rmw_take_response(client, &response_info,
                                    &received_response, &taken) ==
                  RMW_RET_OK) && taken;
        })) {
      return fail(14);
    }
  }

  if ((rmw_destroy_wait_set(wait_set) != RMW_RET_OK) ||
      (rmw_destroy_client(node, client) != RMW_RET_OK) ||
      (rmw_destroy_service(node, service) != RMW_RET_OK) ||
      (rmw_shutdown(&context) != RMW_RET_OK) ||
      (rmw_destroy_node(node) != RMW_RET_OK) ||
      (rmw_context_fini(&context) != RMW_RET_OK) ||
      (rmw_init_options_fini(&options) != RMW_RET_OK)) {
    return fail(15);
  }
  return 0;
}
