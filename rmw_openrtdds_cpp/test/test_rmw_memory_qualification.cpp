#include <array>
#include <cstddef>
#include <cstdlib>

#include "rcutils/allocator.h"
#include "rcutils/strdup.h"
#include "rmw/error_handling.h"
#include "rmw/init.h"
#include "rmw/init_options.h"
#include "rmw/rmw.h"

// Verifies: ORT-RMW-037, ORT-RMW-038, ORT-RMW-039
namespace {

constexpr std::size_t kNodeCapacity = 8U;
constexpr std::size_t kUserGuardCapacity = 15U;
constexpr std::size_t kStressCycles = 256U;

struct AllocationState final {
  std::size_t attempts{0U};
  std::size_t outstanding{0U};
  std::size_t fail_on_attempt{0U};
  bool accounting_error{false};
};

[[nodiscard]] bool should_fail(AllocationState& state) noexcept {
  ++state.attempts;
  return (state.fail_on_attempt != 0U) &&
         (state.attempts == state.fail_on_attempt);
}

void* tracked_allocate(const std::size_t size, void* opaque) {
  auto& state = *static_cast<AllocationState*>(opaque);
  if (should_fail(state)) {
    return nullptr;
  }
  void* pointer = std::malloc(size);
  if (pointer != nullptr) {
    ++state.outstanding;
  }
  return pointer;
}

void tracked_deallocate(void* pointer, void* opaque) {
  if (pointer == nullptr) {
    return;
  }
  auto& state = *static_cast<AllocationState*>(opaque);
  if (state.outstanding == 0U) {
    state.accounting_error = true;
  } else {
    --state.outstanding;
  }
  std::free(pointer);
}

void* tracked_reallocate(void* pointer, const std::size_t size, void* opaque) {
  auto& state = *static_cast<AllocationState*>(opaque);
  if (should_fail(state)) {
    return nullptr;
  }
  if (pointer == nullptr) {
    void* replacement = std::malloc(size);
    if (replacement != nullptr) {
      ++state.outstanding;
    }
    return replacement;
  }
  if (size == 0U) {
    tracked_deallocate(pointer, opaque);
    return nullptr;
  }
  return std::realloc(pointer, size);
}

void* tracked_zero_allocate(const std::size_t count,
                            const std::size_t size, void* opaque) {
  auto& state = *static_cast<AllocationState*>(opaque);
  if (should_fail(state)) {
    return nullptr;
  }
  void* pointer = std::calloc(count, size);
  if (pointer != nullptr) {
    ++state.outstanding;
  }
  return pointer;
}

[[nodiscard]] rcutils_allocator_t make_allocator(
    AllocationState& state) noexcept {
  rcutils_allocator_t allocator{};
  allocator.allocate = tracked_allocate;
  allocator.deallocate = tracked_deallocate;
  allocator.reallocate = tracked_reallocate;
  allocator.zero_allocate = tracked_zero_allocate;
  allocator.state = &state;
  return allocator;
}

[[nodiscard]] int fail(const int code) {
  rmw_reset_error();
  return code;
}

[[nodiscard]] bool initialize_options(AllocationState& state,
                                      rmw_init_options_t& options) {
  const rcutils_allocator_t allocator = make_allocator(state);
  options = rmw_get_zero_initialized_init_options();
  if (rmw_init_options_init(&options, allocator) != RMW_RET_OK) {
    return false;
  }
  options.instance_id = 26U;
  options.domain_id = 26U;
  options.enclave = rcutils_strdup("/openrtdds_memory_qualification",
                                   allocator);
  return options.enclave != nullptr;
}

[[nodiscard]] bool finish_context(rmw_context_t& context) {
  if (rmw_shutdown(&context) != RMW_RET_OK) {
    return false;
  }
  return rmw_context_fini(&context) == RMW_RET_OK;
}

[[nodiscard]] int run_stress_cycles() {
  for (std::size_t cycle = 0U; cycle < kStressCycles; ++cycle) {
    AllocationState state{};
    rmw_init_options_t options{};
    if (!initialize_options(state, options)) {
      return fail(100);
    }

    rmw_context_t context = rmw_get_zero_initialized_context();
    if (rmw_init(&options, &context) != RMW_RET_OK) {
      return fail(101);
    }

    std::array<rmw_node_t*, kNodeCapacity> nodes{};
    for (auto& node : nodes) {
      node = rmw_create_node(&context, "stress_node", "/openrtdds");
      if (node == nullptr) {
        return fail(102);
      }
    }
    if (rmw_create_node(&context, "overflow_node", "/openrtdds") != nullptr) {
      return fail(103);
    }
    if (!rmw_error_is_set()) {
      return fail(104);
    }
    rmw_reset_error();

    std::array<rmw_guard_condition_t*, kUserGuardCapacity> guards{};
    for (auto& guard : guards) {
      guard = rmw_create_guard_condition(&context);
      if (guard == nullptr) {
        return fail(105);
      }
    }
    if (rmw_create_guard_condition(&context) != nullptr) {
      return fail(106);
    }
    if (!rmw_error_is_set()) {
      return fail(107);
    }
    rmw_reset_error();

    if (rmw_context_fini(&context) != RMW_RET_INVALID_ARGUMENT) {
      return fail(108);
    }
    rmw_reset_error();
    if (rmw_shutdown(&context) != RMW_RET_OK) {
      return fail(109);
    }
    if (rmw_create_node(&context, "late_node", "/openrtdds") != nullptr) {
      return fail(110);
    }
    rmw_reset_error();
    if (rmw_trigger_guard_condition(guards.front()) != RMW_RET_ERROR) {
      return fail(111);
    }
    rmw_reset_error();
    if (rmw_context_fini(&context) != RMW_RET_INVALID_ARGUMENT) {
      return fail(112);
    }
    rmw_reset_error();

    for (auto* guard : guards) {
      if (rmw_destroy_guard_condition(guard) != RMW_RET_OK) {
        return fail(113);
      }
    }
    for (auto* node : nodes) {
      if (rmw_destroy_node(node) != RMW_RET_OK) {
        return fail(114);
      }
    }
    if (rmw_context_fini(&context) != RMW_RET_OK) {
      return fail(115);
    }
    if (rmw_init_options_fini(&options) != RMW_RET_OK) {
      return fail(116);
    }
    if ((state.outstanding != 0U) || state.accounting_error) {
      return fail(117);
    }
  }
  return 0;
}

[[nodiscard]] int run_init_failure_sweep() {
  // Sweep beyond the current four adapter allocations so success and every
  // rollback edge are both exercised without encoding helper internals.
  for (std::size_t offset = 1U; offset <= 8U; ++offset) {
    AllocationState state{};
    rmw_init_options_t options{};
    if (!initialize_options(state, options)) {
      return fail(200);
    }
    const std::size_t baseline = state.outstanding;
    state.fail_on_attempt = state.attempts + offset;

    rmw_context_t context = rmw_get_zero_initialized_context();
    const rmw_ret_t result = rmw_init(&options, &context);
    state.fail_on_attempt = 0U;
    if (result == RMW_RET_OK) {
      if (!finish_context(context)) {
        return fail(201);
      }
    } else {
      if ((context.implementation_identifier != nullptr) ||
          (context.impl != nullptr)) {
        return fail(202);
      }
      rmw_reset_error();
    }
    if (state.outstanding != baseline) {
      return fail(203);
    }
    if (rmw_init_options_fini(&options) != RMW_RET_OK) {
      return fail(204);
    }
    if ((state.outstanding != 0U) || state.accounting_error) {
      return fail(205);
    }
  }
  return 0;
}

[[nodiscard]] int run_entity_failure_sweep() {
  AllocationState state{};
  rmw_init_options_t options{};
  if (!initialize_options(state, options)) {
    return fail(300);
  }
  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return fail(301);
  }
  const std::size_t baseline = state.outstanding;

  for (std::size_t offset = 1U; offset <= 4U; ++offset) {
    state.fail_on_attempt = state.attempts + offset;
    if (rmw_create_node(&context, "failure_node", "/openrtdds") != nullptr) {
      return fail(302);
    }
    state.fail_on_attempt = 0U;
    rmw_reset_error();
    if (state.outstanding != baseline) {
      return fail(303);
    }
    rmw_node_t* recovery =
        rmw_create_node(&context, "recovery_node", "/openrtdds");
    if ((recovery == nullptr) || (rmw_destroy_node(recovery) != RMW_RET_OK)) {
      return fail(304);
    }
    if (state.outstanding != baseline) {
      return fail(305);
    }
  }

  for (std::size_t offset = 1U; offset <= 2U; ++offset) {
    state.fail_on_attempt = state.attempts + offset;
    if (rmw_create_guard_condition(&context) != nullptr) {
      return fail(306);
    }
    state.fail_on_attempt = 0U;
    rmw_reset_error();
    if (state.outstanding != baseline) {
      return fail(307);
    }
    rmw_guard_condition_t* recovery = rmw_create_guard_condition(&context);
    if ((recovery == nullptr) ||
        (rmw_destroy_guard_condition(recovery) != RMW_RET_OK)) {
      return fail(308);
    }
    if (state.outstanding != baseline) {
      return fail(309);
    }
  }

  if (!finish_context(context)) {
    return fail(310);
  }
  if (rmw_init_options_fini(&options) != RMW_RET_OK) {
    return fail(311);
  }
  if ((state.outstanding != 0U) || state.accounting_error) {
    return fail(312);
  }
  return 0;
}

[[nodiscard]] int run_invalid_input_checks() {
  if (rmw_shutdown(nullptr) != RMW_RET_INVALID_ARGUMENT) {
    return fail(400);
  }
  rmw_reset_error();
  if (rmw_context_fini(nullptr) != RMW_RET_INVALID_ARGUMENT) {
    return fail(401);
  }
  rmw_reset_error();
  if (rmw_destroy_node(nullptr) != RMW_RET_INVALID_ARGUMENT) {
    return fail(402);
  }
  rmw_reset_error();
  if (rmw_destroy_guard_condition(nullptr) != RMW_RET_INVALID_ARGUMENT) {
    return fail(403);
  }
  rmw_reset_error();

  AllocationState state{};
  rmw_init_options_t options{};
  if (!initialize_options(state, options)) {
    return fail(404);
  }
  rmw_context_t context = rmw_get_zero_initialized_context();
  if (rmw_init(&options, &context) != RMW_RET_OK) {
    return fail(405);
  }
  const std::size_t baseline = state.outstanding;
  if (rmw_create_node(&context, "bad/name", "/openrtdds") != nullptr) {
    return fail(406);
  }
  rmw_reset_error();
  if (state.outstanding != baseline) {
    return fail(407);
  }

  int placeholder = 0;
  rmw_node_t foreign_node{};
  foreign_node.implementation_identifier = "foreign_rmw";
  foreign_node.context = &context;
  foreign_node.data = &placeholder;
  if (rmw_destroy_node(&foreign_node) !=
      RMW_RET_INCORRECT_RMW_IMPLEMENTATION) {
    return fail(408);
  }
  rmw_reset_error();

  rmw_guard_condition_t foreign_guard{};
  foreign_guard.implementation_identifier = "foreign_rmw";
  foreign_guard.context = &context;
  foreign_guard.data = &placeholder;
  if (rmw_trigger_guard_condition(&foreign_guard) !=
      RMW_RET_INCORRECT_RMW_IMPLEMENTATION) {
    return fail(409);
  }
  rmw_reset_error();
  if (state.outstanding != baseline) {
    return fail(410);
  }

  if (!finish_context(context)) {
    return fail(411);
  }
  if (rmw_init_options_fini(&options) != RMW_RET_OK) {
    return fail(412);
  }
  if ((state.outstanding != 0U) || state.accounting_error) {
    return fail(413);
  }
  return 0;
}

}  // namespace

int main() {
  int result = run_stress_cycles();
  if (result != 0) {
    return result;
  }
  result = run_init_failure_sweep();
  if (result != 0) {
    return result;
  }
  result = run_entity_failure_sweep();
  if (result != 0) {
    return result;
  }
  return run_invalid_input_checks();
}
