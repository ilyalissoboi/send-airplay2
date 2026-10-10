// SPDX-License-Identifier: Apache-2.0
// UrlPlaybackSession against a scripted fake receiver. Public synthetic
// identities and secrets only; the fake derives its keys from literal labels.
#include "url_playback_session.h"
#include "url_playback_tracker.h"
#include "fake_receiver.h"
#include "binary_plist.h"
#include "control_crypto.h"
#include "control_records.h"
#include "identity_crypto.h"
#include "mrp_session.h"
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
    auto strict = options_for(receiver);
    strict.url_controls_fallback = false; // The rejection stays fatal (D62 opt-out).
    try {
        (void)UrlPlaybackSession::start(credentials, std::move(strict));
        check(false, "remote SETUP rejection accepted without the URL controls fallback");
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

// ---- URL controls (D62): receivers that refuse remote control ----

/// macOS answers the remote-control SETUP with 500 after pair-verify succeeds.
constexpr unsigned macos_remote_setup_status = 500;
/// The test media's duration, as the recorded MacBook Pro reported it.
constexpr double url_item_duration_seconds = 131.567;

PlistValue cm_time_seconds(double seconds) {
    return PlistDictionary{{"value", static_cast<std::int64_t>(seconds * 1000 + 0.5)},
                           {"timescale", 1000},
                           {"flags", 1},
                           {"epoch", 0}};
}

Bytes enveloped(PlistValue inner) {
    return encode_binary_plist(
        PlistDictionary{{"params", PlistDictionary{{"data", encode_binary_plist(inner)}}}});
}

/// A playbackState event shaped like the MacBook Pro's: params carry the state,
/// position, duration and rate.
Bytes macos_state_event(const std::string& state, double position_seconds, double rate) {
    return enveloped(PlistDictionary{
        {"type", "playbackState"},
        {"params", PlistDictionary{{"playbackState", state},
                                   {"position", cm_time_seconds(position_seconds)},
                                   {"duration", cm_time_seconds(url_item_duration_seconds)},
                                   {"rate", rate}}}});
}

/// A notification with a root position, as macOS sends after a time jump and
/// at the natural end, just before "stopped".
Bytes macos_position_notification(double position_seconds) {
    return enveloped(PlistDictionary{{"type", "notification"},
                                     {"name", "synthetic-notification"},
                                     {"position", cm_time_seconds(position_seconds)}});
}

/// The decoded commands sent after the four start commands, in order.
std::vector<PlistValue> commands_after_start(const FakeReceiver& receiver) {
    constexpr std::size_t start_commands = 4;
    std::vector<PlistValue> commands;
    std::size_t index = 0;
    for (const auto& request : receiver.requests()) {
        if (request.target != "/command" || index++ < start_commands) {
            continue;
        }
        const auto envelope = decode_binary_plist(request.body);
        commands.push_back(decode_binary_plist(envelope.find("params")->find("data")->as_data()));
    }
    return commands;
}

UrlPlaybackOptions url_controls_options(FakeReceiver& receiver) {
    auto options = options_for(receiver);
    options.enable_mrp = true; // The production path: the rejection precedes any data stream.
    return options;
}

void url_playback_tracker_tests() {
    group = "URL playback tracker (D62)";
    const auto start = std::chrono::steady_clock::time_point{} + 1000s;
    UrlPlaybackTracker tracker;
    check(!tracker.status(start).owned && !tracker.status(start).position_seconds,
          "nothing known before an event");

    SessionEvent loading;
    loading.type = "playbackState";
    loading.playback_state = "loading";
    tracker.observe(loading, start);
    check(!tracker.status(start).owned, "loading does not establish the item");

    SessionEvent playing;
    playing.type = "playbackState";
    playing.playback_state = "playing";
    playing.position_seconds = 10.0;
    playing.duration_seconds = url_item_duration_seconds;
    playing.playback_rate = 1.0;
    tracker.observe(playing, start);
    const auto two_seconds_later = tracker.status(start + 2s);
    check(two_seconds_later.owned && two_seconds_later.position_seconds == 12.0 &&
              two_seconds_later.reported_position_seconds == 10.0 &&
              two_seconds_later.duration_seconds == url_item_duration_seconds &&
              two_seconds_later.messages == 2,
          "playing extrapolates rate x elapsed from the reported position");

    SessionEvent paused;
    paused.type = "playbackState";
    paused.playback_state = "paused";
    paused.playback_rate = 0.0;
    tracker.observe(paused, start + 3s);
    check(tracker.status(start + 30s).position_seconds == 13.0 &&
              tracker.status(start + 30s).reported_position_seconds == 10.0,
          "a pause without a position keeps where play had reached");

    SessionEvent jump;
    jump.type = "notification";
    jump.position_seconds = 0.0;
    tracker.observe(jump, start + 31s);
    check(tracker.status(start + 40s).position_seconds == 0.0, "a time jump to zero while paused");

    SessionEvent resumed;
    resumed.type = "playbackState";
    resumed.playback_state = "playing";
    resumed.playback_rate = 1.0;
    resumed.position_seconds = 131.0;
    tracker.observe(resumed, start + 41s);
    check(tracker.status(start + 500s).position_seconds == url_item_duration_seconds,
          "extrapolation is clamped to the duration");

    tracker.observe(paused, start + 46s);
    const auto paused_late = tracker.status(start + 46s);
    check(!paused_late.at_end && !paused_late.near_end,
          "extrapolation alone never reaches the end: reported 131 s is 0.567 s short");

    SessionEvent stopped_near;
    stopped_near.type = "playbackState";
    stopped_near.playback_state = "stopped";
    stopped_near.position_seconds = url_item_duration_seconds - 0.4;
    tracker.observe(stopped_near, start + 47s);
    const auto stopped_short = tracker.status(start + 47s);
    check(stopped_short.near_end && !stopped_short.at_end,
          "stopped within 0.5 s of the duration is near the end");

    SessionEvent final_position;
    final_position.type = "notification";
    final_position.position_seconds = url_item_duration_seconds;
    tracker.observe(final_position, start + 48s);
    check(tracker.status(start + 48s).at_end, "stopped at the duration is at the end");
}

void url_controls_start_tests() {
    group = "URL controls start (D62)";
    Behavior macos;
    macos.remote_setup_status = macos_remote_setup_status;
    FakeReceiver receiver(macos);
    const auto credentials = receiver.credentials();
    SessionStartDiagnostics diagnostics;
    auto session = UrlPlaybackSession::start(credentials, url_controls_options(receiver), nullptr,
                                             &diagnostics);
    const auto status = session->status();
    check(status.url_controls && status.playback_state == "playing",
          "start succeeds with URL controls after the remote SETUP is rejected");
    check(receiver.remote_control().control_was_closed() &&
              !receiver.remote_control().event_channel_opened() &&
              sequence_without_feedback(receiver.remote_control().requests()) ==
                  std::vector<std::string>{"SETUP <uri> RTSP/1.0"},
          "the rejected remote session is closed after its SETUP alone");
    const std::vector<std::string> expected{"SETUP <uri> RTSP/1.0",   "GET /info RTSP/1.0",
                                            "RECORD <uri> RTSP/1.0",  "SETUP <uri> RTSP/1.0",
                                            "POST /command HTTP/1.1", "POST /command HTTP/1.1",
                                            "POST /command HTTP/1.1", "POST /command HTTP/1.1"};
    check(sequence_without_feedback(receiver.requests()) == expected,
          "the URL session keeps the reference request order");
    const auto rejection =
        std::find_if(diagnostics.entries.begin(), diagnostics.entries.begin() + diagnostics.count,
                     [](const SessionStartTraceEntry& entry) {
                         return entry.phase == SessionStartPhase::connecting &&
                                entry.response_status == macos_remote_setup_status;
                     });
    check(rejection != diagnostics.entries.begin() + diagnostics.count,
          "the start trace records the rejected remote SETUP status");
    session->stop();
    check(!session->status().failed && receiver.control_was_closed() && receiver.event_was_closed(),
          "a clean stop closes the URL session");
}

void url_controls_command_tests() {
    group = "URL controls commands (D62)";
    Behavior macos;
    macos.remote_setup_status = macos_remote_setup_status;
    FakeReceiver receiver(macos);
    const auto credentials = receiver.credentials();
    auto session = UrlPlaybackSession::start(credentials, url_controls_options(receiver));
    const auto session_uuid =
        decode_binary_plist(receiver.requests().front().body).find("sessionUUID")->as_string();

    session->command(PlaybackCommand::pause);
    session->command(PlaybackCommand::play);
    session->command(PlaybackCommand::seek, 12.5);
    session->command(PlaybackCommand::stop);
    const auto commands = commands_after_start(receiver);
    check(commands.size() == 4, "one /command per control, got " + std::to_string(commands.size()));
    if (commands.size() == 4) {
        check(commands[0].find("type")->as_string() == "setRate" &&
                  commands[0].find("rate")->as_real() == 0.0,
              "pause is setRate 0");
        check(commands[1].find("type")->as_string() == "setRate" &&
                  commands[1].find("rate")->as_real() == 1.0,
              "play is setRate 1");
        const auto* time = commands[2].find("time");
        check(commands[2].find("type")->as_string() == "seek" && time &&
                  time->find("value")->as_integer() == 12500 &&
                  time->find("timescale")->as_integer() == 1000 &&
                  time->find("flags")->as_integer() == 1 && time->find("epoch")->as_integer() == 0,
              "seek 12.5 s is CMTime 12500/1000");
        check(commands[2].find("kind")->as_string() == "request" &&
                  commands[2].find("messageID")->as_integer() == 1,
              "the first seek is request messageID 1");
        std::string queued_uuid;
        for (const auto& request : receiver.requests()) {
            if (request.target == "/command" && command_type(request) == "insertPlayQueueItem") {
                const auto envelope = decode_binary_plist(request.body);
                const auto insert =
                    decode_binary_plist(envelope.find("params")->find("data")->as_data());
                queued_uuid = insert.find("item")->find("uuid")->as_string();
            }
        }
        const auto* seek_item = commands[2].find("item");
        check(!queued_uuid.empty() && seek_item &&
                  seek_item->find("uuid")->as_string() == queued_uuid,
              "the seek names the queued item by its UUID");
        check(commands[3].find("type")->as_string() == "stop", "stop is the stop command");
    }
    for (const auto& request : receiver.requests()) {
        if (request.target == "/command") {
            check(request.header("x-apple-streamid") == std::to_string(stream_id) &&
                      request.header("x-apple-session-id") == session_uuid,
                  "URL controls use the URL control stream headers");
        }
    }
    check(receiver.remote_control().requests().size() == 1,
          "no control reaches the closed remote session");
    try {
        session->command(PlaybackCommand::seek, -1);
        check(false, "negative seek accepted");
    } catch (const std::invalid_argument&) {
    }
    session->stop();
    try {
        session->command(PlaybackCommand::pause);
        check(false, "command after stop accepted");
    } catch (const MrpException& error) {
        check(error.reason() == MrpError::cancelled, "command after stop is cancelled");
    }

    Behavior rejecting = macos;
    rejecting.reject_command = 4; // The first command after the four start commands.
    FakeReceiver rejecting_receiver(rejecting);
    const auto rejecting_credentials = rejecting_receiver.credentials();
    auto rejected_session =
        UrlPlaybackSession::start(rejecting_credentials, url_controls_options(rejecting_receiver));
    try {
        rejected_session->command(PlaybackCommand::pause);
        check(false, "rejected URL command accepted");
    } catch (const MrpException& error) {
        check(error.reason() == MrpError::rejected, "a non-2xx /command is a rejected control");
    }
    check(!rejected_session->status().failed, "a rejected control does not end the session");
}

void url_controls_progress_tests() {
    group = "URL controls progress and end (D62)";
    Behavior macos;
    macos.remote_setup_status = macos_remote_setup_status;
    FakeReceiver receiver(macos);
    const auto credentials = receiver.credentials();
    auto session = UrlPlaybackSession::start(credentials, url_controls_options(receiver));
    receiver.push_event_body(macos_state_event("Playing", 120.0, 1.0));
    check(eventually([&] {
              const auto playback = session->playback_status();
              return playback.reported_position_seconds == 120.0;
          }),
          "the reported position follows URL events");
    const auto playback = session->playback_status();
    check(playback.owned && playback.state == "playing" &&
              playback.duration_seconds == url_item_duration_seconds && playback.position_seconds &&
              *playback.position_seconds >= 120.0,
          "status shows our playing item with its duration");

    // The recorded natural end: final position, then "stopped".
    receiver.push_event_body(macos_position_notification(url_item_duration_seconds));
    receiver.push_state("Stopped");
    check(eventually([&] { return session->status().end_reason == SessionEnd::media_end; }),
          "the final position before stopped is the media end");
    session->stop();
    check(session->status().end_reason == SessionEnd::media_end && !session->status().failed,
          "the media end is retained after stop");

    FakeReceiver stopped_receiver(macos);
    const auto stopped_credentials = stopped_receiver.credentials();
    auto stopped =
        UrlPlaybackSession::start(stopped_credentials, url_controls_options(stopped_receiver));
    stopped_receiver.push_event_body(macos_state_event("Playing", 40.0, 1.0));
    stopped_receiver.push_state("Stopped");
    check(eventually([&] { return stopped->status().end_reason == SessionEnd::receiver_stop; }),
          "a mid-item stop is a receiver stop");
}

void url_controls_start_position_tests() {
    group = "URL controls start position (D62)";
    Behavior macos;
    macos.remote_setup_status = macos_remote_setup_status;
    FakeReceiver receiver(macos);
    const auto credentials = receiver.credentials();
    auto options = url_controls_options(receiver);
    options.start_position_seconds = 30;
    auto session = UrlPlaybackSession::start(credentials, std::move(options));
    session->command(PlaybackCommand::seek, 45);
    const auto commands = commands_after_start(receiver);
    check(commands.size() == 2 && commands[0].find("type")->as_string() == "seek" &&
              commands[0].find("time")->find("value")->as_integer() == 30000,
          "the start position is applied by one URL seek before start returns");
    check(commands.size() == 2 && commands[0].find("messageID")->as_integer() == 1 &&
              commands[1].find("messageID")->as_integer() == 2,
          "each seek request gets the next messageID");
    session->stop();
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
/// tvOS reports URL "stopped" at the end of HLS with its last MRP position a
/// frame or two short of the duration (D60); a mid-item stop stays a stop.
void stopped_near_end_tests(const std::string& mrp_fixtures) {
    group = "URL stopped near the end";
    struct Case {
        const char* scenario;
        double mrp_elapsed_seconds; // Fake item duration: 131.6 s.
        SessionEnd expected;
    };
    const Case cases[] = {
        {"stopped 0.1 s before the duration", 131.5, SessionEnd::media_end},
        {"stopped 0.5 s before the duration", 131.1, SessionEnd::media_end},
        {"stopped 1 s before the duration", 130.6, SessionEnd::receiver_stop},
        {"stopped mid-item", 17.0, SessionEnd::receiver_stop},
    };
    for (const auto& test : cases) {
        Behavior behavior;
        behavior.mrp_fixtures = mrp_fixtures;
        FakeReceiver receiver(behavior);
        auto options = options_for(receiver);
        options.enable_mrp = true;
        auto session = UrlPlaybackSession::start(receiver.credentials(), options);
        check(receiver.remote_control().push_mrp_state(mrp_state_paused, test.mrp_elapsed_seconds),
              std::string(test.scenario) + ": MRP position pushed");
        check(eventually([&] {
                  const auto playback = session->playback_status();
                  return playback.reported_position_seconds &&
                         *playback.reported_position_seconds == test.mrp_elapsed_seconds;
              }),
              std::string(test.scenario) + ": MRP position received");
        check(session->status().end_reason == SessionEnd::none,
              std::string(test.scenario) + ": a pause alone does not end the session");
        receiver.push_state("Stopped");
        check(eventually([&] { return session->status().cleaned_up; }),
              std::string(test.scenario) + ": URL stop cleans automatically");
        check(session->status().end_reason == test.expected && !session->status().failed,
              std::string(test.scenario) + ": end reason");
        session->stop();
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

/// Observed control-connection close order, for failure messages.
std::string close_order_text(const FakeReceiver& receiver) {
    std::string text;
    for (const auto& session : receiver.control_close_order()) {
        text += (text.empty() ? "" : ",") + session;
    }
    return "[" + text + "]";
}

/// Remote control must outlive the URL session even when stop finds a remote
/// /feedback request in flight: cleanup waits for it rather than cancelling it,
/// because an interrupted request closes its connection.
void remote_feedback_stop_tests(const std::string& mrp_fixtures) {
    group = "stop with remote feedback in flight";
    Behavior behavior;
    behavior.mrp_fixtures = mrp_fixtures;
    {
        FakeReceiver receiver(behavior);
        auto options = options_for(receiver);
        options.enable_mrp = true;
        auto session = UrlPlaybackSession::start(receiver.credentials(), options);
        auto& remote = receiver.remote_control();
        (void)remote.hold_feedback();
        check(eventually([&] { return remote.feedback_held(); }),
              "a remote feedback request is held in flight");
        auto stopping = std::async(std::launch::async, [&] { session->stop(); });
        check(stopping.wait_for(100ms) == std::future_status::timeout &&
                  !receiver.control_was_closed() && !remote.control_was_closed(),
              "stop waits for the in-flight remote request without closing either session");
        check(remote.release_held_feedback(), "the receiver answers the held request");
        check(stopping.wait_for(2s) == std::future_status::ready,
              "stop finishes once the receiver answers");
        check(receiver.control_close_order() == std::vector<std::string>{"URL", "remote"},
              "remote control closes after the URL session (closed " + close_order_text(receiver) +
                  ")");
        const auto status = session->status();
        check(status.cleaned_up && !status.failed && status.end_reason == SessionEnd::sender_stop,
              "a waited-for remote request ends as an ordinary sender stop");
    }
    {
        FakeReceiver receiver(behavior);
        auto options = options_for(receiver);
        options.enable_mrp = true;
        options.request_timeout = 300ms;
        auto session = UrlPlaybackSession::start(receiver.credentials(), options);
        auto& remote = receiver.remote_control();
        (void)remote.hold_feedback();
        check(eventually([&] { return remote.feedback_held(); }),
              "an unanswered remote feedback request is in flight");
        const auto started = std::chrono::steady_clock::now();
        session->stop();
        check(std::chrono::steady_clock::now() - started < 1500ms,
              "a silent receiver delays stop by at most the 300 ms request deadline");
        const auto status = session->status();
        check(status.cleaned_up && !status.failed && status.end_reason == SessionEnd::sender_stop,
              "an unanswered request during stop is not reported as a session failure");
    }
}

/// The receiver's power report (MRP DEVICE_INFO logicalDeviceCount) arrives
/// with the MRP handshake, before the play queue item is inserted, so a start
/// policy can act on it before playback begins.
void startup_power_report_tests(const std::string& mrp_fixtures) {
    group = "startup receiver power report";
    Behavior behavior;
    behavior.mrp_fixtures = mrp_fixtures;
    behavior.mrp_logical_devices = 0;
    FakeReceiver receiver(behavior);
    auto options = options_for(receiver);
    options.enable_mrp = true;
    SessionStartDiagnostics diagnostics;
    auto session =
        UrlPlaybackSession::start(receiver.credentials(), options, nullptr, &diagnostics);
    session->stop();
    check(diagnostics.power_count == 1 && diagnostics.power[0].logical_devices == 0,
          "handshake logicalDeviceCount 0 is in the start trace (count " +
              std::to_string(diagnostics.power_count) + ")");
    const auto recorded = diagnostics.entries.begin() + diagnostics.count;
    const auto insert = std::find_if(diagnostics.entries.begin(), recorded,
                                     [](const SessionStartTraceEntry& entry) {
                                         return entry.phase == SessionStartPhase::insert_item;
                                     });
    check(insert != recorded && diagnostics.power_count == 1 &&
              diagnostics.power[0].elapsed_ms <= insert->elapsed_ms,
          "the power report precedes insert_item");

    Behavior without_count;
    without_count.mrp_fixtures = mrp_fixtures;
    FakeReceiver silent(without_count);
    auto silent_options = options_for(silent);
    silent_options.enable_mrp = true;
    SessionStartDiagnostics none;
    auto quiet = UrlPlaybackSession::start(silent.credentials(), silent_options, nullptr, &none);
    quiet->stop();
    check(none.power_count == 0, "DEVICE_INFO without logicalDeviceCount reports nothing");
}

/// Index of the first trace entry in `phase`, or the record count when absent.
std::size_t first_phase(const SessionStartDiagnostics& diagnostics, SessionStartPhase phase) {
    for (std::size_t index = 0; index < diagnostics.count; ++index) {
        if (diagnostics.entries[index].phase == phase) {
            return index;
        }
    }
    return diagnostics.count;
}

/// D56: a receiver whose handshake reports no logical devices is woken and
/// given time to settle before the play queue item is inserted.
void wake_before_play_tests(const std::string& mrp_fixtures) {
    group = "wake before play";
    {
        Behavior behavior;
        behavior.mrp_fixtures = mrp_fixtures;
        behavior.mrp_logical_devices = 0;
        behavior.mrp_wake_reports_awake = true;
        FakeReceiver receiver(behavior);
        auto options = options_for(receiver);
        options.enable_mrp = true;
        options.wake_settle = 100ms;
        SessionStartDiagnostics diagnostics;
        auto session =
            UrlPlaybackSession::start(receiver.credentials(), options, nullptr, &diagnostics);
        session->stop();
        const auto& remote = receiver.remote_control();
        const auto waking = first_phase(diagnostics, SessionStartPhase::waking);
        const auto insert = first_phase(diagnostics, SessionStartPhase::insert_item);
        check(remote.mrp_wakes() == 1, "one WAKE_DEVICE for a sleeping receiver");
        check(waking < insert && insert < diagnostics.count &&
                  diagnostics.entries[insert].elapsed_ms - diagnostics.entries[waking].elapsed_ms >=
                      100,
              "insert_item waits at least the 100 ms settle after waking (waking at " +
                  std::to_string(waking) + ", insert at " + std::to_string(insert) + ")");
        check(diagnostics.power_count == 2 && diagnostics.power[0].logical_devices == 0 &&
                  diagnostics.power[1].logical_devices == 1,
              "trace shows asleep then awake");
    }
    {
        Behavior behavior;
        behavior.mrp_fixtures = mrp_fixtures;
        behavior.mrp_logical_devices = 0; // Never reports awake.
        FakeReceiver receiver(behavior);
        auto options = options_for(receiver);
        options.enable_mrp = true;
        options.wake_timeout = 150ms;
        SessionStartDiagnostics diagnostics;
        const auto started = std::chrono::steady_clock::now();
        auto session =
            UrlPlaybackSession::start(receiver.credentials(), options, nullptr, &diagnostics);
        session->stop();
        check(receiver.remote_control().mrp_wakes() == 1 &&
                  first_phase(diagnostics, SessionStartPhase::insert_item) < diagnostics.count,
              "a receiver that never reports awake still gets the play request");
        check(std::chrono::steady_clock::now() - started < 5s, "the wake wait is bounded");
    }
    for (const bool awake_report : {true, false}) {
        Behavior behavior;
        behavior.mrp_fixtures = mrp_fixtures;
        if (awake_report) {
            behavior.mrp_logical_devices = 1;
        }
        FakeReceiver receiver(behavior);
        auto options = options_for(receiver);
        options.enable_mrp = true;
        SessionStartDiagnostics diagnostics;
        auto session =
            UrlPlaybackSession::start(receiver.credentials(), options, nullptr, &diagnostics);
        session->stop();
        check(receiver.remote_control().mrp_wakes() == 0 &&
                  first_phase(diagnostics, SessionStartPhase::waking) == diagnostics.count,
              std::string(awake_report ? "an awake receiver" : "a receiver without a report") +
                  " is not woken");
    }
    {
        Behavior behavior;
        behavior.mrp_fixtures = mrp_fixtures;
        behavior.mrp_logical_devices = 0;
        FakeReceiver receiver(behavior);
        auto options = options_for(receiver);
        options.enable_mrp = true;
        options.wake_receiver = false;
        auto session = UrlPlaybackSession::start(receiver.credentials(), options);
        session->stop();
        check(receiver.remote_control().mrp_wakes() == 0, "wake_receiver=false sends no wake");
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: url_playback_session_tests MRP_FIXTURE_DIRECTORY\n";
        return 2;
    }
    const std::string mrp_fixtures = argv[1];
    try {
        happy_path_tests();
        failure_tests();
        startup_readiness_tests();
        startup_confirmation_tests();
        startup_confirmation_deadline_tests();
        startup_confirmation_cancellation_tests();
        startup_trace_tests();
        remote_control_failure_tests();
        url_playback_tracker_tests();
        url_controls_start_tests();
        url_controls_command_tests();
        url_controls_progress_tests();
        url_controls_start_position_tests();
        event_log_tests();
        remote_diagnostic_tests();
        failure_after_start_tests();
        mrp_setup_failure_tests();
        terminal_event_tests();
        paused_connection_loss_tests();
        concurrent_stop_tests();
        feedback_deadline_tests();
        feedback_cancel_tests();
        remote_feedback_stop_tests(mrp_fixtures);
        stopped_near_end_tests(mrp_fixtures);
        startup_power_report_tests(mrp_fixtures);
        wake_before_play_tests(mrp_fixtures);
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
