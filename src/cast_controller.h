// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_CAST_CONTROLLER_H
#define SEND_AIRPLAY2_CAST_CONTROLLER_H
#include "send_airplay2/media_server.h"
#include "mrp_messages.h"
#include "pair_verify.h"
#include "url_playback_session.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace send_airplay2::detail {
/// Result categories of the public playback interface. Values equal the
/// SAP2_* result macros in send_airplay2/playback.h; the C layer checks this.
enum class CastResult : std::int32_t {
    ok = 0,
    invalid_argument = 1,
    invalid_state = 2,
    cancelled = 3,
    profile_not_found = 4,
    credential_store = 5,
    unsupported = 6,
    authentication = 7,
    receiver_rejected = 8,
    start_timeout = 9,
    connection = 10,
    protocol = 11,
    media_server = 12,
    not_owned = 13,
    command_failed = 14,
    ended = 15,
    out_of_memory = 16,
    internal = 17,
    profile_exists = 18,
    pin_timeout = 19,
    media_unsupported = 20,
    media_malformed = 21
};

/// How the media reaches the receiver (SAP2_DELIVERY_*).
enum class CastDelivery {
    progressive, ///< One representation of CastSettings::content_type.
    hls_remux    ///< HLS built from the source by remux_to_hls (D60).
};

/// Handle lifecycle. `ended` is derived from the session's own end reason while
/// the controller is still active; it is never stored.
enum class CastPhase { created, starting, active, ended, start_failed, stopped };

/// Validated, owned copies of the public options.
struct CastSettings {
    std::string receiver_address; // Numeric; validated again by the media server.
    std::uint16_t receiver_port = 7000;
    std::string profile;
    std::string content_type = "video/mp4";
    std::chrono::milliseconds start_timeout{30000};
    std::uint32_t media_connections = 16;
    double start_position_seconds = 0;
    CastDelivery delivery = CastDelivery::progressive;
};

/// Returns null only when no credentials are stored under the profile.
using CredentialLoader = std::function<std::unique_ptr<PairCredentials>(std::string_view profile)>;

/// Test seams. Empty members select the native credential store and sockets.
struct CastDependencies {
    CredentialLoader load_credentials;
    StreamConnector connect;
    /// Applied to the filled session options just before the session starts,
    /// for scripted fakes (shorter intervals, no MRP data stream). Production
    /// and the C interface leave it empty.
    std::function<void(UrlPlaybackOptions&)> adjust_session;
};

/// Controller fields are read together; session and MRP status are each copied
/// under their own lock afterwards.
struct CastSnapshot {
    CastPhase phase = CastPhase::created;
    CastResult start_result = CastResult::ok; // Until a start finishes.
    unsigned rejected_status = 0;             // HTTP status for receiver_rejected.
    SessionStatus session;                    // Default until a session exists.
    MrpPlaybackStatus playback;
};

/**
 * Owns one media server and one URL playback session for the public C interface.
 *
 * The controller composes the same order the validated CLI uses: load trusted
 * credentials before any network work, build the HLS presentation for
 * hls_remux delivery (source reads only, cancellable by stop()), start the
 * media server, then start the session; on failure or teardown the session is cleaned up before the
 * media server stops, so the receiver never fetches from a server mid-teardown.
 *
 * Threading: start() blocks its caller. snapshot(), wait_for_change(), command()
 * and stop() may run concurrently with each other and with start(); stop()
 * cancels a pending start through the session's cancellation flag and waits
 * until that start has finished and recorded its result (start() itself then
 * returns to its caller). The session and server stay allocated until
 * destruction, so
 * concurrent readers never observe a dangling pointer. Destruction must follow
 * the return of every other call. No host code runs on controller threads except
 * the media source callbacks on media server workers.
 *
 * start() and command() report failures as CastResult and never throw.
 * snapshot() and wait_for_change() may throw std::bad_alloc.
 */
class CastController {
public:
    CastController(CastSettings settings, MediaSource source, CastDependencies dependencies = {});
    ~CastController();
    CastController(const CastController&) = delete;
    CastController& operator=(const CastController&) = delete;
    CastController(CastController&&) = delete;
    CastController& operator=(CastController&&) = delete;

    /// Valid once, in CastPhase::created; afterwards active or start_failed.
    CastResult start() noexcept;
    [[nodiscard]] CastSnapshot snapshot() const;
    /// Wait for a URL playback state different from `previous_state`, a phase
    /// change out of starting, or a session end, bounded by `timeout`.
    [[nodiscard]] CastSnapshot wait_for_change(const std::string& previous_state,
                                               std::chrono::milliseconds timeout) const;
    /// Seek requires a finite, nonnegative position; other commands ignore it.
    CastResult command(PlaybackCommand command, double position_seconds) noexcept;
    /// Idempotent ordered teardown: session first, then the media server.
    void stop() noexcept;

private:
    /// Throws the underlying failure; `server`/`session` hold what was started.
    void run_start(std::unique_ptr<MediaServer>& server,
                   std::unique_ptr<UrlPlaybackSession>& session);

    const CastSettings settings_;
    const CastDependencies dependencies_;
    // Declared before server_/session_ so the host source outlives both: the
    // session is destroyed first, then the server joins its workers, then the
    // controller's own source copy releases the host context.
    const MediaSource source_;

    std::atomic_bool cancel_requested_{false}; // Borrowed by UrlPlaybackSession::start.
    mutable std::mutex state_mutex_;           // Guards the fields below.
    mutable std::condition_variable state_changed_;
    CastPhase phase_ = CastPhase::created;
    CastResult start_result_ = CastResult::ok;
    unsigned rejected_status_ = 0;
    std::unique_ptr<MediaServer> server_;
    std::unique_ptr<UrlPlaybackSession> session_;

    std::mutex teardown_mutex_; // Serializes session/server stop between stop() callers.
};

/// Map a failure raised while starting a cast, pairing or removing a profile.
/// `rejected_status` receives the HTTP status of a SessionError::rejected
/// failure and is otherwise left at 0.
[[nodiscard]] CastResult cast_start_result(const std::exception_ptr& failure,
                                           unsigned& rejected_status) noexcept;
/// Map a failure raised by an MRP command on an active session.
[[nodiscard]] CastResult cast_command_result(const std::exception_ptr& failure) noexcept;
} // namespace send_airplay2::detail
#endif
