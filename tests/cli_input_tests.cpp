// SPDX-License-Identifier: Apache-2.0
#include "cli_input.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif
using namespace send_airplay2::detail;
using namespace std::chrono_literals;
namespace {
void check(bool condition, const char* scenario) {
    if (!condition) {
        throw std::runtime_error(scenario);
    }
}
/// Real native pipe; validates idle/partial input without a detached reader.
class Pipe {
public:
    Pipe() {
#ifdef _WIN32
        HANDLE read = nullptr, write = nullptr;
        if (!CreatePipe(&read, &write, nullptr, 0)) {
            throw std::runtime_error("CreatePipe");
        }
        read_ = reinterpret_cast<std::intptr_t>(read);
        write_ = reinterpret_cast<std::intptr_t>(write);
#else
        int descriptors[2];
        if (::pipe(descriptors) != 0) {
            throw std::runtime_error("pipe");
        }
        read_ = descriptors[0];
        write_ = descriptors[1];
#endif
    }
    ~Pipe() {
        close_native(read_);
        close_writer();
    }
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    Pipe(Pipe&&) = delete;
    Pipe& operator=(Pipe&&) = delete;
    std::intptr_t reader() const {
        return read_;
    }
    void close_writer() {
        close_native(write_);
    }
    void write(const std::string& value) {
        for (std::size_t offset = 0; offset < value.size();) {
#ifdef _WIN32
            DWORD count = 0;
            check(WriteFile(reinterpret_cast<HANDLE>(write_), value.data() + offset,
                            static_cast<DWORD>(value.size() - offset), &count, nullptr) != FALSE,
                  "pipe write");
#else
            const auto count =
                ::write(static_cast<int>(write_), value.data() + offset, value.size() - offset);
#endif
            check(count > 0, "partial pipe write");
            offset += static_cast<std::size_t>(count);
        }
    }

private:
    static void close_native(std::intptr_t& handle) {
        if (handle == -1) {
            return;
        }
#ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(handle));
#else
        ::close(static_cast<int>(handle));
#endif
        handle = -1;
    }
    std::intptr_t read_ = -1, write_ = -1;
};
void idle_partial_and_eof() {
    Pipe pipe;
    CliInput input(pipe.reader());
    const auto started = std::chrono::steady_clock::now();
    check(input.poll(20ms).kind == CliInputKind::waiting, "idle pipe returns waiting");
    check(std::chrono::steady_clock::now() - started < 500ms, "idle poll stays bounded");
    pipe.write("sta");
    check(input.poll(20ms).kind == CliInputKind::waiting, "partial line never blocks for newline");
    pipe.write("tus\r\npause\n");
    auto result = input.poll(20ms);
    check(result.kind == CliInputKind::line && result.line == "status", "partial CRLF line");
    result = input.poll(20ms);
    check(result.kind == CliInputKind::line && result.line == "pause", "coalesced next line");
    pipe.write("stop");
    pipe.close_writer();
    result = input.poll(20ms);
    check(result.kind == CliInputKind::line && result.line == "stop", "unterminated final line");
    check(input.poll(20ms).kind == CliInputKind::eof, "EOF follows final line once");
}
void bounds_and_recovery() {
    Pipe pipe;
    CliInput input(pipe.reader());
    pipe.write(std::string(257, 'x') + "\nstatus\n");
    check(input.poll(0ms).kind == CliInputKind::waiting, "per-poll byte budget is bounded");
    check(input.poll(20ms).kind == CliInputKind::invalid, "literal 257-byte line rejected");
    check(input.poll(20ms).line == "status", "overlong line cannot poison next command");
    pipe.write(std::string("seek \xff\n\n", 8));
    check(input.poll(20ms).kind == CliInputKind::invalid, "non-ASCII command refused");
    const auto blank = input.poll(20ms);
    check(blank.kind == CliInputKind::line && blank.line.empty(), "blank Enter line preserved");
    pipe.close_writer();
    check(input.poll(20ms).kind == CliInputKind::eof, "empty EOF");
    try {
        (void)input.poll(1001ms);
        check(false, "unbounded input poll accepted");
    } catch (const std::invalid_argument&) {
    }
}
} // namespace
int main() {
    try {
        idle_partial_and_eof();
        bounds_and_recovery();
    } catch (const std::exception& error) {
        std::cerr << "FAIL [native command input]: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
