#include "rmw/error_handling.h"
#include "rmw/event.h"
#include "rmw/rmw.h"

// Verifies: ORT-RMW-034, ORT-RMW-035
namespace {

int fail(const int code) {
  rmw_reset_error();
  return code;
}

}  // namespace

int main() {
  rmw_reset_error();
  const rmw_ret_t allocation_result =
      rmw_init_publisher_allocation(nullptr, nullptr, nullptr);
  if ((allocation_result != RMW_RET_UNSUPPORTED) || !rmw_error_is_set()) {
    return fail(1);
  }

  rmw_reset_error();
  const rmw_publisher_t* publisher =
      rmw_create_publisher(nullptr, nullptr, nullptr, nullptr, nullptr);
  if ((publisher != nullptr) || !rmw_error_is_set()) {
    return fail(2);
  }

  rmw_reset_error();
  if (rmw_event_type_is_supported(RMW_EVENT_LIVELINESS_CHANGED)) {
    return fail(3);
  }
  if (rmw_error_is_set()) {
    return fail(4);
  }

  return 0;
}
