#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/features.h"
#include "rmw/get_network_flow_endpoints.h"
#include "rmw/get_node_info_and_types.h"
#include "rmw/get_service_names_and_types.h"
#include "rmw/get_topic_endpoint_info.h"
#include "rmw/get_topic_names_and_types.h"
#include "rmw/names_and_types.h"
#include "rmw/rmw.h"

// Requirements: ORT-RMW-034, ORT-RMW-035

extern "C" {

#define OPENRTDDS_EXPAND(value) value
#define ARG_TYPES(...) __VA_ARGS__

#define OPENRTDDS_ARGS_0(...) __VA_ARGS__
#define OPENRTDDS_ARGS_1(t1) t1
#define OPENRTDDS_ARGS_2(t2, ...) \
  t2, OPENRTDDS_EXPAND(OPENRTDDS_ARGS_1(__VA_ARGS__))
#define OPENRTDDS_ARGS_3(t3, ...) \
  t3, OPENRTDDS_EXPAND(OPENRTDDS_ARGS_2(__VA_ARGS__))
#define OPENRTDDS_ARGS_4(t4, ...) \
  t4, OPENRTDDS_EXPAND(OPENRTDDS_ARGS_3(__VA_ARGS__))
#define OPENRTDDS_ARGS_5(t5, ...) \
  t5, OPENRTDDS_EXPAND(OPENRTDDS_ARGS_4(__VA_ARGS__))
#define OPENRTDDS_ARGS_6(t6, ...) \
  t6, OPENRTDDS_EXPAND(OPENRTDDS_ARGS_5(__VA_ARGS__))
#define OPENRTDDS_ARGS_7(t7, ...) \
  t7, OPENRTDDS_EXPAND(OPENRTDDS_ARGS_6(__VA_ARGS__))

#define OPENRTDDS_UNSUPPORTED_RMW_FN( \
    name, ReturnType, error_value, count, ...) \
  ReturnType name( \
      OPENRTDDS_EXPAND(OPENRTDDS_ARGS_ ## count(__VA_ARGS__))) { \
    RMW_SET_ERROR_MSG( \
        #name " is unsupported by the current OpenRTDDS profile"); \
    return error_value; \
  }

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_init_publisher_allocation,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    const rosidl_message_type_support_t *,
    const rosidl_runtime_c__Sequence__bound *,
    rmw_publisher_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_fini_publisher_allocation,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  1, ARG_TYPES(rmw_publisher_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_create_publisher,
  rmw_publisher_t *, nullptr,
  5, ARG_TYPES(
    const rmw_node_t *, const rosidl_message_type_support_t *, const char *,
    const rmw_qos_profile_t *, const rmw_publisher_options_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_destroy_publisher,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(rmw_node_t *, rmw_publisher_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_borrow_loaned_message,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    const rmw_publisher_t *,
    const rosidl_message_type_support_t *,
    void **))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_return_loaned_message_from_publisher,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_publisher_t *, void *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_publish,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_publisher_t *, const void *, rmw_publisher_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_publish_loaned_message,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_publisher_t *, void *, rmw_publisher_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_publisher_count_matched_subscriptions,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_publisher_t *, size_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_publisher_get_actual_qos,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_publisher_t *, rmw_qos_profile_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_publisher_event_init,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(rmw_event_t *, const rmw_publisher_t *, rmw_event_type_t))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_publish_serialized_message,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3,
  ARG_TYPES(
    const rmw_publisher_t *, const rmw_serialized_message_t *,
    rmw_publisher_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_serialized_message_size,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    const rosidl_message_type_support_t *,
    const rosidl_runtime_c__Sequence__bound *,
    size_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_publisher_assert_liveliness,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  1, ARG_TYPES(const rmw_publisher_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_publisher_wait_for_all_acked,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_publisher_t *, rmw_time_t))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_serialize,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const void *, const rosidl_message_type_support_t *, rmw_serialized_message_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_deserialize,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_serialized_message_t *, const rosidl_message_type_support_t *, void *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_init_subscription_allocation,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    const rosidl_message_type_support_t *,
    const rosidl_runtime_c__Sequence__bound *,
    rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_fini_subscription_allocation,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  1, ARG_TYPES(rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_create_subscription,
  rmw_subscription_t *, nullptr,
  5, ARG_TYPES(
    const rmw_node_t *, const rosidl_message_type_support_t *, const char *,
    const rmw_qos_profile_t *, const rmw_subscription_options_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_destroy_subscription,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(rmw_node_t *, rmw_subscription_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_subscription_count_matched_publishers,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_subscription_t *, size_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_subscription_get_actual_qos,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_subscription_t *, rmw_qos_profile_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_subscription_event_init,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(rmw_event_t *, const rmw_subscription_t *, rmw_event_type_t))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_subscription_set_content_filter,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(
    rmw_subscription_t *, const rmw_subscription_content_filter_options_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_subscription_get_content_filter,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    const rmw_subscription_t *, rcutils_allocator_t *,
    rmw_subscription_content_filter_options_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  4, ARG_TYPES(const rmw_subscription_t *, void *, bool *, rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_sequence,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  6, ARG_TYPES(
    const rmw_subscription_t *, size_t, rmw_message_sequence_t *,
    rmw_message_info_sequence_t *, size_t *, rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_with_info,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  5,
  ARG_TYPES(
    const rmw_subscription_t *, void *, bool *, rmw_message_info_t *,
    rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_serialized_message,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  4,
  ARG_TYPES(
    const rmw_subscription_t *, rmw_serialized_message_t *, bool *,
    rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_serialized_message_with_info,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  5, ARG_TYPES(
    const rmw_subscription_t *, rmw_serialized_message_t *, bool *, rmw_message_info_t *,
    rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_loaned_message,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  4, ARG_TYPES(
    const rmw_subscription_t *, void **, bool *, rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_loaned_message_with_info,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  5, ARG_TYPES(
    const rmw_subscription_t *, void **, bool *, rmw_message_info_t *,
    rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_return_loaned_message_from_subscription,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_subscription_t *, void *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_create_client,
  rmw_client_t *, nullptr,
  4, ARG_TYPES(
    const rmw_node_t *, const rosidl_service_type_support_t *, const char *,
    const rmw_qos_profile_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_destroy_client,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(rmw_node_t *, rmw_client_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_send_request,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_client_t *, const void *, int64_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_response,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  4, ARG_TYPES(const rmw_client_t *, rmw_service_info_t *, void *, bool *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_client_request_publisher_get_actual_qos,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_client_t *, rmw_qos_profile_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_client_response_subscription_get_actual_qos,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_client_t *, rmw_qos_profile_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_create_service,
  rmw_service_t *, nullptr,
  4, ARG_TYPES(
    const rmw_node_t *, const rosidl_service_type_support_t *, const char *,
    const rmw_qos_profile_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_destroy_service,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(rmw_node_t *, rmw_service_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_request,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  4, ARG_TYPES(const rmw_service_t *, rmw_service_info_t *, void *, bool *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_send_response,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_service_t *, rmw_request_id_t *, void *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_service_response_publisher_get_actual_qos,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_service_t *, rmw_qos_profile_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_service_request_subscription_get_actual_qos,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_service_t *, rmw_qos_profile_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_event,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_event_t *, void *, bool *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_create_wait_set,
  rmw_wait_set_t *, nullptr,
  2, ARG_TYPES(rmw_context_t *, size_t))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_destroy_wait_set,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  1, ARG_TYPES(rmw_wait_set_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_wait,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  7, ARG_TYPES(
    rmw_subscriptions_t *, rmw_guard_conditions_t *, rmw_services_t *, rmw_clients_t *,
    rmw_events_t *, rmw_wait_set_t *, const rmw_time_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_publisher_names_and_types_by_node,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  6, ARG_TYPES(
    const rmw_node_t *, rcutils_allocator_t *, const char *, const char *, bool,
    rmw_names_and_types_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_subscriber_names_and_types_by_node,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  6, ARG_TYPES(
    const rmw_node_t *, rcutils_allocator_t *, const char *, const char *, bool,
    rmw_names_and_types_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_service_names_and_types_by_node,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  5, ARG_TYPES(
    const rmw_node_t *, rcutils_allocator_t *, const char *, const char *,
    rmw_names_and_types_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_client_names_and_types_by_node,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  5, ARG_TYPES(
    const rmw_node_t *, rcutils_allocator_t *, const char *, const char *,
    rmw_names_and_types_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_topic_names_and_types,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  4, ARG_TYPES(
    const rmw_node_t *, rcutils_allocator_t *, bool,
    rmw_names_and_types_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_service_names_and_types,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    const rmw_node_t *, rcutils_allocator_t *,
    rmw_names_and_types_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_node_names,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_node_t *, rcutils_string_array_t *, rcutils_string_array_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_node_names_with_enclaves,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  4, ARG_TYPES(
    const rmw_node_t *, rcutils_string_array_t *,
    rcutils_string_array_t *, rcutils_string_array_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_count_publishers,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_node_t *, const char *, size_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_count_subscribers,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_node_t *, const char *, size_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_count_clients,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_node_t *, const char *, size_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_count_services,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_node_t *, const char *, size_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_gid_for_client,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_client_t *, rmw_gid_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_gid_for_publisher,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  2, ARG_TYPES(const rmw_publisher_t *, rmw_gid_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_compare_gids_equal,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_gid_t *, const rmw_gid_t *, bool *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_service_server_is_available,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(const rmw_node_t *, const rmw_client_t *, bool *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_set_log_severity,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  1, ARG_TYPES(rmw_log_severity_t))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_publishers_info_by_topic,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  5, ARG_TYPES(
    const rmw_node_t *,
    rcutils_allocator_t *,
    const char *,
    bool,
    rmw_topic_endpoint_info_array_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_get_subscriptions_info_by_topic,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  5, ARG_TYPES(
    const rmw_node_t *,
    rcutils_allocator_t *,
    const char *,
    bool,
    rmw_topic_endpoint_info_array_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_qos_profile_check_compatible,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  5, ARG_TYPES(
    const rmw_qos_profile_t,
    const rmw_qos_profile_t,
    rmw_qos_compatibility_type_t *,
    char *,
    size_t))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_publisher_get_network_flow_endpoints,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    const rmw_publisher_t *,
    rcutils_allocator_t *,
    rmw_network_flow_endpoint_array_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_subscription_get_network_flow_endpoints,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    const rmw_subscription_t *,
    rcutils_allocator_t *,
    rmw_network_flow_endpoint_array_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_subscription_set_on_new_message_callback,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    rmw_subscription_t *, rmw_event_callback_t, const void *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_service_set_on_new_request_callback,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    rmw_service_t *, rmw_event_callback_t, const void *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_client_set_on_new_response_callback,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    rmw_client_t *, rmw_event_callback_t, const void *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_event_set_callback,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    rmw_event_t *, rmw_event_callback_t, const void *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_dynamic_message,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  4, ARG_TYPES(
    const rmw_subscription_t *,
    rosidl_dynamic_typesupport_dynamic_data_t *,
    bool *,
    rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_take_dynamic_message_with_info,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  5, ARG_TYPES(
    const rmw_subscription_t *,
    rosidl_dynamic_typesupport_dynamic_data_t *,
    bool *,
    rmw_message_info_t *,
    rmw_subscription_allocation_t *))

OPENRTDDS_UNSUPPORTED_RMW_FN(
  rmw_serialization_support_init,
  rmw_ret_t, RMW_RET_UNSUPPORTED,
  3, ARG_TYPES(
    const char *, rcutils_allocator_t *, rosidl_dynamic_typesupport_serialization_support_t *))

// Capability predicates report false without leaving a stale error.
bool rmw_event_type_is_supported(const rmw_event_type_t event_type) {
  static_cast<void>(event_type);
  return false;
}

#undef OPENRTDDS_UNSUPPORTED_RMW_FN
#undef OPENRTDDS_ARGS_7
#undef OPENRTDDS_ARGS_6
#undef OPENRTDDS_ARGS_5
#undef OPENRTDDS_ARGS_4
#undef OPENRTDDS_ARGS_3
#undef OPENRTDDS_ARGS_2
#undef OPENRTDDS_ARGS_1
#undef OPENRTDDS_ARGS_0
#undef ARG_TYPES
#undef OPENRTDDS_EXPAND

}  // extern "C"
