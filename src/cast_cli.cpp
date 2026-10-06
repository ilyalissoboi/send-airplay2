// SPDX-License-Identifier: Apache-2.0
#include "cast_cli.h"
#include "auth_cli.h"
#include "credential_store.h"
#include "file_media_source.h"
#include "pair_verify.h"
#include "send_airplay2/media_server.h"
#include "url_playback_session.h"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::uint32_t max_port = 65535;
constexpr std::uint32_t max_start_timeout_ms = 120000;
// The receiver keeps open-ended range requests while it buffers; use the media
// server's maximum per-request budget, as `serve` does.
constexpr std::uint32_t media_request_timeout_ms = 600000;
constexpr std::chrono::milliseconds state_poll{250};

struct CastArguments {
    std::string address;
    std::uint16_t port = 7000;
    std::string profile;
    std::string file;
    std::string content_type = "video/mp4";
    std::uint32_t start_timeout_ms = 30000;
    bool event_log = false; // Diagnostic: print value-free event outlines.
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
        } else if (option == "--content-type") {
            arguments.content_type = std::string(value);
        } else if (option == "--start-timeout-ms") {
            arguments.start_timeout_ms =
                parse_bounded(value, 1, max_start_timeout_ms, "start timeout");
        } else {
            throw std::invalid_argument("unknown option: " + std::string(option));
        }
    }
    if (arguments.address.empty() || arguments.profile.empty() || arguments.file.empty()) {
        throw std::invalid_argument("cast requires --address, --profile and --file");
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

/// Prints playback-state changes until stopped, from its own thread.
class StateReporter {
public:
    explicit StateReporter(UrlPlaybackSession& session)
        : thread_([this, &session] { report(session); }) {}
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
    static void print_event_log(UrlPlaybackSession& session) {
        for (const auto& entry : session.take_event_log()) {
            std::cout << "Event: " << entry << std::endl;
        }
    }
    void report(UrlPlaybackSession& session) {
        auto last = session.status();
        while (!done_) {
            print_event_log(session);
            const auto current = session.wait_for_change(last.playback_state, state_poll);
            if (current.playback_state != last.playback_state) {
                std::cout << "State: " << current.playback_state << std::endl;
            }
            if (current.failed && !last.failed) {
                std::cout << "A receiver connection failed. Press Enter to clean up." << std::endl;
            }
            last = current;
        }
        print_event_log(session);
    }

    std::atomic_bool done_{false};
    std::thread thread_;
};

void write_summary(const SessionStatus& session, const FileReadStats& reads) {
    std::cout << "Stopped. state="
              << (session.playback_state.empty() ? "none" : session.playback_state)
              << " events=" << session.events << " feedback=" << session.feedback_sent
              << " timing=" << session.timing_answered
              << " failed=" << (session.failed ? "yes" : "no") << " reads=" << reads.reads.load()
              << " bytes=" << reads.bytes.load() << " failed_reads=" << reads.failures.load();
    if (reads.reads.load() != 0) {
        std::cout << " span=[" << reads.lowest_offset.load() << ',' << reads.highest_end.load()
                  << ')';
    }
    std::cout << std::endl;
}

/// Starts media serving and the session, waits for the operator, then stops.
int cast(const CastArguments& arguments) {
    // A bad path is an argument error; opening a local file contacts nobody.
    auto reads = std::make_shared<FileReadStats>();
    auto source = open_file_media_source(arguments.file, reads);
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
    auto server = MediaServer::start(std::move(source), server_options);

    UrlPlaybackOptions options;
    options.receiver = {arguments.address, arguments.port, 0};
    options.media_url = server->url();
    options.start_timeout = std::chrono::milliseconds(arguments.start_timeout_ms);
    options.record_event_structure = arguments.event_log;
    std::cout << "Starting playback." << std::endl;
    auto session = UrlPlaybackSession::start(*credentials, std::move(options));
    std::cout << "State: " << session->status().playback_state << std::endl;
    std::cout << "Press Enter to stop." << std::endl;

    {
        StateReporter reporter(*session);
        std::string ignored;
        std::getline(std::cin, ignored);
    }
    session->stop();
    server->stop(); // Joins every read before the summary reads the statistics.
    write_summary(session->status(), *reads);
    return 0;
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
    } catch (const CredentialException& error) {
        std::cerr << "Credentials: " << describe_credential_error(error.reason()) << '\n';
    } catch (const SessionException& error) {
        std::cerr << "Session: " << session_message(error.reason());
        if (error.status() != 0) {
            std::cerr << " (status " << error.status() << ')';
        }
        std::cerr << ".\n";
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
