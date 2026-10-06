// SPDX-License-Identifier: Apache-2.0
// Packet layout and reply fields follow the reference sender's timing server
// (pyatv 0.18.0, MIT; constants only, no source incorporated). See dependencies.md.
#include "ntp_timing.h"
#include "native_socket.h"
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace send_airplay2::detail {
namespace {
/// Byte offsets within a timing packet.
namespace offset {
constexpr std::size_t protocol_byte = 0;
constexpr std::size_t type = 1;
constexpr std::size_t sequence = 2;
constexpr std::size_t reference_time = 8;
constexpr std::size_t receive_time = 16;
constexpr std::size_t send_time = 24;
} // namespace offset
constexpr std::uint64_t microseconds_per_second = 1000000;

std::uint32_t read_u32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24) |
           (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) | data[3];
}
void write_u32(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value >> 24);
    data[1] = static_cast<std::uint8_t>(value >> 16);
    data[2] = static_cast<std::uint8_t>(value >> 8);
    data[3] = static_cast<std::uint8_t>(value);
}
void write_timestamp(std::uint8_t* data, NtpTimestamp time) {
    write_u32(data, time.seconds);
    write_u32(data + 4, time.fraction);
}
} // namespace

NtpTimestamp ntp_from_system_time(std::chrono::system_clock::time_point time) {
    using std::chrono::microseconds;
    const auto since_unix_epoch =
        std::chrono::duration_cast<microseconds>(time.time_since_epoch()).count();
    auto whole_seconds = since_unix_epoch / static_cast<std::int64_t>(microseconds_per_second);
    auto remainder = since_unix_epoch % static_cast<std::int64_t>(microseconds_per_second);
    if (remainder < 0) { // Floor division for times before 1970.
        remainder += static_cast<std::int64_t>(microseconds_per_second);
        --whole_seconds;
    }
    NtpTimestamp output;
    // NTP seconds wrap modulo 2^32 (era rollover in 2036), as the wire field does.
    output.seconds = static_cast<std::uint32_t>(static_cast<std::uint64_t>(
        whole_seconds + static_cast<std::int64_t>(ntp_timing::unix_epoch_in_ntp_seconds)));
    output.fraction = static_cast<std::uint32_t>((static_cast<std::uint64_t>(remainder) << 32) /
                                                 microseconds_per_second);
    return output;
}

std::optional<TimingRequest> parse_timing_request(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr || size != ntp_timing::packet_size ||
        data[offset::type] != ntp_timing::request_type) {
        return std::nullopt;
    }
    TimingRequest request;
    request.protocol_byte = data[offset::protocol_byte];
    request.send_time = {read_u32(data + offset::send_time),
                         read_u32(data + offset::send_time + 4)};
    return request;
}

std::array<std::uint8_t, ntp_timing::packet_size>
encode_timing_response(const TimingRequest& request, NtpTimestamp receive_time,
                       NtpTimestamp send_time) {
    std::array<std::uint8_t, ntp_timing::packet_size> packet{}; // Padding stays zero.
    packet[offset::protocol_byte] = request.protocol_byte;
    packet[offset::type] = ntp_timing::response_type;
    packet[offset::sequence] = static_cast<std::uint8_t>(ntp_timing::response_sequence >> 8);
    packet[offset::sequence + 1] = static_cast<std::uint8_t>(ntp_timing::response_sequence);
    // The reference time echoes the request's send time so the receiver can
    // match the reply and compute the round trip.
    write_timestamp(packet.data() + offset::reference_time, request.send_time);
    write_timestamp(packet.data() + offset::receive_time, receive_time);
    write_timestamp(packet.data() + offset::send_time, send_time);
    return packet;
}

/// Platform socket state; the socket closes before the Winsock reference ends.
struct TimingResponder::Native {
    [[maybe_unused]] native::NetworkRuntime runtime;
    native::SocketOwner socket;
    native::NativeAddress receiver;
};

TimingResponder::TimingResponder(const std::string& local_address, std::uint32_t local_scope_id,
                                 const std::string& receiver_address, Clock clock)
    : native_(std::make_unique<Native>()), clock_(std::move(clock)) {
    try {
        if (!clock_) {
            clock_ = [] { return ntp_from_system_time(std::chrono::system_clock::now()); };
        }
        const auto local = native::numeric_socket_address(local_address, 0, local_scope_id);
        // A link-local receiver is reached through the same interface scope.
        native_->receiver = native::numeric_socket_address(
            receiver_address, 0, local.family == AF_INET6 ? local_scope_id : 0);
        if (native_->receiver.family != local.family) {
            throw TransportException(TransportError::invalid_argument);
        }
        native_->socket.value = ::socket(local.family, SOCK_DGRAM, IPPROTO_UDP);
        if (native_->socket.value == native::invalid_socket) {
            native::network_failure();
        }
        native::nonblocking(native_->socket.value);
        if (::bind(native_->socket.value, reinterpret_cast<const sockaddr*>(&local.storage),
                   local.size) != 0) {
            native::network_failure();
        }
        sockaddr_storage bound{};
        native::SocketLength bound_size = sizeof(bound);
        if (::getsockname(native_->socket.value, reinterpret_cast<sockaddr*>(&bound),
                          &bound_size) != 0) {
            native::network_failure();
        }
        if (bound.ss_family == AF_INET) {
            sockaddr_in ipv4{};
            std::memcpy(&ipv4, &bound, sizeof(ipv4));
            port_ = ntohs(ipv4.sin_port);
        } else {
            sockaddr_in6 ipv6{};
            std::memcpy(&ipv6, &bound, sizeof(ipv6));
            port_ = ntohs(ipv6.sin6_port);
        }
    } catch (...) {
        close();
        throw;
    }
}

TimingResponder::~TimingResponder() {
    close();
}

void TimingResponder::close() noexcept {
    if (native_) {
        native_->socket.close();
    }
}

void TimingResponder::serve(const ReceiverOperation& operation) {
    try {
        if (native_->socket.value == native::invalid_socket) {
            throw TransportException(TransportError::closed);
        }
        for (;;) {
            operation.check();
            if (!native::ready(native_->socket.value, false, native::poll_slice(operation))) {
                continue;
            }
            while (handle_datagram()) {
                operation.check();
            }
        }
    } catch (...) {
        close();
        throw;
    }
}

bool TimingResponder::handle_datagram() {
    // One spare byte reveals datagrams longer than a timing packet.
    std::array<std::uint8_t, ntp_timing::packet_size + 1> buffer{};
    sockaddr_storage source{};
    native::SocketLength source_size = sizeof(source);
    const auto received = ::recvfrom(native_->socket.value, reinterpret_cast<char*>(buffer.data()),
                                     static_cast<int>(buffer.size()), 0,
                                     reinterpret_cast<sockaddr*>(&source), &source_size);
    if (received < 0) {
        const auto error = native::socket_error();
        if (native::retryable(error)) {
            return false;
        }
#ifdef _WIN32
        // Oversized datagrams, and ICMP "port unreachable" from an earlier
        // reply, surface as errors on Windows; neither ends the responder.
        if (error == WSAEMSGSIZE || error == WSAECONNRESET) {
            ++ignored_;
            return true;
        }
#endif
        native::network_failure();
    }
    native::NativeAddress sender;
    std::memcpy(&sender.storage, &source, sizeof(source));
    sender.size = static_cast<int>(source_size);
    sender.family = source.ss_family;
    const auto request = parse_timing_request(buffer.data(), static_cast<std::size_t>(received));
    if (!native::same_host(sender, native_->receiver) || !request) {
        ++ignored_;
        return true;
    }
    const auto now = clock_();
    const auto reply = encode_timing_response(*request, now, now);
    const auto sent = ::sendto(native_->socket.value, reinterpret_cast<const char*>(reply.data()),
                               static_cast<int>(reply.size()), 0,
                               reinterpret_cast<const sockaddr*>(&source), source_size);
    if (sent != static_cast<decltype(sent)>(reply.size())) {
        if (sent < 0 && native::retryable(native::socket_error())) {
            ++ignored_; // A full send buffer drops this reply; the receiver retries.
            return true;
        }
        native::network_failure();
    }
    ++answered_;
    return true;
}
} // namespace send_airplay2::detail
