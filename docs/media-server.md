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
and public file/StorageFile/content-URI adapters remain future work.

## Resource sets (D60)

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
  UWP builds (D55, [uwp-native-build.md](uwp-native-build.md)). No unbounded application connection/task queue
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

## Opt-in request diagnostics

Set `record_request_diagnostics=true` and drain `take_request_log()` to inspect
closed requests. A fixed 256-record ring drops the oldest entries on overflow;
recording allocates no memory. Draining is thread-safe, can allocate, and leaves
the queue intact if allocation fails. Requests retain their admission slot until
source and network callbacks finish, so a cancelled write's partial byte count
is included before the record is emitted. `stop()` drains all accepted work.

Records contain a local request number, GET/HEAD/other, numeric response status,
selected offset, declared length, expected body length, body bytes reported by
socket writes, header completion, active requests on acceptance, and steady-clock
accept/last-body-write/close milliseconds since construction. No peer addresses,
URLs, arbitrary headers, metadata or payloads are included. Terminal categories
are complete, cancelled, timeout, I/O, source or internal error. HEAD declares the
representation length but expects/writes no body. A source failure before headers
can finish an empty HTTP 500 while still being classified as a source error.

Socket-write completion means delivery to the local TCP stack. It proves neither
receiver receipt nor video decoding; source read counts prove even less. The
native `cast --media-log` flag prints these records. `--event-log` additionally
prints only allowlisted finite numeric/boolean buffering values (rate, position,
duration, readyToPlay, stallCount) from root/params/value dictionaries, separately
from the existing event outlines. Diagnostic time values accept zero and valid
numeric CMTime; values over 1e9 seconds and stall counts outside uint32 are omitted.
Unknown strings/metadata never appear in the scalar records. No playback,
retry, range or timeout policy is changed by enabling these logging flags.

For the controlled receiver comparison, `cast --minimal-remote` selects the
existing minimum-session experiment: independently verified remote SETUP/events
remain open, but remote RECORD, the data stream, MRP handshake/subscription/
heartbeat and remote feedback are omitted. The default still enables all MRP
controls. This explicit diagnostic mode has no MRP ownership/position/control
telemetry; a URL stopped/idle event is classified receiver_stop without MRP's
EOF evidence. Require separate human video/audio/Home observations and keep its
results separate from normal control-enabled casting. It is not a recovery mode.

`cast --media-connections N` selects the bounded server limit (1..16). D36 makes
16 the normal `cast` default after explicit 16-slot full-clip, control and selected
lifecycle checks on the recorded Apple TV/tvOS/Windows combination. Four slots
were occupied by long-lived read-ahead responses while needed ranges waited; the
higher-capacity traces admitted five/six concurrent requests. This is a bounded
admission choice, not a decoding guarantee or a fix for the intermittent startup
pause. See the [receiver evidence](receiver-validation.md#bounded-cast-admission-policy-and-controlslifecycle-checks-2026-10-07).

The option changes both active request slots and the maximum source-worker count.
Each slot has one 64-KiB buffer: `cast` now permits up to 1 MiB of body buffers and
16 source workers, versus 256 KiB/four workers before. There is no application
connection queue or eviction/retry of active ranges. The 600,000-ms per-request
cast budget is unchanged. Generic `MediaServerOptions` and `serve` still default
to four; callers can explicitly request a lower `cast` budget. Independent URL/MRP
sessions, credentials and URL-before-remote teardown remain unchanged.

## Development CLI (`serve`)

`airplay2-cli serve --address IP --file PATH` exposes one local file through this
server for manual receiver tests. It is a development tool, not a playback API.
Options: `--port` (receiver port used only for route selection, default 7000),
`--listen-port` (default ephemeral), `--timeout-ms` (default and maximum 600000,
because a receiver may hold one open-ended range request while it buffers),
`--max-connections` (1..16, default 4) and `--content-type` (default `video/mp4`).

- The private file adapter (`src/file_media_source.*`) opens the file once and
  snapshots its size. Reads seek and read one shared `std::ifstream` under a
  mutex, so concurrent workers are serialized; this favors portability over
  parallel throughput. The file must not change while it is served: a shorter
  file fails the affected request, and appended bytes are never served. On
  Windows, paths come from the narrow command line and must be representable
  in the active code page.
- The URL is printed once to standard output for the operator, labelled
  private; diagnostics never repeat it. A line or end-of-file on standard input
  stops the server, which joins all callbacks before the summary
  `Stopped. reads=N bytes=M failed_reads=F span=[first,end)` is printed. The
  summary reports aggregate offsets and counts only. Ctrl+C terminates without
  the summary.
- Exit codes: 0 after a clean stop, 2 for invalid arguments or an unusable
  path/address/content type, 1 for runtime failures such as an unreadable file
  or listener setup failure.

`file_source_tests` checks exact bytes at literal offsets, reads crossing EOF,
statistics, cancellation, four concurrent readers, a file shrinking after open
(skipped where the host refuses the resize), a 5-GiB sparse-file offset
(skipped on Windows to avoid writing gigabytes) and a loopback range request
through `MediaServer`. `cli_serve` runs the real CLI: it checks argument refusals
without printing a URL, and a start/stop cycle driven by end-of-file. Neither
contacts a receiver or opens a firewall.

`airplay2-cli cast --hls PLAYLIST.m3u8` (development, D60) serves a pre-made
HLS presentation as a resource set: the playlist and exactly the files it, and
any playlist it names, references by plain file name in the same directory.
Absolute URLs, subdirectories, queries and unknown extensions are refused.
Each file's size is snapshotted at start and every read reopens the file, so a
long presentation holds no open handles. `hls_directory_tests` covers
references, content types and refusals with temporary files.

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
targets. At head `130a71c490dde25699301915a7a509b72b95b5dc`, all ten
[PR CI checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37461159947)
passed: native static/shared CTest on Windows/Linux/macOS, Linux ASan/UBSan and
offline runner contracts on all three platforms. The macOS IP-restriction fixture
uses an ordinary loopback client with a different allowed peer IP, rather than
binding an unconfigured loopback alias. Check the actual final PR head separately.
Virtual >4-GiB sources verify offset/length arithmetic without claiming a real
large-file or Apple TV seek result. The file adapter and `serve` are implemented;
reference video/audio, receiver fetch and firewall reachability passed. Native
`cast` initially fetched the full MP4 without visible presentation. The minimal
native remote-control SETUP/event session then passed G1. Native MRP controls
and automatic session cleanup are implemented; EOF and ten short native cycles
have selected receiver evidence. Remaining manual G3 validation, packaged/brokered sources,
real-file >4-GiB seeking and network-change checks remain pending; see the
[receiver record](receiver-validation.md) and [session plan](session-design.md).
