#include "rmw_openrtdds_cpp/qos.hpp"

#include <iostream>

#include "rmw/qos_profiles.h"

// Demonstrates: ORT-RMW-043, ORT-RMW-044
int main() {
  rmw_openrtdds_cpp::TypeSupportInfo type{};
  type.maximum_serialized_size = 256U;

  rmw_openrtdds_cpp::QosLimits limits{};
  rmw_openrtdds_cpp::EndpointResourcePlan plan{};
  rmw_openrtdds_cpp::QosError error =
      rmw_openrtdds_cpp::QosError::none;
  if (!rmw_openrtdds_cpp::plan_endpoint_resources(
          rmw_qos_profile_sensor_data, type,
          rmw_openrtdds_cpp::EndpointRole::subscription, limits, plan,
          error)) {
    std::cerr << "QoS plan failed: "
              << rmw_openrtdds_cpp::to_string(error) << '\n';
    return 1;
  }

  std::cout << "history slots: " << plan.history_sample_slots << '\n'
            << "sample bytes: " << plan.maximum_serialized_sample_size << '\n'
            << "history bytes: " << plan.history_storage_bytes << '\n';
  return 0;
}
