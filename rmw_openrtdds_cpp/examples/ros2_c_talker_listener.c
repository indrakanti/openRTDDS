#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "rcl/rcl.h"
#include "rmw/qos_profiles.h"
#include "rosidl_runtime_c/message_type_support_struct.h"
#include "std_msgs/msg/detail/u_int32__functions.h"
#include "std_msgs/msg/detail/u_int32__struct.h"
#include "std_msgs/msg/detail/u_int32__type_support.h"

// Demonstrates: ORT-RMW-052, ORT-RMW-053, ORT-RMW-054

static void pause_for_publish_period(void) {
  const struct timespec delay = {0, 20000000L};
  (void)nanosleep(&delay, NULL);
}

static rmw_qos_profile_t qualification_qos(const bool reliable) {
  rmw_qos_profile_t qos = rmw_qos_profile_default;
  qos.history = RMW_QOS_POLICY_HISTORY_KEEP_LAST;
  qos.depth = 4U;
  qos.reliability = reliable ? RMW_QOS_POLICY_RELIABILITY_RELIABLE
                             : RMW_QOS_POLICY_RELIABILITY_BEST_EFFORT;
  qos.durability = RMW_QOS_POLICY_DURABILITY_VOLATILE;
  return qos;
}

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr,
            "usage: ros2_c_talker_listener talker|listener "
            "best_effort|reliable\n");
    return 2;
  }
  const bool talker = strcmp(argv[1], "talker") == 0;
  const bool listener = strcmp(argv[1], "listener") == 0;
  const bool reliable = strcmp(argv[2], "reliable") == 0;
  if ((!talker && !listener) ||
      (!reliable && strcmp(argv[2], "best_effort") != 0)) {
    fprintf(stderr, "invalid role or QoS\n");
    return 2;
  }

  rcl_allocator_t allocator = rcl_get_default_allocator();
  rcl_init_options_t init_options = rcl_get_zero_initialized_init_options();
  rcl_context_t context = rcl_get_zero_initialized_context();
  rcl_node_t node = rcl_get_zero_initialized_node();
  rcl_publisher_t publisher = rcl_get_zero_initialized_publisher();
  rcl_subscription_t subscription = rcl_get_zero_initialized_subscription();
  rcl_wait_set_t wait_set = rcl_get_zero_initialized_wait_set();
  int result = 1;

  if (rcl_init_options_init(&init_options, allocator) != RCL_RET_OK ||
      rcl_init(0, NULL, &init_options, &context) != RCL_RET_OK) {
    fprintf(stderr, "rcl initialization failed\n");
    goto cleanup;
  }
  rcl_node_options_t node_options = rcl_node_get_default_options();
  if (rcl_node_init(&node,
                    talker ? "openrtdds_c_talker" : "openrtdds_c_listener",
                    "/", &context, &node_options) != RCL_RET_OK) {
    fprintf(stderr, "rcl node initialization failed\n");
    goto cleanup;
  }

  const rosidl_message_type_support_t* type_support =
      ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, UInt32);
  const rmw_qos_profile_t qos = qualification_qos(reliable);
  if (talker) {
    rcl_publisher_options_t options = rcl_publisher_get_default_options();
    options.qos = qos;
    if (rcl_publisher_init(&publisher, &node, type_support,
                           "/openrtdds/qualification", &options) !=
        RCL_RET_OK) {
      fprintf(stderr, "rcl publisher initialization failed\n");
      goto cleanup;
    }
    std_msgs__msg__UInt32 message;
    if (!std_msgs__msg__UInt32__init(&message)) {
      goto cleanup;
    }
    message.data = 42U;
    for (;;) {
      if (rcl_publish(&publisher, &message, NULL) != RCL_RET_OK) {
        std_msgs__msg__UInt32__fini(&message);
        fprintf(stderr, "rcl publish failed\n");
        goto cleanup;
      }
      pause_for_publish_period();
    }
  }

  rcl_subscription_options_t options =
      rcl_subscription_get_default_options();
  options.qos = qos;
  if (rcl_subscription_init(&subscription, &node, type_support,
                            "/openrtdds/qualification", &options) !=
          RCL_RET_OK ||
      rcl_wait_set_init(&wait_set, 1U, 0U, 0U, 0U, 0U, 0U, &context,
                        allocator) != RCL_RET_OK) {
    fprintf(stderr, "rcl subscription/wait initialization failed\n");
    goto cleanup;
  }
  std_msgs__msg__UInt32 message;
  if (!std_msgs__msg__UInt32__init(&message)) {
    goto cleanup;
  }
  for (unsigned attempt = 0U; attempt < 24U; ++attempt) {
    size_t index = 0U;
    if (rcl_wait_set_clear(&wait_set) != RCL_RET_OK ||
        rcl_wait_set_add_subscription(&wait_set, &subscription, &index) !=
            RCL_RET_OK) {
      break;
    }
    const rcl_ret_t wait_result = rcl_wait(&wait_set, RCL_MS_TO_NS(500));
    if (wait_result == RCL_RET_TIMEOUT) {
      continue;
    }
    if (wait_result != RCL_RET_OK) {
      break;
    }
    rmw_message_info_t info;
    if ((wait_set.subscriptions[index] != NULL) &&
        (rcl_take(&subscription, &message, &info, NULL) == RCL_RET_OK) &&
        (message.data == 42U)) {
      printf("OPENRTDDS_ROS2_RECEIVED c %s %u\n",
             reliable ? "reliable" : "best_effort", message.data);
      fflush(stdout);
      result = 0;
      break;
    }
  }
  std_msgs__msg__UInt32__fini(&message);

cleanup:
  {
  rcl_ret_t cleanup_result = RCL_RET_OK;
  if (rcl_wait_set_is_valid(&wait_set)) {
    cleanup_result = rcl_wait_set_fini(&wait_set);
    if ((cleanup_result != RCL_RET_OK) && (result == 0)) {
      result = 1;
    }
  }
  if (rcl_subscription_is_valid(&subscription)) {
    cleanup_result = rcl_subscription_fini(&subscription, &node);
    if ((cleanup_result != RCL_RET_OK) && (result == 0)) {
      result = 1;
    }
  }
  if (rcl_publisher_is_valid(&publisher)) {
    cleanup_result = rcl_publisher_fini(&publisher, &node);
    if ((cleanup_result != RCL_RET_OK) && (result == 0)) {
      result = 1;
    }
  }
  if (rcl_node_is_valid(&node)) {
    cleanup_result = rcl_node_fini(&node);
    if ((cleanup_result != RCL_RET_OK) && (result == 0)) {
      result = 1;
    }
  }
  if (rcl_context_is_valid(&context)) {
    cleanup_result = rcl_shutdown(&context);
    if ((cleanup_result != RCL_RET_OK) && (result == 0)) {
      result = 1;
    }
  }
  cleanup_result = rcl_context_fini(&context);
  if ((cleanup_result != RCL_RET_OK) && (result == 0)) {
    result = 1;
  }
  cleanup_result = rcl_init_options_fini(&init_options);
  if ((cleanup_result != RCL_RET_OK) && (result == 0)) {
    result = 1;
  }
  return result;
  }
}
