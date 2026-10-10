// SPDX-License-Identifier: Apache-2.0
#include "url_playback_session.h"
#include "channel_keys.h"
#include "control_crypto.h"
#include "event_channel.h"
#include "native_socket.h"
#include "ntp_timing.h"
#include "receiver_connection.h"
#include "mrp_session.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace {
/// Longest single wait while polling for cancellation during start.
constexpr std::chrono::milliseconds start_poll_slice{20};
// MRP ownership of our item normally follows the confirmed playing state
// within a few hundred milliseconds (D30).
constexpr std::chrono::milliseconds start_ownership_wait{5000};
constexpr const char* playing_state = "playing";
constexpr std::size_t max_event_log = 256;

SessionStartState start_state(const std::string& state) noexcept {
    if (state == "loading") {
        return SessionStartState::loading;
    }
    if (state == "playing") {
        return SessionStartState::playing;
    }
    if (state == "paused") {
        return SessionStartState::paused;
    }
    if (state == "idle") {
        return SessionStartState::idle;
    }
    if (state == "stopped") {
        return SessionStartState::stopped;
    }
    if (state == "ended") {
        return SessionStartState::ended;
    }
    return SessionStartState::other;
}

SessionFailureReason mrp_failure_reason(MrpError reason) noexcept {
    switch (reason) {
    case MrpError::timeout:
        return SessionFailureReason::timeout;
    case MrpError::disconnected:
        return SessionFailureReason::disconnected;
    case MrpError::malformed:
        return SessionFailureReason::invalid_message;
    case MrpError::authentication:
        return SessionFailureReason::authentication;
    case MrpError::cancelled:
        return SessionFailureReason::cancelled;
    case MrpError::rejected:
        return SessionFailureReason::rejected;
    case MrpError::not_owned:
        return SessionFailureReason::other;
    }
    return SessionFailureReason::other;
}

/// A URL control's transport failure in the categories MRP commands report,
/// so hosts see one command contract whichever control path is active (D62).
MrpError url_command_error(TransportError reason) noexcept {
    switch (reason) {
    case TransportError::timeout:
        return MrpError::timeout;
    case TransportError::cancelled:
        return MrpError::cancelled;
    case TransportError::invalid_message:
    case TransportError::correlation:
        return MrpError::malformed;
    case TransportError::disconnected:
    case TransportError::closed:
    case TransportError::network:
    case TransportError::invalid_argument:
        return MrpError::disconnected;
    }
    return MrpError::disconnected;
}

/// Call only from an active catch handler. Never retain exception text or peer bytes.
SessionFailureReason caught_failure_reason() noexcept {
    try {
        throw;
    } catch (const TransportException& error) {
        switch (error.reason()) {
        case TransportError::timeout:
            return SessionFailureReason::timeout;
        case TransportError::cancelled:
            return SessionFailureReason::cancelled;
        case TransportError::disconnected:
        case TransportError::closed:
            return SessionFailureReason::disconnected;
        case TransportError::network:
            return SessionFailureReason::network;
        case TransportError::invalid_message:
        case TransportError::correlation:
            return SessionFailureReason::invalid_message;
        case TransportError::invalid_argument:
            return SessionFailureReason::other;
        }
    } catch (const ControlException& error) {
        return error.reason() == ControlError::authentication ? SessionFailureReason::authentication
                                                              : SessionFailureReason::other;
    } catch (...) {
        return SessionFailureReason::other;
    }
    return SessionFailureReason::other;
}

std::string session_message(SessionError reason, unsigned status) {
    std::string text =
        "AirPlay session failed (category " + std::to_string(static_cast<int>(reason)) + ")";
    if (status != 0) {
        text += ", receiver status " + std::to_string(status);
    }
    return text;
}

/// The RTSP request URI names the sender's address and a random session
/// number, as the reference does; IPv6 addresses are bracketed.
std::string rtsp_uri(const std::string& local_address) {
    std::array<std::uint8_t, 4> random{};
    public_random_bytes(random.data(), random.size());
    std::uint32_t number = 0;
    for (const auto byte : random) {
        number = (number << 8) | byte;
    }
    const auto host =
        local_address.find(':') == std::string::npos ? local_address : "[" + local_address + "]";
    return "rtsp://" + host + "/" + std::to_string(number);
}

/// Owned plaintext that may contain the private media URL; erased on exit.
struct ErasedRequest {
    ReceiverRequest request;
    explicit ErasedRequest(ReceiverRequest value) : request(std::move(value)) {}
    ~ErasedRequest() {
        cleanse(request.body.data(), request.body.size());
    }
    ErasedRequest(const ErasedRequest&) = delete;
    ErasedRequest& operator=(const ErasedRequest&) = delete;
    ErasedRequest(ErasedRequest&&) = delete;
    ErasedRequest& operator=(ErasedRequest&&) = delete;
};

/// Acknowledged event plaintext is erased even if diagnostic decoding fails.
struct ErasedEvent {
    EventRequest request;
    explicit ErasedEvent(EventRequest value) : request(std::move(value)) {}
    ~ErasedEvent() {
        cleanse(request.body.data(), request.body.size());
    }
    ErasedEvent(const ErasedEvent&) = delete;
    ErasedEvent& operator=(const ErasedEvent&) = delete;
    ErasedEvent(ErasedEvent&&) = delete;
    ErasedEvent& operator=(ErasedEvent&&) = delete;
};
} // namespace

SessionException::SessionException(SessionError reason, unsigned status)
    : std::runtime_error(session_message(reason, status)), reason_(reason), status_(status) {}

const char* session_end_name(SessionEnd reason) noexcept {
    switch (reason) {
    case SessionEnd::none:
        return "none";
    case SessionEnd::sender_stop:
        return "sender_stop";
    case SessionEnd::media_end:
        return "media_end";
    case SessionEnd::receiver_stop:
        return "receiver_stop";
    case SessionEnd::ownership_lost:
        return "ownership_lost";
    case SessionEnd::connection_lost:
        return "connection_lost";
    }
    return "none";
}

const char* session_failure_channel_name(SessionFailureChannel channel) noexcept {
    switch (channel) {
    case SessionFailureChannel::none:
        return "none";
    case SessionFailureChannel::url_events:
        return "url_events";
    case SessionFailureChannel::remote_events:
        return "remote_events";
    case SessionFailureChannel::url_feedback:
        return "url_feedback";
    case SessionFailureChannel::remote_feedback:
        return "remote_feedback";
    case SessionFailureChannel::timing:
        return "timing";
    case SessionFailureChannel::mrp:
        return "mrp";
    case SessionFailureChannel::supervisor:
        return "supervisor";
    }
    return "none";
}

const char* session_failure_reason_name(SessionFailureReason reason) noexcept {
    switch (reason) {
    case SessionFailureReason::none:
        return "none";
    case SessionFailureReason::timeout:
        return "timeout";
    case SessionFailureReason::disconnected:
        return "disconnected";
    case SessionFailureReason::network:
        return "network";
    case SessionFailureReason::invalid_message:
        return "invalid_message";
    case SessionFailureReason::authentication:
        return "authentication";
    case SessionFailureReason::cancelled:
        return "cancelled";
    case SessionFailureReason::rejected:
        return "rejected";
    case SessionFailureReason::other:
        return "other";
    }
    return "other";
}

UrlPlaybackSession::UrlPlaybackSession(UrlPlaybackOptions options)
    : options_(std::move(options)), session_uuid_(random_uuid()),
      headers_(SessionHeaders::random()) {}

std::unique_ptr<UrlPlaybackSession>
UrlPlaybackSession::start(const PairCredentials& credentials, UrlPlaybackOptions options,
                          const std::atomic_bool* cancelled, SessionStartDiagnostics* diagnostics) {
    if (diagnostics) {
        *diagnostics = {};
    }
    if (options.start_confirmation_interval < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Startup confirmation interval must be nonnegative");
    }
    if (!options.connect) {
        options.connect = connect_receiver;
    }
    if (options.identity.device_id.empty()) {
        options.identity.device_id = random_device_id();
    }
    std::unique_ptr<UrlPlaybackSession> session(new UrlPlaybackSession(std::move(options)));
    session->capture_start_trace_ = diagnostics != nullptr;
    try {
        session->trace_start_phase(SessionStartPhase::connecting);
        session->run_start(credentials, cancelled);
        // Owners are fully initialized before the cleanup thread can inspect them.
        session->supervisor_thread_ = std::thread([owner = session.get()] { owner->supervise(); });
        if (session->options_.start_position_seconds > 0) {
            session->apply_start_position(cancelled);
        }
        session->trace_start_phase(SessionStartPhase::ready);
        if (diagnostics) {
            session->finish_start_trace(*diagnostics);
        }
    } catch (...) {
        session->trace_start_phase(SessionStartPhase::failed);
        session->stop();
        if (diagnostics) {
            session->finish_start_trace(*diagnostics);
        }
        throw;
    }
    return session;
}

UrlPlaybackSession::~UrlPlaybackSession() {
    stop();
}

ReceiverOperation UrlPlaybackSession::operation(const std::atomic_bool* cancelled) const {
    return ReceiverOperation::after(options_.request_timeout, cancelled);
}

void UrlPlaybackSession::run_start(const PairCredentials& credentials,
                                   const std::atomic_bool* cancelled) {
    const auto local_address = native::route_local_address(options_.receiver);
    rtsp_uri_ = rtsp_uri(local_address);
    open_remote_control(credentials, cancelled);
    wake_receiver_if_asleep(cancelled);
    control_ = std::make_unique<ReceiverConnection>(
        options_.connect(options_.receiver, operation(cancelled)), options_.receiver.authority());
    control_->verify(credentials, operation(cancelled));

    // The base SETUP announces the timing port; the receiver queries it at once.
    // macOS answers it only once its user allows the sender on screen (D62).
    start_timing(local_address);
    const auto base = control_request(
        rtsp_request(
            "SETUP", rtsp_uri_,
            base_setup_body(options_.identity, session_uuid_, random_uuid(), timing_->port())),
        cancelled, true,
        url_controls_ ? std::optional<std::chrono::milliseconds>{options_.consent_timeout}
                      : std::nullopt);
    open_event_channel(parse_event_port(base.body), cancelled);
    start_feedback();

    // The reference reads /info here and tolerates errors.
    (void)control_request(rtsp_request("GET", "/info", {}), cancelled, false);
    (void)control_request(rtsp_request("RECORD", rtsp_uri_, {}), cancelled, true);
    const auto stream = parse_stream_setup(
        control_request(rtsp_request("SETUP", rtsp_uri_,
                                     url_stream_setup_body(options_.identity, random_uuid())),
                        cancelled, true)
            .body);
    stream_id_ = stream.stream_id;

    item_uuid_ = random_uuid();
    if (mrp_) {
        mrp_->expect_item(item_uuid_, options_.media_url);
    }
    const PlistValue commands[] = {
        insert_play_queue_item(item_uuid_, options_.media_url, options_.start_position_seconds),
        set_interested_in_date_range(item_uuid_),
        set_action_at_item_end(),
        set_rate(1.0),
    };
    constexpr SessionStartPhase phases[] = {SessionStartPhase::insert_item,
                                            SessionStartPhase::date_range,
                                            SessionStartPhase::item_end, SessionStartPhase::rate};
    static_assert(std::size(commands) == std::size(phases));
    for (std::size_t index = 0; index < std::size(commands); ++index) {
        trace_start_phase(phases[index]);
        ReceiverRequest request;
        request.method = "POST";
        request.target = "/command";
        request.protocol = ReceiverProtocol::http;
        request.headers = headers_.command(session_uuid_, stream_id_);
        request.body = command_body(commands[index]);
        const auto response = control_request(std::move(request), cancelled, true);
        trace_start_phase(phases[index], response.status);
    }
    trace_start_phase(SessionStartPhase::waiting);
    wait_until_playing(cancelled);
}

void UrlPlaybackSession::wake_receiver_if_asleep(const std::atomic_bool* cancelled) {
    if (!mrp_ || !options_.wake_receiver) {
        return;
    }
    // Only an explicit "no logical devices" report means asleep. An awake
    // receiver, or one that sends no report, starts exactly as before D56.
    const auto devices = mrp_->logical_devices();
    if (!devices || *devices != 0) {
        return;
    }
    trace_start_phase(SessionStartPhase::waking);
    mrp_->wake(cancelled);
    // Best effort: a receiver that never reports awake still gets the request.
    // Cancellation surfaces at the next receiver operation.
    (void)mrp_->wait_until_awake(
        options_.wake_settle, std::chrono::steady_clock::now() + options_.wake_timeout, cancelled);
}

void UrlPlaybackSession::open_remote_control(const PairCredentials& credentials,
                                             const std::atomic_bool* cancelled) {
    remote_control_ = std::make_unique<ReceiverConnection>(
        options_.connect(options_.receiver, operation(cancelled)), options_.receiver.authority());
    remote_control_->verify(credentials, operation(cancelled));

    // This connection has its own UUID, CSeq, pair-verify secret and record counters.
    // Reusing URL keys here would authenticate against the wrong session.
    ReceiverRequest request;
    request.method = "SETUP";
    request.target = rtsp_uri_;
    request.protocol = ReceiverProtocol::rtsp;
    request.headers = remote_headers_.rtsp(true);
    request.body = remote_control_setup_body(options_.identity, random_uuid());
    const ErasedRequest owned(std::move(request));
    const auto response =
        remote_control_->request(owned.request, receiver_http::max_body, operation(cancelled));
    if (response.status < 200 || response.status > 299) {
        if (!options_.url_controls_fallback) {
            throw SessionException(SessionError::rejected, response.status);
        }
        // macOS AirPlay Receiver answers 500 here after pair-verify succeeds,
        // yet plays the URL session and obeys its own /command controls (D62).
        trace_start_phase(SessionStartPhase::connecting, response.status);
        remote_control_->close();
        remote_control_.reset();
        url_controls_ = true;
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_.url_controls = true;
        return;
    }

    Secret32 sender_write;
    Secret32 sender_read;
    remote_control_->derive_channel_keys(event_channel_labels(), sender_write, sender_read);
    auto endpoint = options_.receiver;
    endpoint.port = parse_event_port(response.body);
    remote_events_ = std::make_unique<EventChannel>(
        options_.connect(endpoint, operation(cancelled)), sender_write, sender_read);
    remote_event_thread_ = std::thread([this] { remote_event_loop(); });
    if (!options_.enable_mrp) {
        return;
    }
    (void)remote_request("RECORD", rtsp_uri_, {}, cancelled, true);
    const auto seed = random_stream_seed();
    const auto data_response =
        remote_request("SETUP", rtsp_uri_,
                       data_stream_setup_body(random_uuid(), random_uuid(), seed), cancelled, true);
    const auto setup = decode_binary_plist(data_response.body);
    const auto* streams = setup.find("streams");
    if (!streams || streams->kind() != PlistKind::array || streams->as_array().size() != 1) {
        throw TransportException(TransportError::invalid_message);
    }
    const auto* port = streams->as_array().front().find("dataPort");
    if (!port || port->kind() != PlistKind::integer || port->as_integer() < 1 ||
        port->as_integer() > 65535) {
        throw TransportException(TransportError::invalid_message);
    }
    remote_control_->derive_channel_keys(data_stream_labels(seed), sender_write, sender_read);
    endpoint.port = static_cast<std::uint16_t>(port->as_integer());
    mrp_ = std::make_unique<MrpSession>(
        std::make_unique<MrpChannel>(options_.connect(endpoint, operation(cancelled)), sender_write,
                                     sender_read),
        options_.request_timeout);
    (void)remote_request("POST", "/feedback", {}, cancelled, false);
    mrp_->handshake(options_.identity, credentials.client_identifier(), cancelled);
}

ReceiverResponse UrlPlaybackSession::remote_request(std::string method, std::string target,
                                                    Bytes body, const std::atomic_bool* cancelled,
                                                    bool require_success) {
    ReceiverRequest request;
    request.method = std::move(method);
    request.target = std::move(target);
    request.protocol = ReceiverProtocol::rtsp;
    request.headers = remote_headers_.rtsp(!body.empty());
    request.body = std::move(body);
    const ErasedRequest owned(std::move(request));
    // Shares serialization with URL feedback; both are complete synchronous
    // exchanges. Remote start finishes before this thread is started.
    std::lock_guard<std::mutex> lock(control_mutex_);
    const auto response =
        remote_control_->request(owned.request, receiver_http::max_body, operation(cancelled));
    if (require_success && (response.status < 200 || response.status > 299)) {
        throw SessionException(SessionError::rejected, response.status);
    }
    return response;
}

void UrlPlaybackSession::remote_event_loop() {
    const auto operation = ReceiverOperation::until_cancelled(&remote_event_stop_);
    try {
        for (;;) {
            ErasedEvent owned(remote_events_->receive(operation)); // Answers before returning.
            // Remote notifications are not the URL session's playback state.
            std::string outline;
            if (options_.record_event_structure) {
                try {
                    outline = describe_remote_event(owned.request.body);
                } catch (const TransportException&) {
                    // A malformed observation has already been acknowledged;
                    // it does not make the authenticated channel unhealthy.
                    outline = "remote unreadable=yes";
                }
            }
            std::lock_guard<std::mutex> lock(state_mutex_);
            ++status_.remote_events;
            if (!outline.empty()) {
                if (event_log_.size() == max_event_log) {
                    event_log_.pop_front();
                }
                event_log_.push_back(std::move(outline));
            }
        }
    } catch (...) {
        if (!remote_event_stop_) {
            mark_failed(SessionFailureChannel::remote_events, caught_failure_reason());
        }
    }
}

ReceiverRequest UrlPlaybackSession::rtsp_request(std::string method, std::string target,
                                                 Bytes body) const {
    ReceiverRequest request;
    request.method = std::move(method);
    request.target = std::move(target);
    request.protocol = ReceiverProtocol::rtsp;
    request.headers = headers_.rtsp(!body.empty());
    request.body = std::move(body);
    return request;
}

ReceiverResponse
UrlPlaybackSession::control_request(ReceiverRequest request, const std::atomic_bool* cancelled,
                                    bool require_success,
                                    std::optional<std::chrono::milliseconds> timeout) {
    const ErasedRequest owned(std::move(request));
    std::lock_guard<std::mutex> lock(control_mutex_);
    if (!control_) {
        throw TransportException(TransportError::closed);
    }
    const auto deadline =
        timeout ? ReceiverOperation::after(*timeout, cancelled) : operation(cancelled);
    auto response = control_->request(owned.request, receiver_http::max_body, deadline);
    if (require_success && (response.status < 200 || response.status > 299)) {
        throw SessionException(SessionError::rejected, response.status);
    }
    return response;
}

void UrlPlaybackSession::start_timing(const std::string& local_address) {
    timing_ = std::make_unique<TimingResponder>(local_address, options_.receiver.scope_id,
                                                options_.receiver.address);
    timing_thread_ = std::thread([this] {
        try {
            timing_->serve(ReceiverOperation::until_cancelled(&timing_stop_));
        } catch (...) {
            if (!timing_stop_) {
                mark_failed(SessionFailureChannel::timing, caught_failure_reason());
            }
        }
    });
}

void UrlPlaybackSession::open_event_channel(std::uint16_t event_port,
                                            const std::atomic_bool* cancelled) {
    Secret32 sender_write;
    Secret32 sender_read;
    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        control_->derive_channel_keys(event_channel_labels(), sender_write, sender_read);
    }
    auto endpoint = options_.receiver;
    endpoint.port = event_port;
    events_ = std::make_unique<EventChannel>(options_.connect(endpoint, operation(cancelled)),
                                             sender_write, sender_read);
    event_thread_ = std::thread([this] { event_loop(); });
}

void UrlPlaybackSession::event_loop() {
    const auto operation = ReceiverOperation::until_cancelled(&event_stop_);
    try {
        for (;;) {
            ErasedEvent owned(events_->receive(operation));
            const auto& request = owned.request;
            std::optional<SessionEvent> event;
            std::string outline;
            std::string buffering;
            try {
                event = parse_session_event(request.body);
                if (options_.record_event_structure) {
                    outline = "URL " + describe_event_structure(request.body);
                    buffering = describe_buffering_values(request.body);
                }
            } catch (const TransportException&) {
                // Answered already; an unreadable body does not end the session.
                if (options_.record_event_structure) {
                    outline = "URL unreadable=yes";
                }
            }
            if (event && event->playback_state == playing_state && event->duration_seconds &&
                mrp_) {
                mrp_->confirm_url_playing(*event->duration_seconds);
            }
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (options_.record_event_structure) {
                    if (event_log_.size() == max_event_log) {
                        event_log_.pop_front();
                    }
                    event_log_.push_back(std::move(outline));
                    if (!buffering.empty()) {
                        if (event_log_.size() == max_event_log) {
                            event_log_.pop_front();
                        }
                        event_log_.push_back(std::move(buffering));
                    }
                }
                if (!event) {
                    ++status_.unreadable_events;
                } else {
                    ++status_.events;
                    if (url_controls_) {
                        url_playback_.observe(*event, std::chrono::steady_clock::now());
                    }
                    if (event->playback_state) {
                        status_.playback_state = *event->playback_state;
                        state_playback_rate_ = event->playback_rate;
                        if (status_.playback_state != playing_state ||
                            (state_playback_rate_ && *state_playback_rate_ <= 0)) {
                            forward_playing_since_.reset();
                        } else if (!forward_playing_since_) {
                            forward_playing_since_ = std::chrono::steady_clock::now();
                        }
                        append_start_trace_locked(start_state(*event->playback_state), 0,
                                                  event->playback_rate);
                    }
                }
            }
            state_changed_.notify_all();
        }
    } catch (...) {
        if (!event_stop_) {
            mark_failed(SessionFailureChannel::url_events, caught_failure_reason());
        }
    }
}

void UrlPlaybackSession::start_feedback() {
    feedback_thread_ = std::thread([this] { feedback_loop(); });
}

void UrlPlaybackSession::feedback_loop() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            if (state_changed_.wait_for(lock, options_.feedback_interval,
                                        [this] { return feedback_stop_.load(); })) {
                return;
            }
        }
        auto channel = SessionFailureChannel::url_feedback;
        try {
            // Best effort, as in the reference: a non-2xx answer is ignored, but
            // a transport failure has closed the control connection.
            (void)control_request(rtsp_request("POST", "/feedback", {}), &feedback_stop_, false);
            if (mrp_ && !feedback_stop_) {
                channel = SessionFailureChannel::remote_feedback;
                // Not cancellable: an interrupted request closes its connection,
                // and cleanup stops feedback first, so cancelling here would
                // close remote control before the URL session. Its request
                // deadline still bounds how long cleanup waits for it.
                (void)remote_request("POST", "/feedback", {}, nullptr, false);
            }
            std::lock_guard<std::mutex> lock(state_mutex_);
            ++status_.feedback_sent;
        } catch (...) {
            if (!feedback_stop_) {
                mark_failed(channel, caught_failure_reason());
            }
            return;
        }
    }
}

void UrlPlaybackSession::mark_failed(SessionFailureChannel channel, SessionFailureReason reason) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (status_.end_reason == SessionEnd::none && !status_.failed) {
            status_.failed = true;
            status_.failure_channel = channel;
            status_.failure_reason = reason;
        }
    }
    state_changed_.notify_all();
}

void UrlPlaybackSession::wait_until_playing(const std::atomic_bool* cancelled) {
    const auto deadline = std::chrono::steady_clock::now() + options_.start_timeout;
    std::unique_lock<std::mutex> lock(state_mutex_);
    for (;;) {
        if (status_.failed || (mrp_ && mrp_->failed())) {
            throw SessionException(SessionError::connection_lost);
        }
        if (cancelled != nullptr && cancelled->load()) {
            throw TransportException(TransportError::cancelled);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            throw SessionException(SessionError::start_timeout);
        }
        // Both sessions must remain healthy throughout the confirmation interval.
        // tvOS has reported positive-rate playing then paused about 0.4 s later;
        // zero-rate playing also occurs. Event processing resets this interval
        // on every observed interruption. Missing rate preserves compatibility.
        if (forward_playing_since_ &&
            now - *forward_playing_since_ >= options_.start_confirmation_interval) {
            return;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        state_changed_.wait_for(lock, std::min(remaining, start_poll_slice));
    }
}

const char* session_start_phase_name(SessionStartPhase phase) noexcept {
    switch (phase) {
    case SessionStartPhase::connecting:
        return "connecting";
    case SessionStartPhase::waking:
        return "waking";
    case SessionStartPhase::insert_item:
        return "insert_item";
    case SessionStartPhase::date_range:
        return "date_range";
    case SessionStartPhase::item_end:
        return "item_end";
    case SessionStartPhase::rate:
        return "rate";
    case SessionStartPhase::waiting:
        return "waiting";
    case SessionStartPhase::ready:
        return "ready";
    case SessionStartPhase::failed:
        return "failed";
    }
    return "failed";
}

const char* session_start_state_name(SessionStartState state) noexcept {
    switch (state) {
    case SessionStartState::none:
        return "none";
    case SessionStartState::loading:
        return "loading";
    case SessionStartState::playing:
        return "playing";
    case SessionStartState::paused:
        return "paused";
    case SessionStartState::idle:
        return "idle";
    case SessionStartState::stopped:
        return "stopped";
    case SessionStartState::ended:
        return "ended";
    case SessionStartState::other:
        return "other";
    }
    return "other";
}

void UrlPlaybackSession::trace_start_phase(SessionStartPhase phase, unsigned response_status) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    start_trace_phase_ = phase;
    append_start_trace_locked(SessionStartState::none, response_status);
}

void UrlPlaybackSession::append_start_trace_locked(SessionStartState state,
                                                   unsigned response_status,
                                                   std::optional<double> playback_rate) {
    if (!capture_start_trace_) {
        return;
    }
    if (start_trace_.count == start_trace_.entries.size()) {
        start_trace_.truncated = true;
        return;
    }
    auto& entry = start_trace_.entries[start_trace_.count++];
    entry.elapsed_ms =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start_trace_origin_)
                                       .count());
    entry.phase = start_trace_phase_;
    entry.state = state;
    entry.response_status = response_status;
    entry.playback_rate = playback_rate;
}

static_assert(max_start_power_entries == max_power_observations,
              "the start trace holds every MRP power report");

void UrlPlaybackSession::finish_start_trace(SessionStartDiagnostics& diagnostics) {
    // Read MRP outside state_mutex_: the MRP worker never takes that lock, but
    // keeping the two locks unnested avoids any ordering question.
    const auto power = mrp_ ? mrp_->power_observations() : std::vector<MrpPowerObservation>{};
    std::lock_guard<std::mutex> lock(state_mutex_);
    capture_start_trace_ = false;
    start_trace_.cleaned_up = status_.cleaned_up;
    start_trace_.power_count = 0;
    for (const auto& observation : power) {
        if (start_trace_.power_count == start_trace_.power.size()) {
            break;
        }
        auto& entry = start_trace_.power[start_trace_.power_count++];
        entry.elapsed_ms =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           observation.received - start_trace_origin_)
                                           .count());
        entry.logical_devices = observation.logical_devices;
    }
    diagnostics = start_trace_;
}

SessionStatus UrlPlaybackSession::status() const {
    // Read MRP first: intentional command cancellation follows request_end().
    // The later state snapshot must retain that normal end rather than combine
    // an earlier "none" snapshot with a later cancellation failure.
    const auto remote_failure = mrp_ ? mrp_->failure() : std::optional<MrpError>{};
    SessionStatus snapshot;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        snapshot = status_;
    }
    if (timing_) {
        snapshot.timing_answered = timing_->answered();
    }
    if (remote_failure && snapshot.failure_channel == SessionFailureChannel::none &&
        (snapshot.end_reason == SessionEnd::none ||
         snapshot.end_reason == SessionEnd::connection_lost)) {
        snapshot.failed = true;
        snapshot.failure_channel = SessionFailureChannel::mrp;
        snapshot.failure_reason = mrp_failure_reason(*remote_failure);
    }
    return snapshot;
}

MrpPlaybackStatus UrlPlaybackSession::playback_status() const {
    if (mrp_) {
        return mrp_->status();
    }
    if (url_controls_) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return url_playback_.status(std::chrono::steady_clock::now());
    }
    return {};
}

void UrlPlaybackSession::apply_start_position(const std::atomic_bool* cancelled) {
    // tvOS 26 accepts the queue item's Start-Position-Seconds but plays from 0,
    // for HLS and progressive media alike, while an MRP seek once the item
    // plays lands (D61). A seek is also harmless where the item is honored.
    if (url_controls_) {
        // Our URL session's own seek needs no ownership: start has confirmed
        // that this session's item plays (D62).
        command(PlaybackCommand::seek, options_.start_position_seconds);
        return;
    }
    const auto deadline = std::chrono::steady_clock::now() + start_ownership_wait;
    while (!playback_status().owned) {
        if ((cancelled && *cancelled) || stopping_) {
            throw MrpException(MrpError::cancelled);
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            throw MrpException(MrpError::not_owned);
        }
        std::this_thread::sleep_for(start_poll_slice);
    }
    command(PlaybackCommand::seek, options_.start_position_seconds);
}

void UrlPlaybackSession::command(PlaybackCommand command, double position_seconds) {
    std::lock_guard<std::mutex> serial(command_mutex_);
    if (stopping_) {
        throw MrpException(MrpError::cancelled);
    }
    if (mrp_) {
        mrp_->command(command, position_seconds, &stopping_);
        return;
    }
    if (!url_controls_) {
        throw MrpException(MrpError::not_owned);
    }
    url_command(command, position_seconds);
}

void UrlPlaybackSession::url_command(PlaybackCommand command, double position_seconds) {
    PlistValue body;
    switch (command) {
    case PlaybackCommand::play:
        body = set_rate(1.0);
        break;
    case PlaybackCommand::pause:
        body = set_rate(0.0);
        break;
    case PlaybackCommand::seek:
        // Throws std::invalid_argument when out of range, before any request.
        body = seek_to(position_seconds, ++url_message_id_, item_uuid_);
        break;
    case PlaybackCommand::stop:
        body = stop_playback();
        break;
    }
    ReceiverRequest request;
    request.method = "POST";
    request.target = "/command";
    request.protocol = ReceiverProtocol::http;
    request.headers = headers_.command(session_uuid_, stream_id_);
    request.body = command_body(body);
    ReceiverResponse response;
    try {
        // Teardown sets stopping_, which cancels a command still in flight.
        response = control_request(std::move(request), &stopping_, false);
    } catch (const TransportException& error) {
        throw MrpException(url_command_error(error.reason()));
    } catch (const ControlException& error) {
        throw MrpException(error.reason() == ControlError::authentication ? MrpError::authentication
                                                                          : MrpError::disconnected);
    }
    if (response.status < 200 || response.status > 299) {
        throw MrpException(MrpError::rejected);
    }
}

SessionStatus UrlPlaybackSession::wait_for_change(const std::string& previous,
                                                  std::chrono::milliseconds timeout) const {
    {
        std::unique_lock<std::mutex> lock(state_mutex_);
        state_changed_.wait_for(lock, timeout, [&] {
            return status_.playback_state != previous || status_.failed ||
                   status_.end_reason != SessionEnd::none;
        });
    }
    return status();
}

std::vector<std::string> UrlPlaybackSession::take_event_log() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::vector<std::string> entries(event_log_.begin(), event_log_.end());
    event_log_.clear();
    return entries;
}

void UrlPlaybackSession::request_end(SessionEnd reason) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (status_.end_reason == SessionEnd::none) {
            status_.end_reason = reason;
            status_.failed = status_.failed || reason == SessionEnd::connection_lost;
        }
        stopping_ = true;
    }
    state_changed_.notify_all();
}

void UrlPlaybackSession::supervise() {
    try {
        bool had_owned_player = false;
        std::optional<std::chrono::steady_clock::time_point> receiver_stop_deadline;
        for (;;) {
            const auto current = status();
            if (current.end_reason != SessionEnd::none) {
                break;
            }
            bool near_end = false;
            if (current.failed) {
                mark_failed(current.failure_channel, current.failure_reason);
                request_end(SessionEnd::connection_lost);
                break;
            }
            if (current.playback_state == "ended") {
                request_end(SessionEnd::media_end);
                break;
            }
            if (mrp_) {
                const auto playback = mrp_->status();
                if (playback.at_end) {
                    request_end(SessionEnd::media_end);
                    break;
                }
                near_end = playback.near_end;
                if (had_owned_player && !playback.owned && current.playback_state != "idle" &&
                    current.playback_state != "stopped") {
                    request_end(SessionEnd::ownership_lost);
                    break;
                }
                had_owned_player = had_owned_player || playback.owned;
            } else if (url_controls_) {
                // URL events are the only progress source; no ownership signal
                // exists without MRP (D62). macOS reports the final position
                // just before "stopped" at the natural end.
                const auto playback = playback_status();
                if (playback.at_end) {
                    request_end(SessionEnd::media_end);
                    break;
                }
                near_end = playback.near_end;
            }
            if (current.playback_state == "idle" || current.playback_state == "stopped") {
                // tvOS reports URL "stopped" before its final MRP position. Give
                // that position (MRP, or a URL notification with URL controls)
                // one second to distinguish EOF from a mid-item receiver stop.
                // A pause alone never starts this timer.
                // Once stopped, a position just short of the duration is EOF:
                // HLS can stop a frame or two before the playlist's end (D60).
                if (near_end) {
                    request_end(SessionEnd::media_end);
                    break;
                }
                const auto now = std::chrono::steady_clock::now();
                if (!receiver_stop_deadline) {
                    receiver_stop_deadline = now + std::chrono::seconds(1);
                }
                if ((!mrp_ && !url_controls_) || now >= *receiver_stop_deadline) {
                    request_end(SessionEnd::receiver_stop);
                    break;
                }
            } else {
                receiver_stop_deadline.reset();
            }
            std::unique_lock<std::mutex> lock(state_mutex_);
            state_changed_.wait_for(lock, start_poll_slice, [&] {
                return status_.end_reason != SessionEnd::none || status_.failed;
            });
        }
    } catch (...) {
        mark_failed(SessionFailureChannel::supervisor, caught_failure_reason());
        request_end(SessionEnd::connection_lost);
    }
    cleanup();
}

void UrlPlaybackSession::stop() noexcept {
    std::lock_guard<std::mutex> join_lock(stop_mutex_);
    bool failed = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        failed = status_.failed;
    }
    // Teardown must not allocate a status string, including on start failure.
    failed = failed || (mrp_ && mrp_->failed());
    request_end(failed ? SessionEnd::connection_lost : SessionEnd::sender_stop);
    if (supervisor_thread_.joinable()) {
        supervisor_thread_.join();
    } else {
        // Start failures have no supervisor. No caller can access this object yet.
        cleanup();
    }
}

void UrlPlaybackSession::cleanup() noexcept {
    std::lock_guard<std::mutex> command_lock(command_mutex_);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (status_.cleaned_up) {
            return;
        }
        feedback_stop_ = true;
    }
    state_changed_.notify_all();
    // Fixed order: feedback, event channel, control connection, timing.
    if (feedback_thread_.joinable()) {
        feedback_thread_.join();
    }
    event_stop_ = true; // The reader notices within one poll slice.
    if (event_thread_.joinable()) {
        event_thread_.join();
    }
    if (events_) {
        events_->close(); // The reader thread has ended; this thread now owns it.
    }
    {
        std::lock_guard<std::mutex> lock(control_mutex_);
        if (control_) {
            control_->close();
        }
    }
    timing_stop_ = true;
    if (timing_thread_.joinable()) {
        timing_thread_.join();
    }
    if (timing_) {
        timing_->close();
    }
    // Retain remote control until the entire URL transport has stopped.
    if (mrp_) {
        mrp_->stop();
    }
    remote_event_stop_ = true;
    if (remote_event_thread_.joinable()) {
        remote_event_thread_.join();
    }
    if (remote_events_) {
        remote_events_->close();
    }
    if (remote_control_) {
        remote_control_->close();
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_.cleaned_up = true;
    }
    state_changed_.notify_all();
}
} // namespace send_airplay2::detail
