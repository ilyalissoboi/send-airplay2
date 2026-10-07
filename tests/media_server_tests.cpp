// SPDX-License-Identifier: Apache-2.0
// Loopback wire evidence only: no Apple TV, PINs, credentials or private media.
#include "send_airplay2/media_server.h"
#include <boost/asio.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
namespace asio = boost::asio;
using Tcp = asio::ip::tcp;
using ErrorCode = boost::system::error_code;
using namespace send_airplay2;
using namespace std::chrono_literals;
int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}
struct Target {
    Tcp::endpoint endpoint;
    std::string authority;
    std::string path;
    explicit Target(const std::string& url) {
        const auto slash = url.find('/', 7);
        authority = url.substr(7, slash - 7);
        path = url.substr(slash);
        const auto colon = authority.rfind(':');
        auto address = authority.substr(0, colon);
        if (address.front() == '[') {
            address = address.substr(1, address.size() - 2);
        }
        endpoint = {asio::ip::make_address(address),
                    static_cast<std::uint16_t>(std::stoul(authority.substr(colon + 1)))};
    }
    std::string request(const std::string& method = "GET", const std::string& headers = "") const {
        return method + " " + path + " HTTP/1.1\r\nHost: " + authority + "\r\n" + headers + "\r\n";
    }
};
struct WireReply {
    std::string wire;
    bool timed_out = false;
    std::string header(const std::string& name) const {
        const auto start = wire.find("\r\n" + name + ": ");
        if (start == std::string::npos) {
            return {};
        }
        const auto value = start + name.size() + 4;
        return wire.substr(value, wire.find("\r\n", value) - value);
    }
    bool status(unsigned expected) const {
        return wire.find("HTTP/1.1 " + std::to_string(expected) + " ") == 0;
    }
    std::string body() const {
        const auto delimiter = wire.find("\r\n\r\n");
        return delimiter == std::string::npos ? std::string{} : wire.substr(delimiter + 4);
    }
};
/** Independent literal HTTP client. A watchdog bounds every test even if server
 * shutdown/framing fails. Read through EOF to catch extra/short response bodies. */
class WireClient {
    asio::io_context context_;
    Tcp::socket socket_{context_};
    asio::steady_timer timer_{context_};
    std::array<char, 8192> buffer_{};
    WireReply result_;
    std::string request_;
    std::size_t sent_ = 0;
    std::size_t fragment_ = 0;
    void finish() {
        timer_.cancel();
        ErrorCode ignored;
        socket_.close(ignored);
    }
    void read() {
        socket_.async_read_some(asio::buffer(buffer_), [this](ErrorCode error, std::size_t count) {
            result_.wire.append(buffer_.data(), count);
            if (error || result_.wire.size() > 512 * 1024) {
                finish();
            } else {
                read();
            }
        });
    }
    void write() {
        if (sent_ == request_.size()) {
            read();
            return;
        }
        const auto count = std::min(fragment_, request_.size() - sent_);
        asio::async_write(socket_, asio::buffer(request_.data() + sent_, count),
                          [this](ErrorCode error, std::size_t written) {
                              if (error) {
                                  finish();
                              } else {
                                  sent_ += written;
                                  write();
                              }
                          });
    }

public:
    WireReply wire_exchange(const Target& target, std::string request,
                            std::size_t fragment = 65536) {
        request_ = std::move(request);
        fragment_ = fragment;
        socket_.open(target.endpoint.protocol());
        timer_.expires_after(3s);
        timer_.async_wait([this](ErrorCode error) {
            if (!error) {
                result_.timed_out = true;
                finish();
            }
        });
        socket_.async_connect(target.endpoint, [this](ErrorCode error) {
            if (error) {
                finish();
            } else {
                write();
            }
        });
        context_.run();
        return result_;
    }
};
WireReply wire_exchange(const Target& target, const std::string& request,
                        std::size_t fragment = 65536) {
    return WireClient{}.wire_exchange(target, request, fragment);
}
std::uint8_t pattern(std::uint64_t offset) {
    return static_cast<std::uint8_t>(offset % 251);
}
std::string expected_body(std::uint64_t offset, std::size_t count) {
    std::string body(count, '\0');
    for (std::size_t index = 0; index < count; ++index) {
        body[index] = static_cast<char>(pattern(offset + index));
    }
    return body;
}
MediaSource patterned_source(std::uint64_t size, std::size_t read_limit = 65536,
                             std::atomic_uint* calls = nullptr) {
    return {[size] { return size; },
            [read_limit, calls](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                                const MediaReadContext& context) {
                if (calls) {
                    ++*calls;
                }
                if (context.should_stop()) {
                    return std::size_t{0};
                }
                const auto count = std::min(read_limit, capacity);
                for (std::size_t index = 0; index < count; ++index) {
                    output[index] = pattern(offset + index);
                }
                return count;
            }};
}
MediaServerOptions options(const std::string& address = "127.0.0.1") {
    MediaServerOptions result;
    result.receiver_address = address;
    return result;
}
void assert_response(const WireReply& reply, unsigned status, std::uint64_t length,
                     const std::string& body, const std::string& scenario) {
    check(!reply.timed_out, scenario + ": bounded completion");
    check(reply.status(status), scenario + ": status " + std::to_string(status));
    check(reply.header("Content-Length") == std::to_string(length), scenario + ": exact length");
    check(reply.body() == body, scenario + ": exact body through EOF");
    check(reply.header("Connection") == "close", scenario + ": closes connection");
    check(reply.header("Cache-Control") == "no-store", scenario + ": no caching");
}
void range_and_length_tests() {
    group = "GET/HEAD and ranges";
    std::atomic_uint reads{0};
    auto server = MediaServer::start(patterned_source(256, 7, &reads), options());
    const Target target(server->url());
    struct Case {
        const char* range;
        unsigned status;
        std::uint64_t offset;
        std::size_t length;
        const char* content_range;
    };
    const Case cases[] = {{"", 200, 0, 256, ""},
                          {"bytes=0-0", 206, 0, 1, "bytes 0-0/256"},
                          {"bytes=50-59", 206, 50, 10, "bytes 50-59/256"},
                          {"bytes=250-", 206, 250, 6, "bytes 250-255/256"},
                          {"bytes=-10", 206, 246, 10, "bytes 246-255/256"},
                          {"bytes=-999", 206, 0, 256, "bytes 0-255/256"},
                          {"bytes=256-", 416, 0, 0, "bytes */256"},
                          {"bytes=-0", 416, 0, 0, "bytes */256"},
                          {"bytes=50-10", 200, 0, 256, ""},
                          {"items=0-1", 200, 0, 256, ""},
                          {"bytes=0-1,4-5", 200, 0, 256, ""},
                          {"bytes=18446744073709551616-", 200, 0, 256, ""}};
    for (const auto& test : cases) {
        const std::string headers = *test.range ? std::string("Range: ") + test.range + "\r\n" : "";
        const auto reply = wire_exchange(target, target.request("GET", headers));
        assert_response(reply, test.status, test.length, expected_body(test.offset, test.length),
                        test.range);
        check(reply.header("Content-Range") == test.content_range,
              std::string(test.range) + ": Content-Range");
    }
    const auto before_head = reads.load();
    assert_response(wire_exchange(target, target.request("HEAD", "Range: bytes=50-59\r\n")), 200,
                    256, "", "HEAD ignores Range");
    check(reads == before_head, "HEAD invokes no read callback");
    assert_response(
        wire_exchange(target, target.request("GET", "Range: bytes=50-59\r\nIf-Range: \"old\"\r\n")),
        200, 256, expected_body(0, 256), "If-Range without validator uses full content");
    assert_response(wire_exchange(target, target.request(), 1), 200, 256, expected_body(0, 256),
                    "bytewise request fragmentation");
    auto other = MediaServer::start(patterned_source(0), options());
    check(other->url().substr(other->url().find("/media/")) != target.path,
          "independent sessions have different bearer paths");
    const Target empty(other->url());
    assert_response(wire_exchange(empty, empty.request()), 200, 0, "", "empty representation");
    assert_response(wire_exchange(empty, empty.request("GET", "Range: bytes=0-\r\n")), 416, 0, "",
                    "range on empty representation");
}
void large_source_tests() {
    group = "64-bit offsets and bounded chunks";
    // No 4-GiB allocation: an independent patterned representation has this size.
    auto server = MediaServer::start(patterned_source(4294967400ULL, 3), options());
    const Target target(server->url());
    const auto reply =
        wire_exchange(target, target.request("GET", "Range: bytes=4294967300-4294967315\r\n"));
    assert_response(reply, 206, 16, expected_body(4294967300ULL, 16), "range above 4 GiB");
    check(reply.header("Content-Range") == "bytes 4294967300-4294967315/4294967400",
          "64-bit range header");
    assert_response(wire_exchange(target, target.request("HEAD")), 200, 4294967400ULL, "",
                    "64-bit HEAD");
    auto maximum =
        MediaServer::start(patterned_source(std::numeric_limits<std::uint64_t>::max()), options());
    const Target max_target(maximum->url());
    assert_response(
        wire_exchange(max_target,
                      max_target.request("GET", "Range: bytes=18446744073709551614-\r\n")),
        206, 1, expected_body(18446744073709551614ULL, 1), "last byte without overflow");
    auto chunks = MediaServer::start(patterned_source(65549, 10007), options());
    const Target chunks_target(chunks->url());
    assert_response(wire_exchange(chunks_target, chunks_target.request()), 200, 65549,
                    expected_body(0, 65549), "multiple partial source reads and chunk boundary");
}
void rejection_tests() {
    group = "request bounds and access restrictions";
    std::atomic_uint reads{0};
    auto server = MediaServer::start(patterned_source(32, 65536, &reads), options());
    const Target target(server->url());
    const std::vector<std::string> invalid = {"Host: " + target.authority + "\r\n",
                                              "Range: bytes=0-1\r\nrange: bytes=2-3\r\n",
                                              "X-Test: one\r\nx-test: two\r\n",
                                              "Transfer-Encoding: chunked\r\n",
                                              "Content-Length: 1\r\n",
                                              "Expect: 100-continue\r\n",
                                              "If-Match: \"old\"\r\n",
                                              "X-Test: " + std::string(2049, 'a') + "\r\n",
                                              std::string("X-Test: a") + char(0) + "b\r\n"};
    for (std::size_t index = 0; index < invalid.size(); ++index) {
        assert_response(wire_exchange(target, target.request("GET", invalid[index])), 400, 0, "",
                        "invalid header case " + std::to_string(index));
    }
    assert_response(wire_exchange(target, "GET " + target.path + " HTTP/1.1\r\n\r\n"), 400, 0, "",
                    "missing Host");
    assert_response(
        wire_exchange(target, "GET " + target.path + " HTTP/1.1\r\nHost: wrong\r\n\r\n"), 400, 0,
        "", "wrong authority");
    assert_response(wire_exchange(target, target.request("POST")), 405, 0, "",
                    "unsupported method");
    assert_response(
        wire_exchange(target, "GET /wrong HTTP/1.1\r\nHost: " + target.authority + "\r\n\r\n"), 404,
        0, "", "unknown bearer path");
    assert_response(wire_exchange(target, target.request() + target.request()), 400, 0, "",
                    "buffered pipelining refused");
    std::string many;
    for (unsigned index = 0; index < 64; ++index) {
        many += "X-" + std::to_string(index) + ": x\r\n";
    }
    assert_response(wire_exchange(target, target.request("GET", many)), 400, 0, "",
                    "field count includes Host");
    std::string oversized;
    for (unsigned index = 0; index < 6; ++index) {
        oversized += "X-" + std::to_string(index) + ": " + std::string(1500, 'x') + "\r\n";
    }
    const auto overflow = wire_exchange(target, target.request("GET", oversized));
    check(!overflow.timed_out && overflow.status(431),
          "total header limit rejects oversized request");
    check(reads == 0, "rejected requests invoke no source callback");
    // Use the normal loopback source against a different allowed receiver IP.
    // macOS cannot bind an unconfigured 127.0.0.2 client alias. Route selection
    // for that peer still chooses the configured local loopback address.
    auto restricted = MediaServer::start(patterned_source(32, 65536, &reads), options("127.0.0.2"));
    const Target restricted_target(restricted->url());
    check(restricted_target.endpoint.address() == asio::ip::make_address("127.0.0.1"),
          "independent route selection chooses configured loopback");
    const auto denied = wire_exchange(restricted_target, restricted_target.request());
    check(!denied.timed_out && denied.wire.empty(),
          "different loopback source IP denied before HTTP");
    check(reads == 0, "IP restriction invokes no source callback");
    assert_response(wire_exchange(target, target.request()), 200, 32, expected_body(0, 32),
                    "recovery after malformed clients");
    assert_response(wire_exchange(target, target.request("GET", "X-Test: a\r\n folded\r\n")), 200,
                    32, expected_body(0, 32), "Beast normalizes folded unknown field");
}
void source_failure_tests() {
    group = "source failures and recovery";
    for (unsigned mode = 0; mode < 3; ++mode) {
        MediaSource source{[] { return std::uint64_t{32}; },
                           [mode](std::uint64_t, std::uint8_t*, std::size_t capacity,
                                  const MediaReadContext&) -> std::size_t {
                               if (mode == 2) {
                                   throw std::runtime_error("synthetic-private-path-must-not-leak");
                               }
                               return mode == 0 ? 0 : capacity + 1;
                           }};
        auto config = options();
        config.record_request_diagnostics = true;
        auto server = MediaServer::start(std::move(source), config);
        const Target target(server->url());
        const auto reply = wire_exchange(target, target.request());
        assert_response(reply, 500, 0, "", "initial read failure " + std::to_string(mode));
        check(reply.wire.find("synthetic-private-path") == std::string::npos,
              "callback exception text not returned");
        server->stop();
        const auto log = server->take_request_log();
        check(log.size() == 1 && log[0].status == 500 && log[0].header_completed &&
                  log[0].expected_body_bytes == 0 && log[0].body_bytes_written == 0 &&
                  log[0].end == MediaRequestEnd::source_error,
              "initial source failure diagnostic " + std::to_string(mode));
    }
    auto source = patterned_source(32, 7);
    const auto read = source.read_at;
    source.read_at = [read](std::uint64_t offset, std::uint8_t* out, std::size_t cap,
                            const MediaReadContext& ctx) {
        return offset ? std::size_t{0} : read(offset, out, cap, ctx);
    };
    auto server = MediaServer::start(std::move(source), options());
    const Target target(server->url());
    const auto truncated = wire_exchange(target, target.request());
    check(!truncated.timed_out && truncated.status(200) &&
              truncated.header("Content-Length") == "32" && truncated.body() == expected_body(0, 7),
          "later failure closes with detectably short body");
    std::atomic_uint calls{0};
    auto transient = patterned_source(32);
    const auto good_read = transient.read_at;
    transient.read_at = [&calls, good_read](std::uint64_t off, std::uint8_t* out, std::size_t cap,
                                            const MediaReadContext& ctx) {
        return calls++ == 0 ? std::size_t{0} : good_read(off, out, cap, ctx);
    };
    auto recovery = MediaServer::start(std::move(transient), options());
    const Target recovered(recovery->url());
    assert_response(wire_exchange(recovered, recovered.request()), 500, 0, "",
                    "transient source failure");
    assert_response(wire_exchange(recovered, recovered.request()), 200, 32, expected_body(0, 32),
                    "fresh request recovery");
}
bool wait_for(const std::atomic_uint& count, unsigned expected) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (count.load() < expected && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(1ms);
    }
    return count.load() >= expected;
}
void concurrency_and_shutdown_tests() {
    group = "bounded concurrent reads and shutdown";
    std::atomic_uint entered{0};
    std::atomic_bool release{false};
    auto source = patterned_source(32);
    const auto read = source.read_at;
    source.read_at = [&entered, &release, read](std::uint64_t off, std::uint8_t* out,
                                                std::size_t cap, const MediaReadContext& ctx) {
        ++entered;
        while (!release && !ctx.should_stop()) {
            std::this_thread::sleep_for(1ms);
        }
        return read(off, out, cap, ctx);
    };
    auto config = options();
    config.max_connections = 2;
    config.record_request_diagnostics = true;
    auto server = MediaServer::start(std::move(source), config);
    const Target target(server->url());
    auto first =
        std::async(std::launch::async, [&] { return wire_exchange(target, target.request()); });
    auto second =
        std::async(std::launch::async, [&] { return wire_exchange(target, target.request()); });
    const bool both_entered = wait_for(entered, 2);
    auto third =
        std::async(std::launch::async, [&] { return wire_exchange(target, target.request()); });
    std::this_thread::sleep_for(50ms);
    check(both_entered && entered == 2,
          "two simultaneous callbacks; third request not admitted yet");
    release = true;
    assert_response(first.get(), 200, 32, expected_body(0, 32), "first concurrent range");
    assert_response(second.get(), 200, 32, expected_body(0, 32), "second concurrent range");
    assert_response(third.get(), 200, 32, expected_body(0, 32),
                    "backlog served after slot released");
    check(entered == 3, "one callback per admitted request");
    entered = 0;
    release = false;
    auto pending =
        std::async(std::launch::async, [&] { return wire_exchange(target, target.request()); });
    check(wait_for(entered, 1), "callback active before stop");
    const auto start = std::chrono::steady_clock::now();
    server->stop();
    check(std::chrono::steady_clock::now() - start < 1s, "cooperative source shutdown bounded");
    const auto stopped = pending.get();
    check(!stopped.timed_out && stopped.wire.empty(), "stop cancels source read before headers");
    server->stop();
    check(entered == 1, "idempotent stop; callbacks quiescent");
    const auto log = server->take_request_log();
    check(log.size() == 4 && log.back().end == MediaRequestEnd::cancelled &&
              !log.back().header_completed && log.back().body_bytes_written == 0,
          "shutdown diagnostic distinguishes cancelled source from a completed response");
    ErrorCode error;
    asio::io_context context;
    Tcp::socket after_stop(context);
    after_stop.connect(target.endpoint, error);
    check(static_cast<bool>(error), "listener closed after stop");
}
void deadline_tests() {
    group = "absolute deadlines and cancellation";
    auto config = options();
    config.request_timeout_ms = 100;
    config.max_connections = 1;
    config.record_request_diagnostics = true;
    std::atomic_uint entered{0};
    auto source = patterned_source(32);
    const auto read = source.read_at;
    source.read_at = [&entered, read](std::uint64_t off, std::uint8_t* out, std::size_t cap,
                                      const MediaReadContext& ctx) {
        if (entered++ == 0) {
            while (!ctx.should_stop()) {
                std::this_thread::sleep_for(1ms);
            }
        }
        return read(off, out, cap, ctx);
    };
    auto server = MediaServer::start(std::move(source), config);
    const Target target(server->url());
    const auto stalled = wire_exchange(target, target.request());
    check(!stalled.timed_out && stalled.wire.empty() && entered == 1,
          "deadline includes stalled source callback");
    assert_response(wire_exchange(target, target.request()), 200, 32, expected_body(0, 32),
                    "slot recovers after source deadline");
    const auto partial = wire_exchange(target, "GET " + target.path + " HTTP/1.1\r\nHost:");
    check(!partial.timed_out && partial.wire.empty(),
          "incomplete headers close on server deadline");
    assert_response(wire_exchange(target, target.request()), 200, 32, expected_body(0, 32),
                    "slot recovers after header deadline");

    // A receiver that stops reading cannot retain a connection slot forever.
    // This 1-TiB virtual source is streamed through fixed buffers, never allocated.
    std::atomic_uint large_reads{0};
    auto large =
        MediaServer::start(patterned_source(1099511627776ULL, 65536, &large_reads), config);
    const Target blocked_target(large->url());
    asio::io_context context;
    Tcp::socket blocked(context);
    blocked.connect(blocked_target.endpoint);
    blocked.set_option(asio::socket_base::receive_buffer_size(1024));
    const auto request = blocked_target.request();
    asio::write(blocked, asio::buffer(request));
    std::this_thread::sleep_for(250ms);
    const auto before_recovery = large_reads.load();
    assert_response(wire_exchange(blocked_target, blocked_target.request("HEAD")), 200,
                    1099511627776ULL, "", "slot released after blocked response write deadline");
    check(before_recovery > 0 && large_reads == before_recovery,
          "stalled receiver causes no source work after deadline");
    large->stop();
    const auto blocked_log = large->take_request_log();
    check(blocked_log.size() == 2 && blocked_log.front().end == MediaRequestEnd::timeout &&
              blocked_log.front().header_completed && blocked_log.front().body_bytes_written > 0 &&
              blocked_log.front().body_bytes_written < 1099511627776ULL &&
              blocked_log.back().end == MediaRequestEnd::complete &&
              blocked_log.back().method == MediaRequestMethod::head &&
              blocked_log.back().body_bytes_written == 0,
          "blocked write deadline includes partial progress and leaves HEAD recovery complete");
    server->stop();
    const auto log = server->take_request_log();
    check(log.size() == 4 && log[0].end == MediaRequestEnd::timeout && !log[0].header_completed &&
              log[2].end == MediaRequestEnd::timeout && log[1].end == MediaRequestEnd::complete &&
              log[3].end == MediaRequestEnd::complete,
          "absolute source/header deadlines remain distinct from subsequent completed requests");
}
void request_diagnostic_tests() {
    group = "bounded request diagnostics";
    auto disabled = MediaServer::start(patterned_source(32), options());
    const Target silent(disabled->url());
    (void)wire_exchange(silent, silent.request());
    disabled->stop();
    check(disabled->take_request_log().empty(), "diagnostics disabled by default");

    auto config = options();
    config.record_request_diagnostics = true;
    auto server = MediaServer::start(patterned_source(32, 7), config);
    const Target target(server->url());
    assert_response(wire_exchange(target, target.request("GET", "Range: bytes=5-13\r\n")), 206, 9,
                    expected_body(5, 9), "recorded short-read range");
    assert_response(wire_exchange(target, target.request("HEAD")), 200, 32, "", "recorded HEAD");
    for (unsigned request = 0; request < 258; ++request) {
        (void)wire_exchange(target, target.request("HEAD"));
    }
    server->stop();
    const auto log = server->take_request_log();
    check(log.size() == 256 && log.front().request_id == 5 && log.back().request_id == 260,
          "literal 256-record bound retains newest requests in order");
    check(server->take_request_log().empty(), "draining discards exactly the returned records");

    auto range_server = MediaServer::start(patterned_source(32, 7), config);
    const Target range_target(range_server->url());
    (void)wire_exchange(range_target, range_target.request("GET", "Range: bytes=5-13\r\n"));
    (void)wire_exchange(range_target, range_target.request("HEAD"));
    range_server->stop();
    const auto ranges = range_server->take_request_log();
    check(ranges.size() == 2 && ranges[0].method == MediaRequestMethod::get &&
              ranges[0].status == 206 && ranges[0].offset == 5 && ranges[0].declared_length == 9 &&
              ranges[0].expected_body_bytes == 9 && ranges[0].body_bytes_written == 9 &&
              ranges[0].header_completed && ranges[0].end == MediaRequestEnd::complete &&
              ranges[0].accepted_ms <= ranges[0].last_body_write_ms &&
              ranges[0].last_body_write_ms <= ranges[0].closed_ms &&
              ranges[1].method == MediaRequestMethod::head && ranges[1].declared_length == 32 &&
              ranges[1].expected_body_bytes == 0 && ranges[1].body_bytes_written == 0 &&
              ranges[1].header_completed && ranges[1].end == MediaRequestEnd::complete,
          "range/body completion and HEAD representation length use distinct counters");

    auto source = patterned_source(32, 7);
    const auto read = source.read_at;
    source.read_at = [read](std::uint64_t off, std::uint8_t* out, std::size_t cap,
                            const MediaReadContext& context) {
        return off ? std::size_t{0} : read(off, out, cap, context);
    };
    auto broken = MediaServer::start(std::move(source), config);
    const Target broken_target(broken->url());
    const auto reply = wire_exchange(broken_target, broken_target.request());
    broken->stop();
    const auto failure = broken->take_request_log();
    check(failure.size() == 1 && failure[0].status == 200 && failure[0].header_completed &&
              failure[0].expected_body_bytes == 32 && failure[0].body_bytes_written == 7 &&
              failure[0].body_bytes_written == reply.body().size() &&
              failure[0].end == MediaRequestEnd::source_error,
          "source read is not counted as body delivery; incomplete success response is diagnosed");
}
void aborted_write_diagnostic_test() {
    group = "peer abort during body write";
    auto config = options();
    config.request_timeout_ms = 1000; // Bounds the synchronous read before the intentional reset.
    config.record_request_diagnostics = true;
    auto server = MediaServer::start(patterned_source(1099511627776ULL), config);
    const Target target(server->url());
    asio::io_context context;
    Tcp::socket client(context);
    client.connect(target.endpoint);
    client.set_option(asio::socket_base::receive_buffer_size(1024));
    const auto request = target.request();
    asio::write(client, asio::buffer(request));
    std::array<char, 1024> received{};
    ErrorCode error;
    const auto count = client.read_some(asio::buffer(received), error);
    check(!error && count > 0, "response starts before intentional TCP reset");
    client.set_option(asio::socket_base::linger(true, 0));
    client.close();
    std::vector<MediaRequestDiagnostic> log;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    do {
        log = server->take_request_log();
        if (log.empty()) {
            std::this_thread::sleep_for(1ms);
        }
    } while (log.empty() && std::chrono::steady_clock::now() < deadline);
    server->stop();
    check(log.size() == 1 && log[0].end == MediaRequestEnd::io_error &&
              log[0].body_bytes_written < 1099511627776ULL,
          "receiver abort is observed before sender stop and cannot be called complete");
}
template <class Action> void invalid(const std::string& scenario, Action action) {
    try {
        action();
        check(false, scenario + ": accepted");
    } catch (const std::invalid_argument&) {
    }
}
void option_and_ipv6_tests() {
    group = "construction failures and IPv6";
    for (const auto* address : {"", "localhost", "0.0.0.0", "255.255.255.255", "224.0.0.1",
                                "::", "ff02::1", "fe80::1", "::ffff:127.0.0.1"}) {
        invalid(address,
                [address] { (void)MediaServer::start(patterned_source(32), options(address)); });
    }
    for (unsigned mode = 0; mode < 5; ++mode) {
        auto config = options();
        if (mode == 0) {
            config.max_connections = 0;
        }
        if (mode == 1) {
            config.max_connections = 17;
        }
        if (mode == 2) {
            config.request_timeout_ms = 600001;
        }
        if (mode == 3) {
            config.receiver_port = 0;
        }
        if (mode == 4) {
            config.content_type = "video/mp4\r\nInjected: yes";
        }
        invalid("option case " + std::to_string(mode),
                [&] { (void)MediaServer::start(patterned_source(32), config); });
    }
    invalid("missing source", [] { (void)MediaServer::start({}, options()); });
    auto throwing = patterned_source(32);
    throwing.size = []() -> std::uint64_t { throw std::logic_error("synthetic size failure"); };
    try {
        (void)MediaServer::start(std::move(throwing), options());
        check(false, "size exception propagated");
    } catch (const std::logic_error&) {
    }
    std::atomic_uint size_calls{0};
    auto source = patterned_source(32);
    source.size = [&] {
        ++size_calls;
        return std::uint64_t{32};
    };
    auto server = MediaServer::start(std::move(source), options());
    const Target target(server->url());
    auto occupied = options();
    occupied.listen_port = target.endpoint.port();
    try {
        (void)MediaServer::start(patterned_source(32), occupied);
        check(false, "occupied listener rejected");
    } catch (const std::runtime_error&) {
    }
    assert_response(wire_exchange(target, target.request()), 200, 32, expected_body(0, 32),
                    "original listener preserved");
    check(size_calls == 1, "representation size snapshot taken once");
    // Probe IPv6 availability independently; do not hide server failures as skips.
    asio::io_context context;
    Tcp::acceptor probe(context);
    ErrorCode error;
    probe.open(Tcp::v6(), error);
    if (!error) {
        probe.bind({asio::ip::address_v6::loopback(), 0}, error);
    }
    if (error) {
        std::cout << "SKIP IPv6 loopback unavailable on this host\n";
        return;
    }
    probe.close();
    auto ipv6 = MediaServer::start(patterned_source(32), options("::1"));
    const Target v6(ipv6->url());
    check(ipv6->url().find("http://[::1]:") == 0, "bracketed IPv6 URL");
    assert_response(wire_exchange(v6, v6.request()), 200, 32, expected_body(0, 32),
                    "IPv6 loopback wire");
}
} // namespace
int main() {
    try {
        range_and_length_tests();
        large_source_tests();
        rejection_tests();
        source_failure_tests();
        concurrency_and_shutdown_tests();
        deadline_tests();
        request_diagnostic_tests();
        aborted_write_diagnostic_test();
        option_and_ipv6_tests();
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << group << "]: test infrastructure exception: " << error.what()
                  << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
