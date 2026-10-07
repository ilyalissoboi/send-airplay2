// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_MRP_SESSION_H
#define SEND_AIRPLAY2_MRP_SESSION_H
#include "mrp_channel.h"
#include "mrp_messages.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
namespace send_airplay2::detail {
enum class MrpError {
    malformed,
    authentication,
    rejected,
    timeout,
    disconnected,
    cancelled,
    not_owned
};
/// Fixed category text only: no receiver descriptions or identifiers.
class MrpException : public std::runtime_error {
public:
    explicit MrpException(MrpError reason);
    [[nodiscard]] MrpError reason() const noexcept {
        return reason_;
    }

private:
    MrpError reason_;
};
/** One verified data stream. A single worker owns its socket/record counters;
 * callers serialize correlated requests under request_mutex_. Unsolicited
 * updates continue during waits. Timeouts/cancellation are terminal (late
 * replies cannot satisfy subsequent commands). No user callbacks run here.
 * stop() joins the worker before erasing keys/closing the stream. Call stop
 * after callers have returned; destruction must not race a callable method.
 */
class MrpSession {
public:
    MrpSession(std::unique_ptr<MrpChannel> channel, std::chrono::milliseconds request_timeout,
               std::chrono::milliseconds heartbeat_interval = std::chrono::seconds(30));
    ~MrpSession();
    MrpSession(const MrpSession&) = delete;
    MrpSession& operator=(const MrpSession&) = delete;
    MrpSession(MrpSession&&) = delete;
    MrpSession& operator=(MrpSession&&) = delete;
    /// DEVICE_INFO first, connection state, then playback update subscription.
    /// Keyboard session is deliberately omitted unless hardware requires it.
    void handshake(const SenderIdentity& identity, const Bytes& pairing_id,
                   const std::atomic_bool* cancelled = nullptr);
    void expect_item(std::string uuid, std::string url);
    void confirm_url_playing(double duration_seconds);
    void command(PlaybackCommand command, double position_seconds = 0,
                 const std::atomic_bool* cancelled = nullptr);
    [[nodiscard]] MrpPlaybackStatus status() const;
    [[nodiscard]] bool failed() const;
    /// First terminal category, retained after stop(); safe alongside status/commands.
    [[nodiscard]] std::optional<MrpError> failure() const;
    void stop() noexcept;

private:
    MrpMessage request(std::uint32_t type, Bytes payload, std::uint32_t response_type,
                       const std::atomic_bool* cancelled);
    void enqueue(std::uint32_t type, Bytes payload, const std::string& identifier);
    void run();
    void fail(MrpError reason);
    std::unique_ptr<MrpChannel> channel_;
    std::chrono::milliseconds request_timeout_, heartbeat_interval_;
    mutable std::mutex mutex_;
    std::mutex request_mutex_;
    std::condition_variable changed_;
    Bytes outbound_;
    std::string waiting_identifier_;
    std::uint32_t waiting_type_ = 0;
    std::optional<MrpMessage> response_;
    std::optional<MrpError> failure_;
    MrpPlaybackTracker tracker_;
    std::uint64_t messages_ = 0, heartbeats_ = 0;
    bool ready_ = false;
    std::atomic_bool stop_{false};
    std::thread worker_;
};
} // namespace send_airplay2::detail
#endif
