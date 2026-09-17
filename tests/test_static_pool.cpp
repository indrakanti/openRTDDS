#include "openrtdds/core/static_pool.hpp"
#include "test_support.hpp"

// Verifies: ORT-CORE-002

namespace {

struct Tracked final {
  explicit Tracked(const int initial_value) noexcept : value(initial_value) {
    ++live_count;
  }
  ~Tracked() noexcept { --live_count; }

  int value;
  static int live_count;
};

int Tracked::live_count = 0;

}  // namespace

void test_static_pool() {
  openrtdds::core::StaticPool<Tracked, 2U> pool;
  CHECK(pool.empty());

  Tracked* const first = pool.acquire(11);
  Tracked* const second = pool.acquire(22);
  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(first->value == 11);
  CHECK(second->value == 22);
  CHECK(pool.full());
  CHECK(Tracked::live_count == 2);
  CHECK(pool.acquire(33) == nullptr);

  CHECK(pool.release(first));
  CHECK(!pool.release(first));
  CHECK(pool.size() == 1U);
  CHECK(Tracked::live_count == 1);

  pool.clear();
  CHECK(pool.empty());
  CHECK(Tracked::live_count == 0);
}
