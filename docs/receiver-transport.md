# Bounded receiver transport

This private C++17 slice connects the existing PIN setup, peer verification and
control-record processors to synchronous native TCP I/O. It is not a public ABI,
CLI pairing command, credential store, media server or playback implementation.
Apple TV interoperability remains untested; loopback/fake receivers are the test
gate for this PR.

## Connection and authentication lifecycle

`connect_receiver` accepts a numeric IPv4/IPv6 endpoint and an absolute deadline.
IPv6 scope IDs are explicit; no DNS resolution, address fallback or automatic
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

Next: define trusted credential serialization and host secret storage, add CLI
pairing/reconnect, then validate the Apple TV 4K / tvOS 26.6 / Windows 11 x64 path.
Resolve the Botan UWP packaging constraint before Screenbox integration.
