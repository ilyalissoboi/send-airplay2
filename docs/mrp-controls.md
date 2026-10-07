# Native MRP controls

Private C++ implementation in draft PR #12, under decisions D29/D31. The CLI
uses MRP over a separately verified remote-control AirPlay data stream; URL
start continues to use the previously validated `/command` path. Hardware
results belong in [receiver-validation.md](receiver-validation.md).

## Sequence and ownership

Remote start orders pair-verify, remote-only SETUP/events, RECORD, dedicated
type-130 SETUP (`controlType=2`), keyed data connection, DEVICE_INFO and its
correlated reply, SET_CONNECTION_STATE, then the reference update subscription.
The keyboard-session request is omitted. Subscription flags match the reference:
artwork, volume, keyboard and output-device true, now-playing false. Unneeded
message types are ignored; reducing subscriptions requires a separate proof.
The minimum G1 experiment is retained through the private `enable_mrp=false`
option for tests. The normal CLI enables MRP.

Before inserting the URL queue item, the tracker receives its generated UUID
and private URL. The preferred ownership proof is an exact match to that UUID
or URL in receiver metadata. A bundle name alone does not establish ownership.
Every command includes the full receiver-provided playerPath. Other active players,
removal and queue replacement invalidate ownership; ambiguous ownership is
refused. Paths distinguish bundle, origin, process and player identities.

On tvOS 26.6 the observed MRP item omitted the URL and original queue UUID.
Engineering decision D32 adds cooperative startup correlation: capture the
pre-start item baseline, require a newly appeared item in the selected
`com.apple.TVAirPlay` path, and match its duration within 0.5 s of our URL
stream's `playing` event. That event represents duration as CMTime value/timescale;
only positive finite numeric times with a valid flag are accepted. Bind once to
that path/item and refuse a replacement;
a stale pre-start item or unrelated app cannot be adopted. This does not prove
ownership against concurrent AirPlay senders with matching media duration.
The public app identifier is documented in the primary
[pyatv client-state discussion](https://github.com/postlund/pyatv/issues/302).

## Framing and bounds

Each HAP-authenticated stream carries network-endian 32-byte frame headers.
The receiver's `sync` frames receive an empty `rply` with the same sequence;
reply frames are not answered and may themselves contain plist bodies.
The plist's `params.data` contains varint-length-prefixed protobuf messages.
The reference's single unprefixed-message form is also accepted within the same bounds.
Sender sync frames use one random sequence for the channel, as in the reference;
this is independent of the HAP nonce counters, which always advance.

The original in-tree wire codec supports varint, fixed32/64 and length-delimited
fields, skips unknown supported fields and rejects groups, field zero,
duplicate mapped singular fields, overflow and truncation. Limits: 64 KiB per
protobuf message/batch, 2048 fields per decoded message, 64 messages per batch,
256 KiB per data frame, 32 player paths and 256 queue items. MRP receive records
allow up to 16 KiB after observing a 2363-byte receiver record; other control/event
readers retain the 1024-byte default. Outgoing records always use 1024-byte chunks.
Known schema
mapping has fixed nesting; it does not recursively decode arbitrary unknown
fields. Schema and fixture provenance is in [dependencies.md](dependencies.md).

## Threads, deadlines and cleanup

One worker owns the data socket and both record counters. Callers serialize
requests with unique correlation IDs and explicit expected response types.
tvOS 26.6 acknowledges update configuration with type 0 and the exact request
identifier; commands require SEND_COMMAND_RESULT and device info requires
DEVICE_INFO. Heartbeats also accept a correlated type-0 acknowledgment.
Unsolicited state updates continue while callers wait. The worker polls native
readiness every 20 ms; an idle poll leaves the socket open. Actual read/write
failures remain terminal. Handshake and command responses use the configured
request deadline; timeout/cancellation terminate MRP so late responses cannot
complete later requests. A correlated generic heartbeat runs every 30 seconds.

Command rejection is reported separately from connection failure; no local
state change is invented from a successful command result. Status exposes only
ownership, state, position/duration/rate and counts. Position extrapolates from
the receiver's Cocoa timestamp only while playing and clamps to duration.
No URLs, metadata, identities or raw receiver error descriptions are printed.

Remote control sends an initial feedback request before the MRP handshake and
then shares the URL feedback worker's periodic schedule on its own verified
control connection. Stop joins feedback/events and closes the URL control/timing transport, then
joins/closes MRP and remote events/control. The CLI's `stop` sends MRP Stop first
and tears down even if the command fails. Enter or stdin EOF tears down directly.
Start failures clean up everything. D33 adds a sole lifecycle supervisor after
startup: terminal failure, URL ended/stopped/idle, owned-item EOF or loss of
previously established ownership cancel commands and trigger the same cleanup.
Callers may read status or call stop concurrently; destruction requires external
callers to finish. Owners remain allocated for safe final snapshots, while close
erases transport keys. `end_reason` is set once; `cleaned_up` follows worker joins.
The CLI stops its media server only after session cleanup.

EOF uses the owned item's receiver-reported paused/stopped position at or beyond
its positive duration. Wall-clock extrapolation or clamping never proves EOF.
tvOS 26.6 reports URL stopped before final MRP telemetry; that state allows up
to one second for EOF evidence before becoming receiver_stop. An ordinary
mid-item pause remains active. Connection failure takes priority when detected
before terminal completion. Loss of ownership ends this cast without adopting
or controlling the replacement player.

The CLI polls borrowed stdin without a detached reader or blocking getline;
partial lines survive polls, commands are bounded to 256 printable ASCII
characters, and Enter/stdin EOF retain their direct-teardown behavior.
Native control/event operations retain cancellation polling and configured
request deadlines (5 s by default). A silent MRP-only peer is detected by the
next 30 s heartbeat plus its request deadline; this is distinct from immediate
socket EOF. No automatic reconnect, resume, re-pairing or credential replacement
is attempted: after restoring reachability, start a fresh cast with the existing
profile. Actual receiver-side stop, sleep/wake and network-loss recovery remain
manual G3 gates; see the dated lifecycle record. Public playback ABI is later work.

## CLI

After `cast` reports playback, type `status`, `pause`, `play` (or `resume`),
`seek SECONDS` for an absolute nonnegative finite time, or `stop`.
Controls refuse an unowned player. `Control: accepted` is a correlated command
result, not a receiver observation. The status line reports `unknown` for
unavailable telemetry. Seek direction depends on the requested absolute time.
