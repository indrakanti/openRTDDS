# Static RTPS DATA path

PR3 adds the first network path: one statically configured, unfragmented RTPS
`DATA` submessage carried in one UDPv4 datagram. It is a foundation for
interoperability testing, not a complete DDS writer or reader.

## Transmit path

```text
Application sample
    -> bounded XCDR1 serialized payload
    -> RTPS Header + DATA submessage
    -> nonblocking UDPv4 send
```

The builder writes into a caller-owned buffer and emits:

- the fixed 20-byte `RTPS` message header
- protocol version, caller-supplied VendorId, and GuidPrefix
- DATA submessage ID `0x15`
- explicit submessage endianness and exact `octetsToNextHeader`
- zero `extraFlags` and `octetsToInlineQos == 16`
- static reader and writer EntityIds
- positive 64-bit writer sequence number encoded as high/low words
- one XCDR1 `CDR_BE` or `CDR_LE` serialized payload

The full RTPS message is capped at the UDP maximum payload size of 65,507
bytes. Fragmentation is rejected rather than performed implicitly.

## Receive path

The parser validates the protocol magic and major version, DATA flags,
declared submessage length, offsets, positive sequence number, and serialized
payload encapsulation before returning a non-owning view. It honors
`octetsToNextHeader == 0` for a final submessage and uses
`octetsToInlineQos` rather than assuming that future header extensions cannot
exist.

Unsupported inline QoS, keys, non-standard payloads, fragmentation, discovery,
and reliability are explicit errors in this profile.

## UDP behavior

`UdpSocket` is Linux-only, nonblocking, close-on-exec, and allocation-free. It
reports `would_block`, truncation, native errors, and oversized datagrams
without retries or hidden waits. Static deployment configuration owns the
retry/deadline policy.

## Vendor identity

The builder requires a caller-supplied VendorId. A product claiming RTPS
conformance must obtain and use an assigned vendor identifier; OpenRTDDS does
not borrow another implementation's identifier.

## Standards baseline

The message header, DATA layout, flags, offsets, sequence-number encoding, and
UDP mapping follow OMG DDSI-RTPS 2.5:

- https://www.omg.org/spec/DDSI-RTPS/2.5/PDF
