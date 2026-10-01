#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "openrtdds/rmw/foundation.hpp"

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/discovery_options.h"
#include "rmw/error_handling.h"
#include "rmw/features.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/rmw.h"
#include "rmw/validate_namespace.h"
#include "rmw/validate_node_name.h"

namespace {

// Requirements: ORT-RMW-030, ORT-RMW-031, ORT-RMW-032, ORT-RMW-033
constexpr std::size_t kNodeCapacity = 8U;
constexpr std::size_t kGuardConditionCapacity = 16U;
using Foundation =
    openrtdds::rmw::AdapterContext<kNodeCapacity, kGuardConditionCapacity>;

struct NodeData final {
  openrtdds::rmw::NodeHandle handle{};
};

struct GuardConditionData final {
  openrtdds::rmw::GuardConditionHandle handle{};
};

[[nodiscard]] bool identifier_matches(const char* identifier) noexcept {
  return (identifier != nullptr) &&
         (std::strcmp(identifier,
                      openrtdds::rmw::kImplementationIdentifier) == 0);
}

rmw_ret_t set_adapter_error(const openrtdds::rmw::AdapterError error) noexcept {
  RMW_SET_ERROR_MSG_WITH_FORMAT_STRING("OpenRTDDS adapter error: %s",
                                       openrtdds::rmw::to_string(error));
  switch (error) {
    case openrtdds::rmw::AdapterError::none:
      return RMW_RET_OK;
    case openrtdds::rmw::AdapterError::incorrect_implementation:
      return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
    case openrtdds::rmw::AdapterError::resource_exhausted:
      return RMW_RET_BAD_ALLOC;
    case openrtdds::rmw::AdapterError::unsupported:
      return RMW_RET_UNSUPPORTED;
    case openrtdds::rmw::AdapterError::invalid_argument:
    case openrtdds::rmw::AdapterError::invalid_limits:
    case openrtdds::rmw::AdapterError::invalid_domain:
    case openrtdds::rmw::AdapterError::name_too_long:
      return RMW_RET_INVALID_ARGUMENT;
    case openrtdds::rmw::AdapterError::invalid_state:
    case openrtdds::rmw::AdapterError::stale_handle:
      return RMW_RET_ERROR;
  }
  return RMW_RET_ERROR;
}

[[nodiscard]] bool context_is_valid(const rmw_context_t* context) noexcept {
  return (context != nullptr) && identifier_matches(context->implementation_identifier) &&
         (context->impl != nullptr);
}

rmw_guard_condition_t* create_guard_condition_handle(
    rmw_context_t* context) noexcept;

rmw_ret_t destroy_guard_condition_handle(
    rmw_guard_condition_t* guard_condition) noexcept;

}  // namespace

// This is the implementation-owned definition promised by rmw/init.h.
struct rmw_context_impl_s final {
  Foundation foundation{};
  rmw_guard_condition_t* graph_guard_condition{nullptr};
  bool shutdown{false};
};

namespace {

rmw_guard_condition_t* create_guard_condition_handle(
    rmw_context_t* context) noexcept {
  if (!context_is_valid(context)) {
    RMW_SET_ERROR_MSG("expected initialized OpenRTDDS context");
    return nullptr;
  }
  if (context->impl->shutdown) {
    RMW_SET_ERROR_MSG("context has been shutdown");
    return nullptr;
  }

  auto& allocator = context->options.allocator;
  auto* guard_condition = static_cast<rmw_guard_condition_t*>(
      allocator.zero_allocate(1U, sizeof(rmw_guard_condition_t),
                              allocator.state));
  if (guard_condition == nullptr) {
    RMW_SET_ERROR_MSG("unable to allocate rmw_guard_condition_t");
    return nullptr;
  }
  void* data_storage =
      allocator.zero_allocate(1U, sizeof(GuardConditionData), allocator.state);
  if (data_storage == nullptr) {
    allocator.deallocate(guard_condition, allocator.state);
    RMW_SET_ERROR_MSG("unable to allocate OpenRTDDS guard-condition data");
    return nullptr;
  }
  auto* data = new (data_storage) GuardConditionData{};

  const auto error = context->impl->foundation.create_guard_condition(data->handle);
  if (error != openrtdds::rmw::AdapterError::none) {
    data->~GuardConditionData();
    allocator.deallocate(data, allocator.state);
    allocator.deallocate(guard_condition, allocator.state);
    static_cast<void>(set_adapter_error(error));
    return nullptr;
  }

  guard_condition->implementation_identifier =
      openrtdds::rmw::kImplementationIdentifier;
  guard_condition->data = data;
  guard_condition->context = context;
  return guard_condition;
}

rmw_ret_t destroy_guard_condition_handle(
    rmw_guard_condition_t* guard_condition) noexcept {
  if ((guard_condition == nullptr) || (guard_condition->context == nullptr) ||
      (guard_condition->data == nullptr)) {
    RMW_SET_ERROR_MSG("invalid guard condition");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!identifier_matches(guard_condition->implementation_identifier)) {
    RMW_SET_ERROR_MSG("guard condition belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  rmw_context_t* context = guard_condition->context;
  if (!context_is_valid(context)) {
    RMW_SET_ERROR_MSG("guard condition has an invalid context");
    return RMW_RET_INVALID_ARGUMENT;
  }

  auto* data = static_cast<GuardConditionData*>(guard_condition->data);
  const auto error = context->impl->foundation.destroy_guard_condition(data->handle);
  if (error != openrtdds::rmw::AdapterError::none) {
    return set_adapter_error(error);
  }

  auto& allocator = context->options.allocator;
  data->~GuardConditionData();
  allocator.deallocate(data, allocator.state);
  allocator.deallocate(guard_condition, allocator.state);
  return RMW_RET_OK;
}

}  // namespace

extern "C" {

// Requirements: ORT-RMW-029, ORT-RMW-033
const char* rmw_get_implementation_identifier(void) {
  return openrtdds::rmw::kImplementationIdentifier;
}

const char* rmw_get_serialization_format(void) { return "cdr"; }

bool rmw_feature_supported(const rmw_feature_t feature) {
  static_cast<void>(feature);
  return false;
}

// Requirements: ORT-RMW-030, ORT-RMW-033
rmw_ret_t rmw_init_options_init(rmw_init_options_t* init_options,
                                rcutils_allocator_t allocator) {
  if (init_options == nullptr) {
    RMW_SET_ERROR_MSG("init_options is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rcutils_allocator_is_valid(&allocator)) {
    RMW_SET_ERROR_MSG("allocator is invalid");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (init_options->implementation_identifier != nullptr) {
    RMW_SET_ERROR_MSG("expected zero-initialized init_options");
    return RMW_RET_INVALID_ARGUMENT;
  }

  rmw_init_options_t value = rmw_get_zero_initialized_init_options();
  value.implementation_identifier = openrtdds::rmw::kImplementationIdentifier;
  value.domain_id = RMW_DEFAULT_DOMAIN_ID;
  value.security_options = rmw_get_default_security_options();
  value.localhost_only = RMW_LOCALHOST_ONLY_DEFAULT;
  value.discovery_options = rmw_get_zero_initialized_discovery_options();
  value.allocator = allocator;
  const rmw_ret_t result =
      rmw_discovery_options_init(&value.discovery_options, 0U, &allocator);
  if (result != RMW_RET_OK) {
    return result;
  }
  *init_options = value;
  return RMW_RET_OK;
}

rmw_ret_t rmw_init_options_copy(const rmw_init_options_t* source,
                                rmw_init_options_t* destination) {
  if ((source == nullptr) || (destination == nullptr)) {
    RMW_SET_ERROR_MSG("source or destination init options is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (source->implementation_identifier == nullptr) {
    RMW_SET_ERROR_MSG("source init options is not initialized");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!identifier_matches(source->implementation_identifier)) {
    RMW_SET_ERROR_MSG("source init options belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if (destination->implementation_identifier != nullptr) {
    RMW_SET_ERROR_MSG("expected zero-initialized destination init options");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!rcutils_allocator_is_valid(&source->allocator)) {
    RMW_SET_ERROR_MSG("source allocator is invalid");
    return RMW_RET_INVALID_ARGUMENT;
  }

  auto allocator = source->allocator;
  rmw_init_options_t value = rmw_get_zero_initialized_init_options();
  value.instance_id = source->instance_id;
  value.implementation_identifier = openrtdds::rmw::kImplementationIdentifier;
  value.domain_id = source->domain_id;
  value.localhost_only = source->localhost_only;
  value.allocator = allocator;
  value.security_options = rmw_get_zero_initialized_security_options();
  rmw_ret_t result = rmw_security_options_copy(
      &source->security_options, &allocator, &value.security_options);
  if (result != RMW_RET_OK) {
    return result;
  }

  value.discovery_options = rmw_get_zero_initialized_discovery_options();
  result = rmw_discovery_options_copy(&source->discovery_options, &allocator,
                                      &value.discovery_options);
  if (result != RMW_RET_OK) {
    [[maybe_unused]] const rmw_ret_t security_cleanup_result =
        rmw_security_options_fini(&value.security_options, &allocator);
    return result;
  }

  if (source->enclave != nullptr) {
    value.enclave = rcutils_strdup(source->enclave, allocator);
    if (value.enclave == nullptr) {
      [[maybe_unused]] const rmw_ret_t discovery_cleanup_result =
          rmw_discovery_options_fini(&value.discovery_options);
      [[maybe_unused]] const rmw_ret_t security_cleanup_result =
          rmw_security_options_fini(&value.security_options, &allocator);
      RMW_SET_ERROR_MSG("unable to copy enclave");
      return RMW_RET_BAD_ALLOC;
    }
  }
  *destination = value;
  return RMW_RET_OK;
}

rmw_ret_t rmw_init_options_fini(rmw_init_options_t* init_options) {
  if ((init_options == nullptr) ||
      (init_options->implementation_identifier == nullptr)) {
    RMW_SET_ERROR_MSG("expected initialized init options");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!identifier_matches(init_options->implementation_identifier)) {
    RMW_SET_ERROR_MSG("init options belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  auto allocator = init_options->allocator;
  if (!rcutils_allocator_is_valid(&allocator)) {
    RMW_SET_ERROR_MSG("init-options allocator is invalid");
    return RMW_RET_INVALID_ARGUMENT;
  }

  if (init_options->enclave != nullptr) {
    allocator.deallocate(init_options->enclave, allocator.state);
  }
  rmw_ret_t result =
      rmw_security_options_fini(&init_options->security_options, &allocator);
  const rmw_ret_t discovery_result =
      rmw_discovery_options_fini(&init_options->discovery_options);
  *init_options = rmw_get_zero_initialized_init_options();
  if (result == RMW_RET_OK) {
    result = discovery_result;
  }
  return result;
}

rmw_ret_t rmw_init(const rmw_init_options_t* options, rmw_context_t* context) {
  if ((options == nullptr) || (context == nullptr)) {
    RMW_SET_ERROR_MSG("options or context is null");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (options->implementation_identifier == nullptr) {
    RMW_SET_ERROR_MSG("expected initialized init options");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!identifier_matches(options->implementation_identifier)) {
    RMW_SET_ERROR_MSG("init options belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  if (options->enclave == nullptr) {
    RMW_SET_ERROR_MSG("expected non-null enclave");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (context->implementation_identifier != nullptr) {
    RMW_SET_ERROR_MSG("expected a zero-initialized context");
    return RMW_RET_INVALID_ARGUMENT;
  }

  const std::size_t domain = options->domain_id == RMW_DEFAULT_DOMAIN_ID
                                 ? 0U
                                 : options->domain_id;
  if (domain > openrtdds::rmw::kMaxPortableDomainId) {
    return set_adapter_error(openrtdds::rmw::AdapterError::invalid_domain);
  }

  rmw_context_t value = rmw_get_zero_initialized_context();
  value.instance_id = options->instance_id;
  value.implementation_identifier = openrtdds::rmw::kImplementationIdentifier;
  value.actual_domain_id = domain;
  rmw_ret_t result = rmw_init_options_copy(options, &value.options);
  if (result != RMW_RET_OK) {
    return result;
  }

  const auto allocator = options->allocator;
  void* storage = allocator.zero_allocate(1U, sizeof(rmw_context_impl_t),
                                           allocator.state);
  if (storage == nullptr) {
    [[maybe_unused]] const rmw_ret_t cleanup_result =
        rmw_init_options_fini(&value.options);
    RMW_SET_ERROR_MSG("unable to allocate OpenRTDDS context implementation");
    return RMW_RET_BAD_ALLOC;
  }
  value.impl = new (storage) rmw_context_impl_t{};

  openrtdds::rmw::AdapterConfig config{};
  config.limits = {kNodeCapacity, kGuardConditionCapacity};
  config.domain_id = static_cast<std::uint32_t>(domain);
  const auto foundation_result = value.impl->foundation.initialize(config);
  if (foundation_result != openrtdds::rmw::AdapterError::none) {
    value.impl->~rmw_context_impl_t();
    allocator.deallocate(value.impl, allocator.state);
    [[maybe_unused]] const rmw_ret_t cleanup_result =
        rmw_init_options_fini(&value.options);
    return set_adapter_error(foundation_result);
  }

  *context = value;
  context->impl->graph_guard_condition = create_guard_condition_handle(context);
  if (context->impl->graph_guard_condition == nullptr) {
    static_cast<void>(context->impl->foundation.shutdown());
    static_cast<void>(context->impl->foundation.finalize());
    context->impl->~rmw_context_impl_t();
    allocator.deallocate(context->impl, allocator.state);
    [[maybe_unused]] const rmw_ret_t cleanup_result =
        rmw_init_options_fini(&context->options);
    *context = rmw_get_zero_initialized_context();
    return RMW_RET_BAD_ALLOC;
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_shutdown(rmw_context_t* context) {
  if (!context_is_valid(context)) {
    RMW_SET_ERROR_MSG("expected initialized OpenRTDDS context");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (context->impl->shutdown) {
    return RMW_RET_OK;
  }

  const auto error = context->impl->foundation.shutdown();
  if (error != openrtdds::rmw::AdapterError::none) {
    return set_adapter_error(error);
  }
  context->impl->shutdown = true;
  rmw_guard_condition_t* graph_guard = context->impl->graph_guard_condition;
  context->impl->graph_guard_condition = nullptr;
  if (graph_guard != nullptr) {
    return destroy_guard_condition_handle(graph_guard);
  }
  return RMW_RET_OK;
}

rmw_ret_t rmw_context_fini(rmw_context_t* context) {
  if (!context_is_valid(context)) {
    RMW_SET_ERROR_MSG("expected initialized OpenRTDDS context");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!context->impl->shutdown) {
    RMW_SET_ERROR_MSG("context has not been shutdown");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const auto foundation_result = context->impl->foundation.finalize();
  if (foundation_result != openrtdds::rmw::AdapterError::none) {
    RMW_SET_ERROR_MSG("context still owns nodes or guard conditions");
    return RMW_RET_INVALID_ARGUMENT;
  }

  const auto allocator = context->options.allocator;
  context->impl->~rmw_context_impl_t();
  allocator.deallocate(context->impl, allocator.state);
  const rmw_ret_t result = rmw_init_options_fini(&context->options);
  *context = rmw_get_zero_initialized_context();
  return result;
}

// Requirements: ORT-RMW-031, ORT-RMW-033
rmw_node_t* rmw_create_node(rmw_context_t* context, const char* name,
                            const char* namespace_) {
  if (!context_is_valid(context)) {
    RMW_SET_ERROR_MSG("expected initialized OpenRTDDS context");
    return nullptr;
  }
  if (context->impl->shutdown) {
    RMW_SET_ERROR_MSG("context has been shutdown");
    return nullptr;
  }

  int validation = RMW_NODE_NAME_VALID;
  rmw_ret_t result = rmw_validate_node_name(name, &validation, nullptr);
  if (result != RMW_RET_OK) {
    return nullptr;
  }
  if (validation != RMW_NODE_NAME_VALID) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "invalid node name: %s",
        rmw_node_name_validation_result_string(validation));
    return nullptr;
  }
  validation = RMW_NAMESPACE_VALID;
  result = rmw_validate_namespace(namespace_, &validation, nullptr);
  if (result != RMW_RET_OK) {
    return nullptr;
  }
  if (validation != RMW_NAMESPACE_VALID) {
    RMW_SET_ERROR_MSG_WITH_FORMAT_STRING(
        "invalid node namespace: %s",
        rmw_namespace_validation_result_string(validation));
    return nullptr;
  }

  auto& allocator = context->options.allocator;
  auto* node = static_cast<rmw_node_t*>(
      allocator.zero_allocate(1U, sizeof(rmw_node_t), allocator.state));
  void* data_storage =
      allocator.zero_allocate(1U, sizeof(NodeData), allocator.state);
  if ((node == nullptr) || (data_storage == nullptr)) {
    if (data_storage != nullptr) {
      allocator.deallocate(data_storage, allocator.state);
    }
    if (node != nullptr) {
      allocator.deallocate(node, allocator.state);
    }
    RMW_SET_ERROR_MSG("unable to allocate OpenRTDDS node handle");
    return nullptr;
  }
  auto* data = new (data_storage) NodeData{};

  char* name_copy = rcutils_strdup(name, allocator);
  char* namespace_copy = rcutils_strdup(namespace_, allocator);
  if ((name_copy == nullptr) || (namespace_copy == nullptr)) {
    if (namespace_copy != nullptr) {
      allocator.deallocate(namespace_copy, allocator.state);
    }
    if (name_copy != nullptr) {
      allocator.deallocate(name_copy, allocator.state);
    }
    data->~NodeData();
    allocator.deallocate(data, allocator.state);
    allocator.deallocate(node, allocator.state);
    RMW_SET_ERROR_MSG("unable to copy node name or namespace");
    return nullptr;
  }

  const auto error =
      context->impl->foundation.create_node(name, namespace_, data->handle);
  if (error != openrtdds::rmw::AdapterError::none) {
    allocator.deallocate(namespace_copy, allocator.state);
    allocator.deallocate(name_copy, allocator.state);
    data->~NodeData();
    allocator.deallocate(data, allocator.state);
    allocator.deallocate(node, allocator.state);
    static_cast<void>(set_adapter_error(error));
    return nullptr;
  }

  node->implementation_identifier = openrtdds::rmw::kImplementationIdentifier;
  node->data = data;
  node->name = name_copy;
  node->namespace_ = namespace_copy;
  node->context = context;
  return node;
}

rmw_ret_t rmw_destroy_node(rmw_node_t* node) {
  if ((node == nullptr) || (node->context == nullptr) || (node->data == nullptr)) {
    RMW_SET_ERROR_MSG("invalid node handle");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!identifier_matches(node->implementation_identifier)) {
    RMW_SET_ERROR_MSG("node belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  rmw_context_t* context = node->context;
  if (!context_is_valid(context)) {
    RMW_SET_ERROR_MSG("node has an invalid context");
    return RMW_RET_INVALID_ARGUMENT;
  }

  auto* data = static_cast<NodeData*>(node->data);
  const auto error = context->impl->foundation.destroy_node(data->handle);
  if (error != openrtdds::rmw::AdapterError::none) {
    return set_adapter_error(error);
  }

  auto& allocator = context->options.allocator;
  allocator.deallocate(const_cast<char*>(node->namespace_), allocator.state);
  allocator.deallocate(const_cast<char*>(node->name), allocator.state);
  data->~NodeData();
  allocator.deallocate(data, allocator.state);
  allocator.deallocate(node, allocator.state);
  return RMW_RET_OK;
}

const rmw_guard_condition_t* rmw_node_get_graph_guard_condition(
    const rmw_node_t* node) {
  if ((node == nullptr) || !identifier_matches(node->implementation_identifier) ||
      (node->context == nullptr) || (node->context->impl == nullptr)) {
    RMW_SET_ERROR_MSG("invalid OpenRTDDS node");
    return nullptr;
  }
  return node->context->impl->graph_guard_condition;
}

// Requirements: ORT-RMW-032, ORT-RMW-033
rmw_guard_condition_t* rmw_create_guard_condition(rmw_context_t* context) {
  return create_guard_condition_handle(context);
}

rmw_ret_t rmw_destroy_guard_condition(
    rmw_guard_condition_t* guard_condition) {
  return destroy_guard_condition_handle(guard_condition);
}

rmw_ret_t rmw_trigger_guard_condition(
    const rmw_guard_condition_t* guard_condition) {
  if ((guard_condition == nullptr) || (guard_condition->context == nullptr) ||
      (guard_condition->data == nullptr)) {
    RMW_SET_ERROR_MSG("invalid guard condition");
    return RMW_RET_INVALID_ARGUMENT;
  }
  if (!identifier_matches(guard_condition->implementation_identifier)) {
    RMW_SET_ERROR_MSG("guard condition belongs to another RMW implementation");
    return RMW_RET_INCORRECT_RMW_IMPLEMENTATION;
  }
  const rmw_context_t* context = guard_condition->context;
  if (!context_is_valid(context)) {
    RMW_SET_ERROR_MSG("guard condition has an invalid context");
    return RMW_RET_INVALID_ARGUMENT;
  }
  const auto* data =
      static_cast<const GuardConditionData*>(guard_condition->data);
  const auto error =
      context->impl->foundation.trigger_guard_condition(data->handle);
  return error == openrtdds::rmw::AdapterError::none
             ? RMW_RET_OK
             : set_adapter_error(error);
}

}  // extern "C"
