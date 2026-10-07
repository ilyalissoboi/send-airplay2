// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_NTP_TIMING_H
#define SEND_AIRPLAY2_NTP_TIMING_H

#include "receiver_stream.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace send_airplay2::detail {
/**
 * AirPlay "NTP" timing exchange. A base SETUP with `timingProtocol` "NTP"
 * announces a sender UDP port; the receiver sends timing requests there and
 * the sender answers each one. Without answers the receiver stalls the SETUP
 * (observed on tvOS 26.6, see receiver-validation.md).
 *
 * Packet layout, 32 bytes, big-endian:
 *   0  protocol byte   1  type   2  sequence (2)   4  padding (4)
 *   8  reference time  16 receive time  24 send time   (each: seconds, fraction)
 */
namespace ntp_timing {
constexpr std::size_t packet_size = 32;
constexpr std::uint8_t request_type = 0xd2;  // Timing request (0x52) with the RTP marker bit.
constexpr std::uint8_t response_type = 0xd3; // Timing response (0x53) with the RTP marker bit.
/// Sequence number in every reply, as sent by the reference sender.
constexpr std::uint16_t response_sequence = 7;
/// Seconds from the NTP epoch (1900-01-01) to the Unix epoch (1970-01-01).
constexpr std::uint32_t unix_epoch_in_ntp_seconds = 2208988800u;
} // namespace ntp_timing

/// NTP timestamp: whole seconds since 1900 (modulo 2^32) and a binary
/// fraction of a second in units of 2^-32 s.
struct NtpTimestamp {
    std::uint32_t seconds = 0;
    std::uint32_t fraction = 0;
};

/// Wall-clock time as an NTP timestamp, at microsecond resolution.
[[nodiscard]] NtpTimestamp ntp_from_system_time(std::chrono::system_clock::time_point time);

/// The fields of a timing request that the reply uses.
struct TimingRequest {
    std::uint8_t protocol_byte = 0;
    NtpTimestamp send_time;
};

/// A request is exactly 32 bytes with type 0xd2; anything else is not one.
[[nodiscard]] std::optional<TimingRequest> parse_timing_request(const std::uint8_t* data,
                                                                std::size_t size);

/// The reference reply: the request's protocol byte, type 0xd3, sequence 7,
/// reference time = the request's send time, then the receive and send times.
[[nodiscard]] std::array<std::uint8_t, ntp_timing::packet_size>
encode_timing_response(const TimingRequest& request, NtpTimestamp receive_time,
                       NtpTimestamp send_time);

/**
 * UDP responder for one receiver's timing requests.
 *
 * Binds `local_address` (numeric, normally the local end of the control
 * connection) on an ephemeral port. Answers only datagrams from
 * `receiver_address`, whatever their source port, that parse as timing
 * requests. Everything else is dropped and counted; malformed or foreign
 * datagrams never end the responder.
 *
 * serve() runs on one dedicated thread until the operation is cancelled or
 * expires, then throws that TransportException; socket failures throw
 * TransportException(network). Every exception closes the socket.
 * answered()/ignored() may be read from any thread. Noncopyable, nonmovable.
 */
class TimingResponder {
public:
    using Clock = std::function<NtpTimestamp()>;

    /// `clock` supplies receive/send times; tests inject a fixed clock.
    TimingResponder(const std::string& local_address, std::uint32_t local_scope_id,
                    const std::string& receiver_address, Clock clock = {});
    ~TimingResponder();
    TimingResponder(const TimingResponder&) = delete;
    TimingResponder& operator=(const TimingResponder&) = delete;
    TimingResponder(TimingResponder&&) = delete;
    TimingResponder& operator=(TimingResponder&&) = delete;

    /// The bound UDP port, for the SETUP `timingPort` field.
    [[nodiscard]] std::uint16_t port() const noexcept {
        return port_;
    }
    void serve(const ReceiverOperation& operation);
    [[nodiscard]] std::uint64_t answered() const noexcept {
        return answered_.load();
    }
    [[nodiscard]] std::uint64_t ignored() const noexcept {
        return ignored_.load();
    }
    void close() noexcept;

private:
    struct Native;
    /// Handle at most one waiting datagram; returns false when none was waiting.
    bool handle_datagram();

    std::unique_ptr<Native> native_;
    Clock clock_;
    std::uint16_t port_ = 0;
    std::atomic<std::uint64_t> answered_{0};
    std::atomic<std::uint64_t> ignored_{0};
};
} // namespace send_airplay2::detail
#endif
