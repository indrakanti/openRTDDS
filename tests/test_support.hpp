#pragma once

#include <iostream>

inline int test_failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: "        \
                << #condition << '\n';                                       \
      ++test_failures;                                                       \
    }                                                                        \
  } while (false)

