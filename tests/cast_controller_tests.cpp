// SPDX-License-Identifier: Apache-2.0
// CastController lifecycle and failure mapping with injected credentials and
// connectors, plus active sessions against the scripted fake receiver.
// Loopback only: no receiver, credential store or private media.
#include "cast_controller.h"
#include "control_crypto.h"
#include "fake_receiver.h"
#include "mp4_fixture.h"
#include "credential_store.h"
#include "mrp_session.h"
#include "receiver_http.h"

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
MediaSource mp4_source(const std::shared_ptr<SourceProbe>& probe) {
    using namespace send_airplay2::test::mp4_fixture;
    const auto file = std::make_shared<const Bytes>(mp4_file({video_spec(), audio_spec()}));
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

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: cast_controller_tests MRP_FIXTURE_DIRECTORY\n";
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
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "cast_controller_tests passed\n";
    return 0;
}
