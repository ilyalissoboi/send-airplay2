// SPDX-License-Identifier: Apache-2.0
#include "cast_cli.h"
#include "cli_input.h"
#include "auth_cli.h"
#include "credential_store.h"
#include "file_media_source.h"
#include "hls_directory.h"
#include "hls_remux.h"
#include "pair_verify.h"
#include "send_airplay2/media_server.h"
#include "url_playback_session.h"
#include "mrp_session.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <exception>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::uint32_t max_port = 65535;
constexpr std::uint32_t max_start_timeout_ms = 120000;
constexpr std::uint32_t max_media_connections = 16; // MediaServer's documented bound.
// The receiver retains read-ahead ranges while requesting smaller playback ranges.
// Use the bounded budget validated with native controls and lifecycle checks (D36).
constexpr std::uint32_t default_cast_media_connections = 16;
// The receiver keeps open-ended range requests while it buffers; use the media
// server's maximum per-request budget, as `serve` does.
constexpr std::uint32_t media_request_timeout_ms = 600000;
constexpr std::chrono::milliseconds state_poll{250};
constexpr std::uint32_t max_start_position_seconds = 24 * 3600;

struct CastArguments {
    std::string address;
    std::uint16_t port = 7000;
    std::string profile;
    std::string file;
    std::string hls;    // Development: a pre-made HLS playlist instead of --file (D60).
    bool remux = false; // Serve --file as HLS built by the remux (D60).
    std::string content_type = "video/mp4";
    std::uint32_t start_timeout_ms = 30000;
    std::uint32_t media_connections = default_cast_media_connections;
    bool event_log = false;                   // Diagnostic: print value-free event outlines.
    bool media_log = false;                   // Diagnostic: bounded HTTP completion facts.
    bool minimal_remote = false;              // Comparison only: remote SETUP/events without MRP.
    std::uint32_t start_position_seconds = 0; // Development: the queue item's start.
};

std::uint32_t parse_bounded(std::string_view value, std::uint32_t minimum, std::uint32_t maximum,
                            const char* name) {
    std::uint32_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed < minimum ||
        parsed > maximum) {
        throw std::invalid_argument(std::string(name) + " must be " + std::to_string(minimum) +
                                    ".." + std::to_string(maximum));
    }
    return parsed;
}

CastArguments parse_arguments(int argc, const char* const* argv) {
    CastArguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--event-log") {
            arguments.event_log = true;
            continue;
        }
        if (option == "--media-log") {
            arguments.media_log = true;
            continue;
        }
        if (option == "--minimal-remote") {
            arguments.minimal_remote = true;
            continue;
        }
        if (option == "--remux") {
            arguments.remux = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
        const std::string_view value = argv[++index];
        if (option == "--address") {
            arguments.address = std::string(value);
        } else if (option == "--port") {
            arguments.port = static_cast<std::uint16_t>(parse_bounded(value, 1, max_port, "port"));
        } else if (option == "--profile") {
            arguments.profile = std::string(value);
        } else if (option == "--file") {
            arguments.file = std::string(value);
        } else if (option == "--hls") {
            arguments.hls = std::string(value);
        } else if (option == "--content-type") {
            arguments.content_type = std::string(value);
        } else if (option == "--start-timeout-ms") {
            arguments.start_timeout_ms =
                parse_bounded(value, 1, max_start_timeout_ms, "start timeout");
        } else if (option == "--media-connections") {
            arguments.media_connections =
                parse_bounded(value, 1, max_media_connections, "media connections");
        } else if (option == "--start-position") {
            arguments.start_position_seconds =
                parse_bounded(value, 0, max_start_position_seconds, "start position");
        } else {
            throw std::invalid_argument("unknown option: " + std::string(option));
        }
    }
    if (arguments.address.empty() || arguments.profile.empty() ||
        (arguments.file.empty() && arguments.hls.empty())) {
        throw std::invalid_argument("cast requires --address, --profile and --file or --hls");
    }
    if (!arguments.file.empty() && !arguments.hls.empty()) {
        throw std::invalid_argument("cast takes either --file or --hls, not both");
    }
    if (arguments.remux && arguments.file.empty()) {
        throw std::invalid_argument("--remux applies to --file");
    }
    return arguments;
}

const char* session_message(SessionError error) {
    switch (error) {
    case SessionError::rejected:
        return "The receiver rejected a session request";
    case SessionError::start_timeout:
        return "The receiver did not report playback before the start deadline";
    case SessionError::connection_lost:
        return "A receiver connection failed during start";
    }
    return "The session failed";
}

const char* media_end_name(MediaRequestEnd end) {
    switch (end) {
    case MediaRequestEnd::complete:
        return "complete";
    case MediaRequestEnd::cancelled:
        return "cancelled";
    case MediaRequestEnd::timeout:
        return "timeout";
    case MediaRequestEnd::io_error:
        return "io_error";
    case MediaRequestEnd::source_error:
        return "source_error";
    case MediaRequestEnd::internal_error:
        return "internal_error";
    }
    return "internal_error";
}

void print_media_log(MediaServer& server) {
    for (const auto& entry : server.take_request_log()) {
        const auto* method = entry.method == MediaRequestMethod::get    ? "get"
                             : entry.method == MediaRequestMethod::head ? "head"
                                                                        : "other";
        std::ostringstream line;
        line << "Media: id=" << entry.request_id << " method=" << method
             << " status=" << entry.status << " offset=" << entry.offset
             << " declared=" << entry.declared_length << " expected=" << entry.expected_body_bytes
             << " written=" << entry.body_bytes_written
             << " header=" << (entry.header_completed ? "yes" : "no")
             << " active=" << entry.active_on_accept << " accepted_ms=" << entry.accepted_ms
             << " last_write_ms=" << entry.last_body_write_ms << " closed_ms=" << entry.closed_ms
             << " end=" << media_end_name(entry.end) << '\n';
        std::cout << line.str() << std::flush;
    }
}

/// Fixed startup phases/statuses and finite scalars only, including failed starts.
void print_start_diagnostics(const SessionStartDiagnostics& diagnostics) {
    for (std::size_t index = 0; index < diagnostics.count; ++index) {
        const auto& entry = diagnostics.entries[index];
        std::ostringstream line;
        line << "Startup: elapsed_ms=" << entry.elapsed_ms
             << " phase=" << session_start_phase_name(entry.phase)
             << " state=" << session_start_state_name(entry.state)
             << " status=" << entry.response_status << " rate=";
        if (entry.playback_rate) {
            line << std::scientific << std::setprecision(6) << *entry.playback_rate;
        } else {
            line << "unknown";
        }
        std::cout << line.str() << '\n';
    }
    for (std::size_t index = 0; index < diagnostics.power_count; ++index) {
        const auto& entry = diagnostics.power[index];
        std::cout << "Receiver power: elapsed_ms=" << entry.elapsed_ms
                  << " logical_devices=" << entry.logical_devices << '\n';
    }
    std::cout << "Startup summary: records=" << diagnostics.count
              << " truncated=" << (diagnostics.truncated ? "yes" : "no")
              << " cleaned=" << (diagnostics.cleaned_up ? "yes" : "no") << std::endl;
}

void print_event_log(UrlPlaybackSession& session) {
    for (const auto& entry : session.take_event_log()) {
        std::cout << (entry.rfind("buffer ", 0) == 0 ? "Buffer: " : "Event: ") + entry + "\n"
                  << std::flush;
    }
}

/// Retained MRP scalars after its worker is joined, not a fresh receiver query.
/// Report the received position separately from the estimated status position.
/// With URL controls (D62) the same scalars come from URL events instead.
void print_final_mrp(UrlPlaybackSession& session, bool enabled) {
    const auto status = session.playback_status();
    const auto state = status.state.empty() ? "unknown" : status.state;
    std::ostringstream line;
    line.imbue(std::locale::classic());
    line << (session.status().url_controls ? "Final URL playback:" : "Final MRP:")
         << " enabled=" << (enabled ? "yes" : "no") << " owned=" << (status.owned ? "yes" : "no")
         << " state=" << state << " at_end=" << (status.at_end ? "yes" : "no");
    const auto scalar = [&line](const char* key, std::optional<double> value) {
        line << ' ' << key << '=';
        if (value && std::isfinite(*value)) {
            line << std::scientific << std::setprecision(6) << *value;
        } else {
            line << "unknown";
        }
    };
    scalar("reported_position", status.reported_position_seconds);
    scalar("duration", status.duration_seconds);
    scalar("rate", status.playback_rate);
    line << " mrp_messages=" << status.messages << " heartbeats=" << status.heartbeats;
    std::cout << line.str() << std::endl;
}

/// Prints playback-state changes and drained diagnostics from its own thread.
class StateReporter {
public:
    StateReporter(UrlPlaybackSession& session, MediaServer& server)
        : thread_([this, &session, &server] { report(session, server); }) {}
    ~StateReporter() {
        stop();
    }
    StateReporter(const StateReporter&) = delete;
    StateReporter& operator=(const StateReporter&) = delete;
    StateReporter(StateReporter&&) = delete;
    StateReporter& operator=(StateReporter&&) = delete;
    void stop() {
        done_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void report(UrlPlaybackSession& session, MediaServer& server) {
        auto last = session.status();
        while (!done_) {
            print_event_log(session);
            print_media_log(server);
            const auto current = session.wait_for_change(last.playback_state, state_poll);
            if (current.playback_state != last.playback_state) {
                std::cout << "State: " << current.playback_state << std::endl;
            }
            if (current.failed && !last.failed) {
                std::cout << "A receiver connection failed; cleaning up." << std::endl;
                std::cout << "Failure: channel="
                          << session_failure_channel_name(current.failure_channel)
                          << " reason=" << session_failure_reason_name(current.failure_reason)
                          << std::endl;
            }
            last = current;
        }
        print_event_log(session);
        print_media_log(server);
    }

    std::atomic_bool done_{false};
    std::thread thread_;
};

void write_summary(const SessionStatus& session, const FileReadStats& reads) {
    std::cout << "Stopped. state="
              << (session.playback_state.empty() ? "none" : session.playback_state)
              << " events=" << session.events << " remote_events=" << session.remote_events
              << " feedback=" << session.feedback_sent << " timing=" << session.timing_answered
              << " failed=" << (session.failed ? "yes" : "no") << " reads=" << reads.reads.load()
              << " bytes=" << reads.bytes.load() << " failed_reads=" << reads.failures.load();
    if (reads.highest_end.load() != 0) { // HLS files do not record one span.
        std::cout << " span=[" << reads.lowest_offset.load() << ',' << reads.highest_end.load()
                  << ')';
    }
    std::cout << " end=" << session_end_name(session.end_reason)
              << " cleaned=" << (session.cleaned_up ? "yes" : "no")
              << " failure_channel=" << session_failure_channel_name(session.failure_channel)
              << " failure_reason=" << session_failure_reason_name(session.failure_reason)
              << std::endl;
}

void print_playback_status(UrlPlaybackSession& session) {
    const auto status = session.playback_status();
    std::cout << "Playback: owned=" << (status.owned ? "yes" : "no")
              << " state=" << (status.state.empty() ? "unknown" : status.state) << " position=";
    if (status.position_seconds) {
        std::cout << std::fixed << std::setprecision(1) << *status.position_seconds;
    } else {
        std::cout << "unknown";
    }
    std::cout << " duration=";
    if (status.duration_seconds) {
        std::cout << std::fixed << std::setprecision(1) << *status.duration_seconds;
    } else {
        std::cout << "unknown";
    }
    std::cout << " mrp_messages=" << status.messages << " heartbeats=" << status.heartbeats
              << std::endl;
}

/// Fixed diagnostics only. A failed stop still tears down both sessions.
bool control_loop(UrlPlaybackSession& session) {
    bool success = true;
    CliInput input;
    for (;;) {
        const auto current = session.status();
        if (current.end_reason != SessionEnd::none || current.failed) {
            return success && !current.failed;
        }
        const auto next = input.poll(std::chrono::milliseconds{20});
        if (next.kind == CliInputKind::waiting) {
            continue;
        }
        if (next.kind == CliInputKind::eof) {
            break;
        }
        if (next.kind == CliInputKind::invalid) {
            std::cout << "Use status, pause, play, seek SECONDS, stop or Enter." << std::endl;
            continue;
        }
        const auto& line = next.line;
        if (line.empty()) {
            break;
        }
        if (line == "status") {
            print_playback_status(session);
            continue;
        }
        const bool stopping = line == "stop";
        try {
            if (line == "pause") {
                session.command(PlaybackCommand::pause);
            } else if (line == "play" || line == "resume") {
                session.command(PlaybackCommand::play);
            } else if (stopping) {
                session.command(PlaybackCommand::stop);
            } else if (line.rfind("seek ", 0) == 0 && line.size() < 64) {
                double position = 0;
                const auto result =
                    std::from_chars(line.data() + 5, line.data() + line.size(), position);
                if (result.ec != std::errc{} || result.ptr != line.data() + line.size() ||
                    !std::isfinite(position) || position < 0) {
                    throw std::invalid_argument("Invalid seek time");
                }
                session.command(PlaybackCommand::seek, position);
            } else {
                throw std::invalid_argument("Unknown control");
            }
            std::cout << "Control: accepted" << std::endl;
        } catch (const MrpException& error) {
            std::cout << "Control: " << error.what() << std::endl;
            if (stopping) {
                success = false;
            }
        } catch (const std::invalid_argument&) {
            std::cout << "Use status, pause, play, seek SECONDS, stop or Enter." << std::endl;
        }
        if (stopping) {
            break;
        }
    }
    return success;
}

/// Starts media serving and the session, waits for the operator, then stops.
int cast(const CastArguments& arguments) {
    // A bad path is an argument error; opening local files contacts nobody.
    auto reads = std::make_shared<FileReadStats>();
    std::optional<MediaSource> file;
    std::optional<HlsDirectory> hls;
    if (arguments.remux) {
        auto remuxed = remux_to_hls(open_file_media_source(arguments.file, reads));
        std::cout << "HLS remux: segments=" << remuxed.segment_count
                  << " target_duration=" << remuxed.target_duration_seconds << std::endl;
        hls = HlsDirectory{std::move(remuxed.playlist_name), std::move(remuxed.resources)};
    } else if (arguments.hls.empty()) {
        file = open_file_media_source(arguments.file, reads);
    } else {
        hls = open_hls_directory(arguments.hls, reads);
        std::cout << "HLS: files=" << hls->resources.size() << std::endl;
    }
    // No network work without a trusted profile.
    auto store = native_credential_store();
    const auto credentials = store->load(arguments.profile);
    if (!credentials) {
        std::cerr << "Credentials: no stored profile with that name; pair first.\n";
        return 1;
    }

    MediaServerOptions server_options;
    server_options.receiver_address = arguments.address;
    server_options.receiver_port = arguments.port;
    server_options.request_timeout_ms = media_request_timeout_ms;
    server_options.content_type = arguments.content_type;
    server_options.record_request_diagnostics = arguments.media_log;
    server_options.max_connections = arguments.media_connections;
    auto server = hls ? MediaServer::start_resource_set(std::move(hls->resources), server_options)
                      : MediaServer::start(std::move(*file), server_options);

    UrlPlaybackOptions options;
    options.receiver = {arguments.address, arguments.port, 0};
    options.media_url = hls ? server->resource_url(hls->playlist_name) : server->url();
    options.start_timeout = std::chrono::milliseconds(arguments.start_timeout_ms);
    options.start_position_seconds = arguments.start_position_seconds;
    options.record_event_structure = arguments.event_log;
    options.enable_mrp = !arguments.minimal_remote;
    std::cout << "Starting playback." << std::endl;
    SessionStartDiagnostics diagnostics;
    std::unique_ptr<UrlPlaybackSession> session;
    try {
        session = UrlPlaybackSession::start(*credentials, std::move(options), nullptr,
                                            arguments.event_log ? &diagnostics : nullptr);
    } catch (...) {
        // start() has already joined URL/remote cleanup. Drain media writes only
        // after that cleanup, including when a startup pause reaches its deadline.
        server->stop();
        if (arguments.event_log) {
            print_start_diagnostics(diagnostics);
        }
        print_media_log(*server);
        throw;
    }
    if (arguments.event_log) {
        print_start_diagnostics(diagnostics);
    }
    std::cout << "State: " << session->status().playback_state << std::endl;
    if (session->status().url_controls) {
        std::cout << "Receiver refused remote control; using URL controls (D62)." << std::endl;
    }
    if (arguments.minimal_remote) {
        std::cout << "Minimal remote comparison: MRP controls unavailable. Press Enter to stop."
                  << std::endl;
    } else {
        std::cout << "Controls: status, pause, play, seek SECONDS, stop. Press Enter to stop."
                  << std::endl;
    }

    bool controls_ok = true;
    {
        StateReporter reporter(*session, *server);
        controls_ok = control_loop(*session);
    }
    session->stop();
    server->stop();            // Joins every read before the summary reads the statistics.
    print_event_log(*session); // Include notifications received during final cleanup.
    if (arguments.event_log) {
        print_final_mrp(*session, !arguments.minimal_remote);
    }
    print_media_log(*server);
    write_summary(session->status(), *reads);
    return controls_ok && !session->status().failed ? 0 : 1;
}
} // namespace

int run_cast_cli(int argc, const char* const* argv) {
    CastArguments arguments;
    try {
        arguments = parse_arguments(argc, argv);
    } catch (const std::invalid_argument& error) {
        std::cerr << "Arguments: " << error.what() << '\n';
        return 2;
    }
    try {
        return cast(arguments);
    } catch (const std::invalid_argument& error) {
        std::cerr << "Arguments: " << error.what() << '\n';
        return 2;
    } catch (const RemuxException& error) {
        std::cerr << "Remux (" << remux_failure_name(error.reason()) << "): " << error.what()
                  << '\n';
        return 2;
    } catch (const CredentialException& error) {
        std::cerr << "Credentials: " << describe_credential_error(error.reason()) << '\n';
    } catch (const SessionException& error) {
        std::cerr << "Session: " << session_message(error.reason());
        if (error.status() != 0) {
            std::cerr << " (status " << error.status() << ')';
        }
        std::cerr << ".\n";
    } catch (const MrpException& error) {
        std::cerr << "MRP: " << error.what() << ".\n";
    } catch (const TransportException& error) {
        std::cerr << "Receiver: " << error.what() << '\n';
    } catch (const PairVerifyException& error) {
        std::cerr << "Peer verification: " << error.what() << '\n';
    } catch (const ControlException& error) {
        std::cerr << "Authentication: " << error.what() << '\n';
    } catch (const std::exception&) {
        std::cerr << "Cast failed due to a host/backend error.\n";
    }
    return 1;
}
} // namespace send_airplay2::detail
