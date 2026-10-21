# MRP controls and ownership

MRP uses a separate authenticated AirPlay data stream while URL commands start
media playback. Remote startup orders peer verify, remote-only SETUP/events,
RECORD, type-130 controlType=2 SETUP, keyed data connection, DEVICE_INFO,
SET_CONNECTION_STATE and update subscription. A correlated heartbeat runs every
30 seconds. URL control fallback is described in [session-design.md](session-design.md).

## Ownership

Prefer an exact match to the inserted queue UUID or private URL. Commands carry
the complete receiver-provided player path; a bundle name alone is insufficient.
If a receiver omits the original UUID/URL, cooperative startup correlation can
bind a newly appeared item in `com.apple.TVAirPlay` whose duration matches the
URL playing event within 0.5 seconds. Capture the pre-start baseline, bind once,
and reject stale items, replacement queues and ambiguous paths.

This fallback does not guarantee exclusive ownership against concurrent senders
with matching media duration. Ownership loss ends the cast; it never grants
control over the replacement item.

## Framing and bounds

HAP records carry data frames with 32-byte network-endian headers. Receiver sync
frames receive an empty reply with the same sequence. Plist `params.data` holds
varint-length-prefixed protobuf messages; the bounded single-message form is also
accepted. Channel frame sequence and directional HAP nonce counters are distinct.

| Structure | Limit |
| --- | --- |
| Protobuf message/batch | 64 KiB |
| Fields per decoded message | 2048 |
| Messages per batch | 64 |
| Data frame | 256 KiB |
| Player paths / queue items | 32 / 256 |
| Incoming MRP record | 16 KiB; outgoing remains 1024 bytes |

The in-tree wire codec supports varint, fixed32/64 and length-delimited fields.
It rejects groups, field zero, duplicate mapped singular fields, overflow and
truncation. Unknown supported fields are skipped without recursive arbitrary
schema decoding. Decoded extension buffers have move-only owners and are cleared
on replacement, failure and destruction.

## Commands and status

One worker owns socket I/O and record counters. Callers serialize requests with
unique correlation IDs and expected response types. Command acknowledgements
do not invent a local state transition: use subsequent receiver updates.
Handshake, commands and heartbeat use bounded deadlines; timeout/cancellation
terminates the channel so late replies cannot satisfy later requests.

Status exposes ownership, state, position, duration, rate and fixed failure
categories. Position can be extrapolated while playing and clamped for display;
that extrapolation does not prove EOF. URLs, titles, player identifiers and raw
peer error text are not emitted as normal diagnostics.

The [session supervisor](session-design.md) coordinates terminal state and joins.
Receiver-remote Stop, network loss and receiver intent are not interchangeable;
see [known limitations](receiver-validation.md). Reference revisions and fixture
oracle provenance are in [dependencies.md](dependencies.md).
