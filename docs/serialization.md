# Serialization baseline

PR2 implements a deliberately small, bounded serialization subset for the
first interoperable RTPS data path.

## Supported now

- XCDR1 `PLAIN_CDR`
- RTPS encapsulation identifiers `CDR_BE` (`0x0000`) and `CDR_LE` (`0x0001`)
- big- and little-endian integer and IEEE-754 floating-point values
- booleans, octet blocks, and bounded IDL strings
- CDR alignment relative to the first byte after the four-byte encapsulation
  header
- zero-filled alignment padding
- caller-owned fixed buffers and sticky explicit errors
- fixed-capacity `KEEP_LAST` history with bounded sample payloads

## Intentionally unsupported

- XCDR2, delimited CDR, and parameter-list CDR
- unbounded strings, sequences, maps, and dynamic types
- optional members, unions, inheritance, and type evolution
- automatic IDL code generation

Receiving an unsupported encapsulation fails explicitly. It is not decoded as
a similar representation.

## Safety properties

Serialization performs no heap allocation. Every write is preflighted so an
overflow does not partially advance the stream. Every read validates bounds
before copying and does not advance the cursor after failure. Errors are
sticky until the stream is deliberately reset.

`KeepLastHistory<Depth, MaxPayloadBytes>` allocates all sample slots inline.
When full, a valid insertion replaces the oldest sample. Invalid or oversized
inputs leave the history unchanged.

## Standards baseline

The representation follows OMG DDS-XTypes 1.3 XCDR1 `PLAIN_CDR` rules and the
DDSI-RTPS 2.5 `SerializedPayload` rule that logically resets CDR alignment
immediately after the representation identifier and options.

- https://www.omg.org/spec/DDS-XTypes/1.3/PDF
- https://www.omg.org/spec/DDSI-RTPS/2.5/PDF
