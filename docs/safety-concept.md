# Preliminary safety concept

OpenRTDDS is safety-oriented software under development. It is not currently
certified, qualified, or suitable as the sole risk-control mechanism in a
safety-related product.

## Safety strategy

The Safety Profile is intended to make behavior bounded and failure modes
observable. It will combine:

- startup validation of resource and topology configuration
- preallocated histories, samples, and protocol state
- static endpoint identity and allowlisted communication paths
- end-to-end protection for source, sequence, freshness, length, version,
  and payload integrity
- deadline, liveliness, queue, and transport supervision
- bounded retries followed by an explicit fault event
- independent application-level degraded-state or fallback handling

## Fault containment

DDS delivery does not prove that data is semantically safe. A receiving
application remains responsible for range, plausibility, consistency, and
system-state checks. Middleware faults will be reported through a bounded
health-event channel and must not trigger hidden recovery loops.

## Evidence plan

The project will evolve traceable requirements, architecture decisions,
interface contracts, static-analysis results, unit/integration tests,
interoperability records, timing measurements, robustness/fault-injection
results, and configuration assumptions. Certification claims require a
separate lifecycle, tool assessment, independent reviews, and product-level
safety case.

