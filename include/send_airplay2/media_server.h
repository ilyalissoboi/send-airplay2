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

struct MediaServerOptions {
    std::string receiver_address; // Numeric unicast IPv4/non-link-local IPv6; no DNS/scoped IPv6.
    std::uint16_t receiver_port = 7000; // Used only to select the local route; no datagram sent.
    std::uint16_t listen_port = 0;      // Zero selects an ephemeral TCP port.
    std::uint32_t max_connections = 4;  // 1..16; no application connection queue.
    std::uint32_t request_timeout_ms = 30000; // 1..600000, absolute per-request deadline.
    std::string content_type =
        "video/mp4"; // Plain type/subtype; no parameters or header injection.
};

/** Experimental C++17 media server, with no Boost types in its public interface.
 * Shared-library hosts require a compatible compiler/runtime. A versioned C ABI,
 * file adapters and playback integration are later gates.
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
    SAP2_API ~MediaServer();
    MediaServer(const MediaServer&) = delete;
    MediaServer& operator=(const MediaServer&) = delete;
    MediaServer(MediaServer&&) = delete;
    MediaServer& operator=(MediaServer&&) = delete;

    /// Owned URL copy, stable for the server lifetime; no logging or persistence.
    [[nodiscard]] SAP2_API std::string url() const;
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
