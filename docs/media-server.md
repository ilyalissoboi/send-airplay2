# Bounded HTTP media server

The experimental C++17 API in `include/send_airplay2/media_server.h` serves one
immutable byte source. Boost.Beast owns HTTP/1 parsing and header serialization;
Boost.Asio owns networking, timers and worker execution. The existing range
resolver supplies the project's single-range policy. No HTTP parser, socket
adapter or upstream server example is copied into this implementation.
See [dependency provenance](dependencies.md) for pinned Boost 1.92.0 and licensing.

This slice does not issue AirPlay playback commands. The authentication CLI is
unchanged, and the noninteractive E2E runner still covers its previous actions.
Library/loopback success does not establish Apple TV fetching or decoding.

## Source and ownership contract

Pass `MediaSource` callbacks and `MediaServerOptions` to `MediaServer::start()`.
The server owns the callback objects. `size()` runs once on the calling thread
before worker startup; its 64-bit result remains the representation size for the
entire session. It must return promptly and may propagate an exception from the
host. Missing callbacks or invalid options throw `std::invalid_argument`.
Network/RNG setup failures throw a sanitized `std::runtime_error`.

`read_at(offset, output, capacity, context)` reads at an absolute 64-bit offset.
Return positive progress up to capacity; shorter successful reads are retried at
the next offset. Capacity is at most 64 KiB and never extends beyond the declared
representation. Zero before the declared end, a returned count above capacity,
or an exception fails the request. A failure before sending the success header
produces an empty HTTP 500; a later failure closes with an incomplete body.
The server never serializes callback exception text.

The source must be immutable and support concurrent reads on up to
`max_connections` threads. Hosts needing serialized brokered access must implement
that inside their adapter. No output pointer or `MediaReadContext` may be retained.
Callback captures/resources must remain valid until `stop()` returns.
Callbacks must check `context.should_stop()` during slow work and return promptly
on cancellation/deadline. The implementation cannot interrupt arbitrary host code.
Calling `stop()` or destroying the server from its callbacks is prohibited.

`stop()` signals callbacks, cancels/closes sockets and joins all workers. It is
idempotent, and no callback runs after it returns. Hosts serialize stop/destruction;
the immutable owned string returned by `url()` can be read concurrently. A server
cannot be restarted; create a new instance and bearer path for a new session.
Shared-library users require a compatible C++ compiler/runtime; a versioned C ABI
and file/StorageFile/content-URI adapters remain future work.

## Network and resource policy

- Supply a numeric unicast receiver address and port. No DNS lookup is performed.
  IPv4 and non-link-local IPv6 are supported, including loopback for tests.
  Scoped/link-local IPv6 and mapped IPv6 are declined: sender-local scope IDs
  cannot safely be embedded into a URL intended for another device.
- A UDP route selection socket connects without transmitting a datagram. Its
  concrete local address is bound by the TCP listener, never a wildcard address.
  `listen_port=0` selects an ephemeral port. Route selection does not prove inbound
  firewall reachability or that the receiver will use the selected source address.
- Only clients with the selected receiver's source IP are admitted. A random
  128-bit URL path is created using OpenSSL's RNG. Treat the entire `url()` as
  private; do not put it into logs, reports or repository fixtures. The protocol
  uses plaintext HTTP: these restrictions do not authenticate peers or encrypt
  media, and IP spoofing/proxies/NAT are outside this access policy.
- `max_connections` is 1..16 (default 4). One network executor and at most that
  many source worker threads run. No unbounded application connection/task queue
  exists; acceptance pauses at the limit. The OS manages its bounded listen
  backlog. Cancelled sessions retain their admission slot until their source
  callback finishes, so cancellation cannot create an unbounded callback queue.
- Headers are bounded to 8192 bytes, 64 fields, and 2048 bytes per parsed value
  and request target. Each active request has a fixed 64-KiB streaming buffer;
  no full representation is buffered. HTTP syntax/normalization, including folded
  fields, is handled by Beast. Duplicate normalized field names are declined.
- `request_timeout_ms` is 1..600000 (default 30000). One steady-clock deadline
  starts at acceptance and covers headers, source reads and response writes.
  Progress never renews it. It does not cover waiting in the OS listen backlog.
  Choose the budget for expected transfer lengths; large/slow full GETs can exceed
  it even though their memory usage remains bounded. Cooperative source behavior
  is necessary for prompt callback shutdown.

No firewall rules, receiver settings, credentials or network interfaces are
modified. Network changes require a new server/route choice; address fallback,
LAN interruption and actual receiver callback reachability remain device gates.

## HTTP behavior

Each TCP connection serves at most one HTTP/1.0 or HTTP/1.1 request, then closes.
The target must exactly match the session path. HTTP/1.1 requires Host; when
supplied, Host must match the advertised URL's numeric authority. Request bodies,
Transfer-Encoding, Expect, duplicate fields and buffered pipelining are declined.
Extra requests arriving later are never served on that connection.

| Request/result | Response |
|---|---|
| GET, absent/ignored Range | 200 with exact representation Content-Length |
| GET, supported single Range | 206 with exact Content-Length and Content-Range |
| GET, unsatisfiable Range | 416 with zero body and `Content-Range: bytes */size` |
| HEAD | 200, full representation Content-Length, no source reads/body; Range ignored |
| If-Range present | No validator is available, so Range is ignored and GET returns full content |
| Unsupported conditional preconditions | 400; validators/conditional caching are deferred |
| Unknown path | Empty 404 |
| Other method on session path | Empty 405 with `Allow: GET, HEAD` |
| Invalid/bounded request | Empty 400 or 431, if the connection still permits a response |
| Initial source failure | Empty 500 |
| Deadline, stop, I/O or later source failure | Close; never pad or pretend an incomplete body succeeded |

All responses disable caching (`Cache-Control: no-store`). Successful content
responses advertise `Accept-Ranges: bytes` and the configured plain MIME
type/subtype (default `video/mp4`). Content is neither inspected, transcoded nor
compressed; the host is responsible for accurate media metadata. Malformed,
overflowing, unknown-unit and multipart ranges use the existing full-response
policy, as documented in [design.md](design.md).

## Validation and next gate

`media_server_tests` uses independent literal HTTP requests and patterned virtual
byte sources over real loopback TCP. It checks GET/HEAD lengths and binary bodies,
single/ignored/unsatisfied ranges, bytewise request delivery, zero-length sources,
offsets above 4 GiB and the uint64 upper boundary, partial source reads, malformed
and oversized requests, receiver-IP/path restrictions, exception/truncation
handling, concurrent admission, absolute deadlines and shutdown/recovery.
IPv6 loopback is tested when an independent host probe supports it; an unavailable
IPv6 host is an explicit skip. No private profiles/media or Apple TV are accessed.

Windows 11 x64 / MSVC Release static and shared builds each pass all 13 CTest
targets. Cross-platform/sanitizer evidence belongs to the actual PR head.
Virtual >4-GiB sources verify offset/length arithmetic without claiming a real
large-file adapter or Apple TV seek result. Reference pyatv playback of an owned
H.264/AAC MP4, real receiver HTTP fetch/ranges, firewall reachability and native
authenticated playback remain pending. Next add the authenticated playback
session and host media adapter, using the same clip as the reference baseline.
