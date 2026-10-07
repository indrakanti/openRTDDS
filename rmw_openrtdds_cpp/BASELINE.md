# `rmw_openrtdds_cpp` compatibility baseline

Requirements: ORT-RMW-027, ORT-RMW-040, ORT-RMW-041

PR23 established the implementation-independent foundation. PR24 adds a
discoverable, loadable ament package and the bounded lifecycle C ABI slice.
PR25 completes the proxy surface, PR26 qualifies G4.1 memory behavior, and
PR27 pins the first introspection and ROS DDS naming subset. This still does
**not** provide ROS topic exchange or a ROS 2 application compatibility claim.

| Item | Pinned value |
|---|---|
| ROS distribution | Jazzy |
| Upstream package | `ros2/rmw` |
| Upstream branch | `jazzy` |
| `rmw` package version | `7.3.4` |
| Upstream `package.xml` blob | `6b81eedd240033dcd695d5fa14511b0e41cdfd39` |
| Accepted build range | `>=7.3.3,<7.4.0` |
| Jazzy CI package | `7.3.3` |
| Jazzy proxy package | `ros2/rmw_implementation` |
| Jazzy proxy commit | `835ff87c676cd65634edd3ad7ba88c8d8a0452e7` |
| Proxy `functions.cpp` blob | `6201e037b7997bea8f66c552f2075887a62adf92` |
| Required proxy symbols | `95` |
| Jazzy `ros2/rosidl` commit | `85fa592b698b0f665e3120f48fac0d35e2f7d8a4` |
| C introspection layout blob | `63f3467d4c9584531ee8eca00d1c6ff7b8227406` |
| Jazzy `ros2/rmw_fastrtps` commit | `eb2b49a66b02c209ce836f04b3611e1ab550c134` |
| Topic-name convention blob | `b19e95706b4736875717143e58b01784da13cc02` |
| Type-name convention blob | `3f5953714fa2b936631b1a82fb0698e780c8ce2b` |
| Platform | Linux |
| Language ABI | ROS `rmw` C ABI wrapped by C++17 implementation |

The version and implementation identifier are compiled into
`openrtdds/rmw/foundation.hpp` and checked by unit tests. The package is now a
member of `rmw_implementation_packages`; its empty type-support registration
prevents discovery from being confused with topic capability. The implemented
lifecycle symbols are pinned in `abi_symbols.txt`. G4.1 passed in PR26; later
gates remain separate.

The reviewed source baseline and accepted build range are distinct on purpose:
7.3.4 is the exact API source used for implementation review, while the range
admits the current Jazzy image's ABI-compatible 7.3.3 patch and rejects a new
minor line until it is reviewed.

PR25 derives `abi_symbols.txt` and the typed unsupported definitions from the
pinned Jazzy proxy function table. The symbol list includes the 94 macro-routed
entry points plus the separately dispatched `rmw_init`. This records ABI
completeness, not semantic completeness: unimplemented API families remain
truthfully unsupported.

PR27 uses the pinned introspection member layouts and established Jazzy Fast
DDS mapping behavior only as compatibility references. The OpenRTDDS
implementation is allocation-free, uses fixed 256-byte outputs, and does not
link Fast DDS. A baseline change requires layout, identifier, size-analysis,
and golden-name-vector review.
