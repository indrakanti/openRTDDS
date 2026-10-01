#include <cstring>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/features.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/rmw.h"

// Verifies: ORT-RMW-029, ORT-RMW-030, ORT-RMW-031, ORT-RMW-032
// Verifies: ORT-RMW-033
namespace {

int fail(const int code) {
  rmw_reset_error();
  return code;
}

}  // namespace

int main() {
  if (std::strcmp(rmw_get_implementation_identifier(),
                  "rmw_openrtdds_cpp") != 0) {
    return fail(1);
  }
  if (std::strcmp(rmw_get_serialization_format(), "cdr") != 0) {
    return fail(2);
  }
  if (rmw_feature_supported(
          RMW_FEATURE_MESSAGE_INFO_PUBLICATION_SEQUENCE_NUMBER)) {
    return fail(3);
  }

  const rcutils_allocator_t allocator = rcutils_get_default_allocator();
  rmw_init_options_t options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return fail(4);
  }
  options.instance_id = 24U;
  options.domain_id = 43U;
  options.enclave = rcutils_strdup("/openrtdds_test", allocator);
  if (options.enclave == nullptr) {
    static_cast<void>(rmw_init_options_fini(&options));
    return fail(5);
  }

  rmw_init_options_t options_copy = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_copy(&options, &options_copy) != RMW_RET_OK) {
    static_cast<void>(rmw_init_options_fini(&options));
    return fail(6);
  }
  if ((options_copy.enclave == options.enclave) ||
      (std::strcmp(options_copy.enclave, options.enclave) != 0)) {
    static_cast<void>(rmw_init_options_fini(&options_copy));
    static_cast<void>(rmw_init_options_fini(&options));
    return fail(7);
  }

  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    static_cast<void>(rmw_init_options_fini(&options_copy));
    static_cast<void>(rmw_init_options_fini(&options));
    return fail(8);
  }
  if ((context.actual_domain_id != 43U) ||
      (context.instance_id != options.instance_id)) {
    return fail(9);
  }

  rmw_node_t* node = rmw_create_node(&context, "bounded_node", "/openrtdds");
  if (node == nullptr) {
    return fail(10);
  }
  if (rmw_node_get_graph_guard_condition(node) == nullptr) {
    return fail(11);
  }

  rmw_guard_condition_t* guard = rmw_create_guard_condition(&context);
  if (guard == nullptr) {
    return fail(12);
  }
  if (rmw_trigger_guard_condition(guard) != RMW_RET_OK) {
    return fail(13);
  }
  if (rmw_context_fini(&context) != RMW_RET_INVALID_ARGUMENT) {
    return fail(14);
  }
  rmw_reset_error();
  if (rmw_shutdown(&context) != RMW_RET_OK) {
    return fail(15);
  }
  if (rmw_shutdown(&context) != RMW_RET_OK) {
    return fail(16);
  }
  if (rmw_trigger_guard_condition(guard) != RMW_RET_ERROR) {
    return fail(17);
  }
  rmw_reset_error();
  if (rmw_context_fini(&context) != RMW_RET_INVALID_ARGUMENT) {
    return fail(18);
  }
  rmw_reset_error();
  if (rmw_destroy_guard_condition(guard) != RMW_RET_OK) {
    return fail(19);
  }
  if (rmw_destroy_node(node) != RMW_RET_OK) {
    return fail(20);
  }
  if (rmw_context_fini(&context) != RMW_RET_OK) {
    return fail(21);
  }
  if (context.implementation_identifier != nullptr) {
    return fail(22);
  }

  if (rmw_init_options_fini(&options_copy) != RMW_RET_OK) {
    return fail(23);
  }
  if (rmw_init_options_fini(&options) != RMW_RET_OK) {
    return fail(24);
  }
  return 0;
}
