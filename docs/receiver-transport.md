# Receiver transport

`ReceiverConnection`, `EventChannel` and `TimingResponder` compose bounded native
sockets with authenticated records. They are private C++ implementation classes;
hosts normally use the [public C API](public-api.md).

## Connection lifecycle

Connections accept numeric IPv4/IPv6 endpoints, an absolute deadline and
cooperative cancellation. There is no DNS fallback or automatic retry. One
connection is used serially with one request outstanding. Provisioning retains
one socket through PIN setup; reconnect opens a fresh socket and runs peer verify.
Only authenticated completion at an idle framing boundary installs record keys.

Any request exception closes the socket, both record directions and pending
setup, and clears owned transient bodies/keys. Partial sends preserve the same
ciphertext bytes. Returned plaintext/credentials belong to the caller and must
not be logged. Fresh connections require fresh verification/randomness.

## Framing profile

| Limit | Value |
| --- | --- |
| Protocols | HTTP/1.1, RTSP/1.0 |
| Header block | 8 KiB; 32 fields; 1-KiB lines |
| Body | 32 KiB, with smaller limits during authentication |
| Native I/O chunk | 4 KiB |
| Parser feed | 64 KiB |
| Sequence | Starts at 1; UINT32_MAX reserved |

Bodies require unambiguous Content-Length; HTTP 204 is bodyless without that
header. RTSP requires matching CSeq; HTTP validates it when supplied. Reject
duplicate/folded headers, injection, chunked/close-delimited bodies, upgrades,
trailers, unsupported protocol forms and unsolicited extra input. Generated
Host/Content-Length/CSeq/Connection fields cannot be overridden by callers.
This is deliberately narrower than a general-purpose HTTP implementation.

Native nonblocking waits poll cancellation in at most 20-ms slices. A steady-clock
operation deadline covers the entire exchange and is never renewed by progress.
Synchronous crypto cannot be interrupted; checks prevent accepting late results.
RAII closes sockets on all failure paths and suppresses SIGPIPE on POSIX.

## Event channels

Base SETUP returns an event port. The event channel uses separately derived HAP
keys and receives RTSP/HTTP requests. It validates the same bounded fields,
allows an absent Content-Length for an empty body, and answers each complete
request with 200, `Content-Length: 0`, `Audio-Latency: 0`, and echoed Server/CSeq
when present. Reply precedes delivery of the event to session code.

An idle reader waits until cancellation; framing, authentication, disconnect and
I/O failures are terminal. Reader threads publish state rather than invoking
host callbacks or joining themselves.

## Timing responder

The sender advertises an ephemeral UDP timing port in NTP SETUP. Only exact
32-byte big-endian type-0xd2 requests from the selected receiver address receive
a reply. The response uses type 0xd3, sequence 7, echoes the request send time
as reference, and fills receive/send NTP timestamps. NTP seconds are Unix seconds
plus 2,208,988,800 modulo 2^32. Invalid datagrams are dropped rather than ending
the responder. Socket failure is terminal; cancellation closes the loop.

The [session guide](session-design.md) explains how these components are joined.
Protocol/reference source provenance is recorded in [dependencies.md](dependencies.md).
