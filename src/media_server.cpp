// SPDX-License-Identifier: Apache-2.0
#include "send_airplay2/media_server.h"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <openssl/rand.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace send_airplay2 {
namespace {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using Tcp = asio::ip::tcp;
using ErrorCode = boost::system::error_code;
constexpr std::size_t header_limit = 8192;
constexpr std::size_t field_limit = 64;
constexpr std::size_t field_value_limit = 2048;
constexpr std::size_t body_chunk_size = 64 * 1024;
constexpr std::size_t token_bytes = 16;
constexpr std::size_t request_log_limit = 256;
constexpr std::uint32_t max_connections_limit = 16;
constexpr std::uint32_t max_request_timeout_ms = 600000;
constexpr std::size_t max_numeric_address_length = 64;
constexpr std::size_t max_content_type_length = 128;
constexpr std::size_t max_resource_name_length = 64;
constexpr std::size_t max_resource_count = 65536;

[[noreturn]] void invalid_options() {
    throw std::invalid_argument("Invalid media server options");
}
[[noreturn]] void setup_failed() {
    throw std::runtime_error("Media server setup failed");
}
bool mime_character(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '+' || c == '.';
}
/// Plain type/subtype only, so a value can never add parameters or headers.
void validate_content_type(std::string_view type) {
    const auto slash = type.find('/');
    if (type.size() > max_content_type_length || slash == 0 || slash == std::string_view::npos ||
        slash + 1 == type.size()) {
        invalid_options();
    }
    for (std::size_t i = 0; i < type.size(); ++i) {
        if (i != slash && !mime_character(type[i])) {
            invalid_options();
        }
    }
}
bool alphanumeric(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
/// One path segment that needs no percent-encoding and cannot be "." or "..".
bool valid_resource_name(std::string_view name) {
    if (name.empty() || name.size() > max_resource_name_length || !alphanumeric(name.front())) {
        return false;
    }
    return std::all_of(name.begin(), name.end(),
                       [](char c) { return alphanumeric(c) || c == '.' || c == '_' || c == '-'; });
}
asio::ip::address receiver_address(const MediaServerOptions& options) {
    if (!options.receiver_port || !options.max_connections ||
        options.max_connections > max_connections_limit || !options.request_timeout_ms ||
        options.request_timeout_ms > max_request_timeout_ms ||
        options.receiver_address.size() > max_numeric_address_length ||
        options.receiver_address.find('\0') != std::string::npos) {
        invalid_options();
    }
    validate_content_type(options.content_type);
    ErrorCode error;
    const auto address = asio::ip::make_address(options.receiver_address, error);
    if (error || address.is_unspecified() || address.is_multicast() ||
        (address.is_v4() && address.to_v4().to_uint() == 0xffffffffU) ||
        (address.is_v6() && (address.to_v6().is_link_local() || address.to_v6().scope_id() ||
                             address.to_v6().is_v4_mapped()))) {
        invalid_options();
    }
    return address;
}
std::string random_path() {
    std::array<unsigned char, token_bytes> token{};
    if (RAND_bytes(token.data(), static_cast<int>(token.size())) != 1) {
        setup_failed();
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string path = "/media/";
    for (const auto byte : token) {
        path += hex[byte >> 4];
        path += hex[byte & 15];
    }
    return path;
}

/**
 * Runs blocking MediaSource reads off the network thread: a fixed set of
 * std::threads drains one io_context. join() lets queued reads finish, then
 * joins every thread; it is idempotent and the destructor calls it.
 *
 * asio::thread_pool is deliberately not used. In app (UWP) builds Boost 1.92
 * implements its threads with winapp_thread, whose join() leaves the thread
 * marked joinable, so destroying a joined pool calls std::terminate (D55).
 * Construction joins any threads already started if a later one fails.
 */
class ReaderPool {
public:
    explicit ReaderPool(std::size_t thread_count) {
        workers_.reserve(thread_count);
        try {
            for (std::size_t index = 0; index < thread_count; ++index) {
                workers_.emplace_back([this] { run(); });
            }
        } catch (...) {
            join();
            throw;
        }
    }
    ~ReaderPool() {
        join();
    }
    ReaderPool(const ReaderPool&) = delete;
    ReaderPool& operator=(const ReaderPool&) = delete;
    ReaderPool(ReaderPool&&) = delete;
    ReaderPool& operator=(ReaderPool&&) = delete;

    [[nodiscard]] asio::io_context::executor_type get_executor() noexcept {
        return context_.get_executor();
    }
    /// Lets queued work finish, then joins. Call from a thread outside the pool.
    void join() {
        work_.reset();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

private:
    void run() {
        for (;;) {
            try {
                context_.run();
                return;
            } catch (...) {
                // Read handlers contain their own failures; keep draining so
                // join() never waits on work that no thread will run.
            }
        }
    }

    asio::io_context context_;
    asio::executor_work_guard<asio::io_context::executor_type> work_{context_.get_executor()};
    std::vector<std::thread> workers_;
};
} // namespace

struct MediaServer::Impl {
    struct Session;
    /// One served representation; its size is the snapshot taken at start.
    /// A size computed on the first request that needs it, then kept.
    struct DeferredSize {
        std::function<std::uint64_t(const MediaReadContext&)> compute;
        std::mutex mutex; // Held while computing, so concurrent first requests wait.
        std::optional<std::uint64_t> value;
    };
    struct Resource {
        std::string name; // Empty for the single-source server.
        std::string content_type;
        std::uint64_t size = 0; // Unused while `deferred` has no value.
        MediaSource source;
        std::shared_ptr<DeferredSize> deferred; // Set for size_on_request resources.
    };
    // Fixed after construction, so workers read them without locking.
    std::vector<Resource> resources;
    std::map<std::string, std::size_t, std::less<>> resource_index; // Resource sets only.
    bool resource_set = false;
    MediaServerOptions options;
    asio::ip::address receiver;
    std::string path;
    std::string authority;
    std::string media_url;
    std::atomic_bool stopped{false};
    asio::io_context network;
    asio::executor_work_guard<asio::io_context::executor_type> work{network.get_executor()};
    Tcp::acceptor listener{network};
    std::unique_ptr<ReaderPool> readers;
    std::thread network_thread;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::uint64_t next_request_id = 1; // Network thread only.
    std::mutex log_mutex;
    std::array<MediaRequestDiagnostic, request_log_limit> request_log{};
    std::size_t log_begin = 0, log_size = 0;

    std::uint64_t elapsed_ms() const {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - started)
                                              .count());
    }
    void record(const MediaRequestDiagnostic& entry) {
        if (!options.record_request_diagnostics) {
            return;
        }
        std::lock_guard<std::mutex> lock(log_mutex);
        if (log_size == request_log_limit) {
            log_begin = (log_begin + 1) % request_log_limit;
            --log_size;
        }
        request_log[(log_begin + log_size++) % request_log_limit] = entry;
    }
    std::vector<MediaRequestDiagnostic> take_request_log() {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::vector<MediaRequestDiagnostic> entries;
        entries.reserve(log_size);
        for (std::size_t i = 0; i < log_size; ++i) {
            entries.push_back(request_log[(log_begin + i) % request_log_limit]);
        }
        log_begin = log_size = 0;
        return entries;
    }
    // Only the network thread mutates sessions; pending source work retains its slot.
    std::set<std::shared_ptr<Session>> sessions;

    Impl(std::vector<MediaResource> input, bool named, MediaServerOptions config)
        : resource_set(named), options(std::move(config)), receiver(receiver_address(options)),
          path(random_path()) {
        if (input.empty() || input.size() > max_resource_count || (!named && input.size() != 1)) {
            invalid_options();
        }
        resources.reserve(input.size());
        for (auto& resource : input) {
            if (!resource.source.read_at || (!resource.source.size && !resource.size_on_request)) {
                invalid_options();
            }
            if (named) {
                validate_content_type(resource.content_type);
                if (!valid_resource_name(resource.name) ||
                    !resource_index.emplace(resource.name, resources.size()).second) {
                    invalid_options();
                }
            }
            std::shared_ptr<DeferredSize> deferred;
            if (resource.size_on_request) {
                deferred = std::make_shared<DeferredSize>();
                deferred->compute = std::move(resource.size_on_request);
            }
            resources.push_back({named ? std::move(resource.name) : std::string{},
                                 named ? std::move(resource.content_type) : options.content_type, 0,
                                 std::move(resource.source), std::move(deferred)});
        }
        for (auto& resource : resources) {
            if (!resource.deferred) {
                resource.size = resource.source.size();
            }
        }
        ErrorCode error;
        // UDP connect selects a route without sending anything to the receiver.
        asio::ip::udp::socket route(network);
        route.open(receiver.is_v4() ? asio::ip::udp::v4() : asio::ip::udp::v6(), error);
        if (error) {
            setup_failed();
        }
        route.connect({receiver, options.receiver_port}, error);
        if (error) {
            setup_failed();
        }
        const auto local = route.local_endpoint(error).address();
        if (error || local.is_unspecified() ||
            (local.is_v6() && (local.to_v6().is_link_local() || local.to_v6().scope_id()))) {
            setup_failed();
        }
        listener.open(local.is_v4() ? Tcp::v4() : Tcp::v6(), error);
        if (error) {
            setup_failed();
        }
        if (local.is_v6()) {
            listener.set_option(asio::ip::v6_only(true), error);
            if (error) {
                setup_failed();
            }
        }
        listener.bind({local, options.listen_port}, error);
        if (error) {
            setup_failed();
        }
        listener.listen(static_cast<int>(options.max_connections), error);
        if (error) {
            setup_failed();
        }
        const auto endpoint = listener.local_endpoint(error);
        if (error) {
            setup_failed();
        }
        authority = (local.is_v6() ? "[" + local.to_string() + "]" : local.to_string()) + ":" +
                    std::to_string(endpoint.port());
        media_url = "http://" + authority + path;
        readers = std::make_unique<ReaderPool>(options.max_connections);
    }
    ~Impl() {
        stop();
    }
    /// The resource a request target names, or null (404). The single-source
    /// server answers only its bearer path; a set answers only "path/name".
    const Resource* find(std::string_view target) const {
        if (!resource_set) {
            return target == path ? &resources.front() : nullptr;
        }
        if (target.size() <= path.size() + 1 || target.compare(0, path.size(), path) != 0 ||
            target[path.size()] != '/') {
            return nullptr;
        }
        const auto entry = resource_index.find(target.substr(path.size() + 1));
        return entry == resource_index.end() ? nullptr : &resources[entry->second];
    }
    void start();
    void accept();
    void remove(const std::shared_ptr<Session>& session);
    void close_network();
    void stop();
};

struct MediaServer::Impl::Session : std::enable_shared_from_this<Session> {
    Impl& server;
    Tcp::socket socket;
    asio::steady_timer deadline_timer;
    const std::chrono::steady_clock::time_point deadline;
    beast::flat_static_buffer<header_limit> input;
    http::request_parser<http::empty_body> parser;
    http::response<http::empty_body> response{http::status::ok, 11};
    std::unique_ptr<http::response_serializer<http::empty_body>> serializer;
    std::array<std::uint8_t, body_chunk_size> chunk{};
    std::atomic_bool cancelled{false};
    const Resource* resource = nullptr; // Set once the target resolves; owned by the server.
    std::uint64_t offset = 0;
    std::uint64_t remaining = 0;
    std::size_t chunk_length = 0;
    bool reading_source = false;
    bool closed = false;
    bool header_sent = false;
    bool pending_network = false; // One header read, header write or body write at a time.
    bool source_failed = false;
    MediaRequestDiagnostic diagnostic;

    Session(Impl& owner, Tcp::socket accepted)
        : server(owner), socket(std::move(accepted)), deadline_timer(server.network),
          deadline(std::chrono::steady_clock::now() +
                   std::chrono::milliseconds(server.options.request_timeout_ms)) {
        diagnostic.request_id = server.next_request_id++;
        diagnostic.accepted_ms = server.elapsed_ms();
        diagnostic.active_on_accept = static_cast<std::uint32_t>(server.sessions.size() + 1);
        parser.header_limit(static_cast<std::uint32_t>(header_limit));
        parser.body_limit(0);
    }
    /// Keep the slot until cancelled callbacks report any partial socket writes.
    void release_if_drained() {
        if (closed && !reading_source && !pending_network) {
            diagnostic.closed_ms = server.elapsed_ms();
            server.remove(shared_from_this());
        }
    }
    void close(MediaRequestEnd reason = MediaRequestEnd::cancelled) {
        if (!closed) {
            diagnostic.end = source_failed && reason == MediaRequestEnd::complete
                                 ? MediaRequestEnd::source_error
                                 : reason;
            closed = true;
            cancelled.store(true, std::memory_order_relaxed);
            ErrorCode ignored;
            deadline_timer.cancel();
            socket.close(ignored);
        }
        release_if_drained();
    }
    void start() {
        deadline_timer.expires_at(deadline);
        deadline_timer.async_wait([self = shared_from_this()](ErrorCode error) {
            if (!error) {
                self->close(MediaRequestEnd::timeout);
            }
        });
        http::async_read_header(
            socket, input, parser, [self = shared_from_this()](ErrorCode error, std::size_t) {
                self->pending_network = false;
                if (self->closed) {
                    self->release_if_drained();
                    return;
                }
                try {
                    if (error) {
                        self->send_error(error == http::error::header_limit ||
                                                 error == http::error::buffer_overflow
                                             ? http::status::request_header_fields_too_large
                                             : http::status::bad_request);
                    } else {
                        self->prepare_response();
                    }
                } catch (...) {
                    self->close(MediaRequestEnd::internal_error);
                }
            });
        pending_network = true; // Initiation succeeded; handlers never run inline.
    }
    void send_error(http::status status) {
        remaining = 0;
        diagnostic.expected_body_bytes = 0;
        diagnostic.declared_length = 0;
        response.result(status);
        response.erase(http::field::content_range);
        response.content_length(0);
        if (status == http::status::method_not_allowed) {
            response.set(http::field::allow, "GET, HEAD");
        }
        send_header();
    }
    bool valid_headers() const {
        const auto& request = parser.get();
        if ((request.version() != 10 && request.version() != 11) || input.size() ||
            request.target().size() > field_value_limit ||
            request.count(http::field::transfer_encoding) || request.count(http::field::expect) ||
            parser.content_length().value_or(0) != 0) {
            return false;
        }
        std::size_t count = 0;
        std::set<std::string> names;
        for (const auto& field : request) {
            std::string name(field.name_string());
            std::transform(name.begin(), name.end(), name.begin(), [](char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
            });
            if (++count > field_limit || field.value().size() > field_value_limit ||
                !names.insert(std::move(name)).second) {
                return false;
            }
        }
        const auto host = request[http::field::host];
        if ((request.version() == 11 && host.empty()) ||
            (!host.empty() && host != server.authority)) {
            return false;
        }
        // No validators are exposed yet. Decline unsupported preconditions instead
        // of serving content after silently discarding a client's condition.
        return !request.count(http::field::if_match) &&
               !request.count(http::field::if_none_match) &&
               !request.count(http::field::if_modified_since) &&
               !request.count(http::field::if_unmodified_since);
    }
    void prepare_response() {
        const auto& request = parser.get();
        diagnostic.method = request.method() == http::verb::get    ? MediaRequestMethod::get
                            : request.method() == http::verb::head ? MediaRequestMethod::head
                                                                   : MediaRequestMethod::other;
        if (!valid_headers()) {
            send_error(http::status::bad_request);
            return;
        }
        response.version(request.version());
        resource = server.find(std::string_view(request.target().data(), request.target().size()));
        if (!resource) {
            send_error(http::status::not_found);
            return;
        }
        const bool head = request.method() == http::verb::head;
        if (!head && request.method() != http::verb::get) {
            send_error(http::status::method_not_allowed);
            return;
        }
        if (resource->deferred) {
            resolve_deferred_size();
            return;
        }
        respond(resource->size);
    }
    /// Computes a deferred size on a worker, then continues on the network
    /// thread. Only a completed computation for a live request is kept.
    void resolve_deferred_size() {
        reading_source = true;
        asio::post(server.readers->get_executor(), [self = shared_from_this()] {
            std::optional<std::uint64_t> size;
            const MediaReadContext context{&self->server.stopped, &self->cancelled, self->deadline};
            try {
                auto& deferred = *self->resource->deferred;
                std::lock_guard<std::mutex> lock(deferred.mutex);
                if (!deferred.value && !context.should_stop()) {
                    const auto computed = deferred.compute(context);
                    if (!context.should_stop()) {
                        deferred.value = computed;
                    }
                }
                size = deferred.value;
            } catch (...) {
                // Not kept: a later request computes it again. Never log the text.
            }
            asio::post(self->server.network, [self, size] {
                self->reading_source = false;
                if (self->closed || self->server.stopped.load(std::memory_order_relaxed) ||
                    std::chrono::steady_clock::now() >= self->deadline) {
                    self->close(std::chrono::steady_clock::now() >= self->deadline
                                    ? MediaRequestEnd::timeout
                                    : MediaRequestEnd::cancelled);
                } else if (!size) {
                    self->source_failed = true;
                    self->send_error(http::status::internal_server_error);
                } else {
                    try {
                        self->respond(*size);
                    } catch (...) {
                        self->close(MediaRequestEnd::internal_error);
                    }
                }
            });
        });
    }
    /// The response for a representation of `representation_size` bytes.
    void respond(std::uint64_t representation_size) {
        const auto& request = parser.get();
        const bool head = request.method() == http::verb::head;
        // Range applies only to GET. Without a representation validator, If-Range
        // cannot match, so ignore Range and send the full representation.
        const auto range = head || request.count(http::field::if_range)
                               ? beast::string_view{}
                               : request[http::field::range];
        sap2_byte_range selection{};
        const auto result =
            sap2_resolve_http_range(range.data(), range.size(), representation_size, &selection);
        response.set(http::field::accept_ranges, "bytes");
        response.set(http::field::content_type, resource->content_type);
        if (result == SAP2_RANGE_UNSATISFIABLE) {
            response.set(http::field::content_range,
                         "bytes */" + std::to_string(representation_size));
            response.result(http::status::range_not_satisfiable);
            response.content_length(0);
            send_header();
            return;
        }
        if (result == SAP2_RANGE_PARTIAL) {
            response.result(http::status::partial_content);
            response.set(http::field::content_range,
                         "bytes " + std::to_string(selection.offset) + "-" +
                             std::to_string(selection.offset + selection.length - 1) + "/" +
                             std::to_string(representation_size));
        }
        response.content_length(selection.length);
        offset = selection.offset;
        remaining = head ? 0 : selection.length;
        diagnostic.offset = selection.offset;
        diagnostic.declared_length = selection.length;
        diagnostic.expected_body_bytes = remaining;
        if (remaining) {
            read_chunk(); // Detect initial source failure before committing a success header.
        } else {
            send_header();
        }
    }
    void send_header() {
        if (closed || server.stopped.load(std::memory_order_relaxed)) {
            close();
            return;
        }
        response.keep_alive(false);
        response.set(http::field::cache_control, "no-store");
        serializer = std::make_unique<http::response_serializer<http::empty_body>>(response);
        diagnostic.status = response.result_int();
        http::async_write_header(socket, *serializer,
                                 [self = shared_from_this()](ErrorCode error, std::size_t) {
                                     self->pending_network = false;
                                     self->diagnostic.header_completed = !error;
                                     if (self->closed) {
                                         self->release_if_drained();
                                         return;
                                     }
                                     if (error) {
                                         self->close(MediaRequestEnd::io_error);
                                     } else if (!self->remaining) {
                                         self->close(MediaRequestEnd::complete);
                                     } else {
                                         self->header_sent = true;
                                         self->write_chunk();
                                     }
                                 });
        pending_network = true; // Initiation succeeded; handlers never run inline.
    }
    void read_chunk() {
        if (closed || server.stopped.load(std::memory_order_relaxed)) {
            close();
            return;
        }
        reading_source = true;
        const auto capacity =
            static_cast<std::size_t>(std::min<std::uint64_t>(remaining, body_chunk_size));
        asio::post(server.readers->get_executor(), [self = shared_from_this(), capacity] {
            std::size_t count = 0;
            const MediaReadContext context{&self->server.stopped, &self->cancelled, self->deadline};
            try {
                if (!context.should_stop()) {
                    count = self->resource->source.read_at(self->offset, self->chunk.data(),
                                                           capacity, context);
                }
            } catch (...) {
                // Exception text may contain host paths or secrets; never log it.
            }
            asio::post(self->server.network, [self, capacity, count] {
                self->reading_source = false;
                if (self->closed || self->server.stopped.load(std::memory_order_relaxed) ||
                    std::chrono::steady_clock::now() >= self->deadline) {
                    self->close(std::chrono::steady_clock::now() >= self->deadline
                                    ? MediaRequestEnd::timeout
                                    : MediaRequestEnd::cancelled);
                } else if (!count || count > capacity) {
                    self->source_failed = true;
                    if (self->header_sent) {
                        self->close(MediaRequestEnd::source_error); // Keep a short body incomplete.
                    } else {
                        self->send_error(http::status::internal_server_error);
                    }
                } else {
                    self->chunk_length = count;
                    if (self->header_sent) {
                        self->write_chunk();
                    } else {
                        self->send_header();
                    }
                }
            });
        });
    }
    void write_chunk() {
        asio::async_write(socket, asio::buffer(chunk.data(), chunk_length),
                          [self = shared_from_this()](ErrorCode error, std::size_t count) {
                              self->pending_network = false;
                              self->diagnostic.body_bytes_written += count;
                              if (count) {
                                  self->diagnostic.last_body_write_ms = self->server.elapsed_ms();
                              }
                              if (self->closed) {
                                  self->release_if_drained();
                                  return;
                              }
                              if (error) {
                                  self->close(MediaRequestEnd::io_error);
                                  return;
                              }
                              self->offset += self->chunk_length;
                              self->remaining -= self->chunk_length;
                              if (!self->remaining) {
                                  self->close(MediaRequestEnd::complete);
                              } else {
                                  self->read_chunk();
                              }
                          });
        pending_network = true; // Initiation succeeded; handlers never run inline.
    }
};

void MediaServer::Impl::start() {
    accept();
    network_thread = std::thread([this] {
        for (;;) {
            try {
                network.run();
                return;
            } catch (...) {
                // An internal handler failure ends serving. Keep draining the
                // executor so stop() can still close/join without a lost barrier.
                stopped.store(true, std::memory_order_relaxed);
                close_network();
            }
        }
    });
}
void MediaServer::Impl::accept() {
    if (stopped.load(std::memory_order_relaxed) || sessions.size() >= options.max_connections) {
        return;
    }
    listener.async_accept([this](ErrorCode error, Tcp::socket socket) {
        if (stopped.load(std::memory_order_relaxed)) {
            return;
        }
        if (error) {
            if (error == asio::error::connection_aborted ||
                error == asio::error::connection_reset) {
                accept(); // A client abort must not terminate the listening session.
                return;
            }
            stopped.store(true, std::memory_order_relaxed);
            close_network();
            return;
        }
        const auto peer = socket.remote_endpoint(error).address();
        if (!error && peer == receiver) {
            auto session = std::make_shared<Session>(*this, std::move(socket));
            sessions.insert(session);
            session->start();
        }
        accept();
    });
}
void MediaServer::Impl::remove(const std::shared_ptr<Session>& session) {
    const bool was_full = sessions.size() == options.max_connections;
    const auto removed = sessions.erase(session);
    if (removed) {
        record(session->diagnostic);
    }
    if (removed && was_full && !stopped.load(std::memory_order_relaxed)) {
        accept();
    }
}
void MediaServer::Impl::close_network() {
    ErrorCode ignored;
    listener.close(ignored);
    // close() can remove a session, so advance before calling it.
    for (auto iterator = sessions.begin(); iterator != sessions.end();) {
        auto session = *iterator++;
        session->close();
    }
}
void MediaServer::Impl::stop() {
    stopped.store(true, std::memory_order_relaxed);
    if (!network_thread.joinable()) {
        return;
    }
    // First prevent new callback work, then join callbacks, then drain cancelled
    // handlers. The owner/source stay alive until every posted completion drains.
    std::promise<void> closed;
    auto completion = closed.get_future();
    asio::post(network, [this, &closed] {
        close_network();
        closed.set_value();
    });
    completion.get();
    readers->join();
    work.reset();
    network_thread.join();
}
MediaServer::MediaServer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MediaServer::~MediaServer() = default;
std::unique_ptr<MediaServer> MediaServer::start(MediaSource source, MediaServerOptions options) {
    std::vector<MediaResource> single;
    single.push_back({{}, {}, std::move(source), {}});
    auto impl = std::make_unique<Impl>(std::move(single), false, std::move(options));
    auto server = std::unique_ptr<MediaServer>(new MediaServer(std::move(impl)));
    server->impl_->start();
    return server;
}
std::unique_ptr<MediaServer> MediaServer::start_resource_set(std::vector<MediaResource> resources,
                                                             MediaServerOptions options) {
    auto impl = std::make_unique<Impl>(std::move(resources), true, std::move(options));
    auto server = std::unique_ptr<MediaServer>(new MediaServer(std::move(impl)));
    server->impl_->start();
    return server;
}
std::string MediaServer::url() const {
    return impl_->media_url;
}
std::string MediaServer::resource_url(std::string_view name) const {
    if (!impl_->resource_set || impl_->resource_index.find(name) == impl_->resource_index.end()) {
        throw std::invalid_argument("Not a resource of this media server");
    }
    return impl_->media_url + "/" + std::string(name);
}
std::vector<MediaRequestDiagnostic> MediaServer::take_request_log() {
    return impl_->take_request_log();
}
void MediaServer::stop() {
    impl_->stop();
}
} // namespace send_airplay2
