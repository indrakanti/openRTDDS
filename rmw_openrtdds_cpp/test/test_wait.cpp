#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <thread>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/rmw.h"

// Verifies: ORT-RMW-009, ORT-RMW-016, ORT-RMW-049, ORT-RMW-050
namespace {

[[nodiscard]] int fail(const int code) {
  rmw_reset_error();
  return code;
}

}  // namespace

int main() {
  const auto allocator = rcutils_get_default_allocator();
  auto options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return fail(1);
  }
  options.instance_id = 32U;
  options.domain_id = 32U;
  options.enclave = rcutils_strdup("/openrtdds_wait", allocator);
  auto context = rmw_get_zero_initialized_context();
  if ((options.enclave == nullptr) ||
      (rmw_init(&options, &context) != RMW_RET_OK)) {
    return fail(2);
  }
  rmw_node_t* node = rmw_create_node(&context, "wait", "/openrtdds");
  rmw_guard_condition_t* guard = rmw_create_guard_condition(&context);
  rmw_wait_set_t* wait_set = rmw_create_wait_set(&context, 1U);
  if ((node == nullptr) || (guard == nullptr) || (wait_set == nullptr)) {
    return fail(3);
  }
  if (rmw_create_wait_set(&context, 33U) != nullptr) {
    return fail(4);
  }
  rmw_reset_error();

  void* guard_data = guard->data;
  rmw_guard_conditions_t guards{1U, &guard_data};
  const rmw_time_t immediate{0U, 0U};
  if ((rmw_wait(nullptr, &guards, nullptr, nullptr, nullptr, wait_set,
                &immediate) != RMW_RET_TIMEOUT) ||
      (guard_data != nullptr)) {
    return fail(5);
  }

  guard_data = guard->data;
  if ((rmw_trigger_guard_condition(guard) != RMW_RET_OK) ||
      (rmw_wait(nullptr, &guards, nullptr, nullptr, nullptr, wait_set,
                nullptr) != RMW_RET_OK) ||
      (guard_data != guard->data)) {
    return fail(6);
  }

  guard_data = guard->data;
  const rmw_time_t finite{0U, 2'000'000U};
  const auto timeout_start = std::chrono::steady_clock::now();
  if (rmw_wait(nullptr, &guards, nullptr, nullptr, nullptr, wait_set,
               &finite) != RMW_RET_TIMEOUT) {
    return fail(7);
  }
  const auto timeout_elapsed = std::chrono::steady_clock::now() - timeout_start;
  if ((timeout_elapsed < std::chrono::milliseconds(1)) ||
      (timeout_elapsed > std::chrono::seconds(1))) {
    return fail(8);
  }

  std::atomic<bool> waiter_started{false};
  rmw_ret_t trigger_wait_result = RMW_RET_ERROR;
  std::thread trigger_waiter([&] {
    void* blocking_guard_data = guard->data;
    rmw_guard_conditions_t blocking_guards{1U, &blocking_guard_data};
    waiter_started.store(true, std::memory_order_release);
    trigger_wait_result = rmw_wait(nullptr, &blocking_guards, nullptr, nullptr,
                                   nullptr, wait_set, nullptr);
  });
  while (!waiter_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  if (rmw_trigger_guard_condition(guard) != RMW_RET_OK) {
    trigger_waiter.join();
    return fail(9);
  }
  trigger_waiter.join();
  if (trigger_wait_result != RMW_RET_OK) {
    return fail(10);
  }

  std::array<rmw_wait_set_t*, 7U> extra_wait_sets{};
  for (auto& item : extra_wait_sets) {
    item = rmw_create_wait_set(&context, 0U);
    if (item == nullptr) {
      return fail(11);
    }
  }
  if (rmw_create_wait_set(&context, 1U) != nullptr) {
    return fail(12);
  }
  rmw_reset_error();

  waiter_started.store(false, std::memory_order_release);
  rmw_ret_t shutdown_wait_result = RMW_RET_OK;
  std::thread shutdown_waiter([&] {
    void* blocking_guard_data = guard->data;
    rmw_guard_conditions_t blocking_guards{1U, &blocking_guard_data};
    waiter_started.store(true, std::memory_order_release);
    shutdown_wait_result = rmw_wait(nullptr, &blocking_guards, nullptr,
                                    nullptr, nullptr, wait_set, nullptr);
  });
  while (!waiter_started.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  if (rmw_shutdown(&context) != RMW_RET_OK) {
    shutdown_waiter.join();
    return fail(13);
  }
  shutdown_waiter.join();
  if (shutdown_wait_result != RMW_RET_ERROR) {
    return fail(14);
  }
  rmw_reset_error();
  if (rmw_context_fini(&context) != RMW_RET_INVALID_ARGUMENT) {
    return fail(15);
  }
  rmw_reset_error();

  for (auto* item : extra_wait_sets) {
    if (rmw_destroy_wait_set(item) != RMW_RET_OK) {
      return fail(16);
    }
  }
  if ((rmw_destroy_wait_set(wait_set) != RMW_RET_OK) ||
      (rmw_destroy_guard_condition(guard) != RMW_RET_OK) ||
      (rmw_destroy_node(node) != RMW_RET_OK) ||
      (rmw_context_fini(&context) != RMW_RET_OK) ||
      (rmw_init_options_fini(&options) != RMW_RET_OK)) {
    return fail(17);
  }
  return 0;
}
