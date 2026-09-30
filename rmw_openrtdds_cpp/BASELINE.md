# `rmw_openrtdds_cpp` compatibility baseline

Requirements: ORT-RMW-027

PR23 establishes the implementation-independent foundation for the future ROS
adapter. It does **not** register an RMW implementation or claim that ROS 2 can
load OpenRTDDS yet.

| Item | Pinned value |
|---|---|
| ROS distribution | Jazzy |
| Upstream package | `ros2/rmw` |
| Upstream branch | `jazzy` |
| `rmw` package version | `7.3.4` |
| Upstream `package.xml` blob | `6b81eedd240033dcd695d5fa14511b0e41cdfd39` |
| Platform | Linux |
| Language ABI | ROS `rmw` C ABI wrapped by C++17 implementation |

The version and implementation identifier are compiled into
`openrtdds/rmw/foundation.hpp` and checked by unit tests. A later PR will add
the ament package, complete required C symbol set, and runtime load test before
membership in `rmw_implementation_packages` is declared.
