# `rmw_openrtdds_cpp` compatibility baseline

Requirements: ORT-RMW-027

PR23 established the implementation-independent foundation. PR24 adds a
discoverable, loadable ament package and the bounded lifecycle C ABI slice. It
does **not** provide ROS type support, the complete Jazzy ABI, or a ROS 2
application compatibility claim.

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
`openrtdds/rmw/foundation.hpp` and checked by unit tests. The package is now a
member of `rmw_implementation_packages`; its empty type-support registration
prevents discovery from being confused with topic capability. The implemented
lifecycle symbols are pinned in `abi_symbols.txt`. Later PRs must complete the
mandatory symbol set and runtime-selection gates before G4.1 passes.
