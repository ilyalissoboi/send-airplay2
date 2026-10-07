// SPDX-License-Identifier: Apache-2.0
// Event-channel framing and record transport against a scripted fake receiver.
// Keys are public synthetic bytes; request and reply bytes are literal here.
#include "event_channel.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace send_airplay2::detail;
using namespace std::chrono_literals;

namespace {
int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}
Bytes text(std::string_view value) {
    return Bytes(value.begin(), value.end());
}
std::string as_text(const Bytes& bytes) {
    return std::string(bytes.begin(), bytes.end());
}
template <typename Action>
void expect_transport_error(const std::string& scenario, TransportError expected, Action action) {
    try {
        action();
        check(false, scenario + ": expected transport error");
    } catch (const TransportException& error) {
        check(error.reason() == expected,
              scenario + ": category " + std::to_string(static_cast<int>(error.reason())));
    } catch (const std::exception& error) {
        check(false, scenario + ": wrong exception: " + error.what());
    }
}
template <typename Action>
void expect_control_error(const std::string& scenario, ControlError expected, Action action) {
    try {
        action();
        check(false, scenario + ": expected record error");
    } catch (const ControlException& error) {
        check(error.reason() == expected,
              scenario + ": category " + std::to_string(static_cast<int>(error.reason())));
    } catch (const std::exception& error) {
        check(false, scenario + ": wrong exception: " + error.what());
    }
}

/// The receiver's server header seen on tvOS 26.6 replies (public version string).
constexpr std::string_view receiver_server = "AirTunes/960.13.1";

/// A receiver-style event request with literal framing.
Bytes event_request(unsigned cseq, std::string_view body) {
    return text("POST /command RTSP/1.0\r\n"
                "CSeq: " +
                std::to_string(cseq) +
                "\r\n"
                "Content-Type: application/x-apple-binary-plist\r\n"
                "Content-Length: " +
                std::to_string(body.size()) + "\r\nServer: " + std::string(receiver_server) +
                "\r\n\r\n" + std::string(body));
}
/// The reply the reference sender gives to event_request(cseq, ...).
std::string expected_reply(unsigned cseq) {
    return "RTSP/1.0 200 OK\r\nContent-Length: 0\r\nAudio-Latency: 0\r\nServer: " +
           std::string(receiver_server) + "\r\nCSeq: " + std::to_string(cseq) + "\r\n\r\n";
}
const std::string* field(const EventRequest& request, std::string_view name) {
    for (const auto& entry : request.headers) {
        if (entry.first == name) {
            return &entry.second;
        }
    }
    return nullptr;
}

// ---- Parser and reply encoder ----

void parser_tests() {
    group = "event request parsing";
    {
        EventRequestParser parser;
        parser.append(event_request(7, "payload"));
        auto request = parser.next();
        check(request.has_value(), "complete request available");
        if (request) {
            check(request->method == "POST" && request->target == "/command" &&
                      request->protocol == ReceiverProtocol::rtsp,
                  "request line");
            check(field(*request, "cseq") != nullptr && *field(*request, "cseq") == "7",
                  "lower-cased CSeq field");
            check(as_text(request->body) == "payload", "body");
        }
        check(!parser.next().has_value(), "nothing further buffered");
    }
    {
        EventRequestParser parser;
        parser.append(text("GET /info HTTP/1.1\r\nX-Note: no length\r\n\r\n"));
        const auto request = parser.next();
        check(request && request->protocol == ReceiverProtocol::http && request->body.empty(),
              "HTTP request without Content-Length has an empty body");
    }
    {
        // Two complete requests and the start of a third in one append.
        EventRequestParser parser;
        auto input = event_request(1, "first");
        const auto second = event_request(2, "second");
        const auto third = event_request(3, "third");
        input.insert(input.end(), second.begin(), second.end());
        constexpr std::size_t third_split = 20;
        input.insert(input.end(), third.begin(), third.begin() + third_split);
        parser.append(input);
        const auto one = parser.next();
        const auto two = parser.next();
        check(one && as_text(one->body) == "first" && two && as_text(two->body) == "second",
              "coalesced requests in order");
        check(!parser.next().has_value(), "partial third request waits");
        parser.append(Bytes(third.begin() + third_split, third.end()));
        const auto three = parser.next();
        check(three && as_text(three->body) == "third", "third request completes");
    }
    {
        const auto request = event_request(9, "byte-at-a-time");
        EventRequestParser parser;
        for (std::size_t index = 0; index < request.size(); ++index) {
            parser.append(Bytes{request[index]});
            const bool last = index + 1 == request.size();
            if (parser.next().has_value() != last) {
                check(false, "request completes only at byte " + std::to_string(index));
                break;
            }
        }
    }
}

void parser_rejection_tests() {
    group = "event request rejection";
    const std::vector<std::pair<const char*, std::string>> malformed{
        {"missing protocol", "POST /command\r\n\r\n"},
        {"unknown protocol", "POST /command RTSP/2.0\r\n\r\n"},
        {"empty target", "POST  RTSP/1.0\r\n\r\n"},
        {"control byte in target", "POST /a\x01z RTSP/1.0\r\n\r\n"},
        {"method not a token", "PO(ST /command RTSP/1.0\r\n\r\n"},
        {"method over 32 bytes", std::string(33, 'P') + " /command RTSP/1.0\r\n\r\n"},
        {"field without colon", "POST /command RTSP/1.0\r\nCSeq 1\r\n\r\n"},
        {"duplicate field", "POST /command RTSP/1.0\r\nCSeq: 1\r\ncseq: 2\r\n\r\n"},
        {"transfer encoding", "POST /command RTSP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n"},
        {"body over 32 KiB", "POST /command RTSP/1.0\r\nContent-Length: 32769\r\n\r\n"},
        {"non-decimal length", "POST /command RTSP/1.0\r\nContent-Length: 12a\r\n\r\n"},
        {"non-decimal CSeq", "POST /command RTSP/1.0\r\nCSeq: one\r\n\r\n"},
    };
    for (const auto& example : malformed) {
        EventRequestParser parser;
        const std::string scenario = example.first;
        expect_transport_error(scenario, TransportError::invalid_message, [&] {
            parser.append(text(example.second));
            (void)parser.next();
        });
        expect_transport_error(scenario + ": parser closed afterwards", TransportError::closed,
                               [&] { (void)parser.next(); });
    }
    {
        std::string many = "POST /command RTSP/1.0\r\n";
        for (std::size_t index = 0; index <= receiver_http::max_fields; ++index) {
            many += "X-F" + std::to_string(index) + ": v\r\n";
        }
        EventRequestParser parser;
        expect_transport_error("33 fields", TransportError::invalid_message, [&] {
            parser.append(text(many + "\r\n"));
            (void)parser.next();
        });
    }
    {
        // A header block reaching the limit without its terminator.
        EventRequestParser parser;
        expect_transport_error("unterminated header block", TransportError::invalid_message, [&] {
            parser.append(Bytes(receiver_http::max_headers, 'a'));
            (void)parser.next();
        });
    }
    {
        EventRequestParser parser;
        expect_transport_error("pending input over limit", TransportError::invalid_message, [&] {
            parser.append(Bytes(receiver_http::max_pending_event_input + 1, 'a'));
        });
    }
    {
        EventRequestParser parser;
        parser.append(text("POST /command RTSP/1.0\r\n"));
        expect_transport_error("end of input inside a request", TransportError::disconnected,
                               [&] { parser.finish(); });
        EventRequestParser idle;
        idle.finish();
        expect_transport_error("clean end closes the parser", TransportError::closed,
                               [&] { (void)idle.next(); });
    }
}

void reply_tests() {
    group = "event reply encoding";
    EventRequestParser parser;
    parser.append(event_request(42, ""));
    const auto request = parser.next();
    check(request && as_text(encode_event_response(*request)) == expected_reply(42),
          "RTSP reply echoes Server then CSeq");
    EventRequest bare;
    bare.protocol = ReceiverProtocol::http;
    check(as_text(encode_event_response(bare)) ==
              "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nAudio-Latency: 0\r\n\r\n",
          "HTTP reply without echoed fields");
    EventRequest injected;
    injected.headers = {{"cseq", "1\r\nX-Injected: 1"}};
    expect_transport_error("reply refuses CRLF in an echoed value", TransportError::invalid_message,
                           [&] { (void)encode_event_response(injected); });
}

// ---- Event channel over a scripted stream ----

/// Inbound wire bytes are scripted up front; a read past them is EOF.
class ScriptedStream final : public ReceiverStream {
public:
    Bytes inbound;
    std::size_t read_fragment = receiver_http::read_chunk;
    std::size_t write_limit = std::numeric_limits<std::size_t>::max();
    Bytes outbound;
    bool closed = false;

    std::size_t write_some(const std::uint8_t* data, std::size_t size,
                           const ReceiverOperation& operation) override {
        operation.check();
        if (closed) {
            throw TransportException(TransportError::closed);
        }
        const auto count = std::min(size, write_limit);
        outbound.insert(outbound.end(), data, data + count);
        return count;
    }
    std::size_t read_some(std::uint8_t* data, std::size_t capacity,
                          const ReceiverOperation& operation) override {
        operation.check();
        if (closed) {
            throw TransportException(TransportError::closed);
        }
        const auto count = std::min({capacity, read_fragment, inbound.size() - read_offset_});
        std::copy_n(inbound.begin() + static_cast<std::ptrdiff_t>(read_offset_), count, data);
        read_offset_ += count;
        return count;
    }
    void require_idle(const ReceiverOperation&) override {}
    void close() noexcept override {
        closed = true;
    }

private:
    std::size_t read_offset_ = 0;
};

ControlKey key_from(std::uint8_t first) {
    ControlKey key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(first + index);
    }
    return key;
}

/// Both ends of one channel: the sender's keys and the matching receiver
/// record owners (the receiver writes with the sender's read key).
struct ChannelFixture {
    Secret32 sender_write{key_from(0x10)};
    Secret32 sender_read{key_from(0x40)};
    ControlWriter receiver_writer{key_from(0x40)};
    ControlReader receiver_reader{key_from(0x10)};
    ScriptedStream* stream = nullptr;
    std::unique_ptr<EventChannel> channel;

    ChannelFixture() {
        auto owned = std::make_unique<ScriptedStream>();
        stream = owned.get();
        channel = std::make_unique<EventChannel>(std::move(owned), sender_write, sender_read);
    }
    void send(const Bytes& plaintext) {
        const auto wire = receiver_writer.encrypt(plaintext);
        stream->inbound.insert(stream->inbound.end(), wire.begin(), wire.end());
    }
    std::string replies() {
        return as_text(receiver_reader.feed(stream->outbound));
    }
};

ReceiverOperation soon() {
    return ReceiverOperation::after(5s);
}

void channel_tests() {
    group = "event channel round trip";
    for (const std::size_t fragment : {std::size_t{1}, std::size_t{7}, receiver_http::read_chunk}) {
        const auto label = "fragment " + std::to_string(fragment);
        ChannelFixture fixture;
        fixture.stream->read_fragment = fragment;
        fixture.send(event_request(1, "state"));
        const auto request = fixture.channel->receive(soon());
        check(as_text(request.body) == "state" && request.target == "/command", label + ": body");
        check(fixture.replies() == expected_reply(1), label + ": literal reply");
        check(!fixture.channel->closed() && !fixture.stream->closed, label + ": channel open");
    }
    {
        // Two requests in one record, then one request split over two records.
        ChannelFixture fixture;
        auto both = event_request(1, "one");
        const auto second = event_request(2, "two");
        both.insert(both.end(), second.begin(), second.end());
        fixture.send(both);
        const auto third = event_request(3, "three");
        constexpr std::size_t record_split = 11;
        fixture.send(Bytes(third.begin(), third.begin() + record_split));
        fixture.send(Bytes(third.begin() + record_split, third.end()));
        std::string bodies;
        for (int index = 0; index < 3; ++index) {
            bodies += as_text(fixture.channel->receive(soon()).body) + ";";
        }
        check(bodies == "one;two;three;", "requests in order across records");
        check(fixture.replies() == expected_reply(1) + expected_reply(2) + expected_reply(3),
              "one reply per request, in order");
    }
    {
        ChannelFixture fixture;
        fixture.stream->write_limit = 3;
        fixture.send(event_request(5, ""));
        (void)fixture.channel->receive(soon());
        check(fixture.replies() == expected_reply(5), "reply survives 3-byte partial writes");
    }
}

void channel_failure_tests() {
    group = "event channel failures";
    auto expect_closed_after = [](ChannelFixture& fixture, const std::string& scenario) {
        check(fixture.channel->closed() && fixture.stream->closed, scenario + ": closed");
        expect_transport_error(scenario + ": later receive", TransportError::closed,
                               [&] { (void)fixture.channel->receive(soon()); });
    };
    {
        ChannelFixture fixture;
        fixture.send(event_request(1, "only"));
        (void)fixture.channel->receive(soon());
        expect_transport_error("end of input between requests", TransportError::disconnected,
                               [&] { (void)fixture.channel->receive(soon()); });
        expect_closed_after(fixture, "clean end");
    }
    {
        ChannelFixture fixture;
        const auto request = event_request(1, "cut");
        fixture.send(Bytes(request.begin(), request.end() - 2));
        expect_transport_error("end of input inside a request", TransportError::disconnected,
                               [&] { (void)fixture.channel->receive(soon()); });
        expect_closed_after(fixture, "partial request");
    }
    {
        ChannelFixture fixture;
        fixture.send(event_request(1, "truncated"));
        fixture.stream->inbound.pop_back(); // Cut the record's tag.
        expect_control_error("end of input inside a record", ControlError::invalid_length,
                             [&] { (void)fixture.channel->receive(soon()); });
        expect_closed_after(fixture, "truncated record");
    }
    {
        ChannelFixture fixture;
        fixture.send(event_request(1, "tampered"));
        fixture.stream->inbound.back() ^= 1;
        expect_control_error("tampered tag", ControlError::authentication,
                             [&] { (void)fixture.channel->receive(soon()); });
        expect_closed_after(fixture, "tampered tag");
        check(fixture.stream->outbound.empty(), "no reply to an unauthenticated record");
    }
    {
        ChannelFixture fixture;
        fixture.send(text("POST /command RTSP/1.0\r\nContent-Length: 32769\r\n\r\n"));
        expect_transport_error("oversized request body", TransportError::invalid_message,
                               [&] { (void)fixture.channel->receive(soon()); });
        expect_closed_after(fixture, "oversized body");
    }
    {
        ChannelFixture fixture;
        std::atomic_bool cancelled{true};
        expect_transport_error("cancellation", TransportError::cancelled, [&] {
            (void)fixture.channel->receive(ReceiverOperation::until_cancelled(&cancelled));
        });
        expect_closed_after(fixture, "cancelled");
    }
    {
        ChannelFixture fixture;
        ReceiverOperation expired{std::chrono::steady_clock::now() - 1ms, nullptr};
        expect_transport_error("deadline", TransportError::timeout,
                               [&] { (void)fixture.channel->receive(expired); });
        expect_closed_after(fixture, "deadline");
    }
}

void construction_tests() {
    group = "event channel construction";
    Secret32 key{key_from(1)};
    Secret32 other{key_from(2)};
    expect_transport_error("missing stream", TransportError::invalid_argument,
                           [&] { EventChannel channel(nullptr, key, other); });
    auto stream = std::make_unique<ScriptedStream>();
    expect_transport_error("same key owner for both directions", TransportError::invalid_argument,
                           [&] { EventChannel channel(std::move(stream), key, key); });
    expect_transport_error("long-lived operation needs a flag", TransportError::invalid_argument,
                           [] { (void)ReceiverOperation::until_cancelled(nullptr); });
    std::atomic_bool flag{false};
    check(ReceiverOperation::until_cancelled(&flag).deadline ==
              std::chrono::steady_clock::time_point::max(),
          "long-lived operation has no deadline");
}
} // namespace

int main() {
    try {
        parser_tests();
        parser_rejection_tests();
        reply_tests();
        channel_tests();
        channel_failure_tests();
        construction_tests();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected test exception [" << group << "]: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " event channel test failure(s)\n";
        return 1;
    }
    std::cout << "event channel tests passed\n";
    return 0;
}
