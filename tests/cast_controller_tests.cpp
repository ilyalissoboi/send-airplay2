// SPDX-License-Identifier: Apache-2.0
// CastController lifecycle and failure mapping with injected credentials and
// connectors. Loopback only: no receiver, credential store or private media.
#include "cast_controller.h"
#include "control_crypto.h"
#include "credential_store.h"
#include "mrp_session.h"
#include "receiver_http.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
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

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
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
        check(start.wait_for(0s) == std::future_status::ready,
              "stop returns only after the pending start");
        check(start.get() == CastResult::cancelled, "pending start is cancelled");
        check(stop_duration < 1500ms, "cancellation is prompt, not the request deadline");
        const auto snapshot = controller.snapshot();
        check(snapshot.phase == CastPhase::start_failed &&
                  snapshot.start_result == CastResult::cancelled,
              "cancelled start is recorded");
    }
    check(probe->releases == 1, "cancelled start releases the source once");
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

int main() {
    start_mapping_tests();
    command_mapping_tests();
    created_phase_tests();
    start_failure_tests();
    cancellation_tests();
    command_argument_tests();
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "cast_controller_tests passed\n";
    return 0;
}
