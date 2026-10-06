// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_URL_PLAYBACK_SESSION_H
#define SEND_AIRPLAY2_URL_PLAYBACK_SESSION_H

#include "pair_verify.h"
#include "mrp_messages.h"
#include "receiver_stream.h"
#include "session_messages.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace send_airplay2::detail {
class EventChannel;
class ReceiverConnection;
class TimingResponder;
class MrpSession;

/// Session-level failures. Transport, record and verification failures keep
/// their own exception types and categories.
enum class SessionError {
    rejected,       // The receiver answered a required request with a non-2xx status.
    start_timeout,  // No "playing" state before the start deadline.
    connection_lost // The control, event or timing connection failed.
};
/// Categories and HTTP status only; never URLs, identifiers or payloads.
class SessionException : public std::runtime_error {
public:
    explicit SessionException(SessionError reason, unsigned status = 0);
    [[nodiscard]] SessionError reason() const noexcept {
        return reason_;
    }
    /// The rejecting status for SessionError::rejected, otherwise 0.
    [[nodiscard]] unsigned status() const noexcept {
        return status_;
    }

private:
    SessionError reason_;
    unsigned status_;
};

/// Opens a TCP stream to a receiver endpoint; tests inject fake receivers.
using StreamConnector = std::function<std::unique_ptr<ReceiverStream>(const ReceiverEndpoint&,
                                                                      const ReceiverOperation&)>;

struct UrlPlaybackOptions {
    ReceiverEndpoint receiver; // The AirPlay control endpoint (port 7000).
    std::string media_url;     // Private: never logged or put in errors.
    double start_position_seconds = 0;
    SenderIdentity identity; // A random device ID is used when empty.
    std::chrono::milliseconds request_timeout{5000};
    std::chrono::milliseconds start_timeout{30000}; // Until the receiver reports "playing".
    std::chrono::milliseconds feedback_interval{2000};
    StreamConnector connect; // Defaults to connect_receiver.
    /// Diagnostic: keep value-free outlines of received events
    /// (describe_event_structure) for take_event_log().
    bool record_event_structure = false;
    /// Keep false only for the recorded minimum-session experiment/tests.
    bool enable_mrp = true;
};

/// A snapshot of session progress, safe to read from any thread.
struct SessionStatus {
    std::string playback_state; // Lower-cased; empty before the first state event.
    std::uint64_t events = 0;
    std::uint64_t remote_events = 0;     // Answered on the separate remote-control session.
    std::uint64_t unreadable_events = 0; // Answered, but not a decodable event body.
    std::uint64_t feedback_sent = 0;
    std::uint64_t timing_answered = 0;
    bool failed = false; // The control or event connection failed after start.
};

/**
 * One URL playback session with a receiver, following the sequence validated
 * on tvOS 26.6 with the reference sender (session-design.md section 2):
 * remote-control pair-verify/SETUP/events, RECORD, data SETUP/MRP handshake,
 * then a separate URL
 * pair-verify, timing responder, base SETUP, event channel, periodic
 * /feedback, GET /info, RECORD, the URL control stream SETUP, then the four
 * /command start commands. start() returns once the receiver reports
 * "playing".
 *
 * Threads (D28): the control connection is shared by the caller and the
 * feedback thread under one mutex, so one request is in flight at a time. An
 * event thread answers receiver requests and publishes state; a timing thread
 * answers NTP requests. No user code runs on these threads.
 *
 * stop() tears down in a fixed order, even after earlier failures: feedback,
 * URL event channel, URL control connection, timing responder, then remote
 * MRP data and remote event/control connections. Remote events do not update URL playback state.
 * It is idempotent and runs from the destructor and when start() fails. Credentials are
 * borrowed for start() only. Noncopyable, nonmovable.
 */
class UrlPlaybackSession {
public:
    /**
     * Throws SessionException, TransportException, PairVerifyException or
     * ControlException or MrpException; every failure tears down what was started.
     * `cancelled`, when given, aborts the start at the next check.
     */
    [[nodiscard]] static std::unique_ptr<UrlPlaybackSession>
    start(const PairCredentials& credentials, UrlPlaybackOptions options,
          const std::atomic_bool* cancelled = nullptr);
    ~UrlPlaybackSession();
    UrlPlaybackSession(const UrlPlaybackSession&) = delete;
    UrlPlaybackSession& operator=(const UrlPlaybackSession&) = delete;
    UrlPlaybackSession(UrlPlaybackSession&&) = delete;
    UrlPlaybackSession& operator=(UrlPlaybackSession&&) = delete;

    [[nodiscard]] SessionStatus status() const;
    [[nodiscard]] MrpPlaybackStatus playback_status() const;
    /// Synchronous correlated MRP controls. A command requires ownership of
    /// our URL item; rejection is reported without changing playback locally.
    void command(PlaybackCommand command, double position_seconds = 0);
    /// Block until the playback state differs from `previous`, the session
    /// fails, or `timeout` passes; returns the status at that point.
    [[nodiscard]] SessionStatus wait_for_change(const std::string& previous,
                                                std::chrono::milliseconds timeout) const;
    void stop() noexcept;
    /// Diagnostic: the event outlines recorded since the last call, oldest
    /// first. At most 256 are kept; older ones are dropped. Empty unless
    /// record_event_structure was set.
    [[nodiscard]] std::vector<std::string> take_event_log();

private:
    explicit UrlPlaybackSession(UrlPlaybackOptions options);
    void run_start(const PairCredentials& credentials, const std::atomic_bool* cancelled);
    /// Independent verified remote session and MRP; retained until URL teardown.
    /// enable_mrp=false preserves the isolated minimum H5 experiment.
    void open_remote_control(const PairCredentials& credentials, const std::atomic_bool* cancelled);
    void remote_event_loop();
    ReceiverResponse remote_request(std::string method, std::string target, Bytes body,
                                    const std::atomic_bool* cancelled, bool require_success);
    [[nodiscard]] ReceiverOperation operation(const std::atomic_bool* cancelled) const;
    /// An RTSP request with the session headers; Content-Type only with a body.
    [[nodiscard]] ReceiverRequest rtsp_request(std::string method, std::string target,
                                               Bytes body) const;
    /// One control request; non-2xx throws SessionException(rejected) when required.
    ReceiverResponse control_request(ReceiverRequest request, const std::atomic_bool* cancelled,
                                     bool require_success);
    void start_timing(const std::string& local_address);
    void open_event_channel(std::uint16_t event_port, const std::atomic_bool* cancelled);
    void start_feedback();
    void wait_until_playing(const std::atomic_bool* cancelled);
    void event_loop();
    void feedback_loop();
    void mark_failed();

    UrlPlaybackOptions options_;
    std::string session_uuid_;
    std::string rtsp_uri_;
    SessionHeaders headers_;

    mutable std::mutex control_mutex_; // Serializes all control_ requests.
    std::unique_ptr<ReceiverConnection> control_;

    // Used only by start/stop; the remote reader owns its separate event socket.
    std::unique_ptr<ReceiverConnection> remote_control_;
    SessionHeaders remote_headers_ = SessionHeaders::random();
    std::unique_ptr<EventChannel> remote_events_;
    std::atomic_bool remote_event_stop_{false};
    std::thread remote_event_thread_;
    std::unique_ptr<MrpSession> mrp_;

    std::unique_ptr<TimingResponder> timing_;
    std::atomic_bool timing_stop_{false};
    std::thread timing_thread_;

    std::unique_ptr<EventChannel> events_;
    std::atomic_bool event_stop_{false};
    std::thread event_thread_;

    std::atomic_bool feedback_stop_{false};
    std::thread feedback_thread_;

    mutable std::mutex state_mutex_; // Guards status_ and the stop flags' waits.
    mutable std::condition_variable state_changed_;
    SessionStatus status_;
    std::deque<std::string> event_log_;
    bool stopped_ = false;
};
} // namespace send_airplay2::detail
#endif
