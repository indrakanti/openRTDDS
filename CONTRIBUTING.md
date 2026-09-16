# Contributing

OpenRTDDS is being developed in small, reviewable increments. Every change
should preserve explicit resource bounds and make timing or failure behavior
no harder to explain.

## Pull-request expectations

- include tests for observable behavior
- avoid new runtime heap allocation on Safety Profile paths
- document new threads, locks, queues, timers, and retry loops
- return errors explicitly; do not silently reduce real-time guarantees
- keep warnings clean under GCC and Clang
- separate standards interoperability from proprietary extensions

Run the local gate before submitting:

```bash
cmake -S . -B build -DOPENRTDDS_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

