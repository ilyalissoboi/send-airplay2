// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_URL_PLAYBACK_SESSION_H
#define SEND_AIRPLAY2_URL_PLAYBACK_SESSION_H

#include "pair_verify.h"
#include "mrp_messages.h"
#include "receiver_stream.h"
#include "session_messages.h"
#include <atomic>
#include <array>
#include <cstddef>
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
    /// Require this continuous eligible URL playing interval before returning.
    /// Reset on every observed non-playing/zero/reverse-rate state; no retry is
    /// sent. Must be nonnegative. It consumes the existing start_timeout budget.
    std::chrono::milliseconds start_confirmation_interval{1000};
    std::chrono::milliseconds feedback_interval{2000};
    StreamConnector connect; // Defaults to connect_receiver.
    /// Diagnostic: keep event outlines and allowlisted numeric/boolean buffering
    /// values (session_messages.h) for take_event_log().
    bool record_event_structure = false;
    /// Keep false only for the recorded minimum-session experiment/tests.
    bool enable_mrp = true;
};

/// First terminal reason; retained after cleanup and subsequent stop() calls.
enum class SessionEnd {
    none,
    sender_stop,
    media_end,
    receiver_stop,
    ownership_lost,
    connection_lost
};
/// Fixed diagnostics only; never receiver-provided descriptions.
[[nodiscard]] const char* session_end_name(SessionEnd reason) noexcept;

/// Local channel/operation and fixed error category; no peer text or identifiers.
/// The first observed failure is retained; cancellation during normal cleanup is omitted.
enum class SessionFailureChannel {
    none,
    url_events,
    remote_events,
    url_feedback,
    remote_feedback,
    timing,
    mrp,
    supervisor
};
enum class SessionFailureReason {
    none,
    timeout,
    disconnected,
    network,
    invalid_message,
    authentication,
    cancelled,
    rejected,
    other
};
[[nodiscard]] const char* session_failure_channel_name(SessionFailureChannel channel) noexcept;
[[nodiscard]] const char* session_failure_reason_name(SessionFailureReason reason) noexcept;

/// Local startup phases and allowlisted receiver states; no peer text or IDs.
enum class SessionStartPhase {
    connecting,
    insert_item,
    date_range,
    item_end,
    rate,
    waiting,
    ready,
    failed
};
enum class SessionStartState { none, loading, playing, paused, idle, stopped, ended, other };
[[nodiscard]] const char* session_start_phase_name(SessionStartPhase phase) noexcept;
[[nodiscard]] const char* session_start_state_name(SessionStartState state) noexcept;
struct SessionStartTraceEntry {
    std::uint64_t elapsed_ms = 0; // Steady time since this session was constructed.
    SessionStartPhase phase = SessionStartPhase::connecting;
    SessionStartState state = SessionStartState::none;
    unsigned response_status = 0;        // Zero for phase/event records, otherwise HTTP status.
    std::optional<double> playback_rate; // Finite receiver scalar, not inferred progress.
};
/// First 64 startup records, preserved on success/failure. Overflow drops new
/// records without changing startup behavior; no allocation or secret payloads.
constexpr std::size_t max_start_trace_entries = 64;
struct SessionStartDiagnostics {
    std::array<SessionStartTraceEntry, max_start_trace_entries> entries{};
    std::size_t count = 0;
    bool truncated = false;
    bool cleaned_up = false;
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
    SessionFailureChannel failure_channel = SessionFailureChannel::none;
    SessionFailureReason failure_reason = SessionFailureReason::none;
    SessionEnd end_reason = SessionEnd::none;
    bool cleaned_up =
        false; // Transport workers joined and channel secrets erased; stop joins supervisor.
};

/**
 * One URL playback session with a receiver, following the sequence validated
 * on tvOS 26.6 with the reference sender (session-design.md section 2):
 * remote-control pair-verify/SETUP/events, RECORD, data SETUP/MRP handshake,
 * then a separate URL
 * pair-verify, timing responder, base SETUP, event channel, periodic
 * /feedback, GET /info, RECORD, the URL control stream SETUP, then the four
 * /command start commands. start() returns once the receiver reports
 * "playing" without an explicitly stationary/reverse rate for the configured
 * confirmation interval. Missing rate keeps state-only compatibility. This
 * filters short startup transitions; it is not decoder/visual proof or a
 * guarantee that the receiver will continue playing after start() returns.
 *
 * Threads (D28): the control connection is shared by the caller and the
 * feedback thread under one mutex, so one request is in flight at a time. An
 * event thread answers receiver requests and publishes state; a timing thread
 * answers NTP requests. A supervisor detects EOF, receiver stop, ownership
 * loss or connection failure and owns cleanup. No user code runs on these threads.
 *
 * Automatic cleanup and stop() use the same fixed order, even after failures: feedback,
 * URL event channel, URL control connection, timing responder, then remote
 * MRP data and remote event/control connections. Remote events do not update URL playback state.
 * stop() is idempotent and runs from the destructor and when start() fails.
 * The host owns its media server and must stop it after session cleanup.
 * Credentials are borrowed for start() only. Noncopyable, nonmovable.
 */
class UrlPlaybackSession {
public:
    /**
     * Throws SessionException, TransportException, PairVerifyException or
     * ControlException or MrpException; every failure tears down what was started.
     * `cancelled`, when given, aborts the start at the next check.
     * Optional `diagnostics` is borrowed only for this call and written by the
     * caller thread before return/rethrow, after cleanup on failure. It never
     * receives peer text, URLs, identifiers or authentication material.
     */
    [[nodiscard]] static std::unique_ptr<UrlPlaybackSession>
    start(const PairCredentials& credentials, UrlPlaybackOptions options,
          const std::atomic_bool* cancelled = nullptr,
          SessionStartDiagnostics* diagnostics = nullptr);
    ~UrlPlaybackSession();
    UrlPlaybackSession(const UrlPlaybackSession&) = delete;
    UrlPlaybackSession& operator=(const UrlPlaybackSession&) = delete;
    UrlPlaybackSession(UrlPlaybackSession&&) = delete;
    UrlPlaybackSession& operator=(UrlPlaybackSession&&) = delete;

    [[nodiscard]] SessionStatus status() const;
    [[nodiscard]] MrpPlaybackStatus playback_status() const;
    /// Synchronous correlated MRP controls. A command requires ownership of
    /// our URL item; rejection is reported without changing playback locally.
    /// Teardown cancels a pending command before closing its transport.
    void command(PlaybackCommand command, double position_seconds = 0);
    /// Block until the playback state differs from `previous`, the session
    /// fails/ends, or `timeout` passes; returns the status at that point.
    [[nodiscard]] SessionStatus wait_for_change(const std::string& previous,
                                                std::chrono::milliseconds timeout) const;
    /// Request local teardown and wait for the sole cleanup owner. Safe alongside
    /// status/commands and another stop; first terminal reason wins. Destruction
    /// still requires all external callers to have returned.
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
    void trace_start_phase(SessionStartPhase phase, unsigned response_status = 0);
    /// Requires state_mutex_; recording uses fixed storage only.
    void append_start_trace_locked(SessionStartState state, unsigned response_status,
                                   std::optional<double> playback_rate = {});
    void finish_start_trace(SessionStartDiagnostics& diagnostics);
    void event_loop();
    void feedback_loop();
    void mark_failed(SessionFailureChannel channel, SessionFailureReason reason);
    void request_end(SessionEnd reason);
    void supervise();
    void cleanup() noexcept;

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
    std::optional<double> state_playback_rate_; // Rate on the latest URL state event.
    std::optional<std::chrono::steady_clock::time_point> forward_playing_since_;
    const std::chrono::steady_clock::time_point start_trace_origin_ =
        std::chrono::steady_clock::now();
    bool capture_start_trace_ = false; // Guarded by state_mutex_ once threads start.
    SessionStartPhase start_trace_phase_ = SessionStartPhase::connecting;
    SessionStartDiagnostics start_trace_;
    std::deque<std::string> event_log_;
    std::atomic_bool stopping_{false}; // Also cancels a pending MRP command.
    std::mutex command_mutex_;         // Prevents teardown racing a callable command.
    std::mutex stop_mutex_;            // Serializes joining the sole supervisor.
    std::thread supervisor_thread_;
};
} // namespace send_airplay2::detail
#endif
