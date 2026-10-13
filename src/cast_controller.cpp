// SPDX-License-Identifier: Apache-2.0
#include "cast_controller.h"
#include "control_crypto.h"
#include "credential_store.h"
#include "hls_remux.h"
#include "mrp_session.h"
#include "pair_setup_crypto.h"
#include "receiver_http.h"

#include <cmath>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>

namespace send_airplay2::detail {
namespace {
// The receiver keeps open-ended range requests while it buffers; use the media
// server's maximum per-request budget, as the `cast` CLI does.
constexpr std::uint32_t media_request_timeout_ms = 600000;
constexpr auto media_preparation_timeout = std::chrono::seconds(120);
constexpr auto media_monitor_interval = std::chrono::milliseconds(20);

/// A local failure that already has its public category.
class CastFailure : public std::runtime_error {
public:
    explicit CastFailure(CastResult result) : std::runtime_error("cast failure"), result_(result) {}
    [[nodiscard]] CastResult result() const noexcept {
        return result_;
    }

private:
    CastResult result_;
};

CredentialLoader native_loader() {
    return [](std::string_view profile) { return native_credential_store()->load(profile); };
}

CastDependencies with_defaults(CastDependencies dependencies) {
    if (!dependencies.load_credentials) {
        dependencies.load_credentials = native_loader();
    }
    if (!dependencies.prepare_stereo) {
        dependencies.prepare_stereo = prepare_stereo_media;
    }
    return dependencies;
}
CastSettings with_cache_cleanup(CastSettings settings) {
    if (settings.fragment_store.clear) {
        auto cleared = std::make_shared<std::atomic_bool>(false);
        auto clear = std::move(settings.fragment_store.clear);
        settings.fragment_store.clear = [cleared, clear = std::move(clear)] {
            if (!cleared->exchange(true)) {
                clear();
            }
        };
    }
    return settings;
}
CastResult media_result(MediaPreparationFailure reason) noexcept {
    switch (reason) {
    case MediaPreparationFailure::backend_unavailable:
        return CastResult::unsupported;
    case MediaPreparationFailure::unsupported:
        return CastResult::media_unsupported;
    case MediaPreparationFailure::malformed:
        return CastResult::media_malformed;
    case MediaPreparationFailure::selection_required:
        return CastResult::audio_selection_required;
    case MediaPreparationFailure::invalid_selection:
        return CastResult::audio_track_invalid;
    case MediaPreparationFailure::source_error:
        return CastResult::media_source;
    case MediaPreparationFailure::codec_error:
        return CastResult::media_codec;
    case MediaPreparationFailure::resource_limit:
        return CastResult::media_limit;
    case MediaPreparationFailure::cache_error:
        return CastResult::media_cache;
    case MediaPreparationFailure::timeout:
        return CastResult::media_generation_timeout;
    case MediaPreparationFailure::cancelled:
        return CastResult::cancelled;
    }
    return CastResult::internal;
}
CastResult conversion_result(StereoAacHlsFailure reason) noexcept {
    switch (reason) {
    case StereoAacHlsFailure::none:
        return CastResult::ok;
    case StereoAacHlsFailure::unsupported:
        return CastResult::media_unsupported;
    case StereoAacHlsFailure::malformed:
        return CastResult::media_malformed;
    case StereoAacHlsFailure::source_error:
        return CastResult::media_source;
    case StereoAacHlsFailure::codec_error:
        return CastResult::media_codec;
    case StereoAacHlsFailure::resource_limit:
        return CastResult::media_limit;
    case StereoAacHlsFailure::cache_error:
        return CastResult::media_cache;
    case StereoAacHlsFailure::generation_timeout:
        return CastResult::media_generation_timeout;
    }
    return CastResult::internal;
}

CastResult transport_result(TransportError reason) noexcept {
    switch (reason) {
    case TransportError::invalid_message:
    case TransportError::correlation:
        return CastResult::protocol;
    case TransportError::cancelled:
        return CastResult::cancelled;
    case TransportError::invalid_argument:
        return CastResult::invalid_argument;
    case TransportError::timeout:
    case TransportError::disconnected:
    case TransportError::network:
    case TransportError::closed:
        return CastResult::connection;
    }
    return CastResult::internal;
}

CastResult credential_result(CredentialError reason) noexcept {
    switch (reason) {
    case CredentialError::invalid_profile:
        return CastResult::invalid_argument;
    case CredentialError::unsupported:
        return CastResult::unsupported;
    case CredentialError::already_exists:
        return CastResult::profile_exists;
    case CredentialError::invalid_record:
    case CredentialError::unavailable:
        return CastResult::credential_store;
    }
    return CastResult::internal;
}

CastResult verification_result(PairVerifyError reason) noexcept {
    switch (reason) {
    case PairVerifyError::authentication:
    case PairVerifyError::peer_rejected:
        return CastResult::authentication;
    case PairVerifyError::invalid_message:
    case PairVerifyError::unexpected_state:
        return CastResult::protocol;
    case PairVerifyError::backend:
        return CastResult::internal;
    }
    return CastResult::internal;
}

/// PIN enrollment: a wrong PIN surfaces as a failed proof or a peer rejection.
CastResult pairing_result(PairSetupError reason) noexcept {
    switch (reason) {
    case PairSetupError::authentication:
    case PairSetupError::peer_rejected:
        return CastResult::authentication;
    case PairSetupError::invalid_message:
    case PairSetupError::unexpected_state:
        return CastResult::protocol;
    case PairSetupError::backend:
        return CastResult::internal;
    }
    return CastResult::internal;
}

CastResult record_result(ControlError reason) noexcept {
    switch (reason) {
    case ControlError::authentication:
        return CastResult::authentication;
    case ControlError::invalid_length:
        return CastResult::protocol;
    case ControlError::counter_exhausted:
    case ControlError::closed:
        return CastResult::connection;
    case ControlError::backend:
        return CastResult::internal;
    }
    return CastResult::internal;
}

CastResult session_result(SessionError reason) noexcept {
    switch (reason) {
    case SessionError::rejected:
        return CastResult::receiver_rejected;
    case SessionError::start_timeout:
        return CastResult::start_timeout;
    case SessionError::connection_lost:
        return CastResult::connection;
    }
    return CastResult::internal;
}

/// MRP failures during startup (handshake/data stream), not user commands.
CastResult mrp_start_result(MrpError reason) noexcept {
    switch (reason) {
    case MrpError::malformed:
        return CastResult::protocol;
    case MrpError::authentication:
        return CastResult::authentication;
    case MrpError::rejected:
        return CastResult::receiver_rejected;
    case MrpError::timeout:
    case MrpError::disconnected:
        return CastResult::connection;
    case MrpError::cancelled:
        return CastResult::cancelled;
    case MrpError::not_owned:
        return CastResult::not_owned;
    }
    return CastResult::internal;
}

CastResult mrp_command_result(MrpError reason) noexcept {
    switch (reason) {
    case MrpError::not_owned:
        return CastResult::not_owned;
    case MrpError::cancelled: // The session is stopping or has stopped.
        return CastResult::ended;
    case MrpError::rejected:
    case MrpError::timeout:
    case MrpError::malformed:
        return CastResult::command_failed;
    case MrpError::disconnected:
        return CastResult::connection;
    case MrpError::authentication:
        return CastResult::authentication;
    }
    return CastResult::internal;
}

bool session_finished(const SessionStatus& status) {
    return status.end_reason != SessionEnd::none || status.failed;
}
} // namespace

CastResult cast_start_result(const std::exception_ptr& failure,
                             unsigned& rejected_status) noexcept {
    try {
        std::rethrow_exception(failure);
    } catch (const CastFailure& error) {
        return error.result();
    } catch (const SessionException& error) {
        rejected_status = error.status();
        return session_result(error.reason());
    } catch (const CredentialException& error) {
        return credential_result(error.reason());
    } catch (const PairVerifyException& error) {
        return verification_result(error.reason());
    } catch (const PairSetupException& error) {
        return pairing_result(error.reason());
    } catch (const ControlException& error) {
        return record_result(error.reason());
    } catch (const TransportException& error) {
        return transport_result(error.reason());
    } catch (const MrpException& error) {
        return mrp_start_result(error.reason());
    } catch (const RemuxException& error) {
        return error.reason() == RemuxFailure::malformed ? CastResult::media_malformed
                                                         : CastResult::media_unsupported;
    } catch (const MediaPreparationException& error) {
        return media_result(error.reason());
    } catch (const std::invalid_argument&) {
        return CastResult::invalid_argument;
    } catch (const std::bad_alloc&) {
        return CastResult::out_of_memory;
    } catch (...) {
        return CastResult::internal;
    }
}

CastResult cast_command_result(const std::exception_ptr& failure) noexcept {
    try {
        std::rethrow_exception(failure);
    } catch (const MrpException& error) {
        return mrp_command_result(error.reason());
    } catch (const TransportException& error) {
        return transport_result(error.reason());
    } catch (const ControlException& error) {
        return record_result(error.reason());
    } catch (const std::invalid_argument&) {
        return CastResult::invalid_argument;
    } catch (const std::bad_alloc&) {
        return CastResult::out_of_memory;
    } catch (...) {
        return CastResult::internal;
    }
}

CastController::CastController(CastSettings settings, MediaSource source,
                               CastDependencies dependencies)
    : settings_(with_cache_cleanup(std::move(settings))),
      dependencies_(with_defaults(std::move(dependencies))), source_(std::move(source)) {}

CastController::~CastController() {
    stop();
}

void CastController::run_start(std::unique_ptr<MediaServer>& server,
                               std::unique_ptr<UrlPlaybackSession>& session) {
    // No network work, and no listener, without a trusted profile.
    const auto credentials = dependencies_.load_credentials(settings_.profile);
    if (!credentials) {
        throw CastFailure(CastResult::profile_not_found);
    }

    // HLS reads the source's index here, before any listener exists; a
    // refusal is a RemuxException and a stop() cancels the reads.
    std::optional<RemuxedHls> remuxed;
    if (settings_.delivery == CastDelivery::hls_remux) {
        remuxed = remux_to_hls(source_, &cancel_requested_);
    } else if (settings_.delivery == CastDelivery::stereo_aac_hls) {
        const MediaReadContext context{&cancel_requested_, &cancel_requested_,
                                       std::chrono::steady_clock::now() +
                                           media_preparation_timeout};
        auto prepared = dependencies_.prepare_stereo(source_, settings_.fragment_store,
                                                     settings_.cache_quota_bytes, context,
                                                     settings_.audio_track_id);
        if (!prepared) {
            throw CastFailure(CastResult::internal);
        }
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            prepared_media_ = std::move(prepared);
        }
        // Begin supervision before any HTTP fetch can start conversion.
        media_monitor_ = std::thread([this] { monitor_media(); });
    }

    MediaServerOptions server_options;
    server_options.receiver_address = settings_.receiver_address;
    server_options.receiver_port = settings_.receiver_port;
    server_options.max_connections = settings_.media_connections;
    server_options.request_timeout_ms = media_request_timeout_ms;
    server_options.content_type = settings_.content_type;
    try {
        if (prepared_media_) {
            server = MediaServer::start_resource_set(prepared_media_->presentation().resources,
                                                     server_options);
        } else {
            server = remuxed ? MediaServer::start_resource_set(std::move(remuxed->resources),
                                                               server_options)
                             : MediaServer::start(source_, server_options);
        }
    } catch (const std::invalid_argument&) {
        throw; // Address or content type rejected by the server's own validation.
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception&) {
        throw CastFailure(CastResult::media_server);
    }

    UrlPlaybackOptions options;
    options.receiver = {settings_.receiver_address, settings_.receiver_port, 0};
    options.media_url = prepared_media_
                            ? server->resource_url(prepared_media_->presentation().playlist_name)
                        : remuxed ? server->resource_url(remuxed->playlist_name)
                                  : server->url();
    options.start_position_seconds = settings_.start_position_seconds;
    options.start_timeout = settings_.start_timeout;
    options.connect = dependencies_.connect;
    if (dependencies_.adjust_session) {
        dependencies_.adjust_session(options);
    }
    session = UrlPlaybackSession::start(*credentials, std::move(options), &cancel_requested_);
    if (dependencies_.after_session_start) {
        dependencies_.after_session_start();
    }
}

CastResult CastController::start() noexcept {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (phase_ != CastPhase::created) {
            return CastResult::invalid_state;
        }
        phase_ = CastPhase::starting;
    }
    std::unique_ptr<MediaServer> server;
    std::unique_ptr<UrlPlaybackSession> session;
    CastResult result = CastResult::ok;
    unsigned rejected_status = 0;
    try {
        run_start(server, session);
    } catch (...) {
        // Preserve the first observed cause; late conversion failure must not
        // replace an earlier user Stop with a different start result.
        std::lock_guard<std::mutex> lock(state_mutex_);
        result = media_failure_first_ ? conversion_result(media_failure_.load())
                 : cancel_requested_ ? CastResult::cancelled
                                     : cast_start_result(std::current_exception(), rejected_status);
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        // Linearize publication against Stop and fatal conversion, including a
        // failure the polling monitor has not observed yet. Keep starting until
        // failed-start cleanup completes, so Stop cannot return ahead of it.
        observe_media_failure_locked(session.get());
        if (cancel_requested_) {
            result = media_failure_first_ ? conversion_result(media_failure_.load())
                                          : CastResult::cancelled;
        }
        if (session && result == CastResult::ok) {
            phase_ = CastPhase::active;
            start_result_ = result;
            server_ = std::move(server);
            session_ = std::move(session);
            state_changed_.notify_all();
            return result;
        }
    }
    monitor_cancelled_ = true;
    state_changed_.notify_all();
    join_media_monitor();
    // Also drain a confirmed but interrupted local session. A failure return
    // guarantees every session/server/conversion worker and cache callback ended.
    drain_media(session.get(), server.get());
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        phase_ = CastPhase::start_failed;
        start_result_ = result;
        rejected_status_ = rejected_status;
        server_ = std::move(server);
        session_ = std::move(session);
    }
    state_changed_.notify_all();
    return result;
}

CastSnapshot CastController::snapshot() const {
    CastSnapshot snapshot;
    const UrlPlaybackSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        snapshot.phase = phase_;
        snapshot.start_result = start_result_;
        snapshot.rejected_status = rejected_status_;
        snapshot.media_failure = media_failure_.load();
        snapshot.media_failure_first = media_failure_first_;
        snapshot.media_cleaned_up = media_cleaned_up_.load();
        if (prepared_media_) {
            snapshot.selected_audio_id = prepared_media_->selected_audio_id();
            snapshot.authoring_flags = prepared_media_->authoring_flags();
        }
        session = session_.get();
    }
    if (session) {
        snapshot.session = session->status();
        snapshot.playback = session->playback_status();
        if (snapshot.phase == CastPhase::active && session_finished(snapshot.session)) {
            snapshot.phase = CastPhase::ended;
        }
    }
    return snapshot;
}

CastSnapshot CastController::wait_for_change(const std::string& previous_state,
                                             std::chrono::milliseconds timeout) const {
    const UrlPlaybackSession* session = nullptr;
    {
        std::unique_lock<std::mutex> lock(state_mutex_);
        if (phase_ == CastPhase::starting) {
            state_changed_.wait_for(lock, timeout,
                                    [this] { return phase_ != CastPhase::starting; });
            lock.unlock(); // snapshot() takes the same lock.
            return snapshot();
        }
        if (phase_ == CastPhase::active) {
            session = session_.get();
        }
    }
    if (session) {
        (void)session->wait_for_change(previous_state, timeout);
    }
    return snapshot();
}

CastResult CastController::command(PlaybackCommand command, double position_seconds) noexcept {
    if (command == PlaybackCommand::seek &&
        (!std::isfinite(position_seconds) || position_seconds < 0)) {
        return CastResult::invalid_argument;
    }
    UrlPlaybackSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (phase_ == CastPhase::stopped) {
            return CastResult::ended;
        }
        if (phase_ != CastPhase::active) {
            return CastResult::invalid_state;
        }
        session = session_.get();
    }
    try {
        if (cancel_requested_ || session_finished(session->status())) {
            return CastResult::ended;
        }
        session->command(command, position_seconds);
        return CastResult::ok;
    } catch (...) {
        return cast_command_result(std::current_exception());
    }
}

void CastController::stop() noexcept {
    sender_stop_requested_ = true;
    cancel_requested_ = true;
    std::lock_guard<std::mutex> stopped(stop_mutex_);
    UrlPlaybackSession* session = nullptr;
    MediaServer* server = nullptr;
    {
        std::unique_lock<std::mutex> lock(state_mutex_);
        state_changed_.wait(lock, [this] { return phase_ != CastPhase::starting; });
        if (phase_ == CastPhase::start_failed || phase_ == CastPhase::stopped) {
            return; // Nothing is running: a failed start already tore down.
        }
        if (phase_ == CastPhase::active) {
            session = session_.get();
            server = server_.get();
        } else if (phase_ == CastPhase::created) {
            // Close the create/start race before invoking host cache cleanup.
            phase_ = CastPhase::stopped;
        }
    }
    monitor_cancelled_ = true;
    state_changed_.notify_all();
    join_media_monitor();
    drain_media(session, server);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        phase_ = CastPhase::stopped;
    }
    state_changed_.notify_all();
}
void CastController::join_media_monitor() noexcept {
    std::lock_guard<std::mutex> join(monitor_join_mutex_);
    if (media_monitor_.joinable()) {
        media_monitor_.join();
    }
}
void CastController::drain_media(UrlPlaybackSession* session, MediaServer* server) noexcept {
    std::lock_guard<std::mutex> teardown(teardown_mutex_);
    if (session) {
        session->stop();
    }
    if (server) {
        server->stop();
    }
    if (prepared_media_) {
        prepared_media_->stop();
    }
    if (settings_.fragment_store.clear) {
        try {
            settings_.fragment_store.clear();
        } catch (...) {
        }
    }
    media_cleaned_up_ = true;
}
void CastController::monitor_media() noexcept {
    for (;;) {
        UrlPlaybackSession* session = nullptr;
        MediaServer* server = nullptr;
        CastPhase phase;
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            state_changed_.wait_for(lock, media_monitor_interval,
                                    [this] { return monitor_cancelled_.load(); });
            if (monitor_cancelled_) {
                return;
            }
            phase = phase_;
            session = session_.get();
            server = server_.get();
            observe_media_failure_locked(session);
        }
        if (phase == CastPhase::active && session &&
            (media_failure_.load() != StereoAacHlsFailure::none || session->terminal())) {
            drain_media(session, server);
            state_changed_.notify_all();
            return;
        }
    }
}
void CastController::observe_media_failure_locked(const UrlPlaybackSession* session) noexcept {
    if (!prepared_media_ || media_failure_.load() != StereoAacHlsFailure::none) {
        return;
    }
    const auto failure = prepared_media_->failure();
    if (failure == StereoAacHlsFailure::none) {
        return;
    }
    media_failure_first_ = !sender_stop_requested_ && !(session && session->terminal());
    media_failure_ = failure;
    cancel_requested_ = true;
    state_changed_.notify_all();
}
} // namespace send_airplay2::detail
