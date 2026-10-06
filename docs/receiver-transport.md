# Bounded receiver transport

This private C++17 slice connects the existing PIN setup, peer verification and
control-record processors to synchronous native TCP I/O. It is not a public ABI,
CLI pairing command, credential store, media server or playback implementation.
Apple TV interoperability remains untested; loopback/fake receivers are the test
gate for this PR.

## Connection and authentication lifecycle

`connect_receiver` accepts a numeric IPv4/IPv6 endpoint and an absolute deadline.
IPv6 scope IDs are explicit in the socket address and omitted from HTTP Host:
they identify sender-local interfaces. No DNS resolution, address fallback or automatic
retry is performed. `ReceiverConnection` owns the stream and permits serial use
only. The cancellation flag is the sole object that another thread may modify.

Provisioning calls `begin_pairing`: POST `/pair-pin-start`, then POST `/pair-setup`
M1, retaining bounded M2 while the host obtains the PIN. `finish_pairing` receives
a separate operation deadline, sends M3, authenticates M4 before sending M5, and
authenticates M6 before releasing credentials once. Success closes that socket.
The host must close an abandoned PIN prompt and erase its PIN/source copies.
Neither PIN entry nor credential persistence is implemented in the CLI.

A new connection uses `verify` with trusted credentials: POST `/pair-verify`
M1/M3 and validate M2/M4, including the pinned identity/signature rules described
in [peer-verification.md](peer-verification.md). Only after complete M4, an idle
socket boundary and successful validation does the connection install fresh
directional `ControlWriter`/`ControlReader` owners at counter zero. Subsequent
requests and responses pass through authenticated records. No generic requests
are available before verification, and there is no plaintext fallback.

Each connection has one outstanding request. Partial sends retain the same
already-encrypted bytes and never re-encrypt them. Any exception closes the
socket, both record directions and pending setup, and erases owned transient
body/key buffers. Replacement connections require fresh verification/randomness.
Returned response bytes and credentials remain caller-owned; never log them.
Header fields are metadata and must not be used to carry secrets.

## Deliberately narrow framing profile

This is a receiver control profile, not a general HTTP client/server:

| Limit/policy | Contract |
|---|---|
| Request protocols | HTTP/1.1 or RTSP/1.0 |
| Header bytes | 8 KiB total, including start line and CRLF delimiter |
| Lines and fields | 1 KiB per line; 32 header fields; ASCII tokens/values |
| Bodies | 32 KiB maximum; setup/verification use their smaller existing limits |
| Native read/write chunk | At most 4 KiB per socket call |
| Parser feed | At most 64 KiB; retained memory stays bounded by headers + body |
| Response body framing | One Content-Length, including explicit zero; HTTP 204 is bodyless and cannot carry Content-Length |
| Request framing | Generated Host, Content-Length, CSeq and Connection; caller overrides rejected |
| Correlation | HTTP response ordering with one request outstanding; validate CSeq if supplied; RTSP requires matching CSeq |
| Sequence | Starts at 1; UINT32_MAX reserved; exhaustion is terminal |

Reject duplicate fields (even equal Content-Length), folded fields, bare line
breaks, injection, unsupported protocol versions, informational/304 responses,
Transfer-Encoding, upgrades/trailers, HEAD/CONNECT and close-delimited framing.
Responses advertising Connection other than keep-alive are rejected. Extra bytes
coalesced after a final response, queued input before a new request/key transition,
or trailing partial encrypted records are terminal correlation failures. Event
channels/server-initiated requests are future separate connections.

HTTP peers need not echo CSeq. Ordering cannot cryptographically correlate an
unlabelled plaintext HTTP response or eliminate a race with delayed unsolicited
input; PIN/verification state and cryptographic transcripts supply authentication.
After verification, record counters reject replay. Broaden this profile only
with explicit firmware evidence and regression coverage, rather than accepting
ambiguous framing as an interoperability workaround.

## Timeouts, cancellation and native ownership

`ReceiverOperation::after` accepts 1..60,000 ms and captures a steady-clock
deadline. The same operation spans every write/read in a request and all exchanges
in a pairing/verification method. Progress never renews it. Native nonblocking
connect/read/write waits use <=20 ms poll slices (Windows select, POSIX poll),
checking cancellation/deadlines before and after I/O. EINTR/would-block preserves
the same deadline. Synchronous cryptographic calls cannot be interrupted; deadline
checks around them prevent late results from being accepted.

Socket and Winsock lifetime use RAII, including failed construction. POSIX sockets
are nonblocking/close-on-exec, and sends suppress SIGPIPE (SO_NOSIGPIPE on macOS,
MSG_NOSIGNAL on Linux/Android). EOF before a complete response is terminal; an
incomplete encrypted record at EOF is rejected by the record codec. Errors expose
sanitized transport/crypto categories, without endpoint, body or native error data.

## Event channel (receiver to sender)

A verified session also receives requests from the receiver on a separate TCP
connection to the base SETUP's `eventPort`. `EventChannel` (`event_channel.*`)
owns that stream and HAP records keyed with `event_channel_labels()` (see
`channel_keys.h`). `EventRequestParser` and `encode_event_response`
(`receiver_http.*`) frame this opposite direction:

- **Request line:** `METHOD target RTSP/1.0` or `HTTP/1.1`. The method is a
  token of at most 32 bytes; the target is visible ASCII.
- **Fields:** the same rules as responses. Token names, visible values, unique
  lower-cased names, at most 32 fields, an 8 KiB header block, and no
  Transfer-Encoding, Upgrade or Trailer. `Content-Length` is at most 32 KiB and
  an absent length means an empty body. `CSeq` must be decimal when present.
- **Buffering:** requests may arrive back to back. The parser keeps at most one
  maximal request plus one read chunk of unparsed input, and erases consumed
  bytes.
- **Replies:** each well-formed request gets `<protocol> 200 OK` with
  `Content-Length: 0`, `Audio-Latency: 0` and the request's `Server` and `CSeq`
  echoed. These are the fields of the reference sender's reply (pyatv 0.18.0
  `channels.py`, MIT; constants only). The reply is sent before the request is
  returned to the caller.
- **Failures:** every error is terminal. A clean or partial end of input,
  authentication or framing failure, deadline or cancellation closes the
  stream and erases keys and buffered plaintext.
- **Waiting:** a reader thread uses `ReceiverOperation::until_cancelled(flag)`.
  The receiver may stay silent indefinitely, so only the session's
  cancellation flag ends that wait; the native stream polls it in short slices.

Tests use a scripted stream and literal request and reply bytes; see
`event_channel_tests.cpp`. Event-channel interoperability with a receiver is
not yet tested.

## NTP timing responder

A base SETUP with `timingProtocol` `NTP` announces a sender UDP port. The
receiver sends timing requests there and stalls the SETUP until they are
answered (observed on tvOS 26.6). `TimingResponder` (`ntp_timing.*`) does this:

- **Socket:** binds a numeric local address (normally the local end of the
  control connection) on an ephemeral port; `port()` supplies `timingPort`.
- **Requests:** exactly 32 bytes, big-endian, type `0xd2`. Only datagrams from
  the receiver's address are answered, from any source port. Anything else is
  dropped and counted, and never ends the responder. That includes Windows
  oversized-datagram and ICMP-reset errors.
- **Reply:** the request's protocol byte, type `0xd3`, sequence 7, zero
  padding. The reference time is the request's send time; receive and send
  times come from the wall clock, at microsecond resolution (NTP seconds =
  Unix seconds + 2,208,988,800, modulo 2^32).
- **Lifecycle:** `serve()` runs on one thread until the operation is cancelled
  (`ReceiverOperation::until_cancelled`), then throws that category. Socket
  failures are terminal and close the socket.

The native socket helpers (Winsock runtime, socket owner, numeric addresses,
nonblocking setup, readiness polling) moved unchanged from `receiver_stream.cpp`
into the private `native_socket.*`, so TCP and UDP share one deadline and
cancellation model.

## Evidence and provenance

No new dependency or external implementation source was added. The in-tree TCP
adapter uses platform sockets and existing OpenSSL/Botan adapters. The repository
remains Apache-2.0. Primary protocol references inspected on 2026-10-06:

- [RFC 9112](https://www.rfc-editor.org/rfc/rfc9112.html), sections 5/6: header
  syntax and framing. The profile deliberately rejects several valid general HTTP
  features rather than implementing them.
- MIT-licensed pyatv `pyatv/protocols/airplay/auth/hap.py`, blob
  `8e6ab4a5774ab7055e89526812b9b2caef5f7500`, and `pyatv/support/http.py`, blob
  `f7d2d74b4203f3597da4dd17eb03ffcf296e4ef2`: pairing endpoints, binary content,
  X-Apple-HKP 3, ordered HTTP pairing requests and the compatibility User-Agent
  AirPlay/320.20. These are research inputs, not proof of tvOS behavior. No source
  copied/linked; license blob `c27c9705f92fb19e805a82225f6f9ab5c17966f3` verified.

`receiver_tests` exercises literal request bytes, every response split/EOF prefix,
bytewise and coalesced header/body input, body/header/field limits, injection and
ambiguous framing. Existing independent PIN fixtures run through fragmented I/O;
proof/signature failures and cancelled prompts close enrollment. A dynamic
synthetic accessory signs the client's fresh ephemeral transcript, checks its
signature and exchanges encrypted RTSP messages across partial writes. It shares
the existing crypto adapters and is not an independent cryptographic oracle;
existing RFC/Python fixtures remain the independent checks. Bad identity, tag,
replay, partial trailing records, I/O failures, sequence exhaustion and deadlines
are terminal. Real IPv4/IPv6 loopback sockets check I/O, cancellation while waiting,
idle-read timeout, EOF, refused connect and queued unsolicited input.

Windows static/shared Release CMake/CTest results are recorded in HANDOFF.md.
Linux/macOS/sanitizer CI must be verified at the actual PR head. No receiver
connection, PIN display/entry, credential change/save or playback was attempted.

Subsequent [credential storage/CLI authentication](credential-storage.md) adds
the private credential codec, Windows desktop store and hidden PIN/reconnect
commands. Next validate the Apple TV 4K / tvOS 26.6 / Windows 11 x64 path.
Resolve the Botan UWP packaging constraint before Screenbox integration.
