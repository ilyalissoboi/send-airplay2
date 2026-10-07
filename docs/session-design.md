# URL playback session design and validation

Status: **PARTIALLY IMPLEMENTED**, updated 2026-10-07 for PR #12 (ready for review).
Channel keys, events, timing, messages, URL orchestration and `cast` are implemented.
Standalone G1 initially failed. The minimal native remote-control SETUP/event
session then passed G1 without pyatv, RECORD or MRP: video/audio played and the TV
returned home after sender shutdown (user-observed). MRP and controls are now
implemented; G2 evidence is recorded separately. See [MRP contracts](mrp-controls.md). Reference-derived details
are hypotheses unless receiver-validation.md explicitly records native evidence.

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
| Event-channel keys: HKDF-SHA512 over the pair-verify shared secret, salt `Events-Salt`; the sender writes with the key from info `Events-Read-Encryption-Key` and reads with `Events-Write-Encryption-Key` | Independent fixtures and native event traffic in the recorded G1 runs |
| Event messages are RTSP-style requests from the receiver; the sender answers `200 OK` with `CSeq` echoed and an empty body | pyatv source; observed traffic shape |
| `/command` body: `{"params": {"data": <bplist of the command>}}`, HTTP/1.1 on the control connection, with `X-Apple-Session-ID` and `X-Apple-StreamID` from the type-130 SETUP | Fork source; matched the successful run |

Historical hardware hypotheses (H5 passed for the recorded minimum; H1-H3
remain optional, untested alternatives to the implemented MRP controls):

- **H1:** `setRate` with rate 0 pauses and rate 1 resumes. Rate 1 is part of the
  validated start sequence.
- **H2:** a `playbackInfo` request (`kind` `request`, with `messageID`) gets a
  response on the event channel that includes position and duration. Seen in
  pyatv#2846, untested.
- **H3:** seek is a `/command` type that is not identified yet. Candidate names
  must come from evidence, not guessing. If none is found, seek waits for the
  control-path decision (decision 2).
- **H4:** closing the control and event connections ends playback on the TV.
  Supported by the minimum native session run: the user observed home-screen
  return after sender shutdown. Native EOF and selected receiver-side lifecycle
  checks are now recorded in receiver-validation.md; normal remote-stop reason
  classification remains unresolved.
- **H5 (G1, 2026-10-07):** the receiver presents URL playback only while the
  sender also holds a remote-control session. Our session alone played
  headlessly (fetching the whole file, reporting `playing`); with pyatv's
  remote-control session open, the same native session was visible. The
  remote-control session therefore belongs to start, not only to controls.
  The native minimum SETUP/event experiment subsequently passed G1 without
  RECORD, feedback or MRP on this receiver. Broader firmware/lifetime validation
  remains pending; D29 still requires MRP for controls.

## 2. Session sequence (reference-derived)

The existing URL session implements steps 1-10 below. G1 showed that omitting
the separate remote-control session prevents visible presentation on this receiver.
The minimal native experiment now starts an independently verified
remote-control-only SETUP/event connection before URL start and retains it until
URL teardown. That historical minimum omitted remote RECORD, feedback and the data stream/MRP handshake
to isolate the SETUP/event contribution. This minimum passed the recorded
45-second native-only G1 run on tvOS 26.6; that does not validate prolonged
session lifetime, other firmware or native playback controls. The normal CLI now
extends it with RECORD, data SETUP and MRP; see [MRP contracts](mrp-controls.md)
and the separate G2 record.

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
connection; stop the timing responder; join/close MRP, then the remote event/control session;
stop `MediaServer`, which joins its callbacks. Each step runs even if an earlier one failed. No automatic
reconnect or re-pair.

## 3. Components

| Component | Responsibility | Implementation / remaining work |
|---|---|---|
| Channel key derivation | Derive named HKDF keys; erase the shared secret at session end | Implemented `ChannelKeySource` and `ReceiverConnection::derive_channel_keys`; secret never returned |
| Event channel | TCP/HAP records; acknowledge requests; decode plist envelopes | Implemented `EventRequestParser`, response encoder and `EventChannel` |
| Timing responder | Answer NTP-style timing requests | Implemented fixed packet codec and receiver-filtered UDP responder |
| Session messages | SETUP and `/command` bodies, response and event parsing | Implemented on D27 with independent plistlib fixtures |
| `UrlPlaybackSession` | URL start, state and teardown; retain remote control | Implemented URL/MRP flow; G1 passed; G2 controls, EOF and receiver-side stop validated separately |
| `airplay2-cli cast` | File/profile start, state lines and Enter/EOF cleanup | Implemented status, pause/play, absolute seek and stop; URL/address/payload values never printed |

## 4. Threading model (D28)

**Decision D28: synchronous components with session threads.** With D29, the
remote-control session adds one more reader thread for its data stream and
reuses the same pattern; its event channel needs a reader too. This matches
the existing transport, which is synchronous with absolute deadlines and
cancellation polling.

- **Control:** callers and the feedback thread share `ReceiverConnection`
  under one mutex. Each request holds it for at most its deadline (5 s
  by default), so one request is in flight, as the connection requires. Feedback
  waits on a condition variable with a 2 s timeout; a late feedback is
  acceptable.
- **Event reader thread:** owns the event socket and its record keys. It reads
  until cancelled, with native waits polling in short slices, answers every
  request, and publishes decoded state under a mutex and condition variable.
  Waiters, such as start waiting for `playing`, use deadlines.
- **Timing thread:** owns the UDP socket. It is stateless per packet and
  bounded per packet.
- **Remote event reader:** owns the separately keyed remote event channel. It
  acknowledges requests and counts them without changing URL playback state.
  Its unexpected failure marks the composite session failed.
- **`MediaServer`:** unchanged; it owns its Boost threads.
- **Lifecycle supervisor (D33):** starts after all owners are initialized.
  Detects terminal control/event/timing/MRP failure, URL terminal state,
  receiver-reported owned-item EOF and loss of established ownership. It alone
  joins workers and closes transports; readers signal state rather than joining
  themselves. Concurrent stop calls serialize the supervisor join. Pending MRP
  commands cancel before cleanup acquires their mutex. Status remains readable.
  The first end reason is retained; cleaned_up follows joins/key erasure.
  The CLI polls stdin and stops its media server after session cleanup.
  Events never run user callbacks; callers poll or wait on state.

*Alternative:* move the event channel, timing and feedback onto one Boost.Asio
`io_context`, since Boost is already a dependency. That means fewer threads,
but it needs an asynchronous rewrite of the record and framing layers, and two
I/O models in one session. Not recommended for the first slice.

**D33 (engineering decision, 2026-10-07): automatic ordered cleanup.**
Use one supervisor and the existing synchronous I/O deadlines. EOF requires
owned receiver telemetry at its positive duration while paused/stopped, never
extrapolated position. URL stopped/idle allows one second for the independently
ordered final MRP position before classifying receiver_stop. A pause alone is
not terminal. Failure takes priority if detected first. Do not reconnect or
adopt a replacement player automatically; recovery is a fresh explicit cast
with retained credentials. Detailed contracts: [mrp-controls.md](mrp-controls.md).
Selected hardware EOF/cycle evidence does not close all G3 gates.

## 5. Controls and status (D29)

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

**D31 (decided 2026-10-06): in-tree wire codec.** The historical options considered (the in-tree codec is selected):

| Option | Licence and footprint | Notes |
|---|---|---|
| In-tree bounded wire codec (varint, length-delimited, fixed32/64; unknown fields skipped) plus hand-written mapping for the message set | Apache-2.0; no dependency | Same reasoning as D27. Fixtures generated in Python with the `protobuf` package and pyatv's compiled MRP messages, an independent oracle. Selected (D31) |
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
  Decoded MRP extension payloads also have move-only owners that erase bytes on
  replacement/destruction, including partially decoded batches and rejected replies (D43).
  Event bodies are protected through acknowledgment and then transferred to the
  caller; failed replies erase the body before release.
- **Logging:** never log the media URL, receiver identifiers, sender identifiers
  or raw event payloads. Diagnostics use categories and allowlisted fields.
  URL type/state strings are fixed labels (`other` for unknown strings); URL
  outlines omit unknown dictionary keys and request targets (D43).
- **Network:** the sender process needs inbound UDP (timing) and inbound TCP
  (media) from the receiver only. On this host, the existing `airplay2-cli.exe`
  Allow rules cover both. Packaged Windows and Screenbox need their own proof.

## 8. Implementation and validation plan

1. Channel key derivation, with independent Python HKDF fixtures for the
   Events labels. Unit only. **Done** on `claude/url-playback-session`:
   `channel_keys.*`, `PairVerifier::take_session_keys` and
   `ReceiverConnection::derive_channel_keys`.
2. Event request parser/encoder and event channel, tested against a loopback
   fake receiver with fragmented and coalesced records, bad tags, oversized
   requests, cancellation and EOF. **Done:** `EventRequestParser`,
   `encode_event_response`, `EventChannel` and
   `ReceiverOperation::until_cancelled`, tested with a scripted stream.
3. Timing packet codec and responder: Python fixtures; source filtering;
   malformed packets. **Done:** `ntp_timing.*`, with literal-byte known answers
   (conversion values computed with Python `datetime` and pyatv's fraction
   formula) and real IPv4/IPv6 UDP loopback tests.
4. Session message builders and parsers, with plistlib fixtures for SETUP and
   `/command` bodies. Field names must match the sanitized reference sequence.
   **Done:** `session_messages.*`, byte-exact against the fixtures. The display
   name is `send-airplay2` rather than the reference's `pyatv`; all
   capability-relevant fields use reference values (D30).
5. `UrlPlaybackSession` against a scripted fake receiver: the full sequence,
   representative step failures, start timeout, teardown order, and secret cleanup.
   **Done:** `url_playback_session.*`. Additions:
   - The RTSP request URI is `rtsp://<local address>/<random 32-bit number>`,
     like the reference's.
   - `/info` errors are tolerated, as in the reference.
   - `/feedback` is best effort: a non-2xx answer is ignored, but a transport
     failure marks the session failed.
   - An unreadable event body is answered and counted, and does not end the
     session.
6. `airplay2-cli cast`, start and stop only. **Implemented** (`cast_cli.*`);
   hardware gate G1 initially FAILED without a remote-control session. The
   minimal native SETUP/event session then PASSED G1 without pyatv. The user
   observed video/audio and return home after sender shutdown.
   D29/D31 framing, message mapping, handshake and controls are implemented
   with independent fixtures; native G2 telemetry is recorded separately.
   **Hardware gate G1:** the user
   observes video and audio; read counts demonstrate fetch; teardown returns
   the TV home. Protocol idle, EOF/home-screen observation and receiver-side
   stop are separate G3 checks; see the latest lifecycle record.
7. Controls over MRP: status, pause/resume, seek, stop. **Implemented. Hardware gate
   G2** is recorded separately. Record each result in
   receiver-validation.md, including failures.
8. Robustness: automatic EOF/failure/ownership-loss cleanup is implemented.
   Native EOF and ten short start/stop cycles passed for the recorded receiver.
   G2/EOF observations and sleep/wake recovery passed by user report.
   Receiver-remote stop cleaned up but classified connection_lost/exit 1.
   A separate full-clip run failed video presentation after buffering near 18 s:
   video froze while audio continued normally through EOF (user-confirmed).
   D35 HTTP/buffering diagnostics and controlled comparisons are implemented:
   minimal/four and MRP/16 passed full video/audio/Home; default MRP/four buffered
   but recovered. D36 passed user-observed controls, ten fresh-process Stop/Enter
   cycles and natural EOF at 16 slots. Normal `cast` now defaults to 16 bounded
   slots (override 1..16); generic server/`serve` defaults remain four. A 16-slot
   attempt also paused at zero without transport commands or remote input, so
   startup ordering/state is the next investigation. Per user decision D37, keep
   the original frozen-video failure at low priority unless it recurs; insufficient
   media connections are the likely cause, with evidence retained. Startup pause
   remains active. D38 records startup phase/state/rate timing and requires one
   continuous second of eligible playing before returning, without retrying Play
   or extending the deadline. D39's [manual batch](manual-validation.md) passed
   selected startup, controls/full EOF and sleep/wake presentation/Home; remote
   Stop/Home cleaned automatically but normal protocol intent remains unresolved.
   Playing telemetry and EOF cleanup do not prove moving video.
   Normal stop classification and longer playback remain **Gate G3**; D42 passed
   the selected Ethernet interruption/explicit fresh recovery check below.
   See the dated validation record.
   D40 observes remote notifications using fixed labels and bounded numeric
   codes in the shared 256-entry log, without updating URL state. Final MRP
   diagnostics report retained received state after joined cleanup; received
   elapsed time is distinct from extrapolated progress. These observations do
   not change failure priority, end reasons or cleanup order. See
   [mrp-controls.md](mrp-controls.md#remote-stop-diagnostics-d40) for the contract.
   D41's [reference audit](pyatv-stop-reference.md) found no validated remote-Stop
   discriminator. D42's user-confirmed Ethernet removal triggered automatic
   connection_lost/exit 1 cleanup on URL feedback timeout. Fresh casting after
   reconnection/Home reused credentials and passed observed video/audio/near-end
   EOF/Home with media_end/exit 0. The existing termination policy is unchanged;
   this is fresh explicit recovery, not automatic reconnect or resume.

Each step follows AGENTS.md: readability rules, clang-format, static/shared
CTest, sanitizer CI. Each step is its own commit on the PR branch.
