#include <cstdint>
#include <iostream>

#include "openrtdds/rmw/foundation.hpp"

// Demonstrates: ORT-RMW-024, ORT-RMW-025, ORT-RMW-026

int main() {
  using openrtdds::rmw::AdapterConfig;
  using openrtdds::rmw::AdapterContext;
  using openrtdds::rmw::AdapterError;
  using openrtdds::rmw::GuardConditionHandle;
  using openrtdds::rmw::NodeHandle;

  AdapterContext<2U, 2U> context;
  AdapterConfig config;
  config.limits.max_nodes = 2U;
  config.limits.max_guard_conditions = 2U;
  config.domain_id = 43U;

  if (context.initialize(config) != AdapterError::none) {
    return 1;
  }

  NodeHandle node;
  GuardConditionHandle guard;
  if ((context.create_node("openrtdds_probe", "/openrtdds", node) !=
       AdapterError::none) ||
      (context.create_guard_condition(guard) != AdapterError::none)) {
    return 2;
  }

  std::uint64_t observed = 0U;
  std::uint64_t current = 0U;
  bool ready = false;
  if ((context.trigger_guard_condition(guard) != AdapterError::none) ||
      (context.observe_guard_condition(guard, observed, current, ready) !=
       AdapterError::none) ||
      !ready) {
    return 3;
  }

  if ((context.shutdown() != AdapterError::none) ||
      (context.destroy_guard_condition(guard) != AdapterError::none) ||
      (context.destroy_node(node) != AdapterError::none) ||
      (context.finalize() != AdapterError::none)) {
    return 4;
  }

  std::cout << openrtdds::rmw::kImplementationIdentifier << " baseline "
            << openrtdds::rmw::kRosDistribution << " rmw "
            << openrtdds::rmw::kRmwBaselineVersion
            << ": bounded lifecycle complete\n";
  return 0;
}
