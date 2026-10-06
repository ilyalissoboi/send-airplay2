// SPDX-License-Identifier: Apache-2.0
// NTP timing codec known answers and a real UDP loopback responder.
// Expected packets are literal bytes; conversion answers were computed
// independently (Python datetime; pyatv's microsecond fraction formula).
#include "native_socket.h"
#include "ntp_timing.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
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
template <typename Action>
void expect_transport_error(const std::string& scenario, TransportError expected, Action action) {
    try {
        action();
        check(false, scenario + ": expected transport error");
    } catch (const TransportException& error) {
        check(error.reason() == expected,
              scenario + ": category " + std::to_string(static_cast<int>(error.reason())));
    }
}
bool same(NtpTimestamp left, NtpTimestamp right) {
    return left.seconds == right.seconds && left.fraction == right.fraction;
}

using Packet = std::array<std::uint8_t, ntp_timing::packet_size>;

/// A receiver-style request: RTP version byte 0x80, type 0xd2, sequence 7,
/// zero padding and times, send time 4000233600.5 (2026-10-06T00:00:00.5Z).
const Packet literal_request{0x80, 0xd2, 0x00, 0x07, 0,    0,    0,    0,    0,    0,   0,
                             0,    0,    0,    0,    0,    0,    0,    0,    0,    0,   0,
                             0,    0,    0xee, 0x6e, 0xb8, 0x80, 0x80, 0x00, 0x00, 0x00};

void conversion_tests() {
    group = "NTP time conversion";
    using std::chrono::system_clock;
    const auto unix_epoch = system_clock::time_point{};
    check(same(ntp_from_system_time(unix_epoch), {2208988800u, 0}), "Unix epoch");
    const auto october_sixth = unix_epoch + std::chrono::seconds(1791244800);
    check(same(ntp_from_system_time(october_sixth), {4000233600u, 0}), "2026-10-06T00:00:00Z");
    const std::pair<long long, std::uint32_t> fractions[] = {
        {1, 4294u}, {500000, 2147483648u}, {999999, 4294963001u}};
    for (const auto& [microseconds, fraction] : fractions) {
        const auto result =
            ntp_from_system_time(october_sixth + std::chrono::microseconds(microseconds));
        check(result.seconds == 4000233600u && result.fraction == fraction,
              std::to_string(microseconds) + " us fraction");
    }
    check(same(ntp_from_system_time(unix_epoch - 1us), {2208988799u, 4294963001u}),
          "floor division before 1970");
}

void codec_tests() {
    group = "timing packet codec";
    const auto request = parse_timing_request(literal_request.data(), literal_request.size());
    check(request.has_value() && request->protocol_byte == 0x80 &&
              same(request->send_time, {4000233600u, 0x80000000u}),
          "literal request fields");
    // Reply: same protocol byte, type 0xd3, sequence 7, zero padding, the
    // request's send time as reference, then receive and send times.
    const Packet expected{0x80, 0xd3, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, 0xee, 0x6e, 0xb8,
                          0x80, 0x80, 0x00, 0x00, 0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
                          0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x01};
    if (request) {
        check(encode_timing_response(*request, {0x11223344u, 0x55667788u},
                                     {0x99aabbccu, 0xddeeff01u}) == expected,
              "literal reply bytes");
    }
    check(!parse_timing_request(literal_request.data(), 31), "31 bytes is not a request");
    std::array<std::uint8_t, 33> longer{};
    std::memcpy(longer.data(), literal_request.data(), literal_request.size());
    check(!parse_timing_request(longer.data(), longer.size()), "33 bytes is not a request");
    for (const std::uint8_t type : {std::uint8_t{0xd3}, std::uint8_t{0x52}}) {
        auto other = literal_request;
        other[1] = type;
        check(!parse_timing_request(other.data(), other.size()),
              "type " + std::to_string(type) + " is not a request");
    }
    check(!parse_timing_request(nullptr, ntp_timing::packet_size), "null data");
}

// ---- Real UDP loopback ----

/// A plain UDP client socket on a loopback address.
class Client {
public:
    explicit Client(const std::string& address) {
        const auto local = native::numeric_socket_address(address, 0, 0);
        socket_.value = ::socket(local.family, SOCK_DGRAM, IPPROTO_UDP);
        if (socket_.value == native::invalid_socket) {
            throw std::runtime_error("client socket");
        }
        native::nonblocking(socket_.value);
        if (::bind(socket_.value, reinterpret_cast<const sockaddr*>(&local.storage), local.size) !=
            0) {
            throw std::runtime_error("client bind");
        }
    }
    void send(const std::string& address, std::uint16_t port, const std::uint8_t* data,
              std::size_t size) {
        const auto target = native::numeric_socket_address(address, port, 0);
        if (::sendto(socket_.value, reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                     reinterpret_cast<const sockaddr*>(&target.storage),
                     target.size) != static_cast<int>(size)) {
            throw std::runtime_error("client send");
        }
    }
    /// The next datagram within `wait`, or nullopt.
    std::optional<std::vector<std::uint8_t>> receive(std::chrono::milliseconds wait) {
        if (!native::ready(socket_.value, false, static_cast<int>(wait.count()))) {
            return std::nullopt;
        }
        std::vector<std::uint8_t> buffer(64);
        const auto received = ::recv(socket_.value, reinterpret_cast<char*>(buffer.data()),
                                     static_cast<int>(buffer.size()), 0);
        if (received < 0) {
            return std::nullopt;
        }
        buffer.resize(static_cast<std::size_t>(received));
        return buffer;
    }

private:
    [[maybe_unused]] native::NetworkRuntime runtime_;
    native::SocketOwner socket_;
};

/// Runs serve() on a thread until stop(); records how serving ended.
class ServingThread {
public:
    explicit ServingThread(TimingResponder& responder)
        : thread_([this, &responder] {
              try {
                  responder.serve(ReceiverOperation::until_cancelled(&stop_flag_));
              } catch (const TransportException& error) {
                  end_reason_ = static_cast<int>(error.reason());
              }
          }) {}
    ~ServingThread() {
        stop();
    }
    ServingThread(const ServingThread&) = delete;
    ServingThread& operator=(const ServingThread&) = delete;
    /// Returns the TransportError category serving ended with, or -1.
    int stop() {
        stop_flag_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
        return end_reason_;
    }

private:
    std::atomic_bool stop_flag_{false};
    int end_reason_ = -1;
    std::thread thread_;
};

template <typename Predicate> bool eventually(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return predicate();
}

constexpr NtpTimestamp fixed_now{0x11223344u, 0x55667788u};

void loopback_tests(const std::string& address) {
    const auto label = "loopback " + address + ": ";
    TimingResponder responder(address, 0, address, [] { return fixed_now; });
    check(responder.port() != 0, label + "ephemeral port bound");
    ServingThread serving(responder);
    Client client(address);
    const std::array<std::uint8_t, 5> garbage{1, 2, 3, 4, 5};
    client.send(address, responder.port(), garbage.data(), garbage.size());
    check(!client.receive(200ms).has_value(), label + "no reply to garbage");
    check(eventually([&] { return responder.ignored() == 1; }), label + "garbage counted");
    client.send(address, responder.port(), literal_request.data(), literal_request.size());
    const auto reply = client.receive(2000ms);
    const auto expected = encode_timing_response(
        *parse_timing_request(literal_request.data(), literal_request.size()), fixed_now,
        fixed_now);
    check(reply.has_value() && reply->size() == expected.size() &&
              std::equal(expected.begin(), expected.end(), reply->begin()),
          label + "reply matches the codec");
    check(eventually([&] { return responder.answered() == 1; }), label + "request counted");
    check(serving.stop() == static_cast<int>(TransportError::cancelled),
          label + "cancellation ends serving");
    expect_transport_error(label + "serve after close", TransportError::closed, [&] {
        std::atomic_bool flag{false};
        responder.serve(ReceiverOperation::until_cancelled(&flag));
    });
}

void responder_tests() {
    group = "timing responder";
    loopback_tests("127.0.0.1");
    try {
        loopback_tests("::1");
    } catch (const TransportException&) {
        std::cout << "IPv6 loopback unavailable; IPv6 responder test skipped\n";
    }
    {
        // Requests from any host other than the receiver are ignored.
        TimingResponder responder("127.0.0.1", 0, "127.0.0.2", [] { return fixed_now; });
        ServingThread serving(responder);
        Client client("127.0.0.1");
        client.send("127.0.0.1", responder.port(), literal_request.data(), literal_request.size());
        check(!client.receive(300ms).has_value(), "no reply to a foreign host");
        check(eventually([&] { return responder.ignored() == 1; }) && responder.answered() == 0,
              "foreign request counted as ignored");
    }
    expect_transport_error("non-numeric local address", TransportError::invalid_argument,
                           [] { TimingResponder responder("localhost", 0, "127.0.0.1"); });
    expect_transport_error("address family mismatch", TransportError::invalid_argument,
                           [] { TimingResponder responder("127.0.0.1", 0, "::1"); });
}
} // namespace

int main() {
    try {
        conversion_tests();
        codec_tests();
        responder_tests();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected test exception [" << group << "]: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " NTP timing test failure(s)\n";
        return 1;
    }
    std::cout << "NTP timing tests passed\n";
    return 0;
}
