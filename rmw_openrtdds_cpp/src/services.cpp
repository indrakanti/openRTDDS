#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

#include "internal.hpp"

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/publisher_options.h"
#include "rmw/qos_profiles.h"
#include "rmw/subscription_options.h"
#include "rosidl_runtime_c/service_type_support_struct.h"
#include "rosidl_typesupport_introspection_c/identifier.h"
#include "rosidl_typesupport_introspection_cpp/identifier.hpp"

namespace rmw_openrtdds_cpp {
namespace {

// Requirements: ORT-RMW-011, ORT-RMW-021
struct OutstandingRequest final {
  std::int64_t sequence{0};
  bool occupied{false};
};

struct ClientData final {
  const rmw_node_t* node{nullptr};
  rmw_publisher_t* request_publisher{nullptr};
  rmw_subscription_t* response_subscription{nullptr};
  rmw_gid_t gid{};
  std::int64_t next_sequence{1};
  std::array<OutstandingRequest, detail::kServiceOutstandingCapacity>
      outstanding{};
  std::mutex mutex{};
};

struct ServiceData final {
  const rmw_node_t* node{nullptr};
  rmw_subscription_t* request_subscription{nullptr};
  rmw_publisher_t* response_publisher{nullptr};
};

struct ServiceTypes final {
  const rosidl_message_type_support_t* request{nullptr};
  const rosidl_message_type_support_t* response{nullptr};
};

[[nodiscard]] bool resolve_service_types(
    const rosidl_service_type_support_t* const type_support,
    ServiceTypes& output) noexcept {
  if ((type_support == nullptr) ||
      (type_support->typesupport_identifier == nullptr)) {
    return false;
  }
  const rosidl_service_type_support_t* resolved = nullptr;
  if ((std::strcmp(type_support->typesupport_identifier,
                   rosidl_typesupport_introspection_c__identifier) == 0) ||
      (std::strcmp(type_support->typesupport_identifier,
                   rosidl_typesupport_introspection_cpp::
                       typesupport_identifier) == 0)) {
    resolved = type_support;
  } else if (type_support->func != nullptr) {
    resolved = get_service_typesupport_handle(
        type_support, rosidl_typesupport_introspection_c__identifier);
    if (resolved == nullptr) {
      resolved = get_service_typesupport_handle(
          type_support,
          rosidl_typesupport_introspection_cpp::typesupport_identifier);
    }
  }
  if ((resolved == nullptr) || (resolved->request_typesupport == nullptr) ||
      (resolved->response_typesupport == nullptr)) {
    return false;
  }
  TypeSupportError error = TypeSupportError::none;
  ServiceTypes candidate{};
  candidate.request = resolve_type_support(resolved->request_typesupport, error);
  if (candidate.request == nullptr) {
    return false;
  }
  candidate.response =
      resolve_type_support(resolved->response_typesupport, error);
  if (candidate.response == nullptr) {
    return false;
  }
  output = candidate;
  return true;
}

[[nodiscard]] bool valid_node(const rmw_node_t* const node) noexcept {
  return (node != nullptr) && (node->data != nullptr) &&
         detail::identifier_matches(node->implementation_identifier) &&
         detail::context_is_valid(node->context);
}

[[nodiscard]] bool valid_client(const rmw_client_t* const client,
                                ClientData*& output) noexcept {
  if ((client == nullptr) || (client->data == nullptr) ||
      !detail::identifier_matches(client->implementation_identifier)) {
    return false;
  }
  auto* data = static_cast<ClientData*>(client->data);
  if (!valid_node(data->node) || (data->request_publisher == nullptr) ||
      (data->response_subscription == nullptr)) {
    return false;
  }
  output = data;
  return true;
}

[[nodiscard]] bool valid_service(const rmw_service_t* const service,
                                 ServiceData*& output) noexcept {
  if ((service == nullptr) || (service->data == nullptr) ||
      !detail::identifier_matches(service->implementation_identifier)) {
    return false;
  }
  auto* data = static_cast<ServiceData*>(service->data);
  if (!valid_node(data->node) || (data->request_subscription == nullptr) ||
      (data->response_publisher == nullptr)) {
    return false;
  }
  output = data;
  return true;
}

[[nodiscard]] rmw_ret_t invalid_handle(const char* const kind,
                                       const char* const identifier) noexcept {
  if ((identifier != nullptr) && !detail::identifier_matches(identifier)) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "%s belongs to another RMW implementation", kind);
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("invalid OpenRTDDS %s", kind);
  return RMW_RET_INVALID_ARGUMENT;
}

[[nodiscard]] bool request_id_equal(const rmw_request_id_t& left,
                                    const rmw_request_id_t& right) noexcept {
  return (left.sequence_number == right.sequence_number) &&
         (std::memcmp(left.writer_guid, right.writer_guid,
                      RMW_GID_STORAGE_SIZE) == 0);
}

struct ClientResponseFilter final {
  ClientData* client{nullptr};
  OutstandingRequest* match{nullptr};
};

[[nodiscard]] bool accept_client_response(
    const rmw_request_id_t& candidate, void* const opaque) noexcept {
  auto* filter = static_cast<ClientResponseFilter*>(opaque);
  rmw_request_id_t expected{};
  std::memcpy(expected.writer_guid, filter->client->gid.data,
              RMW_GID_STORAGE_SIZE);
  expected.sequence_number = candidate.sequence_number;
  if (!request_id_equal(expected, candidate)) {
    return false;
  }
  for (auto& request : filter->client->outstanding) {
    if (request.occupied && (request.sequence == candidate.sequence_number)) {
      filter->match = &request;
      return true;
    }
  }
  return false;
}

}  // namespace

namespace detail {

bool client_wait_data_valid(const void* const opaque,
                            const rmw_context_t* const context) noexcept {
  if ((opaque == nullptr) || !context_is_valid(context)) {
    return false;
  }
  const auto* data = static_cast<const ClientData*>(opaque);
  return (data->node != nullptr) && (data->node->context == context) &&
         (data->response_subscription != nullptr);
}

bool client_wait_ready(const void* const opaque) noexcept {
  if (opaque == nullptr) {
    return false;
  }
  const auto* data = static_cast<const ClientData*>(opaque);
  return subscription_wait_ready(data->response_subscription->data);
}

bool service_wait_data_valid(const void* const opaque,
                             const rmw_context_t* const context) noexcept {
  if ((opaque == nullptr) || !context_is_valid(context)) {
    return false;
  }
  const auto* data = static_cast<const ServiceData*>(opaque);
  return (data->node != nullptr) && (data->node->context == context) &&
         (data->request_subscription != nullptr);
}

bool service_wait_ready(const void* const opaque) noexcept {
  if (opaque == nullptr) {
    return false;
  }
  const auto* data = static_cast<const ServiceData*>(opaque);
  return subscription_wait_ready(data->request_subscription->data);
}

}  // namespace detail
}  // namespace rmw_openrtdds_cpp

extern "C" {

rmw_client_t* rmw_create_client(
    const rmw_node_t* node,
    const rosidl_service_type_support_t* type_support,
    const char* service_name,
    const rmw_qos_profile_t* qos_profile) {
  using namespace rmw_openrtdds_cpp;
  if (!valid_node(node) || node->context->impl->shutdown.load() ||
      (type_support == nullptr) ||
      (service_name == nullptr) || (service_name[0] == '\0') ||
      (qos_profile == nullptr)) {
    RMW_SET_ERROR_MSG("client creation requires valid non-null arguments");
    return nullptr;
  }
  ServiceTypes types{};
  if (!resolve_service_types(type_support, types)) {
    RMW_SET_ERROR_MSG("client requires bounded C or C++ introspection types");
    return nullptr;
  }

  auto& allocator = node->context->options.allocator;
  auto* client = static_cast<rmw_client_t*>(allocator.zero_allocate(
      1U, sizeof(rmw_client_t), allocator.state));
  void* storage = allocator.zero_allocate(1U, sizeof(ClientData),
                                          allocator.state);
  auto* data = storage == nullptr ? nullptr : new (storage) ClientData{};
  char* name = rcutils_strdup(service_name, allocator);
  if ((client == nullptr) || (data == nullptr) || (name == nullptr)) {
    if (name != nullptr) {
      allocator.deallocate(name, allocator.state);
    }
    if (data != nullptr) {
      data->~ClientData();
      allocator.deallocate(data, allocator.state);
    }
    if (client != nullptr) {
      allocator.deallocate(client, allocator.state);
    }
    RMW_SET_ERROR_MSG("unable to allocate bounded client state");
    return nullptr;
  }

  const auto publisher_options = rmw_get_default_publisher_options();
  const auto subscription_options = rmw_get_default_subscription_options();
  data->node = node;
  data->request_publisher = detail::create_publisher_endpoint(
      node, types.request, service_name, qos_profile, &publisher_options,
      RosChannel::service_request, false);
  if (data->request_publisher != nullptr) {
    data->response_subscription = detail::create_subscription_endpoint(
        node, types.response, service_name, qos_profile,
        &subscription_options, RosChannel::service_response, false);
  }
  if ((data->request_publisher == nullptr) ||
      (data->response_subscription == nullptr) ||
      (rmw_get_gid_for_publisher(data->request_publisher, &data->gid) !=
       RMW_RET_OK)) {
    if (data->response_subscription != nullptr) {
      const rmw_ret_t cleanup_result = rmw_destroy_subscription(
          const_cast<rmw_node_t*>(node), data->response_subscription);
      static_cast<void>(cleanup_result);
    }
    if (data->request_publisher != nullptr) {
      const rmw_ret_t cleanup_result = rmw_destroy_publisher(
          const_cast<rmw_node_t*>(node), data->request_publisher);
      static_cast<void>(cleanup_result);
    }
    allocator.deallocate(name, allocator.state);
    data->~ClientData();
    allocator.deallocate(data, allocator.state);
    allocator.deallocate(client, allocator.state);
    RMW_SET_ERROR_MSG("unable to create bounded client endpoints");
    return nullptr;
  }
  client->implementation_identifier =
      openrtdds::rmw::kImplementationIdentifier;
  client->data = data;
  client->service_name = name;
  return client;
}

rmw_ret_t rmw_destroy_client(rmw_node_t* node, rmw_client_t* client) {
  using namespace rmw_openrtdds_cpp;
  ClientData* data = nullptr;
  if (!valid_client(client, data)) {
    return invalid_handle("client",
                          client == nullptr ? nullptr
                                            : client->implementation_identifier);
  }
  if ((node == nullptr) || (data->node != node)) {
    RMW_SET_ERROR_MSG("client does not belong to the supplied node");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto& allocator = node->context->options.allocator;
  const rmw_ret_t subscription_result =
      rmw_destroy_subscription(node, data->response_subscription);
  const rmw_ret_t publisher_result =
      rmw_destroy_publisher(node, data->request_publisher);
  allocator.deallocate(const_cast<char*>(client->service_name),
                       allocator.state);
  data->~ClientData();
  allocator.deallocate(data, allocator.state);
  allocator.deallocate(client, allocator.state);
  return (subscription_result == RMW_RET_OK) &&
                 (publisher_result == RMW_RET_OK)
             ? RMW_RET_OK
             : RMW_RET_ERROR;
}

rmw_ret_t rmw_send_request(const rmw_client_t* client,
                           const void* ros_request,
                           int64_t* sequence_id) {
  using namespace rmw_openrtdds_cpp;
  if ((ros_request == nullptr) || (sequence_id == nullptr)) {
    RMW_SET_ERROR_MSG("request and sequence output must be non-null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ClientData* data = nullptr;
  if (!valid_client(client, data)) {
    return invalid_handle("client",
                          client == nullptr ? nullptr
                                            : client->implementation_identifier);
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  OutstandingRequest* free_slot = nullptr;
  for (auto& request : data->outstanding) {
    if (!request.occupied) {
      free_slot = &request;
      break;
    }
  }
  if (free_slot == nullptr) {
    RMW_SET_ERROR_MSG("client outstanding-request table is full");
    return RMW_RET_ERROR;
  }
  if (data->next_sequence <= 0 ||
      data->next_sequence == std::numeric_limits<std::int64_t>::max()) {
    RMW_SET_ERROR_MSG("client request sequence space is exhausted");
    return RMW_RET_ERROR;
  }
  rmw_request_id_t request_id{};
  std::memcpy(request_id.writer_guid, data->gid.data, RMW_GID_STORAGE_SIZE);
  request_id.sequence_number = data->next_sequence;
  const rmw_ret_t result = detail::publish_service_sample(
      data->request_publisher, request_id, ros_request);
  if (result != RMW_RET_OK) {
    return result;
  }
  free_slot->sequence = request_id.sequence_number;
  free_slot->occupied = true;
  *sequence_id = request_id.sequence_number;
  ++data->next_sequence;
  return RMW_RET_OK;
}

rmw_ret_t rmw_take_response(const rmw_client_t* client,
                            rmw_service_info_t* response_header,
                            void* ros_response, bool* taken) {
  using namespace rmw_openrtdds_cpp;
  if ((response_header == nullptr) || (ros_response == nullptr) ||
      (taken == nullptr)) {
    RMW_SET_ERROR_MSG("response take requires non-null outputs");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ClientData* data = nullptr;
  if (!valid_client(client, data)) {
    return invalid_handle("client",
                          client == nullptr ? nullptr
                                            : client->implementation_identifier);
  }
  std::lock_guard<std::mutex> lock(data->mutex);
  *taken = false;
  for (;;) {
    rmw_service_info_t candidate{};
    bool candidate_taken = false;
    ClientResponseFilter filter{data, nullptr};
    const rmw_ret_t result = detail::take_service_sample(
        data->response_subscription, &candidate, ros_response,
        &candidate_taken, accept_client_response, &filter);
    if ((result != RMW_RET_OK) || !candidate_taken) {
      if ((result == RMW_RET_OK) &&
          detail::subscription_wait_ready(
              data->response_subscription->data)) {
        continue;
      }
      return result;
    }
    if (filter.match == nullptr) {
      continue;
    }
    filter.match->occupied = false;
    *response_header = candidate;
    *taken = true;
    return RMW_RET_OK;
  }
}

rmw_ret_t rmw_client_request_publisher_get_actual_qos(
    const rmw_client_t* client, rmw_qos_profile_t* qos) {
  using namespace rmw_openrtdds_cpp;
  ClientData* data = nullptr;
  if ((qos == nullptr) || !valid_client(client, data)) {
    RMW_SET_ERROR_MSG("invalid client QoS query");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return rmw_publisher_get_actual_qos(data->request_publisher, qos);
}

rmw_ret_t rmw_client_response_subscription_get_actual_qos(
    const rmw_client_t* client, rmw_qos_profile_t* qos) {
  using namespace rmw_openrtdds_cpp;
  ClientData* data = nullptr;
  if ((qos == nullptr) || !valid_client(client, data)) {
    RMW_SET_ERROR_MSG("invalid client QoS query");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return rmw_subscription_get_actual_qos(data->response_subscription, qos);
}

rmw_service_t* rmw_create_service(
    const rmw_node_t* node,
    const rosidl_service_type_support_t* type_support,
    const char* service_name,
    const rmw_qos_profile_t* qos_profile) {
  using namespace rmw_openrtdds_cpp;
  if (!valid_node(node) || node->context->impl->shutdown.load() ||
      (type_support == nullptr) ||
      (service_name == nullptr) || (service_name[0] == '\0') ||
      (qos_profile == nullptr)) {
    RMW_SET_ERROR_MSG("service creation requires valid non-null arguments");
    return nullptr;
  }
  ServiceTypes types{};
  if (!resolve_service_types(type_support, types)) {
    RMW_SET_ERROR_MSG("service requires bounded C or C++ introspection types");
    return nullptr;
  }
  auto& allocator = node->context->options.allocator;
  auto* service = static_cast<rmw_service_t*>(allocator.zero_allocate(
      1U, sizeof(rmw_service_t), allocator.state));
  void* storage = allocator.zero_allocate(1U, sizeof(ServiceData),
                                          allocator.state);
  auto* data = storage == nullptr ? nullptr : new (storage) ServiceData{};
  char* name = rcutils_strdup(service_name, allocator);
  if ((service == nullptr) || (data == nullptr) || (name == nullptr)) {
    if (name != nullptr) allocator.deallocate(name, allocator.state);
    if (data != nullptr) {
      data->~ServiceData();
      allocator.deallocate(data, allocator.state);
    }
    if (service != nullptr) allocator.deallocate(service, allocator.state);
    RMW_SET_ERROR_MSG("unable to allocate bounded service state");
    return nullptr;
  }
  const auto subscription_options = rmw_get_default_subscription_options();
  const auto publisher_options = rmw_get_default_publisher_options();
  data->node = node;
  data->request_subscription = detail::create_subscription_endpoint(
      node, types.request, service_name, qos_profile,
      &subscription_options, RosChannel::service_request, false);
  if (data->request_subscription != nullptr) {
    data->response_publisher = detail::create_publisher_endpoint(
        node, types.response, service_name, qos_profile, &publisher_options,
        RosChannel::service_response, false);
  }
  if ((data->request_subscription == nullptr) ||
      (data->response_publisher == nullptr)) {
    if (data->response_publisher != nullptr) {
      const rmw_ret_t cleanup_result = rmw_destroy_publisher(
          const_cast<rmw_node_t*>(node), data->response_publisher);
      static_cast<void>(cleanup_result);
    }
    if (data->request_subscription != nullptr) {
      const rmw_ret_t cleanup_result = rmw_destroy_subscription(
          const_cast<rmw_node_t*>(node), data->request_subscription);
      static_cast<void>(cleanup_result);
    }
    allocator.deallocate(name, allocator.state);
    data->~ServiceData();
    allocator.deallocate(data, allocator.state);
    allocator.deallocate(service, allocator.state);
    RMW_SET_ERROR_MSG("unable to create bounded service endpoints");
    return nullptr;
  }
  service->implementation_identifier =
      openrtdds::rmw::kImplementationIdentifier;
  service->data = data;
  service->service_name = name;
  return service;
}

rmw_ret_t rmw_destroy_service(rmw_node_t* node, rmw_service_t* service) {
  using namespace rmw_openrtdds_cpp;
  ServiceData* data = nullptr;
  if (!valid_service(service, data)) {
    return invalid_handle("service",
                          service == nullptr ? nullptr
                                             : service->implementation_identifier);
  }
  if ((node == nullptr) || (data->node != node)) {
    RMW_SET_ERROR_MSG("service does not belong to the supplied node");
    return RMW_RET_INVALID_ARGUMENT;
  }
  auto& allocator = node->context->options.allocator;
  const rmw_ret_t publisher_result =
      rmw_destroy_publisher(node, data->response_publisher);
  const rmw_ret_t subscription_result =
      rmw_destroy_subscription(node, data->request_subscription);
  allocator.deallocate(const_cast<char*>(service->service_name),
                       allocator.state);
  data->~ServiceData();
  allocator.deallocate(data, allocator.state);
  allocator.deallocate(service, allocator.state);
  return (publisher_result == RMW_RET_OK) &&
                 (subscription_result == RMW_RET_OK)
             ? RMW_RET_OK
             : RMW_RET_ERROR;
}

rmw_ret_t rmw_take_request(const rmw_service_t* service,
                           rmw_service_info_t* request_header,
                           void* ros_request, bool* taken) {
  using namespace rmw_openrtdds_cpp;
  ServiceData* data = nullptr;
  if (!valid_service(service, data)) {
    return invalid_handle("service",
                          service == nullptr ? nullptr
                                             : service->implementation_identifier);
  }
  return detail::take_service_sample(
      data->request_subscription, request_header, ros_request, taken);
}

rmw_ret_t rmw_send_response(const rmw_service_t* service,
                            rmw_request_id_t* request_header,
                            void* ros_response) {
  using namespace rmw_openrtdds_cpp;
  if ((request_header == nullptr) || (ros_response == nullptr)) {
    RMW_SET_ERROR_MSG("response and request identity must be non-null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  ServiceData* data = nullptr;
  if (!valid_service(service, data)) {
    return invalid_handle("service",
                          service == nullptr ? nullptr
                                             : service->implementation_identifier);
  }
  return detail::publish_service_sample(
      data->response_publisher, *request_header, ros_response);
}

rmw_ret_t rmw_service_response_publisher_get_actual_qos(
    const rmw_service_t* service, rmw_qos_profile_t* qos) {
  using namespace rmw_openrtdds_cpp;
  ServiceData* data = nullptr;
  if ((qos == nullptr) || !valid_service(service, data)) {
    RMW_SET_ERROR_MSG("invalid service QoS query");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return rmw_publisher_get_actual_qos(data->response_publisher, qos);
}

rmw_ret_t rmw_service_request_subscription_get_actual_qos(
    const rmw_service_t* service, rmw_qos_profile_t* qos) {
  using namespace rmw_openrtdds_cpp;
  ServiceData* data = nullptr;
  if ((qos == nullptr) || !valid_service(service, data)) {
    RMW_SET_ERROR_MSG("invalid service QoS query");
    return RMW_RET_INVALID_ARGUMENT;
  }
  return rmw_subscription_get_actual_qos(data->request_subscription, qos);
}

rmw_ret_t rmw_get_gid_for_client(const rmw_client_t* client,
                                 rmw_gid_t* gid) {
  using namespace rmw_openrtdds_cpp;
  ClientData* data = nullptr;
  if ((gid == nullptr) || !valid_client(client, data)) {
    RMW_SET_ERROR_MSG("invalid client GID query");
    return RMW_RET_INVALID_ARGUMENT;
  }
  *gid = data->gid;
  return RMW_RET_OK;
}

rmw_ret_t rmw_service_server_is_available(const rmw_node_t* node,
                                          const rmw_client_t* client,
                                          bool* is_available) {
  using namespace rmw_openrtdds_cpp;
  ClientData* data = nullptr;
  if ((is_available == nullptr) || !valid_client(client, data) ||
      (node == nullptr) || (data->node->context != node->context)) {
    RMW_SET_ERROR_MSG("invalid service availability query");
    return RMW_RET_INVALID_ARGUMENT;
  }
  std::size_t request_readers = 0U;
  std::size_t response_writers = 0U;
  const rmw_ret_t request_result = rmw_publisher_count_matched_subscriptions(
      data->request_publisher, &request_readers);
  const rmw_ret_t response_result = rmw_subscription_count_matched_publishers(
      data->response_subscription, &response_writers);
  if ((request_result != RMW_RET_OK) || (response_result != RMW_RET_OK)) {
    return RMW_RET_ERROR;
  }
  *is_available = (request_readers != 0U) && (response_writers != 0U);
  return RMW_RET_OK;
}

}  // extern "C"
