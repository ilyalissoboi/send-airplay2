// SPDX-License-Identifier: Apache-2.0
// UrlPlaybackSession against a scripted fake receiver. Public synthetic
// identities and secrets only; the fake derives its keys from literal labels.
#include "url_playback_session.h"
#include "fake_receiver.h"
#include "binary_plist.h"
#include "control_crypto.h"
#include "control_records.h"
#include "identity_crypto.h"
#include "native_socket.h"
#include "ntp_timing.h"
#include "pairing_tlv.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using namespace send_airplay2::detail;
using namespace send_airplay2::detail::testing;
using namespace std::chrono_literals;

namespace {
int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}

// ---- Scenarios ----

constexpr const char* media_url = "http://127.0.0.1:49153/synthetic-token/media";

UrlPlaybackOptions options_for(FakeReceiver& receiver) {
    UrlPlaybackOptions options;
    options.enable_mrp = false; // Preserve coverage of the minimum G1 sequence.
    options.receiver = {"127.0.0.1", control_port, 0};
    options.media_url = media_url;
    options.request_timeout = 2000ms;
    options.start_timeout = 3000ms;
    options.start_confirmation_interval = 20ms; // Keep scripted scenarios fast.
    options.feedback_interval = 30ms;
    options.connect = receiver.connector();
    return options;
}

std::vector<std::string> sequence_without_feedback(const std::vector<ParsedRequest>& requests) {
    std::vector<std::string> sequence;
    for (const auto& request : requests) {
        if (request.target == "/feedback") {
            continue;
        }
        const bool rtsp_uri = request.target.rfind("rtsp://", 0) == 0;
        sequence.push_back(request.method + " " + (rtsp_uri ? "<uri>" : request.target) + " " +
                           request.protocol);
    }
    return sequence;
}

std::string command_type(const ParsedRequest& request) {
    const auto envelope = decode_binary_plist(request.body);
    return decode_binary_plist(envelope.find("params")->find("data")->as_data())
        .find("type")
        ->as_string();
}

/// Sends one timing request from loopback and returns whether a reply came.
bool timing_replies(std::uint16_t port) {
    // Empty on POSIX; on Windows it holds the Winsock reference for this socket.
    [[maybe_unused]] native::NetworkRuntime runtime;
    native::SocketOwner socket;
    const auto local = native::numeric_socket_address("127.0.0.1", 0, 0);
    socket.value = ::socket(local.family, SOCK_DGRAM, IPPROTO_UDP);
    if (socket.value == native::invalid_socket) {
        return false;
    }
    const auto target = native::numeric_socket_address("127.0.0.1", port, 0);
    std::array<std::uint8_t, ntp_timing::packet_size> request{};
    request[0] = 0x80;
    request[1] = ntp_timing::request_type;
    if (::sendto(socket.value, reinterpret_cast<const char*>(request.data()),
                 static_cast<int>(request.size()), 0,
                 reinterpret_cast<const sockaddr*>(&target.storage), target.size) < 0) {
        return false;
    }
    if (!native::ready(socket.value, false, 2000)) {
        return false;
    }
    std::array<std::uint8_t, 64> reply{};
    const auto received = ::recv(socket.value, reinterpret_cast<char*>(reply.data()),
                                 static_cast<int>(reply.size()), 0);
    return received == static_cast<int>(ntp_timing::packet_size) &&
           reply[1] == ntp_timing::response_type;
}

void happy_path_tests() {
    group = "start, state and stop";
    FakeReceiver receiver({});
    const auto credentials = receiver.credentials();
    auto session = UrlPlaybackSession::start(credentials, options_for(receiver));
    check(session->status().playback_state == "playing", "start returns once playing");

    const auto requests = receiver.requests();
    const std::vector<std::string> expected{"SETUP <uri> RTSP/1.0",   "GET /info RTSP/1.0",
                                            "RECORD <uri> RTSP/1.0",  "SETUP <uri> RTSP/1.0",
                                            "POST /command HTTP/1.1", "POST /command HTTP/1.1",
                                            "POST /command HTTP/1.1", "POST /command HTTP/1.1"};
    check(sequence_without_feedback(requests) == expected, "reference request order");

    const auto& base = requests.front();
    check(base.target.rfind("rtsp://127.0.0.1/", 0) == 0, "RTSP URI names the local address");
    check(!base.header("dacp-id").empty() &&
              base.header("client-instance") == base.header("dacp-id") &&
              !base.header("active-remote").empty() &&
              base.header("user-agent") == "AirPlay/550.10" &&
              base.header("content-type") == "application/x-apple-binary-plist",
          "RTSP session headers");
    const auto base_body = decode_binary_plist(base.body);
    const auto session_uuid = base_body.find("sessionUUID")->as_string();
    check(base_body.find("timingProtocol")->as_string() == "NTP", "base SETUP asks for NTP");
    const auto remote_requests = receiver.remote_control().requests();
    check(sequence_without_feedback(remote_requests) ==
              std::vector<std::string>{"SETUP <uri> RTSP/1.0"},
          "minimum remote session has only SETUP, without RECORD or data stream");
    const auto remote_body = decode_binary_plist(remote_requests.front().body);
    check(remote_body.find("isRemoteControlOnly")->as_boolean() &&
              remote_body.find("timingProtocol")->as_string() == "None" &&
              remote_body.find("timingPort") == nullptr,
          "remote session has no UDP timing");
    check(remote_body.find("sessionUUID")->as_string() != session_uuid &&
              remote_requests.front().header("cseq") == "3" && base.header("cseq") == "3",
          "remote and URL sessions have distinct UUIDs and independent CSeq");
    check(receiver.remote_control().event_channel_opened() &&
              !receiver.remote_control().control_was_closed(),
          "remote session remains open through URL start");
    receiver.remote_control().push_state("Paused");
    check(eventually([&] { return session->status().remote_events == 1; }),
          "remote event answered with its independent keys");
    check(session->status().playback_state == "playing" &&
              receiver.remote_control().event_replies().find("CSeq: 1\r\n") != std::string::npos,
          "remote event does not overwrite URL playback state");

    std::vector<std::string> commands;
    for (const auto& request : requests) {
        if (request.target != "/command") {
            continue;
        }
        commands.push_back(command_type(request));
        check(request.header("x-apple-streamid") == std::to_string(stream_id) &&
                  request.header("x-apple-session-id") == session_uuid &&
                  request.header("user-agent") == "AirPlay/870.14.1",
              "/command headers");
    }
    check(commands == std::vector<std::string>{"insertPlayQueueItem", "setProperty", "setProperty",
                                               "setRate"},
          "start commands in order");
    for (const auto& request : requests) {
        if (request.target == "/command" && command_type(request) == "insertPlayQueueItem") {
            const auto envelope = decode_binary_plist(request.body);
            // Keep the decoded command alive: find() points into it.
            const auto command =
                decode_binary_plist(envelope.find("params")->find("data")->as_data());
            check(command.find("item")->find("Content-Location")->as_string() == media_url,
                  "the media URL is the queued item");
        }
    }

    check(timing_replies(receiver.announced_timing_port()), "announced timing port answers");
    check(eventually([&] { return receiver.feedback_count() >= 2; }), "periodic /feedback");
    check(eventually([&] {
              const auto replies = receiver.event_replies();
              return replies.find("CSeq: 1\r\n") != std::string::npos &&
                     replies.find("CSeq: 2\r\n") != std::string::npos;
          }),
          "each event answered");

    receiver.push_state("Paused");
    const auto paused = session->wait_for_change("playing", 2000ms);
    check(paused.playback_state == "paused" && paused.events == 3, "later state changes");
    check(session->status().timing_answered >= 1, "timing counted");

    session->stop();
    check(receiver.control_was_closed() && receiver.event_was_closed(),
          "stop closes control and event connections");
    check(receiver.remote_control().control_was_closed() &&
              receiver.remote_control().event_was_closed(),
          "stop also joins and closes the remote session");
    check(receiver.control_close_order() == std::vector<std::string>{"URL", "remote"},
          "remote control remains open until URL control closes");
    session->stop(); // Idempotent.
    check(!session->status().failed, "a clean stop is not a failure");
}

template <typename Exception, typename Check>
void expect_start_failure(const std::string& scenario, Behavior behavior,
                          std::chrono::milliseconds start_timeout, Check matches) {
    FakeReceiver receiver(behavior);
    const auto credentials = receiver.credentials();
    auto options = options_for(receiver);
    options.start_timeout = start_timeout;
    try {
        (void)UrlPlaybackSession::start(credentials, std::move(options));
        check(false, scenario + ": start succeeded");
    } catch (const Exception& error) {
        check(matches(error), scenario + ": wrong category");
    }
    check(receiver.control_was_closed(), scenario + ": control closed");
    check(!receiver.event_channel_opened() || receiver.event_was_closed(),
          scenario + ": event channel closed");
    check(receiver.remote_control().control_was_closed() &&
              (!receiver.remote_control().event_channel_opened() ||
               receiver.remote_control().event_was_closed()),
          scenario + ": remote session closed after URL failure");
}

void remote_control_failure_tests() {
    group = "remote-control failures";
    Behavior rejected;
    rejected.remote_setup_status = 403;
    FakeReceiver receiver(rejected);
    const auto credentials = receiver.credentials();
    try {
        (void)UrlPlaybackSession::start(credentials, options_for(receiver));
        check(false, "remote SETUP rejection accepted");
    } catch (const SessionException& error) {
        check(error.reason() == SessionError::rejected && error.status() == 403,
              "remote SETUP preserves rejection status");
    }
    check(receiver.remote_control().control_was_closed() && receiver.requests().empty(),
          "rejected remote session closes before any URL requests");

    Behavior refused_events;
    refused_events.reject_remote_event_connect = true;
    FakeReceiver refused(refused_events);
    const auto refused_credentials = refused.credentials();
    try {
        (void)UrlPlaybackSession::start(refused_credentials, options_for(refused));
        check(false, "remote event connect failure accepted");
    } catch (const TransportException& error) {
        check(error.reason() == TransportError::network, "remote event connect failure category");
    }
    check(refused.remote_control().control_was_closed() && refused.requests().empty(),
          "remote event failure closes authenticated control before URL start");

    Behavior lost_events;
    lost_events.report_playing = false;
    lost_events.end_remote_events_on_rate = true;
    expect_start_failure<SessionException>("remote events end during URL start", lost_events,
                                           3000ms, [](const SessionException& error) {
                                               return error.reason() ==
                                                      SessionError::connection_lost;
                                           });
}

void failure_tests() {
    group = "start failures";
    Behavior rejected_setup;
    rejected_setup.base_setup_status = 500;
    expect_start_failure<SessionException>(
        "base SETUP rejected", rejected_setup, 3000ms, [](const SessionException& error) {
            return error.reason() == SessionError::rejected && error.status() == 500;
        });
    Behavior rejected_insert;
    rejected_insert.reject_command = 0;
    expect_start_failure<SessionException>(
        "insertPlayQueueItem rejected", rejected_insert, 3000ms, [](const SessionException& error) {
            return error.reason() == SessionError::rejected && error.status() == 400;
        });
    Behavior loading_only;
    loading_only.report_playing = false;
    expect_start_failure<SessionException>("never playing", loading_only, 300ms,
                                           [](const SessionException& error) {
                                               return error.reason() == SessionError::start_timeout;
                                           });
    Behavior lost_events;
    lost_events.end_events_on_rate = true;
    expect_start_failure<SessionException>(
        "event channel ends during start", lost_events, 3000ms, [](const SessionException& error) {
            return error.reason() == SessionError::connection_lost;
        });

    FakeReceiver receiver({});
    const auto credentials = receiver.credentials();
    std::atomic_bool cancelled{true};
    try {
        (void)UrlPlaybackSession::start(credentials, options_for(receiver), &cancelled);
        check(false, "cancelled start succeeded");
    } catch (const TransportException& error) {
        check(error.reason() == TransportError::cancelled, "cancelled start category");
    }
}

void startup_readiness_tests() {
    group = "stationary startup and bounded diagnostics";
    for (const bool pause_after_zero : {false, true}) {
        Behavior stationary;
        stationary.zero_rate_events = 1;
        stationary.pause_after_zero_rate = pause_after_zero;
        stationary.remain_stationary = !pause_after_zero;
        FakeReceiver paused(stationary);
        auto options = options_for(paused);
        options.start_timeout = 150ms;
        SessionStartDiagnostics failed;
        try {
            (void)UrlPlaybackSession::start(paused.credentials(), options, nullptr, &failed);
            check(false, "playing/rate zero then paused must not report startup success");
        } catch (const SessionException& error) {
            check(error.reason() == SessionError::start_timeout,
                  "stationary startup expires without a recovery Play");
        }
        check(failed.cleaned_up && paused.control_was_closed() && paused.event_was_closed() &&
                  paused.remote_control().control_was_closed(),
              "startup deadline preserves ordered cleanup before diagnostics return");
        const auto has_zero_rate = std::any_of(
            failed.entries.begin(), failed.entries.begin() + failed.count,
            [](const SessionStartTraceEntry& entry) {
                return entry.state == SessionStartState::playing && entry.playback_rate == 0.0;
            });
        check(has_zero_rate && failed.count != 0 &&
                  failed.entries[failed.count - 1].phase == SessionStartPhase::failed,
              "failed trace retains contradictory playing/rate-zero event and deadline phase");
        std::vector<std::string> commands;
        for (const auto& request : paused.requests()) {
            if (request.target == "/command") {
                commands.push_back(command_type(request));
            }
        }
        check(commands == std::vector<std::string>{"insertPlayQueueItem", "setProperty",
                                                   "setProperty", "setRate"},
              "startup pause does not trigger repeated queue commands or Play");
    }
}

void startup_confirmation_tests() {
    group = "positive startup interrupted during confirmation";
    FakeReceiver receiver({});
    auto options = options_for(receiver);
    options.start_timeout = 500ms;
    options.start_confirmation_interval = 250ms;
    auto pause = std::async(std::launch::async, [&] {
        const auto started = eventually([&] {
            const auto requests = receiver.requests();
            return std::any_of(requests.begin(), requests.end(), [](const ParsedRequest& request) {
                return request.target == "/command" && command_type(request) == "setRate";
            });
        });
        if (started) {
            std::this_thread::sleep_for(10ms);
            receiver.push_state("Paused");
        }
        return started;
    });
    SessionStartDiagnostics diagnostics;
    try {
        (void)UrlPlaybackSession::start(receiver.credentials(), options, nullptr, &diagnostics);
        check(false, "short playing transition must not bypass startup confirmation");
    } catch (const SessionException& error) {
        check(error.reason() == SessionError::start_timeout,
              "pause during confirmation retains the original deadline");
    }
    check(pause.get() && diagnostics.cleaned_up, "pause was sent after setRate and cleanup joined");
    check(std::any_of(diagnostics.entries.begin(), diagnostics.entries.begin() + diagnostics.count,
                      [](const SessionStartTraceEntry& entry) {
                          return entry.state == SessionStartState::paused;
                      }),
          "startup trace retains pause before ready");
}

void startup_confirmation_deadline_tests() {
    group = "startup confirmation budget";
    FakeReceiver receiver({});
    auto options = options_for(receiver);
    options.start_timeout = 60ms;
    options.start_confirmation_interval = 250ms;
    SessionStartDiagnostics diagnostics;
    try {
        (void)UrlPlaybackSession::start(receiver.credentials(), options, nullptr, &diagnostics);
        check(false, "confirmation must not extend a shorter startup deadline");
    } catch (const SessionException& error) {
        check(error.reason() == SessionError::start_timeout && diagnostics.cleaned_up,
              "healthy playing still times out when confirmation exceeds the remaining budget");
    }

    FakeReceiver invalid({});
    auto invalid_options = options_for(invalid);
    invalid_options.start_confirmation_interval = -1ms;
    diagnostics.count = 1; // A reused output must not retain previous startup records.
    try {
        (void)UrlPlaybackSession::start(invalid.credentials(), invalid_options, nullptr,
                                        &diagnostics);
        check(false, "negative confirmation interval accepted");
    } catch (const std::invalid_argument&) {
        check(diagnostics.count == 0 && invalid.requests().empty() &&
                  invalid.remote_control().requests().empty(),
              "invalid interval resets output and refuses startup before authentication/network "
              "work");
    }
}

void startup_confirmation_cancellation_tests() {
    group = "startup confirmation cancellation";
    FakeReceiver receiver({});
    auto options = options_for(receiver);
    options.start_timeout = 1000ms;
    options.start_confirmation_interval = 500ms;
    std::atomic_bool cancelled{false};
    auto cancel = std::async(std::launch::async, [&] {
        const auto command_observed = eventually([&] {
            const auto requests = receiver.requests();
            return std::any_of(requests.begin(), requests.end(), [](const ParsedRequest& request) {
                return request.target == "/command" && command_type(request) == "setRate";
            });
        });
        if (command_observed) {
            cancelled = true;
        }
        return command_observed;
    });
    SessionStartDiagnostics diagnostics;
    try {
        (void)UrlPlaybackSession::start(receiver.credentials(), options, &cancelled, &diagnostics);
        check(false, "cancellation during confirmation ignored");
    } catch (const TransportException& error) {
        check(error.reason() == TransportError::cancelled && diagnostics.cleaned_up,
              "cancelled confirmation tears down before rethrowing cancellation");
    }
    check(cancel.get(), "cancellation requested after startup setRate");
}

void startup_trace_tests() {
    group = "bounded startup diagnostics";
    for (const unsigned zero_events : {1u, 80u}) {
        Behavior progressing;
        progressing.zero_rate_events = zero_events;
        FakeReceiver receiver(progressing);
        SessionStartDiagnostics diagnostics;
        auto session = UrlPlaybackSession::start(receiver.credentials(), options_for(receiver),
                                                 nullptr, &diagnostics);
        check(session->status().playback_state == "playing",
              "positive rate after stationary events permits startup, event count " +
                  std::to_string(zero_events));
        check(diagnostics.count <= 64 && !diagnostics.cleaned_up,
              "startup trace bound and successful lifetime, event count " +
                  std::to_string(zero_events));
        if (zero_events == 80) {
            check(diagnostics.count == 64 && diagnostics.truncated,
                  "trace overflow drops new entries without suppressing the positive-rate event");
        } else {
            check(!diagnostics.truncated &&
                      diagnostics.entries[diagnostics.count - 1].phase == SessionStartPhase::ready,
                  "successful trace ends with ready");
            unsigned acknowledged_commands = 0;
            for (std::size_t index = 0; index < diagnostics.count; ++index) {
                if (diagnostics.entries[index].response_status == 200) {
                    ++acknowledged_commands;
                }
                if (index != 0) {
                    check(diagnostics.entries[index].elapsed_ms >=
                              diagnostics.entries[index - 1].elapsed_ms,
                          "startup trace timestamps ordered at index " + std::to_string(index));
                }
            }
            check(acknowledged_commands == 4,
                  "four command acknowledgements recorded independently");
        }
        session->stop();
    }
}

void event_log_tests() {
    group = "diagnostic event log";
    FakeReceiver receiver({});
    const auto credentials = receiver.credentials();
    auto options = options_for(receiver);
    options.record_event_structure = true;
    auto session = UrlPlaybackSession::start(credentials, std::move(options));
    const auto log = session->take_event_log();
    check(log.size() == 2 && log.back() == "URL type=playbackState state=playing "
                                           "keys=type,params,params.playbackState",
          "outlines of the start events");
    check(session->take_event_log().empty(), "taking the log empties it");
    receiver.push_event_body(encode_binary_plist(PlistDictionary{{"type", "private-type"},
                                                                 {"private-key", "private-value"}}),
                             "/private-token?secret=private-value");
    check(eventually([&] { return session->status().events == 3; }),
          "private synthetic notification processed");
    check(session->take_event_log() == std::vector<std::string>{"URL type=other keys=type"},
          "URL log excludes private target, type and key text");
    receiver.push_event_body(Bytes{0}, "/private-token");
    check(eventually([&] { return session->status().unreadable_events == 1; }),
          "malformed notification acknowledged without terminating session");
    check(session->take_event_log() == std::vector<std::string>{"URL unreadable=yes"} &&
              !session->status().failed,
          "malformed-body diagnostics exclude request target and retain channel health");
    receiver.push_state("PRIVATE-STATE\nforged-output");
    check(session->wait_for_change("playing", 1000ms).playback_state == "other",
          "URL status exposes a fixed label for unknown receiver state");
    session->stop();

    FakeReceiver quiet({});
    const auto quiet_credentials = quiet.credentials();
    auto quiet_session = UrlPlaybackSession::start(quiet_credentials, options_for(quiet));
    check(quiet_session->take_event_log().empty(), "no outlines unless enabled");
    quiet.remote_control().push_state("Stopped");
    check(eventually([&] { return quiet_session->status().remote_events == 1; }),
          "disabled remote diagnostics still acknowledge events");
    check(quiet_session->take_event_log().empty() &&
              quiet_session->status().end_reason == SessionEnd::none,
          "disabled remote diagnostics neither log nor terminate URL playback");
}

void remote_diagnostic_tests() {
    group = "bounded remote observations";
    FakeReceiver receiver({});
    auto options = options_for(receiver);
    options.record_event_structure = true;
    auto session = UrlPlaybackSession::start(receiver.credentials(), std::move(options));
    (void)session->take_event_log();
    auto& remote = receiver.remote_control();
    remote.push_state("Stopped");
    remote.push_event_body(Bytes{0xff});
    check(eventually([&] { return session->status().remote_events == 2; }),
          "valid and malformed remote bodies are answered and counted");
    const auto initial = session->take_event_log();
    check(initial.size() == 2 &&
              initial.front() == "remote type=playbackState state=stopped root_keys=type,params "
                                 "params_keys=playbackState value_keys=none" &&
              initial.back() == "remote unreadable=yes",
          "fixed remote outline and malformed observation");
    check(!session->status().failed && session->status().playback_state == "playing" &&
              session->status().end_reason == SessionEnd::none,
          "remote state and malformed diagnostics do not change URL lifecycle");
    for (int index = 0; index < 300; ++index) {
        remote.push_event_body(encode_binary_plist(PlistDictionary{
            {"type", "private-type"}, {"private-key", "private-url"}, {"reason", index}}));
    }
    check(eventually([&] { return session->status().remote_events == 302; }),
          "all 300 remote notifications consumed");
    const auto bounded = session->take_event_log();
    check(bounded.size() == 256 &&
              bounded.front() == "remote type=other state=none root_keys=type,reason "
                                 "root.reason=44 params_keys=none value_keys=none" &&
              bounded.back() == "remote type=other state=none root_keys=type,reason "
                                "root.reason=299 params_keys=none value_keys=none",
          "literal 256-record bound drops oldest notifications and omits peer text");
    session->stop();
    check(session->status().end_reason == SessionEnd::sender_stop && session->status().cleaned_up,
          "observation leaves explicit Stop and ordered cleanup intact");
}

void failure_after_start_tests() {
    group = "failure after start";
    FakeReceiver receiver({});
    const auto credentials = receiver.credentials();
    auto session = UrlPlaybackSession::start(credentials, options_for(receiver));
    receiver.end_event_channel();
    const auto status = session->wait_for_change("playing", 2000ms);
    check(status.failed, "a lost event channel marks the session failed");
    check(eventually([&] { return session->status().cleaned_up; }),
          "event loss automatically joins and closes workers without operator input");
    check(session->status().end_reason == SessionEnd::connection_lost,
          "event failure retains terminal reason");
    check(session->status().failure_channel == SessionFailureChannel::url_events &&
              session->status().failure_reason == SessionFailureReason::disconnected,
          "URL EOF preserves its fixed channel/category after cleanup");
    session->stop();
    check(receiver.control_was_closed(), "stop still closes the control connection");

    FakeReceiver remote_lost({});
    const auto remote_credentials = remote_lost.credentials();
    auto remote_session = UrlPlaybackSession::start(remote_credentials, options_for(remote_lost));
    remote_lost.remote_control().end_event_channel();
    check(remote_session->wait_for_change("playing", 2000ms).failed,
          "remote event loss after start marks the composite session failed");
    check(eventually([&] { return remote_session->status().cleaned_up; }),
          "remote loss automatically cleans both sessions");
    check(remote_session->status().failure_channel == SessionFailureChannel::remote_events &&
              remote_session->status().failure_reason == SessionFailureReason::disconnected,
          "remote event EOF is distinct from URL EOF");
    remote_session->stop();
    check(remote_lost.control_was_closed() && remote_lost.remote_control().control_was_closed(),
          "stop after remote failure closes both authenticated connections");
}
void terminal_event_tests() {
    group = "automatic terminal events";
    for (const auto* terminal : {"Ended", "Stopped", "Idle"}) {
        FakeReceiver receiver({});
        auto session = UrlPlaybackSession::start(receiver.credentials(), options_for(receiver));
        receiver.push_state("Paused");
        check(session->wait_for_change("playing", 1000ms).end_reason == SessionEnd::none,
              std::string(terminal) + ": ordinary pause remains active");
        receiver.push_state(terminal);
        check(eventually([&] { return session->status().cleaned_up; }),
              std::string(terminal) + ": terminal event cleans automatically");
        const auto expected =
            std::string(terminal) == "Ended" ? SessionEnd::media_end : SessionEnd::receiver_stop;
        check(session->status().end_reason == expected && !session->status().failed,
              std::string(terminal) + ": normal completion reason");
        check(receiver.control_close_order() == std::vector<std::string>{"URL", "remote"},
              std::string(terminal) + ": remote retained through URL closure");
        session->stop();
        check(session->status().end_reason == expected, "first terminal reason survives stop");
    }
}
void paused_connection_loss_tests() {
    group = "connection loss during ordinary pause";
    FakeReceiver receiver({});
    auto session = UrlPlaybackSession::start(receiver.credentials(), options_for(receiver));
    receiver.push_state("Paused");
    check(session->wait_for_change("playing", 1000ms).playback_state == "paused",
          "pause established before event socket closes");
    receiver.end_event_channel();
    check(eventually([&] { return session->status().cleaned_up; }),
          "paused connection loss automatically finishes cleanup");
    const auto status = session->status();
    check(status.failed && status.end_reason == SessionEnd::connection_lost &&
              status.failure_channel == SessionFailureChannel::url_events &&
              status.failure_reason == SessionFailureReason::disconnected,
          "paused socket EOF remains failure, rather than an inferred normal receiver stop");
}
void concurrent_stop_tests() {
    group = "concurrent stop";
    for (int cycle = 0; cycle < 10; ++cycle) {
        FakeReceiver receiver({});
        auto session = UrlPlaybackSession::start(receiver.credentials(), options_for(receiver));
        std::thread first([&] { session->stop(); });
        std::thread second([&] { session->stop(); });
        first.join();
        second.join();
        check(session->status().cleaned_up &&
                  session->status().end_reason == SessionEnd::sender_stop,
              "concurrent stops finish cycle " + std::to_string(cycle));
        check(receiver.control_close_order() == std::vector<std::string>{"URL", "remote"},
              "one cleanup owner cycle " + std::to_string(cycle));
    }
}
void feedback_deadline_tests() {
    group = "silent feedback deadline";
    FakeReceiver receiver({});
    auto options = options_for(receiver);
    options.request_timeout = 80ms;
    // Deliberately span a feedback interval plus its failure deadline: startup
    // must stay healthy until the established-session fault is armed.
    options.start_confirmation_interval = 120ms;
    auto session = UrlPlaybackSession::start(receiver.credentials(), options);
    // Feedback starts before the URL commands and repeats every 30 ms, so an
    // answer normally precedes start()'s return. A loaded CI runner can delay
    // the feedback thread past it, so wait for one healthy answer rather than
    // assume it; the fault must still be armed only after that answer.
    check(eventually([&] { return receiver.feedback_count() > 0; }),
          "healthy feedback was answered before the fault was armed");
    const auto started = std::chrono::steady_clock::now();
    const auto answered_feedback = receiver.silence_feedback();
    check(eventually([&] { return session->status().cleaned_up; }),
          "silent established control peer triggers automatic cleanup");
    check(session->status().failed && session->status().end_reason == SessionEnd::connection_lost &&
              std::chrono::steady_clock::now() - started < 500ms,
          "80 ms request deadline bounds failure and joined cleanup to 500 ms");
    check(receiver.control_close_order() == std::vector<std::string>{"URL", "remote"},
          "deadline cleanup preserves closure order");
    check(session->status().failure_channel == SessionFailureChannel::url_feedback &&
              session->status().failure_reason == SessionFailureReason::timeout,
          "silent URL feedback preserves deadline failure through cleanup cancellation");
    check(receiver.feedback_count() > answered_feedback,
          "a post-start feedback request reached the armed silent peer");
}
void feedback_cancel_tests() {
    group = "stop during pending feedback";
    FakeReceiver receiver({});
    auto session = UrlPlaybackSession::start(receiver.credentials(), options_for(receiver));
    const auto answered_feedback = receiver.silence_feedback();
    check(eventually([&] { return receiver.feedback_count() > answered_feedback; }),
          "feedback request in flight before stop");
    const auto started = std::chrono::steady_clock::now();
    session->stop();
    check(session->status().cleaned_up && !session->status().failed &&
              std::chrono::steady_clock::now() - started < 500ms,
          "stop cancels blocked feedback rather than waiting its two-second deadline");
    check(session->status().failure_channel == SessionFailureChannel::none &&
              session->status().failure_reason == SessionFailureReason::none,
          "intentional cleanup cancellation does not invent a failure diagnostic");
}
void mrp_setup_failure_tests() {
    group = "MRP setup cleanup";
    FakeReceiver receiver({});
    const auto credentials = receiver.credentials();
    auto options = options_for(receiver);
    options.enable_mrp = true;
    try {
        (void)UrlPlaybackSession::start(credentials, options);
        check(false, "data SETUP without dataPort accepted");
    } catch (const TransportException& error) {
        check(error.reason() == TransportError::invalid_message, "missing dataPort category");
    }
    const auto requests = receiver.remote_control().requests();
    check(sequence_without_feedback(requests) == std::vector<std::string>{"SETUP <uri> RTSP/1.0",
                                                                          "RECORD <uri> RTSP/1.0",
                                                                          "SETUP <uri> RTSP/1.0"},
          "MRP remote SETUP/events, RECORD, then data SETUP");
    const auto data_setup = decode_binary_plist(requests.back().body);
    const auto& stream = data_setup.find("streams")->as_array().front();
    check(stream.find("controlType")->as_integer() == 2 &&
              stream.find("wantsDedicatedSocket")->as_boolean(),
          "MRP owns a dedicated type-130 controlType-2 stream");
    check(std::all_of(requests.begin(), requests.end(),
                      [&](const ParsedRequest& request) {
                          return request.header("dacp-id") == requests.front().header("dacp-id");
                      }),
          "remote sender identity stable across setup and record");
    check(receiver.remote_control().control_was_closed() &&
              receiver.remote_control().event_was_closed() && receiver.requests().empty(),
          "MRP start failure closes remote and prevents URL start");
}
} // namespace

int main() {
    try {
        happy_path_tests();
        failure_tests();
        startup_readiness_tests();
        startup_confirmation_tests();
        startup_confirmation_deadline_tests();
        startup_confirmation_cancellation_tests();
        startup_trace_tests();
        remote_control_failure_tests();
        event_log_tests();
        remote_diagnostic_tests();
        failure_after_start_tests();
        mrp_setup_failure_tests();
        terminal_event_tests();
        paused_connection_loss_tests();
        concurrent_stop_tests();
        feedback_deadline_tests();
        feedback_cancel_tests();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected test exception [" << group << "]: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " URL playback session test failure(s)\n";
        return 1;
    }
    std::cout << "URL playback session tests passed\n";
    return 0;
}
