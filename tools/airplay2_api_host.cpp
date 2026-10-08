// SPDX-License-Identifier: Apache-2.0
// Development host for the public C playback interface (docs/public-api.md).
//
// Every library call goes through send_airplay2/playback.h only, so this is
// the same boundary a C# or JNI host sees. Stdin polling reuses the CLI's
// host-side helper. Output is fixed fields only: never the receiver address,
// profile, media URL, file path, PIN or receiver-provided text.
#include "send_airplay2/pairing.h"
#include "send_airplay2/playback.h"
#include "cli_input.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
using namespace std::chrono_literals;
using send_airplay2::detail::CliInput;
using send_airplay2::detail::CliInputKind;

constexpr int exit_success = 0;
constexpr int exit_failure = 1;
constexpr int exit_arguments = 2;
// Matches the cast CLI's input poll and keeps natural-end detection prompt.
constexpr std::chrono::milliseconds input_poll{20};
// Bounded so the watcher notices its stop flag promptly.
constexpr std::uint32_t watch_timeout_ms = 250;

const char* usage =
    "Usage: airplay2-api-host --address IP --profile NAME --file PATH [--port 7000]\n"
    "         [--content-type TYPE] [--start-timeout-ms N] [--media-connections N]\n"
    "         [--start-position SECONDS] [--cancel-after-ms N | --cycles N [--hold-ms N]]\n"
    "Interactive (default): status, pause, play, seek SECONDS, stop; Enter stops locally.\n"
    "--cancel-after-ms: call sap2_cast_stop() N ms into a blocking start.\n"
    "--cycles: N casts in one process, alternating MRP stop and local stop.\n"
    "       airplay2-api-host --pair --address IP --profile NAME [--port 7000]\n"
    "--pair: sap2_pair() into the built-in store; the PIN is typed hidden in this\n"
    "        process's console window (Windows).\n";

struct HostArguments {
    std::string address;
    std::uint16_t port = SAP2_DEFAULT_RECEIVER_PORT;
    std::string profile;
    std::string file;
    std::optional<std::string> content_type;
    std::uint32_t start_timeout_ms = SAP2_DEFAULT_START_TIMEOUT_MS;
    std::uint32_t media_connections = SAP2_DEFAULT_MEDIA_CONNECTIONS;
    double start_position_seconds = 0;
    std::optional<std::uint32_t> cancel_after_ms;
    std::optional<std::uint32_t> cycles;
    std::uint32_t hold_ms = 5000; // Playing time per cycle before stopping.
    bool pair = false;            // Pair a new profile instead of casting.
};

std::uint32_t parse_unsigned(std::string_view value, const char* name) {
    std::uint32_t parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
        throw std::invalid_argument(std::string(name) + " must be an unsigned integer");
    }
    return parsed;
}

double parse_seconds(std::string_view value) {
    double parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        !std::isfinite(parsed) || parsed < 0) {
        throw std::invalid_argument("seconds must be a finite number >= 0");
    }
    return parsed;
}

/// Syntax only. Option ranges are left to sap2_cast_create(), which is what
/// this host exercises; its SAP2_ERROR_INVALID_ARGUMENT also exits 2.
HostArguments parse_arguments(int argc, const char* const* argv) {
    HostArguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (option == "--pair") {
            arguments.pair = true;
            continue;
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
        const std::string_view value = argv[++index];
        if (option == "--address") {
            arguments.address = std::string(value);
        } else if (option == "--port") {
            const auto port = parse_unsigned(value, "port");
            if (port > UINT16_MAX) {
                throw std::invalid_argument("port must be 1..65535");
            }
            arguments.port = static_cast<std::uint16_t>(port);
        } else if (option == "--profile") {
            arguments.profile = std::string(value);
        } else if (option == "--file") {
            arguments.file = std::string(value);
        } else if (option == "--content-type") {
            arguments.content_type = std::string(value);
        } else if (option == "--start-timeout-ms") {
            arguments.start_timeout_ms = parse_unsigned(value, "start timeout");
        } else if (option == "--media-connections") {
            arguments.media_connections = parse_unsigned(value, "media connections");
        } else if (option == "--start-position") {
            arguments.start_position_seconds = parse_seconds(value);
        } else if (option == "--cancel-after-ms") {
            arguments.cancel_after_ms = parse_unsigned(value, "cancel delay");
        } else if (option == "--cycles") {
            arguments.cycles = parse_unsigned(value, "cycles");
        } else if (option == "--hold-ms") {
            arguments.hold_ms = parse_unsigned(value, "hold time");
        } else {
            throw std::invalid_argument("unknown option: " + std::string(option));
        }
    }
    if (arguments.pair) {
        if (arguments.address.empty() || arguments.profile.empty() || !arguments.file.empty() ||
            arguments.cancel_after_ms || arguments.cycles) {
            throw std::invalid_argument("--pair requires --address and --profile only");
        }
        return arguments;
    }
    if (arguments.address.empty() || arguments.profile.empty() || arguments.file.empty()) {
        throw std::invalid_argument("requires --address, --profile and --file");
    }
    if (arguments.cancel_after_ms && arguments.cycles) {
        throw std::invalid_argument("--cancel-after-ms and --cycles are exclusive");
    }
    if (arguments.cycles && *arguments.cycles == 0) {
        throw std::invalid_argument("cycles must be at least 1");
    }
    return arguments;
}

/**
 * One regular file served through sap2_media_source. Reads arrive on up to
 * media_connections library threads, so every seek/read pair runs under one
 * mutex. The host owns this object; `release` only records that the library
 * gave up the context, which lets each run check the exactly-once contract.
 * The file must not change while it is served.
 */
class HostFile {
public:
    explicit HostFile(const std::string& path) {
        const std::filesystem::path file(path);
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error)) {
            throw std::invalid_argument("--file is not an existing regular file");
        }
        stream_.open(file, std::ios::binary);
        const auto size = std::filesystem::file_size(file, error);
        if (!stream_ || error) {
            throw std::invalid_argument("--file cannot be opened for reading");
        }
        size_ = size;
    }
    HostFile(const HostFile&) = delete;
    HostFile& operator=(const HostFile&) = delete;
    HostFile(HostFile&&) = delete;
    HostFile& operator=(HostFile&&) = delete;

    /// A fresh table for one sap2_cast_create() call; `this` must outlive the handle.
    [[nodiscard]] sap2_media_source source() {
        sap2_media_source source{};
        source.struct_size = sizeof(source);
        source.context = this;
        source.size = size_;
        source.read_at = &HostFile::read_at;
        source.release = &HostFile::release;
        return source;
    }
    [[nodiscard]] std::uint64_t releases() const noexcept {
        return releases_.load();
    }
    [[nodiscard]] std::string read_summary() const {
        std::ostringstream line;
        line << "reads=" << reads_.load() << " bytes=" << bytes_.load()
             << " failed_reads=" << failed_reads_.load() << " releases=" << releases_.load();
        return line.str();
    }

private:
    static std::size_t read_at(void* context, std::uint64_t offset, std::uint8_t* buffer,
                               std::size_t capacity, const sap2_read_control* control) {
        auto& file = *static_cast<HostFile*>(context);
        if (sap2_read_should_stop(control) || capacity == 0) {
            ++file.failed_reads_;
            return 0;
        }
        std::size_t count = 0;
        {
            std::lock_guard<std::mutex> lock(file.mutex_);
            file.stream_.clear();
            file.stream_.seekg(static_cast<std::streamoff>(offset));
            file.stream_.read(reinterpret_cast<char*>(buffer),
                              static_cast<std::streamsize>(capacity));
            count = static_cast<std::size_t>(file.stream_.gcount());
        }
        if (count == 0) {
            ++file.failed_reads_;
        } else {
            ++file.reads_;
            file.bytes_ += count;
        }
        return count;
    }
    static void release(void* context) {
        ++static_cast<HostFile*>(context)->releases_;
    }

    std::mutex mutex_;
    std::ifstream stream_;
    std::uint64_t size_ = 0;
    std::atomic<std::uint64_t> reads_{0};
    std::atomic<std::uint64_t> bytes_{0};
    std::atomic<std::uint64_t> failed_reads_{0};
    std::atomic<std::uint64_t> releases_{0};
};

const char* phase_name(std::uint32_t phase) {
    switch (phase) {
    case SAP2_PHASE_CREATED:
        return "created";
    case SAP2_PHASE_STARTING:
        return "starting";
    case SAP2_PHASE_ACTIVE:
        return "active";
    case SAP2_PHASE_ENDED:
        return "ended";
    case SAP2_PHASE_START_FAILED:
        return "start_failed";
    case SAP2_PHASE_STOPPED:
        return "stopped";
    default:
        return "unknown";
    }
}

const char* state_name(std::uint32_t state) {
    switch (state) {
    case SAP2_STATE_NONE:
        return "none";
    case SAP2_STATE_LOADING:
        return "loading";
    case SAP2_STATE_PLAYING:
        return "playing";
    case SAP2_STATE_PAUSED:
        return "paused";
    case SAP2_STATE_IDLE:
        return "idle";
    case SAP2_STATE_STOPPED:
        return "stopped";
    case SAP2_STATE_ENDED:
        return "ended";
    default:
        return "other";
    }
}

const char* end_name(std::uint32_t reason) {
    switch (reason) {
    case SAP2_END_NONE:
        return "none";
    case SAP2_END_SENDER_STOP:
        return "sender_stop";
    case SAP2_END_MEDIA_END:
        return "media_end";
    case SAP2_END_RECEIVER_STOP:
        return "receiver_stop";
    case SAP2_END_OWNERSHIP_LOST:
        return "ownership_lost";
    case SAP2_END_CONNECTION_LOST:
        return "connection_lost";
    default:
        return "unknown";
    }
}

const char* failure_channel_name(std::uint32_t channel) {
    switch (channel) {
    case SAP2_FAILURE_CHANNEL_NONE:
        return "none";
    case SAP2_FAILURE_CHANNEL_URL_EVENTS:
        return "url_events";
    case SAP2_FAILURE_CHANNEL_REMOTE_EVENTS:
        return "remote_events";
    case SAP2_FAILURE_CHANNEL_URL_FEEDBACK:
        return "url_feedback";
    case SAP2_FAILURE_CHANNEL_REMOTE_FEEDBACK:
        return "remote_feedback";
    case SAP2_FAILURE_CHANNEL_TIMING:
        return "timing";
    case SAP2_FAILURE_CHANNEL_MRP:
        return "mrp";
    case SAP2_FAILURE_CHANNEL_SUPERVISOR:
        return "supervisor";
    default:
        return "unknown";
    }
}

const char* failure_reason_name(std::uint32_t reason) {
    switch (reason) {
    case SAP2_FAILURE_REASON_NONE:
        return "none";
    case SAP2_FAILURE_REASON_TIMEOUT:
        return "timeout";
    case SAP2_FAILURE_REASON_DISCONNECTED:
        return "disconnected";
    case SAP2_FAILURE_REASON_NETWORK:
        return "network";
    case SAP2_FAILURE_REASON_INVALID_MESSAGE:
        return "invalid_message";
    case SAP2_FAILURE_REASON_AUTHENTICATION:
        return "authentication";
    case SAP2_FAILURE_REASON_CANCELLED:
        return "cancelled";
    case SAP2_FAILURE_REASON_REJECTED:
        return "rejected";
    default:
        return "other";
    }
}

const char* linkage() {
#if defined(SAP2_SHARED)
    return "shared";
#else
    return "static";
#endif
}

sap2_cast_status empty_status() {
    sap2_cast_status status{};
    status.struct_size = sizeof(status);
    return status;
}

/// Library failures here are host bugs or allocation failures; report fixed text.
sap2_cast_status read_status(sap2_cast* cast) {
    auto status = empty_status();
    const auto result = sap2_cast_get_status(cast, &status);
    if (result != SAP2_OK) {
        std::cout << "Status: unavailable result=" << sap2_result_name(result) << std::endl;
    }
    return status;
}

void append_scalar(std::ostringstream& line, const char* key, std::uint32_t has, double value) {
    line << ' ' << key << '=';
    if (has) {
        line << std::fixed;
        line.precision(1);
        line << value;
    } else {
        line << "unknown";
    }
}

std::string describe(const sap2_cast_status& status) {
    std::ostringstream line;
    line << "phase=" << phase_name(status.phase) << " state=" << state_name(status.playback_state)
         << " end=" << end_name(status.end_reason) << " owned=" << (status.owned ? "yes" : "no");
    append_scalar(line, "position", status.has_position, status.position_seconds);
    append_scalar(line, "duration", status.has_duration, status.duration_seconds);
    append_scalar(line, "rate", status.has_playback_rate, status.playback_rate);
    line << " failure=" << failure_channel_name(status.failure_channel) << '/'
         << failure_reason_name(status.failure_reason)
         << " cleaned=" << (status.cleaned_up ? "yes" : "no");
    return line.str();
}

bool failed(const sap2_cast_status& status) {
    return status.failure_channel != SAP2_FAILURE_CHANNEL_NONE ||
           status.end_reason == SAP2_END_CONNECTION_LOST;
}

/**
 * Prints playback-state and end-reason changes from its own thread through
 * sap2_cast_wait_for_change(), the waiting path a binding would use. It must be
 * stopped before the handle is destroyed.
 */
class StateWatcher {
public:
    explicit StateWatcher(sap2_cast* cast) : thread_([this, cast] { watch(cast); }) {}
    ~StateWatcher() {
        stop();
    }
    StateWatcher(const StateWatcher&) = delete;
    StateWatcher& operator=(const StateWatcher&) = delete;
    StateWatcher(StateWatcher&&) = delete;
    StateWatcher& operator=(StateWatcher&&) = delete;
    void stop() {
        done_ = true;
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void watch(sap2_cast* cast) {
        auto last = read_status(cast);
        while (!done_) {
            auto current = empty_status();
            if (sap2_cast_wait_for_change(cast, last.playback_state, watch_timeout_ms, &current) !=
                SAP2_OK) {
                return;
            }
            if (current.playback_state != last.playback_state) {
                std::cout << "State: " << state_name(current.playback_state) << std::endl;
            }
            if (current.end_reason != last.end_reason) {
                std::cout << "End: " << end_name(current.end_reason)
                          << " failure=" << failure_channel_name(current.failure_channel) << '/'
                          << failure_reason_name(current.failure_reason) << std::endl;
            }
            if (current.phase == SAP2_PHASE_ENDED || current.phase == SAP2_PHASE_STOPPED) {
                // Nothing further can change; avoid spinning on immediate returns.
                std::this_thread::sleep_for(std::chrono::milliseconds(watch_timeout_ms));
            }
            last = current;
        }
    }

    std::atomic_bool done_{false};
    std::thread thread_;
};

/// Owns one handle; destroys it on every path so the source is always released.
struct CastHandle {
    sap2_cast* cast = nullptr;
    CastHandle() = default;
    ~CastHandle() {
        sap2_cast_destroy(cast);
    }
    CastHandle(const CastHandle&) = delete;
    CastHandle& operator=(const CastHandle&) = delete;
    CastHandle(CastHandle&&) = delete;
    CastHandle& operator=(CastHandle&&) = delete;
};

sap2_cast_options options_for(const HostArguments& arguments) {
    sap2_cast_options options;
    sap2_cast_options_init_sized(&options, sizeof(options));
    options.receiver_address = arguments.address.c_str();
    options.receiver_port = arguments.port;
    options.profile = arguments.profile.c_str();
    options.content_type = arguments.content_type ? arguments.content_type->c_str() : nullptr;
    options.start_timeout_ms = arguments.start_timeout_ms;
    options.media_connections = arguments.media_connections;
    options.start_position_seconds = arguments.start_position_seconds;
    return options;
}

/// Returns SAP2_OK with `handle.cast` set, or the create result.
std::int32_t create(const HostArguments& arguments, HostFile& file, CastHandle& handle) {
    const auto options = options_for(arguments);
    const auto source = file.source();
    const auto result = sap2_cast_create(&options, &source, &handle.cast);
    if (result != SAP2_OK) {
        std::cerr << "Create: " << sap2_result_name(result) << '\n';
    }
    return result;
}

void print_start_failure(std::int32_t result, const sap2_cast_status& status) {
    std::cout << "Start: " << sap2_result_name(result);
    if (status.rejected_status != 0) {
        std::cout << " status=" << status.rejected_status;
    }
    std::cout << ' ' << describe(status) << std::endl;
}

void print_summary(const char* start, const sap2_cast_status& status, const HostFile& file) {
    std::cout << "Summary: api_version=" << sap2_playback_api_version() << " linkage=" << linkage()
              << " start=" << start << ' ' << describe(status) << ' ' << file.read_summary()
              << std::endl;
}

/// Sends one command and prints its result; returns whether it was accepted.
bool send_command(sap2_cast* cast, std::uint32_t command, double position_seconds = 0) {
    const auto result = sap2_cast_command(cast, command, position_seconds);
    std::cout << "Control: " << sap2_result_name(result) << std::endl;
    return result == SAP2_OK;
}

const char* controls_help = "Use status, pause, play, seek SECONDS, stop or Enter.";

/// Returns false when an MRP stop was not accepted. Ends on Enter/EOF, stop,
/// or when the session ends by itself while stdin stays open.
bool control_loop(sap2_cast* cast) {
    CliInput input;
    for (;;) {
        if (read_status(cast).phase != SAP2_PHASE_ACTIVE) {
            std::cout << "Session ended; cleaning up." << std::endl;
            return true;
        }
        const auto next = input.poll(input_poll);
        if (next.kind == CliInputKind::waiting) {
            continue;
        }
        if (next.kind == CliInputKind::eof ||
            (next.kind == CliInputKind::line && next.line.empty())) {
            return true;
        }
        if (next.kind == CliInputKind::invalid) {
            std::cout << controls_help << std::endl;
            continue;
        }
        const auto& line = next.line;
        if (line == "status") {
            std::cout << "Status: " << describe(read_status(cast)) << std::endl;
        } else if (line == "pause") {
            send_command(cast, SAP2_COMMAND_PAUSE);
        } else if (line == "play" || line == "resume") {
            send_command(cast, SAP2_COMMAND_PLAY);
        } else if (line == "stop") {
            return send_command(cast, SAP2_COMMAND_STOP);
        } else if (line.rfind("seek ", 0) == 0) {
            try {
                send_command(cast, SAP2_COMMAND_SEEK,
                             parse_seconds(std::string_view(line).substr(5)));
            } catch (const std::invalid_argument&) {
                std::cout << controls_help << std::endl;
            }
        } else {
            std::cout << controls_help << std::endl;
        }
    }
}

int run_interactive(const HostArguments& arguments, HostFile& file) {
    bool controls_ok = true;
    sap2_cast_status final_status = empty_status();
    std::int32_t started = SAP2_OK;
    {
        CastHandle handle;
        const auto created = create(arguments, file, handle);
        if (created != SAP2_OK) {
            return created == SAP2_ERROR_INVALID_ARGUMENT ? exit_arguments : exit_failure;
        }
        std::cout << "Starting playback." << std::endl;
        started = sap2_cast_start(handle.cast);
        if (started != SAP2_OK) {
            final_status = read_status(handle.cast);
            print_start_failure(started, final_status);
        } else {
            std::cout << "Start: ok " << describe(read_status(handle.cast)) << std::endl;
            std::cout << "Controls: status, pause, play, seek SECONDS, stop. Press Enter to stop."
                      << std::endl;
            StateWatcher watcher(handle.cast);
            controls_ok = control_loop(handle.cast);
            watcher.stop();
            sap2_cast_stop(handle.cast);
            final_status = read_status(handle.cast);
        }
    } // Destroy releases the source; the summary then shows the release count.
    print_summary(sap2_result_name(started), final_status, file);
    const bool success = started == SAP2_OK && controls_ok && !failed(final_status);
    return success ? exit_success : exit_failure;
}

/// Calls sap2_cast_stop() after a delay unless cancelled first.
class DelayedStop {
public:
    DelayedStop(sap2_cast* cast, std::chrono::milliseconds delay)
        : thread_([this, cast, delay] { run(cast, delay); }) {}
    ~DelayedStop() {
        cancel();
    }
    DelayedStop(const DelayedStop&) = delete;
    DelayedStop& operator=(const DelayedStop&) = delete;
    DelayedStop(DelayedStop&&) = delete;
    DelayedStop& operator=(DelayedStop&&) = delete;
    /// Joins the timer; returns whether it had already called stop.
    bool cancel() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            cancelled_ = true;
        }
        changed_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
        return fired_;
    }

private:
    void run(sap2_cast* cast, std::chrono::milliseconds delay) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (changed_.wait_for(lock, delay, [this] { return cancelled_; })) {
            return;
        }
        fired_ = true;
        lock.unlock();
        sap2_cast_stop(cast);
    }

    std::mutex mutex_;
    std::condition_variable changed_;
    bool cancelled_ = false;
    std::atomic_bool fired_{false};
    std::thread thread_;
};

/// Expected outcome: start returns SAP2_ERROR_CANCELLED promptly after the delay.
int run_cancel(const HostArguments& arguments, HostFile& file) {
    std::int32_t started = SAP2_OK;
    bool fired = false;
    sap2_cast_status final_status = empty_status();
    std::chrono::milliseconds elapsed{};
    {
        CastHandle handle;
        const auto created = create(arguments, file, handle);
        if (created != SAP2_OK) {
            return created == SAP2_ERROR_INVALID_ARGUMENT ? exit_arguments : exit_failure;
        }
        std::cout << "Starting playback; stop scheduled after " << *arguments.cancel_after_ms
                  << " ms." << std::endl;
        const auto begin = std::chrono::steady_clock::now();
        {
            DelayedStop timer(handle.cast, std::chrono::milliseconds(*arguments.cancel_after_ms));
            started = sap2_cast_start(handle.cast);
            elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - begin);
            fired = timer.cancel();
        }
        sap2_cast_stop(handle.cast); // Tears down a start that won the race.
        final_status = read_status(handle.cast);
    }
    std::cout << "Cancel: start=" << sap2_result_name(started) << " start_ms=" << elapsed.count()
              << " stop_fired=" << (fired ? "yes" : "no") << std::endl;
    print_summary(sap2_result_name(started), final_status, file);
    return started == SAP2_ERROR_CANCELLED ? exit_success : exit_failure;
}

/// Wait up to `hold` while the session stays active.
void hold_while_active(sap2_cast* cast, std::chrono::milliseconds hold) {
    const auto deadline = std::chrono::steady_clock::now() + hold;
    auto status = read_status(cast);
    while (status.phase == SAP2_PHASE_ACTIVE && std::chrono::steady_clock::now() < deadline) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        const auto wait = std::min<std::int64_t>(remaining.count(), watch_timeout_ms);
        auto next = empty_status();
        if (sap2_cast_wait_for_change(cast, status.playback_state,
                                      static_cast<std::uint32_t>(std::max<std::int64_t>(wait, 0)),
                                      &next) != SAP2_OK) {
            return;
        }
        status = next;
    }
}

/// Repeated casts in one process. Odd cycles send MRP Stop before the local
/// stop; even cycles stop locally only. Each handle must release its source once.
int run_cycles(const HostArguments& arguments, HostFile& file) {
    const auto cycles = *arguments.cycles;
    std::uint32_t passed = 0;
    for (std::uint32_t cycle = 1; cycle <= cycles; ++cycle) {
        const bool mrp_stop = cycle % 2 == 1;
        const auto releases_before = file.releases();
        std::int32_t started = SAP2_OK;
        bool control_ok = true;
        sap2_cast_status final_status = empty_status();
        {
            CastHandle handle;
            const auto created = create(arguments, file, handle);
            if (created != SAP2_OK) {
                return created == SAP2_ERROR_INVALID_ARGUMENT ? exit_arguments : exit_failure;
            }
            started = sap2_cast_start(handle.cast);
            if (started == SAP2_OK) {
                hold_while_active(handle.cast, std::chrono::milliseconds(arguments.hold_ms));
                if (mrp_stop) {
                    control_ok = sap2_cast_command(handle.cast, SAP2_COMMAND_STOP, 0) == SAP2_OK;
                }
                sap2_cast_stop(handle.cast);
            }
            final_status = read_status(handle.cast);
        }
        const bool released_once = file.releases() == releases_before + 1;
        const bool ok = started == SAP2_OK && control_ok && !failed(final_status) &&
                        final_status.cleaned_up && released_once;
        passed += ok ? 1 : 0;
        std::cout << "Cycle " << cycle << '/' << cycles << ": " << (ok ? "PASS" : "FAIL")
                  << " start=" << sap2_result_name(started)
                  << " stop=" << (mrp_stop ? "mrp+local" : "local")
                  << " control=" << (control_ok ? "ok" : "failed")
                  << " released_once=" << (released_once ? "yes" : "no") << ' '
                  << describe(final_status) << std::endl;
    }
    std::cout << "Cycles: passed=" << passed << '/' << cycles
              << " api_version=" << sap2_playback_api_version() << " linkage=" << linkage() << ' '
              << file.read_summary() << std::endl;
    return passed == cycles ? exit_success : exit_failure;
}

#ifdef _WIN32
/**
 * Hidden PIN entry for sap2_pair_options::read_pin, written against the public
 * callback contract only. It reads the process's own console through CONIN$
 * and CONOUT$, so it works while stdout is redirected to a log, and it restores
 * the console mode and drops queued keystrokes on every path. Esc cancels; any
 * printable non-digit or a ninth digit makes the entry invalid, which the
 * library rejects. Key events are erased after use; nothing is echoed.
 */
class ConsolePin {
public:
    ConsolePin()
        : input_(CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                             nullptr)),
          output_(CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                              nullptr)) {}
    ~ConsolePin() {
        if (mode_saved_) {
            FlushConsoleInputBuffer(input_);
            SetConsoleMode(input_, original_mode_);
        }
        if (input_ != INVALID_HANDLE_VALUE) {
            CloseHandle(input_);
        }
        if (output_ != INVALID_HANDLE_VALUE) {
            CloseHandle(output_);
        }
    }
    ConsolePin(const ConsolePin&) = delete;
    ConsolePin& operator=(const ConsolePin&) = delete;
    ConsolePin(ConsolePin&&) = delete;
    ConsolePin& operator=(ConsolePin&&) = delete;

    /// Returns SAP2_OK with the digits typed, or SAP2_ERROR_CANCELLED.
    int32_t read(char* digits, std::size_t capacity, std::size_t* length) {
        if (input_ == INVALID_HANDLE_VALUE || output_ == INVALID_HANDLE_VALUE ||
            !GetConsoleMode(input_, &original_mode_)) {
            return SAP2_ERROR_CANCELLED; // No interactive console to read from.
        }
        mode_saved_ = true;
        const DWORD hidden = (original_mode_ | ENABLE_EXTENDED_FLAGS | ENABLE_PROCESSED_INPUT) &
                             ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_QUICK_EDIT_MODE |
                               ENABLE_VIRTUAL_TERMINAL_INPUT);
        if (!SetConsoleMode(input_, hidden)) {
            return SAP2_ERROR_CANCELLED;
        }
        FlushConsoleInputBuffer(input_);
        write(L"Enter the PIN shown on the TV (hidden), then Enter; Esc cancels: ");
        std::size_t count = 0;
        bool invalid = false;
        for (;;) {
            INPUT_RECORD event{};
            DWORD read = 0;
            if (!ReadConsoleInputW(input_, &event, 1, &read) || read != 1) {
                SecureZeroMemory(&event, sizeof(event));
                return SAP2_ERROR_CANCELLED;
            }
            const bool key_down = event.EventType == KEY_EVENT && event.Event.KeyEvent.bKeyDown;
            const auto key = event.Event.KeyEvent.wVirtualKeyCode;
            const auto character = event.Event.KeyEvent.uChar.UnicodeChar;
            SecureZeroMemory(&event, sizeof(event)); // The record holds the typed digit.
            if (!key_down) {
                continue;
            }
            if (key == VK_ESCAPE) {
                write(L"\r\n");
                return SAP2_ERROR_CANCELLED;
            }
            if (key == VK_RETURN) {
                write(L"\r\n");
                *length = invalid ? 0 : count;
                return SAP2_OK;
            }
            if (key == VK_BACK) {
                if (count > 0) {
                    digits[--count] = 0;
                }
                continue;
            }
            if (character == 0) {
                continue; // Modifier or navigation key.
            }
            if (character < L'0' || character > L'9' || count == capacity) {
                invalid = true;
                continue;
            }
            digits[count++] = static_cast<char>(character);
        }
    }

private:
    void write(const wchar_t* text) {
        DWORD written = 0;
        WriteConsoleW(output_, text, static_cast<DWORD>(std::wcslen(text)), &written, nullptr);
    }

    HANDLE input_;
    HANDLE output_;
    DWORD original_mode_ = 0;
    bool mode_saved_ = false;
};

int32_t read_pin_from_console(void*, char* digits, std::size_t capacity, std::size_t* length) {
    ConsolePin console;
    return console.read(digits, capacity, length);
}
#else
int32_t read_pin_from_console(void*, char*, std::size_t, std::size_t*) {
    std::cerr << "Pair: hidden PIN entry is implemented for Windows consoles only\n";
    return SAP2_ERROR_CANCELLED;
}
#endif

/// Pair a new profile into the built-in store with hidden console PIN entry.
int run_pair(const HostArguments& arguments) {
    sap2_pair_options options;
    sap2_pair_options_init(&options, sizeof(options));
    options.receiver_address = arguments.address.c_str();
    options.receiver_port = arguments.port;
    options.profile = arguments.profile.c_str();
    options.read_pin = &read_pin_from_console;
    std::cout << "Pairing: starting; type the PIN in this console once the TV shows it."
              << std::endl;
    const auto result = sap2_pair(&options);
    std::cout << "Pair: " << sap2_result_name(result)
              << " api_version=" << sap2_playback_api_version() << " linkage=" << linkage()
              << std::endl;
    if (result == SAP2_ERROR_INVALID_ARGUMENT) {
        return exit_arguments;
    }
    return result == SAP2_OK ? exit_success : exit_failure;
}
} // namespace

int main(int argc, char** argv) {
    HostArguments arguments;
    std::unique_ptr<HostFile> file;
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--help") {
            std::cout << usage;
            return exit_success;
        }
        arguments = parse_arguments(argc, argv);
        if (!arguments.pair) {
            file = std::make_unique<HostFile>(arguments.file);
        }
    } catch (const std::invalid_argument& error) {
        std::cerr << "Arguments: " << error.what() << '\n' << usage;
        return exit_arguments;
    }
    try {
        if (sap2_playback_api_version() != SAP2_PLAYBACK_API_VERSION) {
            std::cerr << "Library: playback interface version mismatch\n";
            return exit_failure;
        }
        if (arguments.pair) {
            return run_pair(arguments);
        }
        if (arguments.cancel_after_ms) {
            return run_cancel(arguments, *file);
        }
        if (arguments.cycles) {
            return run_cycles(arguments, *file);
        }
        return run_interactive(arguments, *file);
    } catch (const std::exception&) {
        std::cerr << "Host: unexpected failure\n";
        return exit_failure;
    }
}
