# Bounded HTTP media server

The C++ API in [media_server.h](../include/send_airplay2/media_server.h) serves
immutable callback-backed resources. Beast parses/serializes HTTP and Asio handles
bounded networking. The playback controller owns this server for a cast; it does
not itself authenticate receivers or issue playback commands.

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
Shared-library C++ users require a compatible compiler/runtime. Playback hosts
can instead use the versioned [C callback interface](public-api.md).

## Resource sets

`MediaServer::start_resource_set(resources, options)` serves a fixed table of
1..65,536 `MediaResource {name, content_type, source}` entries, such as an HLS
playlist with its init and media segments, at `resource_url(name)`:
`url()` (the random bearer path) plus `/name`. Names are single path segments
(1..64 ASCII letters, digits, `.`, `_`, `-`, starting with a letter or digit)
and unique; each content type follows the `content_type` rules, and
`options.content_type` is not used. The bearer path itself, any other name, any
extra path segment and `.`/`..` forms answer 404; there is no directory
serving. Each `size()` is called once at start, in table order; ranges, HEAD,
the receiver-only source check, the connection bound, deadlines and
diagnostics apply per request exactly as for one source. `resource_url` throws
`std::invalid_argument` for a name outside the set and on a single-source
server. Growing tables (live HLS) are not supported; see [hls.md](hls.md).

`MediaResource::size_on_request` (optional) defers a resource's size: its
`source.size` is not called; instead the first request that needs the size
(HEAD or GET) runs `size_on_request` on a worker thread with that request's
context, and the result is kept for the server's lifetime. Concurrent first
requests wait for one computation; an exception or a result after the request
stopped is not kept, that request answers 500 or closes, and a later request
computes it again. `media_resource_size()` gives the size either way. The
indexed MKV remux uses it so that segments are read only when requested.
`media_server_tests` covers the deferred size, its caching, a retried failure,
concurrent first requests and the construction rule.

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
  many source worker threads run. The workers are the server's own `std::thread`
  pool over one `io_context`, not `asio::thread_pool`, which aborts on join in
  UWP builds ([platform notes](uwp-native-build.md)). No unbounded application connection/task queue
  exists; acceptance pauses at the limit. The OS manages its bounded listen
  backlog. Cancelled sessions retain their admission slot until their source
  callback and pending network operation finish, so cancellation cannot create an unbounded callback queue.
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
LAN interruption and actual receiver callback reachability require receiver checks.

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
policy, rather than interpreting ambiguous ranges as partial-content requests.

## Byte ranges and diagnostics

The resolver supports one bounded byte range, open-ended range or suffix range.
End offsets clamp to the representation; ranges starting beyond its end are
unsatisfiable. Malformed, overflowing, unsupported-unit or multipart input is
ignored in favor of the complete representation. See
[http_range.h](../include/send_airplay2/http_range.h).

Opt-in request diagnostics use a fixed 256-record ring. Records include request
number, method category, numeric status/range/lengths, body bytes, timing and fixed
terminal category. They omit peers, URLs, arbitrary headers and payloads. Draining
is thread-safe; stop drains accepted work. Socket-write completion proves delivery
to the sender's TCP stack, not receiver decoding.

Generic MediaServer/serve defaults to four connections; normal casting uses
sixteen. Each slot has a 64-KiB body buffer and retains admission until its work
finishes. Raising admission is not evidence of decoder progress or a substitute
for bounded callbacks. `cast --media-log` enables request records; `--event-log`
adds allowlisted numeric/boolean playback diagnostics.
