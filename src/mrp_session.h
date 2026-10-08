// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_MRP_SESSION_H
#define SEND_AIRPLAY2_MRP_SESSION_H
#include "mrp_channel.h"
#include "mrp_messages.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>
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
/// One receiver power report: logicalDeviceCount from the handshake's
/// DEVICE_INFO or a later DEVICE_INFO_UPDATE, and when it arrived.
struct MrpPowerObservation {
    std::chrono::steady_clock::time_point received{};
    std::uint32_t logical_devices = 0;
};
/// Reports kept per session; later ones are dropped (diagnostics only).
constexpr std::size_t max_power_observations = 16;
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
    /// Receiver power reports so far, oldest first (at most max_power_observations).
    [[nodiscard]] std::vector<MrpPowerObservation> power_observations() const;
    /// The latest logicalDeviceCount, from every report (not only the kept ones).
    [[nodiscard]] std::optional<std::uint32_t> logical_devices() const;
    /// Send WAKE_DEVICE and return once the worker has taken it for sending; no
    /// response is defined. Cancellation or failure is terminal, as in handshake().
    void wake(const std::atomic_bool* cancelled = nullptr);
    /**
     * Wait until the receiver reports at least one logical device and no report
     * has changed the count for `settle` (tvOS reports a short 1-0-1 sequence
     * while the TV comes up). Returns false at `deadline`, on cancellation, stop
     * or session failure; waking is best effort, so these are not exceptions.
     */
    [[nodiscard]] bool wait_until_awake(std::chrono::milliseconds settle,
                                        std::chrono::steady_clock::time_point deadline,
                                        const std::atomic_bool* cancelled = nullptr);
    void stop() noexcept;

private:
    MrpMessage request(std::uint32_t type, Bytes payload, std::uint32_t response_type,
                       const std::atomic_bool* cancelled);
    void enqueue(std::uint32_t type, Bytes payload, const std::string& identifier);
    void run();
    void fail(MrpError reason);
    void record_power_locked(const MrpMessage& message);
    void send_unanswered(std::uint32_t type, Bytes payload, const std::atomic_bool* cancelled);
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
    std::vector<MrpPowerObservation> power_observations_;   // Guarded by mutex_.
    std::optional<std::uint32_t> logical_devices_;          // Latest report; mutex_.
    std::chrono::steady_clock::time_point power_changed_{}; // Last count change; mutex_.
    std::atomic_bool stop_{false};
    std::thread worker_;
};
} // namespace send_airplay2::detail
#endif
