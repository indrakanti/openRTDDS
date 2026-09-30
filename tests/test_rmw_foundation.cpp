#include <cstring>

#include "openrtdds/rmw/foundation.hpp"
#include "test_support.hpp"

// Verifies: ORT-RMW-023, ORT-RMW-024, ORT-RMW-025, ORT-RMW-026
// Verifies: ORT-RMW-027, ORT-RMW-028

namespace {

using openrtdds::rmw::AdapterConfig;
using openrtdds::rmw::AdapterContext;
using openrtdds::rmw::AdapterError;
using openrtdds::rmw::ContextState;
using openrtdds::rmw::GuardConditionHandle;
using openrtdds::rmw::NodeHandle;

void test_baseline_and_limits() {
  CHECK(std::strcmp(openrtdds::rmw::kImplementationIdentifier,
                    "rmw_openrtdds_cpp") == 0);
  CHECK(std::strcmp(openrtdds::rmw::kRosDistribution, "jazzy") == 0);
  CHECK(std::strcmp(openrtdds::rmw::kRmwBaselineVersion, "7.3.4") == 0);

  AdapterContext<2U, 2U> context;
  AdapterConfig config;
  config.limits.max_nodes = 0U;
  config.limits.max_guard_conditions = 2U;
  CHECK(context.initialize(config) == AdapterError::invalid_limits);
  CHECK(context.state() == ContextState::zero);

  config.limits.max_nodes = 2U;
  config.limits.max_guard_conditions = 0U;
  CHECK(context.initialize(config) == AdapterError::invalid_limits);
  CHECK(context.state() == ContextState::zero);

  config.limits.max_nodes = 3U;
  config.limits.max_guard_conditions = 2U;
  CHECK(context.initialize(config) == AdapterError::invalid_limits);
  CHECK(context.state() == ContextState::zero);

  config.limits.max_nodes = 2U;
  config.limits.max_guard_conditions = 2U;
  config.domain_id = openrtdds::rmw::kMaxPortableDomainId + 1U;
  CHECK(context.initialize(config) == AdapterError::invalid_domain);
  CHECK(context.state() == ContextState::zero);

  config.domain_id = 7U;
  config.implementation_identifier = "rmw_other";
  CHECK(context.initialize(config) ==
        AdapterError::incorrect_implementation);
  CHECK(context.state() == ContextState::zero);
}

void test_context_and_node_lifecycle() {
  AdapterContext<2U, 2U> context;
  AdapterConfig config;
  config.limits.max_nodes = 1U;
  config.limits.max_guard_conditions = 1U;
  config.domain_id = 43U;

  CHECK(context.initialize(config) == AdapterError::none);
  CHECK(context.state() == ContextState::initialized);
  CHECK(context.domain_id() == 43U);
  const std::uint32_t first_context_generation =
      context.context_generation();

  NodeHandle node;
  CHECK(context.create_node("controller", "/vehicle", node) ==
        AdapterError::none);
  CHECK(context.state() == ContextState::active);
  CHECK(context.node_count() == 1U);

  NodeHandle unchanged{9U, 9U, 9U};
  CHECK(context.create_node("second", "/vehicle", unchanged) ==
        AdapterError::resource_exhausted);
  CHECK(unchanged.slot == 9U);
  CHECK(context.node_count() == 1U);
  CHECK(context.finalize() == AdapterError::invalid_state);

  CHECK(context.destroy_node(node) == AdapterError::none);
  CHECK(context.destroy_node(node) == AdapterError::invalid_state);
  CHECK(context.state() == ContextState::initialized);
  CHECK(context.shutdown() == AdapterError::none);
  CHECK(context.state() == ContextState::finalizable);
  CHECK(context.finalize() == AdapterError::none);
  CHECK(context.state() == ContextState::zero);

  CHECK(context.initialize(config) == AdapterError::none);
  CHECK(context.context_generation() != first_context_generation);
  NodeHandle replacement;
  CHECK(context.create_node("replacement", "/vehicle", replacement) ==
        AdapterError::none);
  CHECK(context.destroy_node(node) == AdapterError::stale_handle);
  CHECK(context.destroy_node(replacement) == AdapterError::none);
  CHECK(context.shutdown() == AdapterError::none);
  CHECK(context.finalize() == AdapterError::none);
}

void test_node_validation_and_reuse() {
  AdapterContext<1U, 1U, 8U, 12U> context;
  AdapterConfig config;
  config.limits.max_nodes = 1U;
  config.limits.max_guard_conditions = 1U;
  CHECK(context.initialize(config) == AdapterError::none);

  NodeHandle output{7U, 7U, 7U};
  CHECK(context.create_node(nullptr, "/", output) ==
        AdapterError::invalid_argument);
  CHECK(context.create_node("", "/", output) ==
        AdapterError::invalid_argument);
  CHECK(context.create_node("node", "", output) ==
        AdapterError::invalid_argument);
  CHECK(context.create_node("12345678", "/", output) ==
        AdapterError::name_too_long);
  CHECK(context.create_node("node", "123456789012", output) ==
        AdapterError::name_too_long);
  CHECK(output.slot == 7U);

  NodeHandle first;
  CHECK(context.create_node("node", "/", first) == AdapterError::none);
  CHECK(context.destroy_node(first) == AdapterError::none);
  NodeHandle second;
  CHECK(context.create_node("node", "/", second) == AdapterError::none);
  CHECK(second.slot == first.slot);
  CHECK(second.slot_generation != first.slot_generation);
  CHECK(context.destroy_node(first) == AdapterError::stale_handle);
  CHECK(context.destroy_node(second) == AdapterError::none);
  CHECK(context.shutdown() == AdapterError::none);
  CHECK(context.finalize() == AdapterError::none);
}

void test_guard_condition_and_shutdown() {
  AdapterContext<1U, 1U> context;
  AdapterConfig config;
  config.limits.max_nodes = 1U;
  config.limits.max_guard_conditions = 1U;
  CHECK(context.initialize(config) == AdapterError::none);

  GuardConditionHandle guard;
  CHECK(context.create_guard_condition(guard) == AdapterError::none);
  GuardConditionHandle unchanged{9U, 9U, 9U};
  CHECK(context.create_guard_condition(unchanged) ==
        AdapterError::resource_exhausted);
  CHECK(unchanged.slot == 9U);
  CHECK(context.guard_condition_count() == 1U);
  std::uint64_t observed = 0U;
  bool ready = true;
  CHECK(context.observe_guard_condition(guard, observed, observed, ready) ==
        AdapterError::none);
  CHECK(!ready);

  const std::uint64_t wake_before = context.wake_generation();
  CHECK(context.trigger_guard_condition(guard) == AdapterError::none);
  std::uint64_t current = 0U;
  CHECK(context.observe_guard_condition(guard, observed, current, ready) ==
        AdapterError::none);
  CHECK(ready);
  CHECK(current != observed);
  CHECK(context.wake_generation() != wake_before);

  observed = current;
  CHECK(context.observe_guard_condition(guard, observed, current, ready) ==
        AdapterError::none);
  CHECK(!ready);

  const std::uint64_t wake_before_shutdown = context.wake_generation();
  CHECK(context.shutdown() == AdapterError::none);
  CHECK(context.wake_generation() != wake_before_shutdown);
  CHECK(context.state() == ContextState::shutting_down);
  CHECK(context.trigger_guard_condition(guard) == AdapterError::invalid_state);
  CHECK(context.destroy_guard_condition(guard) == AdapterError::none);
  CHECK(context.state() == ContextState::finalizable);
  CHECK(context.finalize() == AdapterError::none);
}

void test_error_names() {
  CHECK(std::strcmp(openrtdds::rmw::to_string(AdapterError::none), "none") ==
        0);
  CHECK(std::strcmp(openrtdds::rmw::to_string(AdapterError::invalid_argument),
                    "invalid_argument") == 0);
  CHECK(std::strcmp(
            openrtdds::rmw::to_string(AdapterError::incorrect_implementation),
            "incorrect_implementation") == 0);
  CHECK(std::strcmp(openrtdds::rmw::to_string(AdapterError::invalid_limits),
                    "invalid_limits") == 0);
  CHECK(std::strcmp(openrtdds::rmw::to_string(AdapterError::invalid_domain),
                    "invalid_domain") == 0);
  CHECK(std::strcmp(openrtdds::rmw::to_string(AdapterError::invalid_state),
                    "invalid_state") == 0);
  CHECK(std::strcmp(
            openrtdds::rmw::to_string(AdapterError::resource_exhausted),
            "resource_exhausted") == 0);
  CHECK(std::strcmp(openrtdds::rmw::to_string(AdapterError::name_too_long),
                    "name_too_long") == 0);
  CHECK(std::strcmp(openrtdds::rmw::to_string(AdapterError::stale_handle),
                    "stale_handle") == 0);
  CHECK(std::strcmp(openrtdds::rmw::to_string(AdapterError::unsupported),
                    "unsupported") == 0);
  CHECK(std::strcmp(openrtdds::rmw::to_string(
                        static_cast<AdapterError>(255U)),
                    "unknown") == 0);
}

}  // namespace

void test_rmw_foundation() {
  test_baseline_and_limits();
  test_context_and_node_lifecycle();
  test_node_validation_and_reuse();
  test_guard_condition_and_shutdown();
  test_error_names();
}
