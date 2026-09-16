# OpenRTDDS

OpenRTDDS is an early-stage, open-source implementation of deterministic,
safety-oriented DDS/RTPS for Linux. The project targets bounded execution,
explicit resource ownership, observable timing, and interoperability with
standards-compliant DDS implementations.

> **Status:** foundation release. This repository is not a certified safety
> product and is not ready for production use.

## Design goals

- C++17 with a small, auditable dependency surface
- no heap allocation after initialization in the future Safety Profile
- compile-time or startup-validated resource bounds
- fixed and documented thread topology
- first-class PREEMPT_RT, affinity, memory locking, and monotonic clocks
- RTPS wire interoperability rather than a proprietary transport
- built-in timing metrics, fault injection hooks, and safety evidence

## Profiles

| Capability | General Profile | Safety Profile |
|---|---|---|
| Discovery | Dynamic or static | Static/prevalidated |
| Memory | Configurable | Preallocated after initialization |
| Queues/history | Bounded | Bounded and startup-validated |
| Threads | Configurable | Fixed topology |
| Reliability | Standard policy | Bounded retries and recovery windows |
| Logging | Standard | Nonblocking, bounded event records |

## Build

```bash
cmake -S . -B build -DOPENRTDDS_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The runtime probe reports whether the current process can lock memory. This
normally requires an appropriate `RLIMIT_MEMLOCK`; failure is reported but is
not treated as a build/test failure.

```bash
./build/openrtdds_runtime_probe
```

## Initial roadmap

1. Project foundation and deterministic Linux primitives
2. CDR serialization and bounded sample storage
3. UDPv4 RTPS DATA path with static endpoint configuration
4. HEARTBEAT/ACKNACK with bounded reliability
5. SPDP/SEDP discovery for the General Profile
6. Fast DDS and Cyclone DDS interoperability gates
7. Shared-memory transport, latency instrumentation, and fault injection

See [the architecture](docs/architecture.md),
[deterministic profile](docs/deterministic-profile.md), and
[safety concept](docs/safety-concept.md) for the current design baseline.

## License

Apache License 2.0. See [LICENSE](LICENSE).

