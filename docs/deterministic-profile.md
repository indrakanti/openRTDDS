# Deterministic profile

Determinism is a system property, not a DDS configuration switch. OpenRTDDS
will therefore define bounds from the application API through the kernel and
network interface.

## Initialization-to-run transition

Initialization may create entities, allocate configured pools, establish
static peers, create threads, lock mappings, and validate limits. Once the
runtime enters `RUN`, the Safety Profile will prohibit:

- heap allocation and container growth
- thread creation and dynamic library loading
- unbounded queues, retries, fragmentation, or discovery work
- blocking diagnostic output on real-time threads

The current bounded storage, serialization, RTPS, reliability, UDP, and
static typed DDS components allocate no runtime heap storage internally. A
future runtime lifecycle controller will enforce the system-wide transition
and detect allocations in application/type-support code; the library does not
yet claim whole-process enforcement.

## Linux contract

Deployment is expected to configure PREEMPT_RT, CPU isolation/housekeeping,
NIC IRQ affinity, real-time runtime limits, memory-lock limits, and stable
power/frequency behavior. OpenRTDDS reports a failed scheduling, affinity, or
memory-lock request explicitly; it never silently claims a real-time mode.

All intervals and deadlines use `CLOCK_MONOTONIC`. Wall-clock/PTP time will be
carried separately where globally correlated timestamps are required.

## Measurement

Planned release gates include allocation counts after initialization, queue
high-water marks, scheduler latency, RX-to-callback latency, deadline misses,
packet loss/recovery bounds, and long-duration stress runs.
