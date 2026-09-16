#include <iostream>

#include "test_support.hpp"

void test_runtime_limits();
void test_cdr();
void test_keep_last_history();
void test_static_pool();

int main() {
  test_runtime_limits();
  test_cdr();
  test_keep_last_history();
  test_static_pool();

  if (test_failures != 0) {
    std::cerr << test_failures << " test assertion(s) failed\n";
    return 1;
  }
  std::cout << "all tests passed\n";
  return 0;
}
