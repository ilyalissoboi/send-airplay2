# URL playback session: design proposal

Status: **PROPOSAL, not implemented.** Written 2026-10-06 for PR #11. Nothing
here is receiver-tested unless the evidence column says so.

User decisions on 2026-10-06:

- **D28:** synchronous threads (section 4).
- **D29:** implement the MRP control path now, rather than `/command`-only
  controls (sections 5 and 5a).
- **D30:** reference sender-identity values first, configurable, then a
  hardware test of neutral values (section 6).

- **D31:** an in-tree bounded protobuf wire codec with hand-written message
  mapping (section 5a).

The goal is the first native vertical slice: `airplay2-cli cast` plays one local
H.264/AAC MP4 on "Living Room" (Apple TV 4K, tvOS 26.6), served by this
project's `MediaServer`, with explicit start, state, controls and teardown.

## 1. What is known

| Fact | Evidence |
|---|---|
| Legacy `/play` and `/playback-info` do not start playback on tvOS 26.6 | Observed: pyatv 0.18.0 runs, receiver-validation.md |
| The `/command` queue flow starts playback, and the receiver fetches from `serve` | Observed: unmerged pyatv fix, video and audio played (user-observed) |
| The base SETUP needs a reachable sender UDP timing (NTP) responder | Observed: SETUP stalled until inbound UDP was allowed; cause inferred, not packet-captured |
| Playback state arrives as `playbackState` events (`loading`, `playing`) on the event channel | Observed: filtered event log of the fork run |
| Seek, pause/resume and position work through pyatv's remote control | Observed, but that path is MRP over a separate data stream (`controlType` 2), **not** `/command` |
| After the reference `stop`, the TV left playback, but the receiver reported `Paused` and the sender session stayed open | Observed |
| Event-channel keys: HKDF-SHA512 over the pair-verify shared secret, salt `Events-Salt`; the sender writes with the key from info `Events-Read-Encryption-Key` and reads with `Events-Write-Encryption-Key` | pyatv 0.18.0 source (MIT), `ap2_session.py`/`airplayv2.py`; not yet exercised by this library |
| Event messages are RTSP-style requests from the receiver; the sender answers `200 OK` with `CSeq` echoed and an empty body | pyatv source; observed traffic shape |
| `/command` body: `{"params": {"data": <bplist of the command>}}`, HTTP/1.1 on the control connection, with `X-Apple-Session-ID` and `X-Apple-StreamID` from the type-130 SETUP | Fork source; matched the successful run |

Hypotheses to test on hardware, in this order:

- **H1:** `setRate` with rate 0 pauses and rate 1 resumes. Rate 1 is part of the
  validated start sequence.
- **H2:** a `playbackInfo` request (`kind` `request`, with `messageID`) gets a
  response on the event channel that includes position and duration. Seen in
  pyatv#2846, untested.
- **H3:** seek is a `/command` type that is not identified yet. Candidate names
  must come from evidence, not guessing. If none is found, seek waits for the
  control-path decision (decision 2).
- **H4:** closing the control and event connections ends playback on the TV.
  Explicit stop may also need a command or `TEARDOWN`.

## 2. Session sequence (reference-derived)

Steps 1-9 reproduce the validated fork sequence without its separate
remote-control session. That omission is itself a hypothesis: verify it first.

1. Connect TCP to the receiver's AirPlay port and run pair-verify with stored
   credentials (existing `ReceiverConnection`). Record the route-selected local
   address.
2. Start the UDP timing responder on that local address. Accept only the
   receiver's address.
3. Base `SETUP` (bplist): session UUID and correlation UUID, timing port and
   `timingProtocol` `NTP`, and sender identity fields (decision 3). The response
   gives `eventPort`.
4. Connect the event channel to `eventPort` with derived event keys. Start the
   event reader.
5. Start `POST /feedback` every 2 s on the control connection.
6. `GET /info`, then `RECORD`. The fork sends `/info` here; keep it until shown
   unnecessary. Record capability flags in sanitized form.
7. Type-130 `SETUP` with `controlType` 1. The response gives `streamID`.
8. Start `MediaServer` for the file; its URL is private and never logged.
9. `/command` in order: `insertPlayQueueItem` (item UUID, `mediaType` `file`,
   `Content-Location`, start position); `setProperty isInterestedInDateRange`;
   `setProperty actionAtItemEnd`; `setRate 1`. Any non-2xx response fails the
   start.
10. Wait, with a deadline, for `playbackState` to reach `playing`. `loading`
    alone is not success. A timeout or an error event fails the start with a
    sanitized reason.

Teardown order: mark the session stopping; send the stop command if H4 needs
one; stop the feedback thread; close the event channel; close the control
connection; stop the timing responder; stop `MediaServer`, which joins its
callbacks. Each step runs even if an earlier one failed. No automatic
reconnect or re-pair.

## 3. Components

| Component | Responsibility | Reuse / new |
|---|---|---|
| Channel key derivation | Derive named HKDF keys from the verified shared secret, then wipe it at session end | **Change:** today `PairVerifier` releases only control keys and wipes the secret. Proposed: `ReceiverConnection` keeps an erasing owner of the secret while verified and exposes `derive_channel_keys(salt, write_info, read_info)`. The secret is never returned to callers |
| Event channel | TCP to `eventPort`; HAP records; parse receiver requests; reply `200`; decode plist envelopes | Reuse `ReceiverStream`, `ControlReader`/`ControlWriter`. **New:** a bounded request parser and response encoder (`receiver_http` handles only the opposite direction) |
| Timing responder | Answer NTP-style timing requests | **New:** 32-byte packet codec and UDP socket adapter, with fixtures from an independent Python encoder |
| Session messages | Build the SETUP and `/command` bodies; parse SETUP, `/info` and event replies | **New**, on the D27 plist codec |
| `UrlPlaybackSession` | Private orchestrator: start, state, controls, teardown | **New** |
| `airplay2-cli cast` | Development command: `--address --profile --file`; interactive stdin controls; sanitized state lines | **New**; the private URL, identifiers and payloads are never printed |

## 4. Threading model (D28)

**Decision D28: synchronous components with session threads.** With D29, the
remote-control session adds one more reader thread for its data stream and
reuses the same pattern; its event channel needs a reader too. This matches
the existing transport, which is synchronous with absolute deadlines and
cancellation polling.

- **Control:** callers and the feedback thread share `ReceiverConnection`
  under one mutex. Each request holds it for at most its deadline (5 s
  proposed), so one request is in flight, as the connection requires. Feedback
  waits on a condition variable with a 2 s timeout; a late feedback is
  acceptable.
- **Event reader thread:** owns the event socket and its record keys. It reads
  with short deadlines so it can see cancellation, answers every request, and
  publishes decoded state into a `SessionState` (mutex and condition variable).
  Waiters, such as start waiting for `playing`, use deadlines.
- **Timing thread:** owns the UDP socket. It is stateless per packet and
  bounded per packet.
- **`MediaServer`:** unchanged; it owns its Boost threads.
- **Failure:** any terminal control or event error moves the session to
  `failed` with a sanitized category. Every thread stops and later calls throw.
  Events never run user callbacks on internal threads in this slice; callers
  poll or wait on state.

*Alternative:* move the event channel, timing and feedback onto one Boost.Asio
`io_context`, since Boost is already a dependency. That means fewer threads,
but it needs an asynchronous rewrite of the record and framing layers, and two
I/O models in one session. Not recommended for the first slice.

## 5. Controls and status (D29)

- **Proposed: `/command` only for this slice.** Start, state events and, once
  H1/H2 pass, pause/resume and status. Seek and stop ship only after H3/H4 are
  confirmed on hardware.
- **Alternative: implement the MRP data stream.** This is the path where seek,
  pause and status are already validated. It needs a protobuf dependency or
  codec, a second encrypted channel with `DataStream-Salt` plus seed, and
  pyatv's remote-control session. That is a large surface; defer it unless
  H1-H3 fail.

**Decision D29: MRP now.** `/command` is still used to start playback
(validated). H1-H3 remain useful fallbacks but are no longer on the critical
path. Hypotheses get tested with this library's own `cast` command, not with
further runs of the unmerged fork. Each test prints only the command type, the status
code, the event `type`, allowlisted state fields and the event key names.

## 5a. MRP control path (D29)

These are the reference facts, from the pyatv 0.18.0 source (MIT) and the
filtered fork log.

- **Separate session.** pyatv opens a second AirPlay connection for remote
  control: its own pair-verify, then a base `SETUP` with
  `isRemoteControlOnly: true` and `timingProtocol` `None`, then its own event
  channel. The fork log shows both sessions (two pair-verifies, two base
  SETUPs). Whether one session can carry both is unknown; start with two, as
  the reference does.
- **Data stream.** A type-130 `SETUP` with `controlType` 2,
  `wantsDedicatedSocket: true`, a random 64-bit `seed` and a fixed
  `clientTypeUUID`. The response gives `dataPort`; the sender connects to it.
  Keys use salt `DataStream-Salt` with the decimal seed appended, and infos
  `DataStream-Output-Encryption-Key` (sender writes) and
  `DataStream-Input-Encryption-Key` (sender reads). Framing is HAP records
  again.
- **Messages.** Each data-stream message is a 32-byte header (total size,
  12-byte type such as `sync` or `rply`, 4-byte command, 8-byte sequence
  number, 4-byte padding) followed by a plist `{"params": {"data": ...}}`.
  `data` holds varint-length-prefixed protobuf `ProtocolMessage` values. Each
  `sync` from the receiver needs an empty `rply` with the same sequence
  number.
- **Handshake.** `DEVICE_INFO` must be first and gets a reply. Then
  `SET_CONNECTION_STATE`, then `CLIENT_UPDATES_CONFIG` to subscribe to state
  updates (pyatv also requests a keyboard session; check whether that is
  needed). A heartbeat runs every 30 s.
- **Controls and status.** `SEND_COMMAND` with Play, Pause, Stop or
  SeekToPlaybackPosition (option `playbackPosition`), each answered by a
  command result. State and position come from receiver-sent state messages
  (`SET_STATE` and now-playing content metadata: playback state, rate, elapsed
  time with a timestamp, duration). pyatv tracks clients, players and content
  items; this slice tracks only the client that owns our URL playback.

Message set for this slice, about 10 messages plus their nested types:
`ProtocolMessage` (type, identifier, error code), device info, connection
state, client updates config, send command, command options, command result,
set state, now-playing client/player, content item metadata, and the
heartbeat. Field numbers come from pyatv's `.proto` definitions (MIT, derived
from reverse engineering); record that provenance in dependencies.md before
use.

**D31 (decided 2026-10-06): in-tree wire codec.** The options considered:

| Option | Licence and footprint | Notes |
|---|---|---|
| In-tree bounded wire codec (varint, length-delimited, fixed32/64; unknown fields skipped) plus hand-written mapping for the message set | Apache-2.0; no dependency | Same reasoning as D27. Fixtures generated in Python with the `protobuf` package and pyatv's compiled MRP messages, an independent oracle. Recommended |
| protozero (header-only wire reader and writer) plus hand-written mapping | BSD-2-Clause; vcpkg | Removes the wire-codec work; mapping and bounds stay ours |
| Google protobuf runtime with `protoc` code generation from pyatv's `.proto` files | BSD-3-Clause runtime; build-time `protoc`; vendored MIT `.proto` | Complete and generated, but adds a heavy runtime and code generation to the Windows, Android and UWP builds, and generated parsers need their own size limits |

## 6. Sender identity (D30)

The reference sends `model` `iPhone14,3`, iOS-like `osName`, `osVersion` and
`osBuildVersion`, `sourceVersion`, a `User-Agent` of `AirPlay/870.14.1`, and a
random locally administered `deviceID`/`macAddress`.

**Decision D30:** start with the reference values, which are the only ones
known to work, and make them configurable. Then test neutral values (for example
`model` `send-airplay2`) on hardware, and keep them if playback still works.
Never send the host's real MAC address; generate a random locally
administered one per session.

## 7. Bounds, privacy and cleanup

- **Bounds:** plist documents are bounded by D27. Event requests use the
  existing 32 KiB body and 8 KiB header limits. Timing packets have a fixed
  size. Waits for `playing` and for command responses have explicit deadlines.
- **Secrets:** shared-secret, channel-key and record-key owners erase
  themselves on success, failure and destruction, and are non-copyable.
- **Logging:** never log the media URL, receiver identifiers, sender identifiers
  or raw event payloads. Diagnostics use categories and allowlisted fields.
- **Network:** the sender process needs inbound UDP (timing) and inbound TCP
  (media) from the receiver only. On this host, the existing `airplay2-cli.exe`
  Allow rules cover both. Packaged Windows and Screenbox need their own proof.

## 8. Implementation and validation plan

1. Channel key derivation, with independent Python HKDF fixtures for the
   Events labels. Unit only.
2. Event request parser/encoder and event channel, tested against a loopback
   fake receiver with fragmented and coalesced records, bad tags, oversized
   requests, cancellation and EOF.
3. Timing packet codec and responder: Python fixtures; source filtering;
   malformed packets.
4. Session message builders and parsers, with plistlib fixtures for SETUP and
   `/command` bodies. Field names must match the sanitized reference sequence.
5. `UrlPlaybackSession` against a scripted fake receiver: the full sequence,
   each step failing, start timeout, teardown order, and secret cleanup.
6. `airplay2-cli cast`, start and stop only. Then MRP, after D31: wire codec
   and message mapping with fixtures; data-stream framing; the
   remote-control session against a fake receiver; then status and
   controls. **Hardware gate G1:** the user
   observes video and audio; `serve`-style read counts; teardown returns the
   TV to idle (H4).
7. Controls over MRP: status, pause/resume, seek, stop. **Hardware gate
   G2.** Record each result in
   receiver-validation.md, including failures.
8. Robustness: 10 start/stop cycles, receiver sleep/wake, network loss
   mid-play. **Gate G3.**

Each step follows AGENTS.md: readability rules, clang-format, static/shared
CTest, sanitizer CI. Each step is its own commit on the PR branch.
