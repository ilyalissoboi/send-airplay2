// SPDX-License-Identifier: Apache-2.0
#include "url_playback_session.h"
#include "channel_keys.h"
#include "control_crypto.h"
#include "event_channel.h"
#include "native_socket.h"
#include "ntp_timing.h"
#include "receiver_connection.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>

namespace send_airplay2::detail {
namespace {
/// Longest single wait while polling for cancellation during start.
constexpr std::chrono::milliseconds start_poll_slice{20};
constexpr const char* playing_state = "playing";

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
} // namespace

SessionException::SessionException(SessionError reason, unsigned status)
    : std::runtime_error(session_message(reason, status)), reason_(reason), status_(status) {}

UrlPlaybackSession::UrlPlaybackSession(UrlPlaybackOptions options)
    : options_(std::move(options)), session_uuid_(random_uuid()),
      headers_(SessionHeaders::random()) {}

std::unique_ptr<UrlPlaybackSession> UrlPlaybackSession::start(const PairCredentials& credentials,
                                                              UrlPlaybackOptions options,
                                                              const std::atomic_bool* cancelled) {
    if (!options.connect) {
        options.connect = connect_receiver;
    }
    if (options.identity.device_id.empty()) {
        options.identity.device_id = random_device_id();
    }
    std::unique_ptr<UrlPlaybackSession> session(new UrlPlaybackSession(std::move(options)));
    try {
        session->run_start(credentials, cancelled);
    } catch (...) {
        session->stop();
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
    control_ = std::make_unique<ReceiverConnection>(
        options_.connect(options_.receiver, operation(cancelled)), options_.receiver.authority());
    control_->verify(credentials, operation(cancelled));

    // The base SETUP announces the timing port; the receiver queries it at once.
    start_timing(local_address);
    const auto base = control_request(rtsp_request("SETUP", rtsp_uri_,
                                                   base_setup_body(options_.identity, session_uuid_,
                                                                   random_uuid(), timing_->port())),
                                      cancelled, true);
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

    const auto item_uuid = random_uuid();
    const PlistValue commands[] = {
        insert_play_queue_item(item_uuid, options_.media_url, options_.start_position_seconds),
        set_interested_in_date_range(item_uuid),
        set_action_at_item_end(),
        set_rate(1.0),
    };
    for (const auto& command : commands) {
        ReceiverRequest request;
        request.method = "POST";
        request.target = "/command";
        request.protocol = ReceiverProtocol::http;
        request.headers = headers_.command(session_uuid_, stream.stream_id);
        request.body = command_body(command);
        (void)control_request(std::move(request), cancelled, true);
    }
    wait_until_playing(cancelled);
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

ReceiverResponse UrlPlaybackSession::control_request(ReceiverRequest request,
                                                     const std::atomic_bool* cancelled,
                                                     bool require_success) {
    const ErasedRequest owned(std::move(request));
    std::lock_guard<std::mutex> lock(control_mutex_);
    if (!control_) {
        throw TransportException(TransportError::closed);
    }
    auto response = control_->request(owned.request, receiver_http::max_body, operation(cancelled));
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
                mark_failed();
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
            auto request = events_->receive(operation);
            std::optional<SessionEvent> event;
            try {
                event = parse_session_event(request.body);
            } catch (const TransportException&) {
                // Answered already; an unreadable body does not end the session.
            }
            cleanse(request.body.data(), request.body.size());
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                if (!event) {
                    ++status_.unreadable_events;
                } else {
                    ++status_.events;
                    if (event->playback_state) {
                        status_.playback_state = *event->playback_state;
                    }
                }
            }
            state_changed_.notify_all();
        }
    } catch (...) {
        if (!event_stop_) {
            mark_failed();
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
        try {
            // Best effort, as in the reference: a non-2xx answer is ignored, but
            // a transport failure has closed the control connection.
            (void)control_request(rtsp_request("POST", "/feedback", {}), &feedback_stop_, false);
            std::lock_guard<std::mutex> lock(state_mutex_);
            ++status_.feedback_sent;
        } catch (...) {
            if (!feedback_stop_) {
                mark_failed();
            }
            return;
        }
    }
}

void UrlPlaybackSession::mark_failed() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        status_.failed = true;
    }
    state_changed_.notify_all();
}

void UrlPlaybackSession::wait_until_playing(const std::atomic_bool* cancelled) {
    const auto deadline = std::chrono::steady_clock::now() + options_.start_timeout;
    std::unique_lock<std::mutex> lock(state_mutex_);
    for (;;) {
        // "loading" is not success: only "playing" completes the start.
        if (status_.playback_state == playing_state) {
            return;
        }
        if (status_.failed) {
            throw SessionException(SessionError::connection_lost);
        }
        if (cancelled != nullptr && cancelled->load()) {
            throw TransportException(TransportError::cancelled);
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            throw SessionException(SessionError::start_timeout);
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        state_changed_.wait_for(lock, std::min(remaining, start_poll_slice));
    }
}

SessionStatus UrlPlaybackSession::status() const {
    SessionStatus snapshot;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        snapshot = status_;
    }
    if (timing_) {
        snapshot.timing_answered = timing_->answered();
    }
    return snapshot;
}

SessionStatus UrlPlaybackSession::wait_for_change(const std::string& previous,
                                                  std::chrono::milliseconds timeout) const {
    {
        std::unique_lock<std::mutex> lock(state_mutex_);
        state_changed_.wait_for(lock, timeout, [&] {
            return status_.playback_state != previous || status_.failed || stopped_;
        });
    }
    return status();
}

void UrlPlaybackSession::stop() noexcept {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
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
    state_changed_.notify_all();
}
} // namespace send_airplay2::detail
