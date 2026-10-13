// SPDX-License-Identifier: Apache-2.0
// CastController lifecycle and failure mapping with injected credentials and
// connectors, plus active sessions against the scripted fake receiver.
// Loopback only: no receiver, credential store or private media.
#include "cast_controller.h"
#include "binary_plist.h"
#include "control_crypto.h"
#include "fake_receiver.h"
#include "mp4_fixture.h"
#include "credential_store.h"
#include "mrp_session.h"
#include "receiver_http.h"
#ifdef SAP2_HAS_STEREO_AAC
#include "file_media_source.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
using namespace send_airplay2::detail::testing;
using namespace std::chrono_literals;

int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}

// Synthetic, never-verified identity: every connector below fails before
// pair-verify, so the key bytes are never used.
std::unique_ptr<PairCredentials> synthetic_credentials() {
    const std::string receiver = "synthetic-receiver";
    const std::string client = "synthetic-client";
    return std::make_unique<PairCredentials>(Bytes(receiver.begin(), receiver.end()), PublicKey{},
                                             Bytes(client.begin(), client.end()), Secret32{});
}

/// Counts host reads and observes when the last MediaSource copy is released.
struct SourceProbe {
    std::atomic<int> reads{0};
    std::atomic<int> releases{0};
};
/// Shared by every MediaSource copy; counts one release when the last copy goes.
/// Noncopyable so a temporary can never add a spurious release.
struct ReleaseMarker {
    explicit ReleaseMarker(std::shared_ptr<SourceProbe> observed) : probe(std::move(observed)) {}
    ~ReleaseMarker() {
        ++probe->releases;
    }
    ReleaseMarker(const ReleaseMarker&) = delete;
    ReleaseMarker& operator=(const ReleaseMarker&) = delete;
    ReleaseMarker(ReleaseMarker&&) = delete;
    ReleaseMarker& operator=(ReleaseMarker&&) = delete;
    std::shared_ptr<SourceProbe> probe;
};
constexpr std::uint64_t source_size = 4096;

MediaSource probed_source(const std::shared_ptr<SourceProbe>& probe) {
    auto marker = std::make_shared<ReleaseMarker>(probe);
    MediaSource source;
    source.size = [marker] { return source_size; };
    source.read_at = [marker](std::uint64_t, std::uint8_t* buffer, std::size_t capacity,
                              const MediaReadContext&) {
        ++marker->probe->reads;
        buffer[0] = 0;
        return capacity == 0 ? std::size_t{0} : std::size_t{1};
    };
    return source;
}

CastSettings loopback_settings() {
    CastSettings settings;
    settings.receiver_address = "127.0.0.1";
    settings.receiver_port = 7000; // Selects the local route only; nothing listens.
    settings.profile = "synthetic";
    settings.start_timeout = 2000ms;
    settings.media_connections = 4;
    return settings;
}

CastDependencies stored_credentials(StreamConnector connect) {
    CastDependencies dependencies;
    dependencies.load_credentials = [](std::string_view) { return synthetic_credentials(); };
    dependencies.connect = std::move(connect);
    return dependencies;
}

StreamConnector failing_connector(TransportError reason) {
    return [reason](const ReceiverEndpoint&,
                    const ReceiverOperation&) -> std::unique_ptr<ReceiverStream> {
        throw TransportException(reason);
    };
}

/// Blocks like an unreachable receiver until the operation is cancelled or
/// reaches its request deadline.
StreamConnector blocking_connector(std::atomic_bool& entered) {
    return [&entered](const ReceiverEndpoint&,
                      const ReceiverOperation& operation) -> std::unique_ptr<ReceiverStream> {
        entered = true;
        for (;;) {
            operation.check();
            std::this_thread::sleep_for(5ms);
        }
    };
}

template <typename Exception, typename Reason>
CastResult start_result_for(Reason reason, unsigned& rejected_status) {
    return cast_start_result(std::make_exception_ptr(Exception(reason)), rejected_status);
}

void start_mapping_tests() {
    group = "start result mapping";
    unsigned status = 0;
    check(cast_start_result(std::make_exception_ptr(SessionException(SessionError::rejected, 403)),
                            status) == CastResult::receiver_rejected &&
              status == 403,
          "rejected session keeps its HTTP status");
    status = 0;
    check(start_result_for<SessionException>(SessionError::start_timeout, status) ==
                  CastResult::start_timeout &&
              status == 0,
          "start timeout");
    check(start_result_for<SessionException>(SessionError::connection_lost, status) ==
              CastResult::connection,
          "session connection loss");
    check(start_result_for<CredentialException>(CredentialError::unsupported, status) ==
              CastResult::unsupported,
          "credential store unsupported");
    check(start_result_for<CredentialException>(CredentialError::unavailable, status) ==
              CastResult::credential_store,
          "credential store unavailable");
    check(start_result_for<CredentialException>(CredentialError::invalid_record, status) ==
              CastResult::credential_store,
          "malformed credential record");
    check(start_result_for<PairVerifyException>(PairVerifyError::authentication, status) ==
              CastResult::authentication,
          "peer verification authentication");
    check(start_result_for<PairVerifyException>(PairVerifyError::invalid_message, status) ==
              CastResult::protocol,
          "peer verification malformed message");
    check(start_result_for<ControlException>(ControlError::authentication, status) ==
              CastResult::authentication,
          "record authentication");
    check(start_result_for<TransportException>(TransportError::network, status) ==
              CastResult::connection,
          "transport network failure");
    check(start_result_for<TransportException>(TransportError::timeout, status) ==
              CastResult::connection,
          "transport timeout");
    check(start_result_for<TransportException>(TransportError::correlation, status) ==
              CastResult::protocol,
          "transport correlation");
    check(start_result_for<TransportException>(TransportError::cancelled, status) ==
              CastResult::cancelled,
          "transport cancellation");
    check(start_result_for<MrpException>(MrpError::rejected, status) ==
              CastResult::receiver_rejected,
          "MRP handshake rejection");
    check(cast_start_result(std::make_exception_ptr(std::invalid_argument("x")), status) ==
              CastResult::invalid_argument,
          "invalid argument");
    check(cast_start_result(std::make_exception_ptr(std::bad_alloc()), status) ==
              CastResult::out_of_memory,
          "allocation failure");
    check(cast_start_result(std::make_exception_ptr(std::runtime_error("x")), status) ==
              CastResult::internal,
          "unexpected backend failure");
}

void command_mapping_tests() {
    group = "command result mapping";
    const auto mapped = [](MrpError reason) {
        return cast_command_result(std::make_exception_ptr(MrpException(reason)));
    };
    check(mapped(MrpError::not_owned) == CastResult::not_owned, "not owned");
    check(mapped(MrpError::cancelled) == CastResult::ended, "cancelled by teardown");
    check(mapped(MrpError::rejected) == CastResult::command_failed, "rejected");
    check(mapped(MrpError::timeout) == CastResult::command_failed, "unanswered");
    check(mapped(MrpError::disconnected) == CastResult::connection, "disconnected");
    check(mapped(MrpError::authentication) == CastResult::authentication, "authentication");
}

void created_phase_tests() {
    group = "created phase";
    auto probe = std::make_shared<SourceProbe>();
    {
        CastController controller(loopback_settings(), probed_source(probe),
                                  stored_credentials(failing_connector(TransportError::network)));
        const auto snapshot = controller.snapshot();
        check(snapshot.phase == CastPhase::created && snapshot.start_result == CastResult::ok,
              "new controller is created with no start result");
        check(controller.command(PlaybackCommand::pause, 0) == CastResult::invalid_state,
              "command before start");
        const auto started = std::chrono::steady_clock::now();
        const auto waited = controller.wait_for_change("", 2s);
        check(waited.phase == CastPhase::created && std::chrono::steady_clock::now() - started < 1s,
              "wait returns immediately when nothing can change");
        controller.stop();
        check(controller.snapshot().phase == CastPhase::stopped, "stop before start");
        check(controller.start() == CastResult::invalid_state, "start after stop");
        controller.stop();
        check(controller.snapshot().phase == CastPhase::stopped, "stop is idempotent");
        check(probe->releases == 0, "source retained until destruction");
    }
    check(probe->reads == 0, "no media read without a start");
    check(probe->releases == 1, "source released exactly once at destruction");
}

void start_failure_tests() {
    group = "start failures";
    {
        auto probe = std::make_shared<SourceProbe>();
        CastDependencies dependencies;
        dependencies.load_credentials = [](std::string_view) {
            return std::unique_ptr<PairCredentials>();
        };
        std::atomic_bool connected{false};
        dependencies.connect =
            [&connected](const ReceiverEndpoint&,
                         const ReceiverOperation&) -> std::unique_ptr<ReceiverStream> {
            connected = true;
            throw TransportException(TransportError::network);
        };
        CastController controller(loopback_settings(), probed_source(probe),
                                  std::move(dependencies));
        check(controller.start() == CastResult::profile_not_found, "absent profile");
        check(!connected, "absent profile makes no receiver connection");
        const auto snapshot = controller.snapshot();
        check(snapshot.phase == CastPhase::start_failed &&
                  snapshot.start_result == CastResult::profile_not_found,
              "absent profile is recorded as the start result");
        check(controller.start() == CastResult::invalid_state, "start is valid only once");
    }
    {
        CastDependencies dependencies;
        dependencies.load_credentials = [](std::string_view) -> std::unique_ptr<PairCredentials> {
            throw CredentialException(CredentialError::unavailable);
        };
        CastController controller(loopback_settings(),
                                  probed_source(std::make_shared<SourceProbe>()),
                                  std::move(dependencies));
        check(controller.start() == CastResult::credential_store, "unavailable credential store");
    }
    {
        auto probe = std::make_shared<SourceProbe>();
        {
            CastController controller(
                loopback_settings(), probed_source(probe),
                stored_credentials(failing_connector(TransportError::network)));
            check(controller.start() == CastResult::connection, "unreachable receiver");
            const auto snapshot = controller.snapshot();
            check(snapshot.phase == CastPhase::start_failed &&
                      snapshot.start_result == CastResult::connection,
                  "connection failure is recorded");
            check(controller.command(PlaybackCommand::play, 0) == CastResult::invalid_state,
                  "command after failed start");
            controller.stop();
            check(controller.snapshot().phase == CastPhase::start_failed,
                  "stop keeps the failed-start phase");
        }
        check(probe->releases == 1, "failed start still releases the source once");
    }
    {
        auto settings = loopback_settings();
        settings.receiver_address = "not-an-address";
        CastController controller(settings, probed_source(std::make_shared<SourceProbe>()),
                                  stored_credentials(failing_connector(TransportError::network)));
        check(controller.start() == CastResult::invalid_argument,
              "media server rejects a non-numeric address");
    }
    {
        auto settings = loopback_settings();
        settings.content_type = "video/mp4; injected=1";
        CastController controller(settings, probed_source(std::make_shared<SourceProbe>()),
                                  stored_credentials(failing_connector(TransportError::network)));
        check(controller.start() == CastResult::invalid_argument,
              "media server rejects content type parameters");
    }
}

void cancellation_tests() {
    group = "cancellation";
    auto probe = std::make_shared<SourceProbe>();
    std::atomic_bool entered{false};
    {
        CastController controller(loopback_settings(), probed_source(probe),
                                  stored_credentials(blocking_connector(entered)));
        auto start = std::async(std::launch::async, [&controller] { return controller.start(); });
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!entered && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(5ms);
        }
        check(entered, "start reached the receiver connection");
        const auto starting = controller.wait_for_change("", 10ms);
        check(starting.phase == CastPhase::starting, "blocked start reports starting");
        const auto stop_started = std::chrono::steady_clock::now();
        controller.stop();
        const auto stop_duration = std::chrono::steady_clock::now() - stop_started;
        // stop() waits until start() has recorded its result. start() returns
        // to the async wrapper just after, and the future becomes ready after
        // that, so its readiness is checked with a bound rather than at once
        // (an immediate check raced on loaded CI runners).
        const auto snapshot = controller.snapshot();
        check(snapshot.phase == CastPhase::start_failed &&
                  snapshot.start_result == CastResult::cancelled,
              "stop returns only after the pending start recorded its cancellation");
        check(start.wait_for(2s) == std::future_status::ready, "the pending start returns");
        check(start.get() == CastResult::cancelled, "pending start is cancelled");
        check(stop_duration < 1500ms, "cancellation is prompt, not the request deadline");
    }
    check(probe->releases == 1, "cancelled start releases the source once");
}

/// Credentials, connector and session timing for one scripted fake receiver.
/// Without Behavior::mrp_fixtures the fake has no MRP data stream, so the
/// session runs without MRP and its commands report not_owned.
CastDependencies fake_receiver_dependencies(FakeReceiver& receiver, bool mrp = false) {
    CastDependencies dependencies;
    dependencies.load_credentials = [&receiver](std::string_view) {
        // Direct initialization from the returned prvalue; PairCredentials is
        // neither copyable nor movable.
        return std::unique_ptr<PairCredentials>(new PairCredentials(receiver.credentials()));
    };
    dependencies.connect = receiver.connector();
    dependencies.adjust_session = [mrp](UrlPlaybackOptions& options) {
        options.enable_mrp = mrp;
        options.start_confirmation_interval = 20ms; // Keep scripted scenarios fast.
        options.feedback_interval = 30ms;
    };
    return dependencies;
}

const std::vector<std::string> url_then_remote{"URL", "remote"};

/// Observed control-connection close order, for failure messages.
std::string close_order_text(const FakeReceiver& receiver) {
    std::string text;
    for (const auto& session : receiver.control_close_order()) {
        text += (text.empty() ? "" : ",") + session;
    }
    return "[" + text + "]";
}

void active_session_tests() {
    group = "active session";
    auto probe = std::make_shared<SourceProbe>();
    FakeReceiver receiver({});
    {
        CastController controller(loopback_settings(), probed_source(probe),
                                  fake_receiver_dependencies(receiver));
        check(controller.start() == CastResult::ok, "scripted receiver start");
        auto snapshot = controller.snapshot();
        check(snapshot.phase == CastPhase::active && snapshot.start_result == CastResult::ok &&
                  snapshot.session.playback_state == "playing" && !snapshot.session.cleaned_up,
              "started snapshot is active and playing");
        check(controller.start() == CastResult::invalid_state, "second start while active");
        check(controller.command(PlaybackCommand::pause, 0) == CastResult::not_owned,
              "command without an MRP session reports not_owned");

        receiver.push_state("Paused");
        const auto paused = controller.wait_for_change("playing", 2s);
        check(paused.phase == CastPhase::active && paused.session.playback_state == "paused" &&
                  paused.session.end_reason == SessionEnd::none,
              "receiver pause reaches wait_for_change without ending the session");

        controller.stop();
        snapshot = controller.snapshot();
        check(snapshot.phase == CastPhase::stopped &&
                  snapshot.session.end_reason == SessionEnd::sender_stop &&
                  snapshot.session.cleaned_up && !snapshot.session.failed,
              "stop records sender_stop after joined cleanup");
        check(receiver.control_close_order() == url_then_remote,
              std::string("stop closes the URL session before the remote session") + " (closed " +
                  close_order_text(receiver) + ")");
        check(controller.command(PlaybackCommand::play, 0) == CastResult::ended,
              "command after stop");
        controller.stop();
        check(controller.snapshot().session.end_reason == SessionEnd::sender_stop,
              "repeated stop keeps the first end reason");
        check(probe->releases == 0, "source retained until destruction");
    }
    check(probe->releases == 1, "active session releases the source exactly once");
}

void natural_end_tests() {
    group = "natural end";
    auto probe = std::make_shared<SourceProbe>();
    FakeReceiver receiver({});
    {
        CastController controller(loopback_settings(), probed_source(probe),
                                  fake_receiver_dependencies(receiver));
        check(controller.start() == CastResult::ok, "scripted receiver start");
        receiver.push_state("Ended");
        check(eventually([&] { return controller.snapshot().session.cleaned_up; }),
              "receiver end cleans the session without host input");
        const auto ended = controller.snapshot();
        check(ended.phase == CastPhase::ended &&
                  ended.session.end_reason == SessionEnd::media_end && !ended.session.failed,
              "ended phase with media_end");
        const auto wait_started = std::chrono::steady_clock::now();
        (void)controller.wait_for_change("playing", 2s);
        check(std::chrono::steady_clock::now() - wait_started < 1s,
              "wait returns promptly once the session has ended");
        check(controller.command(PlaybackCommand::pause, 0) == CastResult::ended,
              "command after natural end");
        controller.stop();
        const auto stopped = controller.snapshot();
        check(stopped.phase == CastPhase::stopped &&
                  stopped.session.end_reason == SessionEnd::media_end,
              "host stop after natural end keeps media_end");
    }
    check(probe->releases == 1, "natural end releases the source exactly once");
}

void connection_loss_tests() {
    group = "connection loss";
    auto probe = std::make_shared<SourceProbe>();
    FakeReceiver receiver({});
    {
        CastController controller(loopback_settings(), probed_source(probe),
                                  fake_receiver_dependencies(receiver));
        check(controller.start() == CastResult::ok, "scripted receiver start");
        receiver.end_event_channel();
        check(eventually([&] { return controller.snapshot().session.cleaned_up; }),
              "event channel loss cleans the session without host input");
        const auto lost = controller.snapshot();
        check(lost.phase == CastPhase::ended &&
                  lost.session.end_reason == SessionEnd::connection_lost &&
                  lost.session.failure_channel == SessionFailureChannel::url_events &&
                  lost.session.failure_reason == SessionFailureReason::disconnected,
              "loss is reported as connection_lost on the URL event channel");
        check(controller.command(PlaybackCommand::play, 0) == CastResult::ended,
              "command after connection loss");
        controller.stop();
        check(controller.snapshot().session.end_reason == SessionEnd::connection_lost,
              "stop after loss keeps connection_lost");
    }
    check(probe->releases == 1, "connection loss releases the source exactly once");
}

void concurrent_stop_tests() {
    group = "concurrent stop";
    auto probe = std::make_shared<SourceProbe>();
    FakeReceiver receiver({});
    {
        CastController controller(loopback_settings(), probed_source(probe),
                                  fake_receiver_dependencies(receiver));
        check(controller.start() == CastResult::ok, "scripted receiver start");
        std::thread first([&] { controller.stop(); });
        std::thread second([&] { controller.stop(); });
        first.join();
        second.join();
        const auto snapshot = controller.snapshot();
        check(snapshot.phase == CastPhase::stopped && snapshot.session.cleaned_up &&
                  snapshot.session.end_reason == SessionEnd::sender_stop,
              "concurrent stops finish one teardown");
        check(receiver.control_close_order() == url_then_remote,
              std::string("concurrent stops close each session once, in order") + " (closed " +
                  close_order_text(receiver) + ")");
    }
    check(probe->releases == 1, "concurrent stops release the source exactly once");
}

Behavior with_mrp(const std::string& fixtures) {
    Behavior behavior;
    behavior.mrp_fixtures = fixtures;
    return behavior;
}

// Twenty-five of the session's 20 ms supervisor poll slices.
constexpr auto supervisor_observation_time = 500ms;

// Wire values of PlaybackCommand, independent of the enum under test.
constexpr std::uint32_t wire_play = 1, wire_pause = 2, wire_stop = 4, wire_seek = 45;

void mrp_command_tests(const std::string& fixtures) {
    group = "MRP commands";
    auto probe = std::make_shared<SourceProbe>();
    FakeReceiver receiver(with_mrp(fixtures));
    {
        CastController controller(loopback_settings(), probed_source(probe),
                                  fake_receiver_dependencies(receiver, true));
        check(controller.start() == CastResult::ok, "scripted MRP receiver start");
        check(eventually([&] { return controller.snapshot().playback.owned; }),
              "MRP state for the inserted item establishes ownership");
        auto snapshot = controller.snapshot();
        check(snapshot.playback.state == "playing" && snapshot.playback.duration_seconds &&
                  std::abs(*snapshot.playback.duration_seconds - 131.6) < 1e-9,
              "owned status reports the receiver's state and duration");

        check(controller.command(PlaybackCommand::pause, 0) == CastResult::ok, "pause accepted");
        check(eventually([&] { return controller.snapshot().playback.state == "paused"; }),
              "receiver pause state follows the command");
        check(controller.command(PlaybackCommand::play, 0) == CastResult::ok, "play accepted");
        check(eventually([&] { return controller.snapshot().playback.state == "playing"; }),
              "receiver playing state follows the command");
        check(controller.command(PlaybackCommand::seek, 30.5) == CastResult::ok,
              "forward seek accepted");
        check(controller.command(PlaybackCommand::seek, 0) == CastResult::ok,
              "seek to the start accepted");
        check(controller.command(PlaybackCommand::stop, 0) == CastResult::ok, "MRP stop accepted");

        const auto& remote = receiver.remote_control();
        check(remote.mrp_commands() == std::vector<std::uint32_t>{wire_pause, wire_play, wire_seek,
                                                                  wire_seek, wire_stop},
              "commands reach the receiver in order with their wire numbers");
        check(remote.mrp_seek_positions() == std::vector<double>{30.5, 0.0},
              "seek positions reach the receiver unchanged");

        controller.stop();
        snapshot = controller.snapshot();
        check(snapshot.phase == CastPhase::stopped && snapshot.session.cleaned_up &&
                  snapshot.session.end_reason == SessionEnd::sender_stop,
              "local stop after MRP commands records sender_stop");
        check(receiver.control_close_order() == url_then_remote,
              std::string("MRP session closes the URL session before the remote session") +
                  " (closed " + close_order_text(receiver) + ")");
    }
    check(probe->releases == 1, "MRP session releases the source exactly once");
}

void mrp_end_tests(const std::string& fixtures) {
    group = "MRP-reported end";
    {
        FakeReceiver receiver(with_mrp(fixtures));
        CastController controller(loopback_settings(),
                                  probed_source(std::make_shared<SourceProbe>()),
                                  fake_receiver_dependencies(receiver, true));
        check(controller.start() == CastResult::ok, "scripted MRP receiver start");
        check(eventually([&] { return controller.snapshot().playback.owned; }), "owned");
        // Paused exactly at the item's duration is the receiver's EOF report.
        check(receiver.remote_control().push_mrp_state(mrp_state_paused, 131.6),
              "end state pushed");
        check(eventually([&] { return controller.snapshot().session.cleaned_up; }),
              "MRP end cleans the session without host input");
        const auto ended = controller.snapshot();
        check(ended.phase == CastPhase::ended &&
                  ended.session.end_reason == SessionEnd::media_end && !ended.session.failed,
              "MRP paused-at-duration ends with media_end");
        check(controller.command(PlaybackCommand::play, 0) == CastResult::ended,
              "command after MRP end");
    }
    group = "MRP ownership loss";
    {
        FakeReceiver receiver(with_mrp(fixtures));
        CastController controller(loopback_settings(),
                                  probed_source(std::make_shared<SourceProbe>()),
                                  fake_receiver_dependencies(receiver, true));
        check(controller.start() == CastResult::ok, "scripted MRP receiver start");
        check(eventually([&] { return controller.snapshot().playback.owned; }), "owned");
        // The session counts a loss only after its own 20 ms supervisor poll has
        // seen ownership, so a momentary owner is never "lost". Keep ownership
        // for many poll slices before replacing the item; on a slow runner an
        // immediate replacement can precede the supervisor's next poll.
        std::this_thread::sleep_for(supervisor_observation_time);
        check(receiver.remote_control().push_mrp_state(mrp_state_playing, 0, "another-item"),
              "replacement item pushed");
        check(eventually([&] { return controller.snapshot().session.cleaned_up; }),
              "ownership loss cleans the session without host input");
        const auto lost = controller.snapshot();
        check(lost.phase == CastPhase::ended &&
                  lost.session.end_reason == SessionEnd::ownership_lost && !lost.playback.owned,
              "another item on our player ends with ownership_lost");
        check(controller.command(PlaybackCommand::pause, 0) == CastResult::ended,
              "command after ownership loss");
    }
}

void command_argument_tests() {
    group = "command arguments";
    CastController controller(loopback_settings(), probed_source(std::make_shared<SourceProbe>()),
                              stored_credentials(failing_connector(TransportError::network)));
    check(controller.command(PlaybackCommand::seek, std::numeric_limits<double>::quiet_NaN()) ==
              CastResult::invalid_argument,
          "seek to NaN");
    check(controller.command(PlaybackCommand::seek, -1) == CastResult::invalid_argument,
          "seek before zero");
    check(controller.command(PlaybackCommand::seek, std::numeric_limits<double>::infinity()) ==
              CastResult::invalid_argument,
          "seek to infinity");
    controller.stop();
    check(controller.command(PlaybackCommand::pause, 0) == CastResult::ended, "command after stop");
}
} // namespace

/// The synthetic MP4 of mp4_fixture.h as a host source, counting reads.
MediaSource mp4_source(const std::shared_ptr<SourceProbe>& probe,
                       std::vector<send_airplay2::test::mp4_fixture::TrackSpec> tracks = {}) {
    using namespace send_airplay2::test::mp4_fixture;
    if (tracks.empty()) {
        tracks = {video_spec(), audio_spec()};
    }
    const auto file = std::make_shared<const Bytes>(mp4_file(tracks));
    auto marker = std::make_shared<ReleaseMarker>(probe);
    MediaSource source;
    source.size = [file, marker] { return static_cast<std::uint64_t>(file->size()); };
    source.read_at = [file, marker](std::uint64_t offset, std::uint8_t* buffer,
                                    std::size_t capacity, const MediaReadContext&) -> std::size_t {
        ++marker->probe->reads;
        if (offset >= file->size()) {
            return 0;
        }
        const auto count = std::min<std::uint64_t>(capacity, file->size() - offset);
        std::memcpy(buffer, file->data() + offset, static_cast<std::size_t>(count));
        return static_cast<std::size_t>(count);
    };
    return source;
}

CastSettings hls_settings() {
    auto settings = loopback_settings();
    settings.delivery = CastDelivery::hls_remux;
    return settings;
}

bool ends_with(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

/// The public controller must inherit the production URL fallback, including
/// HLS delivery and the seek performed before start returns (D62).
void url_controls_delivery_tests() {
    for (const auto delivery : {CastDelivery::progressive, CastDelivery::hls_remux}) {
        group = delivery == CastDelivery::progressive ? "URL controls progressive delivery"
                                                      : "URL controls HLS delivery";
        Behavior macos;
        macos.remote_setup_status = 500; // Recorded macOS remote-control SETUP rejection.
        FakeReceiver receiver(macos);
        auto probe = std::make_shared<SourceProbe>();
        {
            auto settings = loopback_settings();
            settings.delivery = delivery;
            settings.start_position_seconds = 45.0;
            CastController controller(settings, mp4_source(probe),
                                      fake_receiver_dependencies(receiver, true));
            check(controller.start() == CastResult::ok, "start succeeds after remote rejection");
            const auto started = controller.snapshot();
            check(started.phase == CastPhase::active && started.session.url_controls &&
                      started.playback.owned && started.playback.state == "playing",
                  "the controller exposes URL playback without an MRP stream");
            check(ends_with(receiver.inserted_media_url(), "/index.m3u8") ==
                      (delivery == CastDelivery::hls_remux),
                  "delivery selects the remux playlist or progressive resource");
            check(receiver.remote_control().control_was_closed() &&
                      !receiver.remote_control().event_channel_opened(),
                  "the rejected remote session has no retained event connection");

            check(controller.command(PlaybackCommand::pause, 0) == CastResult::ok,
                  "pause uses the URL controls through the public controller");
            check(controller.command(PlaybackCommand::play, 0) == CastResult::ok,
                  "play uses the URL controls through the public controller");
            check(controller.command(PlaybackCommand::seek, 60.5) == CastResult::ok,
                  "seek uses the URL controls through the public controller");
            check(controller.command(PlaybackCommand::stop, 0) == CastResult::ok,
                  "the controller sends the URL stop command");

            constexpr std::size_t queue_start_command_count = 4;
            std::size_t command_count = 0;
            std::vector<PlistValue> controls;
            for (const auto& request : receiver.requests()) {
                if (request.target != "/command" || command_count++ < queue_start_command_count) {
                    continue;
                }
                const auto envelope = decode_binary_plist(request.body);
                controls.push_back(
                    decode_binary_plist(envelope.find("params")->find("data")->as_data()));
            }
            check(controls.size() == 5, "one start-position seek and four controls, got " +
                                            std::to_string(controls.size()));
            if (controls.size() == 5) {
                check(controls[0].find("type")->as_string() == "seek" &&
                          controls[0].find("time")->find("value")->as_integer() == 45000,
                      "the start-position seek is sent before the host controls");
                check(controls[1].find("type")->as_string() == "setRate" &&
                          controls[1].find("rate")->as_real() == 0.0 &&
                          controls[2].find("type")->as_string() == "setRate" &&
                          controls[2].find("rate")->as_real() == 1.0,
                      "pause and play retain their literal URL rates");
                check(controls[3].find("type")->as_string() == "seek" &&
                          controls[3].find("time")->find("value")->as_integer() == 60500 &&
                          controls[3].find("time")->find("timescale")->as_integer() == 1000 &&
                          controls[3].find("item")->find("uuid")->as_string() ==
                              controls[0].find("item")->find("uuid")->as_string(),
                      "both seeks name the same queued item with millisecond CMTime units");
                check(controls[4].find("type")->as_string() == "stop", "stop is a URL command");
            }
            controller.stop(); // Host stop joins the session and media listener.
            const auto stopped = controller.snapshot();
            check(stopped.phase == CastPhase::stopped && stopped.session.cleaned_up &&
                      stopped.session.end_reason == SessionEnd::sender_stop &&
                      !stopped.session.failed && receiver.control_was_closed(),
                  "sender stop records successful joined URL cleanup");
        }
        check(probe->releases == 1, "the controller releases the host source exactly once");
    }
}

void url_controls_terminal_tests() {
    group = "URL controls terminal mapping";
    {
        Behavior macos;
        macos.remote_setup_status = 500;
        FakeReceiver receiver(macos);
        auto probe = std::make_shared<SourceProbe>();
        {
            CastController controller(hls_settings(), mp4_source(probe),
                                      fake_receiver_dependencies(receiver, true));
            check(controller.start() == CastResult::ok, "HLS start before natural end");
            receiver.push_event_body(encode_binary_plist(PlistDictionary{
                {"type", "playbackState"}, {"name", "stopped"}, {"reason", "ended"}}));
            check(eventually([&] { return controller.snapshot().session.cleaned_up; }),
                  "natural end joins cleanup without a host stop");
            const auto ended = controller.snapshot();
            check(ended.phase == CastPhase::ended &&
                      ended.session.end_reason == SessionEnd::media_end && !ended.session.failed,
                  "URL natural end maps to the public controller's media_end");
            check(controller.command(PlaybackCommand::play, 0) == CastResult::ended,
                  "a control after natural end reports ended");
            controller.stop();
            check(controller.snapshot().session.end_reason == SessionEnd::media_end &&
                      probe->releases == 0,
                  "host stop preserves EOF and retains the source until destruction");
        }
        check(probe->releases == 1, "destruction releases the source exactly once after EOF");
    }
    {
        Behavior rejected;
        rejected.remote_setup_status = 500;
        rejected.base_setup_status = 400; // Consent rejection remains a failed URL start.
        FakeReceiver receiver(rejected);
        auto probe = std::make_shared<SourceProbe>();
        {
            CastController controller(hls_settings(), mp4_source(probe),
                                      fake_receiver_dependencies(receiver, true));
            check(controller.start() == CastResult::receiver_rejected,
                  "fallback does not turn a rejected URL SETUP into success");
            const auto failed = controller.snapshot();
            check(failed.phase == CastPhase::start_failed && failed.rejected_status == 400 &&
                      receiver.control_was_closed() &&
                      receiver.remote_control().control_was_closed(),
                  "the URL rejection retains its status and closes both connections");
        }
        check(probe->releases == 1, "a rejected URL start releases its host source exactly once");
    }
}

void hls_delivery_tests() {
    group = "HLS remux delivery";
    {
        auto probe = std::make_shared<SourceProbe>();
        FakeReceiver receiver({});
        CastController controller(hls_settings(), mp4_source(probe),
                                  fake_receiver_dependencies(receiver));
        check(controller.start() == CastResult::ok, "remuxed MP4 starts on the scripted receiver");
        const auto url = receiver.inserted_media_url();
        check(url.rfind("http://127.0.0.1:", 0) == 0 && ends_with(url, "/index.m3u8"),
              "the receiver is sent the playlist below the private path");
        check(probe->reads > 0, "the remux read the source's sample tables");
        controller.stop();
        check(controller.snapshot().phase == CastPhase::stopped, "stop after an HLS start");
    }
    {
        FakeReceiver receiver({});
        CastController controller(loopback_settings(),
                                  probed_source(std::make_shared<SourceProbe>()),
                                  fake_receiver_dependencies(receiver));
        check(controller.start() == CastResult::ok, "progressive start");
        check(!ends_with(receiver.inserted_media_url(), ".m3u8"),
              "progressive delivery sends the single representation's URL");
        controller.stop();
    }
    {
        // The probed source reads zeros: no moov, so the remux finds it malformed
        // before any listener or receiver connection exists.
        auto probe = std::make_shared<SourceProbe>();
        std::atomic_bool connected{false};
        auto dependencies = stored_credentials(blocking_connector(connected));
        CastController controller(hls_settings(), probed_source(probe), dependencies);
        check(controller.start() == CastResult::media_malformed, "unremuxable source is malformed");
        check(!connected, "no receiver connection after a remux refusal");
        check(controller.snapshot().phase == CastPhase::start_failed, "refusal is a failed start");
    }
    unsigned rejected_status = 0;
    for (const auto reason : {RemuxFailure::unsupported, RemuxFailure::too_large}) {
        check(cast_start_result(std::make_exception_ptr(RemuxException(reason, "synthetic")),
                                rejected_status) == CastResult::media_unsupported,
              std::string(remux_failure_name(reason)) + " maps to media_unsupported");
    }
    {
        // A source read that blocks until its request is cancelled: stop()
        // must end the remux, and the start reports cancelled.
        auto probe = std::make_shared<SourceProbe>();
        std::atomic_bool entered{false};
        auto marker = std::make_shared<ReleaseMarker>(probe);
        MediaSource stalled;
        stalled.size = [marker] { return std::uint64_t{1} << 20; };
        stalled.read_at = [marker, &entered](std::uint64_t, std::uint8_t*, std::size_t,
                                             const MediaReadContext& context) -> std::size_t {
            entered = true;
            while (!context.should_stop()) {
                std::this_thread::sleep_for(5ms);
            }
            return 0;
        };
        std::atomic_bool connected{false};
        CastController controller(hls_settings(), std::move(stalled),
                                  stored_credentials(blocking_connector(connected)));
        auto start = std::async(std::launch::async, [&controller] { return controller.start(); });
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (!entered && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(5ms);
        }
        check(entered, "the remux is reading the source");
        const auto stop_started = std::chrono::steady_clock::now();
        controller.stop();
        check(std::chrono::steady_clock::now() - stop_started < 1500ms,
              "stop ends a blocked remux read promptly");
        check(start.wait_for(2s) == std::future_status::ready &&
                  start.get() == CastResult::cancelled,
              "a stopped remux start reports cancelled");
        check(!connected, "no receiver connection after a cancelled remux");
    }
}

/// tvOS 26 plays from 0 whatever the queue item's Start-Position-Seconds says
/// (D61), so a nonzero start position becomes one MRP seek before start()
/// returns; a zero start sends no command.
void start_position_tests(const std::string& fixtures) {
    group = "Start position";
    constexpr double requested_start = 300.5;
    {
        FakeReceiver receiver(with_mrp(fixtures));
        auto settings = loopback_settings();
        settings.start_position_seconds = requested_start;
        CastController controller(settings, probed_source(std::make_shared<SourceProbe>()),
                                  fake_receiver_dependencies(receiver, true));
        check(controller.start() == CastResult::ok, "start with a start position");
        const auto& remote = receiver.remote_control();
        check(remote.mrp_commands() == std::vector<std::uint32_t>{wire_seek} &&
                  remote.mrp_seek_positions() == std::vector<double>{requested_start},
              "one seek to the start position reaches the receiver before start returns");
        controller.stop();
    }
    {
        FakeReceiver receiver(with_mrp(fixtures));
        CastController controller(loopback_settings(),
                                  probed_source(std::make_shared<SourceProbe>()),
                                  fake_receiver_dependencies(receiver, true));
        check(controller.start() == CastResult::ok, "start from the beginning");
        check(receiver.remote_control().mrp_commands().empty(),
              "a zero start position sends no seek");
        controller.stop();
    }
}

struct ConversionProbe {
    std::atomic<StereoAacHlsFailure> failure{StereoAacHlsFailure::none};
    std::atomic<int> stops{0};
    std::atomic<int> clears{0};
};
class ScriptedConversion final : public PreparedStereoMedia {
public:
    ScriptedConversion(RemuxedHls presentation, AacFragmentStore store,
                       std::shared_ptr<ConversionProbe> probe)
        : presentation_(std::move(presentation)), store_(std::move(store)),
          probe_(std::move(probe)) {}
    const RemuxedHls& presentation() const noexcept override {
        return presentation_;
    }
    StereoAacHlsFailure failure() const noexcept override {
        return probe_->failure.load();
    }
    std::uint64_t selected_audio_id() const noexcept override {
        return 47;
    }
    std::uint32_t authoring_flags() const noexcept override {
        return authoring_audio_estimated;
    }
    void stop() noexcept override {
        if (!stopped_.exchange(true)) {
            ++probe_->stops;
            if (store_.clear) {
                store_.clear();
            }
        }
    }

private:
    RemuxedHls presentation_;
    AacFragmentStore store_;
    std::shared_ptr<ConversionProbe> probe_;
    std::atomic_bool stopped_{false};
};
CastSettings conversion_settings(const std::shared_ptr<ConversionProbe>& probe) {
    auto settings = loopback_settings();
    settings.delivery = CastDelivery::stereo_aac_hls;
    settings.audio_track_id = 47;
    settings.cache_quota_bytes = 1024 * 1024;
    settings.fragment_store.seal = [](std::size_t, const Bytes&, const AacCacheContext&) {};
    settings.fragment_store.read_at = [](std::size_t, std::uint64_t, std::uint8_t*, std::size_t,
                                         const AacCacheContext&) { return std::size_t{0}; };
    settings.fragment_store.clear = [probe] { ++probe->clears; };
    return settings;
}
void scripted_conversion(CastDependencies& dependencies,
                         const std::shared_ptr<ConversionProbe>& probe) {
    dependencies.prepare_stereo =
        [probe](MediaSource source, AacFragmentStore store, std::uint64_t quota,
                const MediaReadContext& context,
                std::optional<std::uint64_t> selected) -> std::unique_ptr<PreparedStereoMedia> {
        check(selected == 47 && quota == 1024 * 1024 && !context.should_stop(),
              "controller passes original selection, quota and live preparation context");
        return std::make_unique<ScriptedConversion>(remux_to_hls(source), std::move(store), probe);
    };
}
void conversion_start_failure_tests() {
    group = "conversion startup failure";
    auto probe = std::make_shared<ConversionProbe>();
    std::atomic_bool entered{false};
    auto dependencies = stored_credentials(blocking_connector(entered));
    scripted_conversion(dependencies, probe);
    CastController controller(conversion_settings(probe),
                              mp4_source(std::make_shared<SourceProbe>()), dependencies);
    auto start = std::async(std::launch::async, [&] { return controller.start(); });
    check(eventually([&] { return entered.load(); }),
          "start reached cancellable receiver connection");
    probe->failure = StereoAacHlsFailure::cache_error;
    check(start.wait_for(2s) == std::future_status::ready,
          "terminal conversion failure interrupts start");
    check(start.get() == CastResult::media_cache, "cache failure wins over transport cancellation");
    const auto snapshot = controller.snapshot();
    check(snapshot.phase == CastPhase::start_failed && snapshot.media_failure_first &&
              snapshot.media_cleaned_up,
          "failed start records terminal category after cleanup");
    controller.stop();
    check(probe->stops == 1 && probe->clears == 1, "failed start and later Stop clear once");
}
void conversion_terminal_tests() {
    group = "conversion terminal supervision";
    for (const bool natural_end : {false, true}) {
        auto probe = std::make_shared<ConversionProbe>();
        FakeReceiver receiver({});
        auto dependencies = fake_receiver_dependencies(receiver);
        scripted_conversion(dependencies, probe);
        CastController controller(conversion_settings(probe),
                                  mp4_source(std::make_shared<SourceProbe>()), dependencies);
        check(controller.start() == CastResult::ok, "scripted converted presentation starts");
        check(ends_with(receiver.inserted_media_url(), "/index.m3u8"),
              "prepared playlist URL reaches receiver");
        if (natural_end) {
            receiver.push_state("Ended");
        } else {
            probe->failure = StereoAacHlsFailure::source_error;
        }
        check(eventually([&] { return controller.snapshot().media_cleaned_up; }),
              natural_end ? "natural end drains conversion and cache"
                          : "source failure drains conversion and cache");
        auto snapshot = controller.snapshot();
        check(probe->stops == 1 && probe->clears == 1 && snapshot.session.cleaned_up &&
                  receiver.control_close_order() == url_then_remote,
              "session cleanup precedes final conversion/cache cleanup");
        check(snapshot.selected_audio_id == 47 && snapshot.authoring_flags == 1,
              "immutable source selection and literal estimate flag persist after cleanup");
        check(snapshot.media_failure_first == !natural_end,
              "conversion failure overrides only a conversion-led end");
        check(controller.command(PlaybackCommand::play, 0) == CastResult::ended,
              "terminal conversion prevents further commands");
        controller.stop();
        snapshot = controller.snapshot();
        check(probe->clears == 1 &&
                  snapshot.session.end_reason ==
                      (natural_end ? SessionEnd::media_end : SessionEnd::sender_stop),
              "later Stop preserves prior receiver end and cleanup count");
    }
    auto probe = std::make_shared<ConversionProbe>();
    {
        CastController controller(conversion_settings(probe),
                                  probed_source(std::make_shared<SourceProbe>()));
        controller.stop();
        controller.stop();
    }
    check(probe->clears == 1, "never-started cast clears its per-cast cache exactly once");
}
void conversion_stop_first_tests() {
    group = "user Stop before conversion failure";
    auto probe = std::make_shared<ConversionProbe>();
    std::atomic_bool entered{false};
    CastController* observed = nullptr; // Published before the async start begins.
    auto dependencies = stored_credentials(
        [&](const ReceiverEndpoint&,
            const ReceiverOperation& operation) -> std::unique_ptr<ReceiverStream> {
            entered = true;
            while (operation.cancelled && !operation.cancelled->load()) {
                operation.check();
                std::this_thread::sleep_for(1ms);
            }
            probe->failure = StereoAacHlsFailure::cache_error;
            check(eventually([&] {
                      return observed->snapshot().media_failure == StereoAacHlsFailure::cache_error;
                  }),
                  "supervisor observes the deliberately late conversion failure");
            throw TransportException(TransportError::cancelled);
        });
    scripted_conversion(dependencies, probe);
    CastController controller(conversion_settings(probe),
                              mp4_source(std::make_shared<SourceProbe>()), dependencies);
    observed = &controller;
    auto start = std::async(std::launch::async, [&] { return controller.start(); });
    check(eventually([&] { return entered.load(); }),
          "receiver start reached cancellable operation");
    controller.stop();
    check(start.get() == CastResult::cancelled && !controller.snapshot().media_failure_first &&
              probe->clears == 1,
          "late cache failure keeps the earlier user cancellation and cleanup once");
}

// Real compressed fixtures exercise the controller's production factory, HTTP
// delivery and terminal worker signal. The receiver remains scripted/loopback.
#ifdef SAP2_HAS_STEREO_AAC
unsigned fetch_resource(const std::string& playlist_url, const std::string& name) {
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    const auto slash = playlist_url.find('/', 7);
    const auto authority = playlist_url.substr(7, slash - 7);
    const auto colon = authority.rfind(':');
    const auto path = playlist_url.substr(slash, playlist_url.rfind('/') - slash + 1) + name;
    asio::io_context context;
    beast::tcp_stream stream(context);
    stream.expires_after(3s);
    stream.connect({asio::ip::make_address(authority.substr(0, colon)),
                    static_cast<std::uint16_t>(std::stoul(authority.substr(colon + 1)))});
    beast::http::request<beast::http::empty_body> request{beast::http::verb::get, path, 11};
    request.set(beast::http::field::host, authority);
    request.set(beast::http::field::connection, "close");
    beast::http::write(stream, request);
    beast::flat_buffer buffer;
    beast::http::response<beast::http::vector_body<std::uint8_t>> response;
    beast::http::read(stream, buffer, response);
    check(response.result_int() != 200 || !response.body().empty(),
          "successful AAC resource has bytes");
    return response.result_int();
}
void real_conversion_tests(const std::filesystem::path& fixtures) {
    group = "enabled production conversion";
    for (const auto* filename : {"dts.mp4", "dts.mkv", "aac.mp4"}) {
        auto probe = std::make_shared<ConversionProbe>();
        std::mutex mutex;
        std::map<std::size_t, Bytes> fragments;
        std::atomic_bool fail_reads{false};
        auto settings = conversion_settings(probe);
        settings.audio_track_id.reset();
        if (std::string_view(filename) == "dts.mp4") {
            settings.audio_track_id = 42;
        }
        settings.cache_quota_bytes = 16 * 1024 * 1024;
        settings.fragment_store.seal = [&](std::size_t index, const Bytes& bytes,
                                           const AacCacheContext& context) {
            check(!context.should_stop(), "seal sees live conversion control");
            std::lock_guard<std::mutex> lock(mutex);
            fragments.emplace(index, bytes);
        };
        settings.fragment_store.read_at = [&](std::size_t index, std::uint64_t offset,
                                              std::uint8_t* output, std::size_t capacity,
                                              const AacCacheContext& context) -> std::size_t {
            if (fail_reads || context.should_stop()) {
                return 0;
            }
            std::lock_guard<std::mutex> lock(mutex);
            const auto& bytes = fragments.at(index);
            const auto count = std::min<std::size_t>(
                {capacity, 509, bytes.size() - static_cast<std::size_t>(offset)});
            std::memcpy(output, bytes.data() + offset, count);
            return count;
        };
        settings.fragment_store.clear = [&] {
            std::lock_guard<std::mutex> lock(mutex);
            fragments.clear();
            ++probe->clears;
        };
        FakeReceiver receiver({});
        auto source =
            open_file_media_source(fixtures / filename, std::make_shared<FileReadStats>());
        CastController controller(settings, source, fake_receiver_dependencies(receiver));
        check(controller.start() == CastResult::ok,
              std::string(filename) + ": real selected decoder starts");
        const auto url = receiver.inserted_media_url();
        check(ends_with(url, "/main.m3u8") && controller.snapshot().selected_audio_id == 42,
              std::string(filename) + ": production composition and original ID");
        check(fetch_resource(url, "audio.m3u8") == 200 && fetch_resource(url, "a0.m4s") == 200,
              std::string(filename) + ": lazy AAC fragment reaches HTTP client");
        fail_reads = true;
        try {
            (void)fetch_resource(url, "a0.m4s");
        } catch (const boost::system::system_error&) { /* Supervisor may close the failing HTTP
                                                          request. */
        }
        check(eventually([&] { return controller.snapshot().media_cleaned_up; }),
              std::string(filename) + ": fatal sealed-cache read drains controller");
        const auto snapshot = controller.snapshot();
        check(snapshot.media_failure == StereoAacHlsFailure::cache_error &&
                  snapshot.media_failure_first && snapshot.session.cleaned_up &&
                  probe->clears == 1 && fragments.empty(),
              std::string(filename) + ": fixed cache failure and complete cleanup");
        controller.stop();
        check(probe->clears == 1, "later Stop does not clear cache twice");
    }
}
void real_selection_order_tests(const std::filesystem::path& decoder_fixtures) {
    group = "real AAC commentary and DTS source order";
    // Original D70 fixture has AAC commentary ID 7 before DTS main ID 42.
    // Reorder only contiguous trak boxes for the second case: payload offsets,
    // source IDs and total moov size stay fixed. No production parser authors input.
    const auto owned_fixture = decoder_fixtures / "dts-commentary-first.mp4";
    std::ifstream input(owned_fixture, std::ios::binary);
    Bytes original{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    check(!original.empty(), "owned AAC-commentary/DTS movie fixture exists");
    constexpr std::size_t box_header_bytes = 8;
    constexpr std::size_t box_type_offset = 4;
    const auto box_size = [&](std::size_t offset, std::size_t end) {
        if (offset > end || end - offset < box_header_bytes) {
            throw std::logic_error("fixture box header");
        }
        std::uint32_t size = 0;
        for (std::size_t byte = 0; byte < box_type_offset; ++byte) {
            size = (size << 8) | original[offset + byte];
        }
        if (size < box_header_bytes || size > end - offset) {
            throw std::logic_error("fixture box extent");
        }
        return static_cast<std::size_t>(size);
    };
    Bytes reversed = original;
    for (std::size_t offset = 0; offset < original.size();) {
        const auto size = box_size(offset, original.size());
        if (std::memcmp(original.data() + offset + box_type_offset, "moov", 4) == 0) {
            std::vector<std::pair<std::size_t, std::size_t>> tracks;
            for (auto child = offset + box_header_bytes; child < offset + size;) {
                const auto length = box_size(child, offset + size);
                if (std::memcmp(original.data() + child + box_type_offset, "trak", 4) == 0) {
                    tracks.emplace_back(child, length);
                }
                child += length;
            }
            if (tracks.size() != 3 || tracks[1].first + tracks[1].second != tracks[2].first) {
                throw std::logic_error("fixture audio track adjacency");
            }
            std::rotate(reversed.begin() + static_cast<std::ptrdiff_t>(tracks[1].first),
                        reversed.begin() + static_cast<std::ptrdiff_t>(tracks[2].first),
                        reversed.begin() +
                            static_cast<std::ptrdiff_t>(tracks[2].first + tracks[2].second));
            break;
        }
        offset += size;
    }
    for (const auto* bytes : {&original, &reversed}) {
        auto owner = std::make_shared<const Bytes>(*bytes);
        MediaSource source{[owner] { return static_cast<std::uint64_t>(owner->size()); },
                           [owner](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                                   const MediaReadContext& context) {
                               if (context.should_stop() || offset >= owner->size()) {
                                   return std::size_t{0};
                               }
                               const auto count = std::min<std::size_t>(
                                   capacity, owner->size() - static_cast<std::size_t>(offset));
                               std::memcpy(output, owner->data() + offset, count);
                               return count;
                           }};
        auto probe = std::make_shared<ConversionProbe>();
        auto settings = conversion_settings(probe);
        settings.audio_track_id.reset();
        settings.cache_quota_bytes = 16 * 1024 * 1024;
        {
            CastController ambiguous(
                settings, source, stored_credentials(failing_connector(TransportError::network)));
            check(ambiguous.start() == CastResult::audio_selection_required,
                  bytes == &original ? "AAC-before-DTS requires selection"
                                     : "DTS-before-AAC requires selection");
        }
        settings.audio_track_id = 42;
        FakeReceiver receiver({});
        CastController selected(settings, source, fake_receiver_dependencies(receiver));
        check(selected.start() == CastResult::ok && selected.snapshot().selected_audio_id == 42,
              bytes == &original ? "explicit DTS ID selects main after AAC commentary"
                                 : "explicit DTS ID selects main before AAC commentary");
        selected.stop();
        check(probe->clears == 2, "both real-file selection cases clean their own cast cache");
    }
}
void production_selection_tests() {
    group = "production selection before codec filtering";
    using namespace send_airplay2::test::mp4_fixture;
    auto commentary = audio_spec();
    commentary.source_id = 7;
    auto unsupported = audio_spec();
    unsupported.source_id = 42;
    // Literal unsupported fourcc; retain a valid container/sample-entry layout.
    constexpr std::size_t sample_entry_type_offset = 4; // After the 32-bit big-endian box size.
    std::memcpy(unsupported.entry.data() + sample_entry_type_offset, "zzzz", 4);
    auto video = video_spec();
    video.source_id = 11;
    for (const bool reversed : {false, true}) {
        const auto tracks = reversed ? std::vector<TrackSpec>{video, unsupported, commentary}
                                     : std::vector<TrackSpec>{video, commentary, unsupported};
        for (const auto& choice : std::vector<std::pair<std::optional<std::uint64_t>, CastResult>>{
                 {{}, CastResult::audio_selection_required},
                 {42, CastResult::media_unsupported},
                 {11, CastResult::audio_track_invalid},
                 {99, CastResult::audio_track_invalid}}) {
            auto probe = std::make_shared<ConversionProbe>();
            auto settings = conversion_settings(probe);
            settings.audio_track_id = choice.first;
            CastController controller(
                settings, mp4_source(std::make_shared<SourceProbe>(), tracks),
                stored_credentials(failing_connector(TransportError::network)));
            const auto result = controller.start();
            check(result == choice.second,
                  "source order=" + std::to_string(reversed) +
                      " ID=" + (choice.first ? std::to_string(*choice.first) : "auto") +
                      " result=" + std::to_string(static_cast<std::int32_t>(result)) +
                      " expected=" + std::to_string(static_cast<std::int32_t>(choice.second)));
            check(controller.snapshot().media_cleaned_up && probe->clears == 1,
                  "production selection refusal clears the host cache once");
        }
    }
}
#endif

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: cast_controller_tests MRP_FIXTURES HLS_FIXTURES DECODER_FIXTURES\n";
        return 2;
    }
    const std::string mrp_fixtures = argv[1];
    start_mapping_tests();
    command_mapping_tests();
    created_phase_tests();
    start_failure_tests();
    cancellation_tests();
    command_argument_tests();
    active_session_tests();
    natural_end_tests();
    connection_loss_tests();
    concurrent_stop_tests();
    mrp_command_tests(mrp_fixtures);
    mrp_end_tests(mrp_fixtures);
    start_position_tests(mrp_fixtures);
    hls_delivery_tests();
    url_controls_delivery_tests();
    url_controls_terminal_tests();
    conversion_start_failure_tests();
    conversion_terminal_tests();
    conversion_stop_first_tests();
#ifdef SAP2_HAS_STEREO_AAC
    real_conversion_tests(argv[2]);
    real_selection_order_tests(argv[3]);
    production_selection_tests();
#endif
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "cast_controller_tests passed\n";
    return 0;
}
