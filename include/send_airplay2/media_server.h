// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_MEDIA_SERVER_H
#define SEND_AIRPLAY2_MEDIA_SERVER_H
#include "send_airplay2/http_range.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace send_airplay2 {
/** Borrowed only for the duration of read_at. Callbacks must poll should_stop()
 * during slow work and return promptly when true. The deadline covers the entire
 * request, including callback time; progress never extends it. */
struct MediaReadContext {
    const std::atomic_bool* server_stopped;
    const std::atomic_bool* request_cancelled;
    std::chrono::steady_clock::time_point deadline;
    [[nodiscard]] bool should_stop() const noexcept {
        return server_stopped->load(std::memory_order_relaxed) ||
               request_cancelled->load(std::memory_order_relaxed) ||
               std::chrono::steady_clock::now() >= deadline;
    }
};

/** Owned callback objects for one immutable representation. size() is called
 * exactly once, synchronously by start(), and may throw. read_at receives a
 * 64-bit offset and writable capacity <=64 KiB; return 1..capacity bytes of
 * progress. Short reads are supported; zero before the declared end, oversized
 * results and exceptions fail that request. No buffer/context may be retained.
 * Captured resources must remain valid until stop() returns. read_at can run
 * concurrently on at most max_connections worker threads; it must be thread-safe.
 * Callback code must not call stop() or destroy the server. size() must return
 * promptly; C++ cannot forcibly interrupt a blocked callback. */
struct MediaSource {
    std::function<std::uint64_t()> size;
    std::function<std::size_t(std::uint64_t, std::uint8_t*, std::size_t, const MediaReadContext&)>
        read_at;
};

/** One named representation of a resource set (MediaServer::start_resource_set).
 * The name is one URL path segment: 1..64 ASCII letters, digits, '.', '_' or
 * '-', starting with a letter or digit, unique within the set. Names appear in
 * URLs next to the private bearer path, so they should not reveal titles.
 * content_type follows MediaServerOptions::content_type's rules.
 *
 * size_on_request (optional) defers the size: when set, source.size is not
 * called; instead size_on_request runs on a worker thread for the first
 * request that needs the size (HEAD and GET alike), receiving that request's
 * context, and its result is kept for the server's lifetime. It may do slow
 * work, such as reading an index, and must poll should_stop(). Requests that
 * arrive while it runs wait for it. An exception, or a result after the
 * request stopped, is not kept: that request fails (HTTP 500 or closed) and a
 * later request calls it again. The value is the representation size, so
 * read_at must then serve exactly that many bytes. */
struct MediaResource {
    std::string name;
    std::string content_type;
    MediaSource source;
    std::function<std::uint64_t(const MediaReadContext&)> size_on_request;
};

/// The size `resource` is served with: size_on_request(context) when set,
/// otherwise source.size().
[[nodiscard]] inline std::uint64_t media_resource_size(const MediaResource& resource,
                                                       const MediaReadContext& context) {
    return resource.size_on_request ? resource.size_on_request(context) : resource.source.size();
}

struct MediaServerOptions {
    std::string receiver_address; // Numeric unicast IPv4/non-link-local IPv6; no DNS/scoped IPv6.
    std::uint16_t receiver_port = 7000; // Used only to select the local route; no datagram sent.
    std::uint16_t listen_port = 0;      // Zero selects an ephemeral TCP port.
    std::uint32_t max_connections = 4;  // 1..16; no application connection queue.
    std::uint32_t request_timeout_ms = 30000; // 1..600000, absolute per-request deadline.
    std::string content_type =
        "video/mp4"; // Plain type/subtype; no parameters or header injection.
                     // Resource sets use each resource's own type instead.
    bool record_request_diagnostics = false;
};

enum class MediaRequestMethod { other, get, head };
enum class MediaRequestEnd { complete, cancelled, timeout, io_error, source_error, internal_error };

/** Closed-request diagnostics contain no peer addresses, URLs, headers or payloads.
 * Byte counts cover body writes reported by the local socket, including partial
 * writes on failure; completion does not prove receiver receipt or decoding.
 * Times are steady-clock milliseconds since server construction. HEAD declares
 * a representation length but expects/writes zero body bytes. */
struct MediaRequestDiagnostic {
    std::uint64_t request_id = 0;
    MediaRequestMethod method = MediaRequestMethod::other;
    MediaRequestEnd end = MediaRequestEnd::cancelled;
    unsigned status = 0; // HTTP status at header serialization; zero if not reached.
    std::uint64_t offset = 0;
    std::uint64_t declared_length = 0;
    std::uint64_t expected_body_bytes = 0;
    std::uint64_t body_bytes_written = 0;
    std::uint64_t accepted_ms = 0;
    std::uint64_t last_body_write_ms = 0; // Zero if no body progress was reported.
    std::uint64_t closed_ms = 0;
    std::uint32_t active_on_accept = 0;
    bool header_completed = false;
};

/** Experimental C++17 media server, with no Boost types in its public interface.
 * Shared-library hosts require a compatible compiler/runtime. A versioned C ABI,
 * packaged-host adapters and public playback integration are later gates.
 *
 * The OS route selects one concrete bind address. No wildcard listener, DNS,
 * directory serving or firewall changes. Only the selected receiver's source IP
 * is accepted. url() includes a random 128-bit bearer path: keep it private. IP
 * restriction and URL secrecy do not encrypt media or authenticate a LAN peer.
 * Route selection does not prove receiver reachability through the firewall.
 * Each connection serves one bounded GET/HEAD request, then closes. */
class MediaServer {
public:
    /** Start workers/listener; invalid options throw std::invalid_argument,
     * setup/RNG failures throw std::runtime_error, size() exceptions propagate.
     * Failed construction releases all owned resources. No media is buffered in
     * full. Boost/OS details and private URLs are omitted from error messages. */
    [[nodiscard]] static SAP2_API std::unique_ptr<MediaServer> start(MediaSource source,
                                                                     MediaServerOptions options);
    /** Start a server for a fixed set of 1..65536 named resources, such as an
     * HLS playlist with its segments. Each resource is served at
     * resource_url(name), below the same private bearer path; the bearer path
     * itself and every other name answer 404. Every size() of a resource
     * without size_on_request is called once, in table order, by this call.
     * A resource needs source.read_at and either source.size or
     * size_on_request. Everything else, including the callback
     * contract, failures and threading, is as for start(). An invalid or
     * duplicate name or content type throws std::invalid_argument. */
    [[nodiscard]] static SAP2_API std::unique_ptr<MediaServer>
    start_resource_set(std::vector<MediaResource> resources, MediaServerOptions options);
    SAP2_API ~MediaServer();
    MediaServer(const MediaServer&) = delete;
    MediaServer& operator=(const MediaServer&) = delete;
    MediaServer(MediaServer&&) = delete;
    MediaServer& operator=(MediaServer&&) = delete;

    /// Owned URL copy, stable for the server lifetime; no logging or persistence.
    /// For a resource set it is the private base below which resources are served.
    [[nodiscard]] SAP2_API std::string url() const;
    /** URL of one resource of a resource set, as private as url(). Throws
     * std::invalid_argument for a name that is not in the set, and for every
     * name on a single-source server. */
    [[nodiscard]] SAP2_API std::string resource_url(std::string_view name) const;
    /** Drain up to 256 closed-request records, oldest first; overflow drops the
     * oldest record. Disabled by default. Thread-safe with serving and stop(),
     * subject to the server's lifetime. Recording allocates no memory; draining
     * may throw std::bad_alloc without discarding queued records. After stop(),
     * all accepted requests have completed or been cancelled. */
    [[nodiscard]] SAP2_API std::vector<MediaRequestDiagnostic> take_request_log();
    /** Idempotently cancel network work, signal callbacks and join all workers.
     * No callback runs after return. Calls/destruction must be serialized by the
     * host and run outside callbacks. Shutdown latency depends on cooperative
     * callbacks. url() can be read concurrently; it is unusable after stop(). */
    SAP2_API void stop();

private:
    struct Impl;
    explicit MediaServer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace send_airplay2
#endif
