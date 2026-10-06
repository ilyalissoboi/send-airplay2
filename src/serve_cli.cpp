// SPDX-License-Identifier: Apache-2.0
#include "serve_cli.h"
#include "file_media_source.h"
#include "send_airplay2/media_server.h"

#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::uint32_t max_port = 65535;
constexpr std::uint32_t max_connections_limit = 16;
// A receiver may hold one open-ended range request for a long time while it
// buffers; the CLI therefore uses the server's maximum per-request budget.
constexpr std::uint32_t max_request_timeout_ms = 600000;

struct ServeArguments {
    MediaServerOptions server;
    std::string file;
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

ServeArguments parse_arguments(int argc, const char* const* argv) {
    ServeArguments arguments;
    arguments.server.request_timeout_ms = max_request_timeout_ms;
    bool has_address = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (index + 1 >= argc) {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
        const std::string_view value = argv[++index];
        if (option == "--address") {
            arguments.server.receiver_address = std::string(value);
            has_address = true;
        } else if (option == "--file") {
            arguments.file = std::string(value);
        } else if (option == "--port") {
            arguments.server.receiver_port =
                static_cast<std::uint16_t>(parse_bounded(value, 1, max_port, "port"));
        } else if (option == "--listen-port") {
            arguments.server.listen_port =
                static_cast<std::uint16_t>(parse_bounded(value, 0, max_port, "listen port"));
        } else if (option == "--timeout-ms") {
            arguments.server.request_timeout_ms =
                parse_bounded(value, 1, max_request_timeout_ms, "timeout");
        } else if (option == "--max-connections") {
            arguments.server.max_connections =
                parse_bounded(value, 1, max_connections_limit, "max connections");
        } else if (option == "--content-type") {
            arguments.server.content_type = std::string(value);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(option));
        }
    }
    if (!has_address || arguments.file.empty()) {
        throw std::invalid_argument("serve requires --address and --file");
    }
    return arguments;
}

/// Running server plus the statistics its file source updates. Destroying the
/// server stops it and joins all callbacks before the statistics owner is released.
struct ServingSession {
    std::shared_ptr<FileReadStats> stats;
    std::unique_ptr<MediaServer> server;
    std::uint64_t size = 0;
};

/// Opens the file and starts the server. Unusable paths, addresses or content types
/// throw std::invalid_argument; other setup failures throw std::runtime_error.
ServingSession start_serving(const ServeArguments& arguments) {
    ServingSession session;
    session.stats = std::make_shared<FileReadStats>();
    auto source = open_file_media_source(arguments.file, session.stats);
    session.size = source.size(); // Pure snapshot; start() reads the same value.
    session.server = MediaServer::start(std::move(source), arguments.server);
    return session;
}

void write_announcement(const ServeArguments& arguments, const ServingSession& session) {
    std::cout << "Serving " << session.size << " bytes as " << arguments.server.content_type
              << " to receiver " << arguments.server.receiver_address << " only.\n"
              << "Private URL (do not share or log): " << session.server->url() << '\n'
              << "Press Enter to stop." << std::endl;
}

/// Blocks until the operator enters a line or standard input reaches end-of-file.
void wait_for_stop_request() {
    std::string ignored;
    std::getline(std::cin, ignored);
}

void write_summary(const FileReadStats& stats) {
    const auto reads = stats.reads.load();
    std::cout << "Stopped. reads=" << reads << " bytes=" << stats.bytes.load()
              << " failed_reads=" << stats.failures.load();
    if (reads != 0) {
        // Half-open byte interval covered by the lowest and highest successful reads.
        std::cout << " span=[" << stats.lowest_offset.load() << ',' << stats.highest_end.load()
                  << ')';
    }
    std::cout << '\n';
}
} // namespace

int run_serve_cli(int argc, const char* const* argv) {
    ServeArguments arguments;
    try {
        arguments = parse_arguments(argc, argv);
    } catch (const std::invalid_argument& error) {
        std::cerr << "Arguments: " << error.what() << '\n';
        return 2;
    }

    ServingSession session;
    try {
        session = start_serving(arguments);
    } catch (const std::invalid_argument& error) {
        std::cerr << "Arguments: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Serve: " << error.what() << '\n';
        return 1;
    }

    write_announcement(arguments, session);
    wait_for_stop_request();
    // Stop joins every callback, so the summary reads final statistics.
    session.server->stop();
    write_summary(*session.stats);
    return 0;
}
} // namespace send_airplay2::detail
