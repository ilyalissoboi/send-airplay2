// SPDX-License-Identifier: Apache-2.0
#include "cli_input.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <poll.h>
#include <unistd.h>
#endif

namespace send_airplay2::detail {
namespace {
constexpr int input_idle = -2, input_eof = -1;
constexpr std::size_t max_line_size = 256, max_characters_per_poll = 256;
std::intptr_t standard_input() {
#ifdef _WIN32
    return reinterpret_cast<std::intptr_t>(GetStdHandle(STD_INPUT_HANDLE));
#else
    return STDIN_FILENO;
#endif
}
[[noreturn]] void input_error() {
    throw std::runtime_error("Command input is unavailable");
}
} // namespace
CliInput::CliInput() : CliInput(standard_input()) {}
CliInput::CliInput(std::intptr_t native_input) : input_(native_input) {
#ifdef _WIN32
    const auto handle = reinterpret_cast<HANDLE>(input_);
    if (!handle || handle == INVALID_HANDLE_VALUE) {
        input_error();
    }
    DWORD mode = 0;
    console_ = GetConsoleMode(handle, &mode) != FALSE;
#else
    if (input_ < 0) {
        input_error();
    }
#endif
}
int CliInput::read_character(std::chrono::milliseconds timeout) {
#ifdef _WIN32
    const auto handle = reinterpret_cast<HANDLE>(input_);
    if (console_) {
        const auto ready = WaitForSingleObject(handle, static_cast<DWORD>(timeout.count()));
        if (ready == WAIT_TIMEOUT) {
            return input_idle;
        }
        if (ready != WAIT_OBJECT_0) {
            input_error();
        }
        INPUT_RECORD event{};
        DWORD count = 0;
        if (!ReadConsoleInputW(handle, &event, 1, &count) || count != 1) {
            input_error();
        }
        if (event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown) {
            return input_idle;
        }
        const auto character = event.Event.KeyEvent.uChar.UnicodeChar;
        if (character == 26) { // Windows console Ctrl+Z convention.
            return input_eof;
        }
        return character ? (character < 128 ? static_cast<int>(character) : 255) : input_idle;
    }
    if (GetFileType(handle) == FILE_TYPE_PIPE) {
        DWORD available = 0;
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                return input_eof;
            }
            input_error();
        }
        if (available == 0) {
            std::this_thread::sleep_for(std::min(timeout, std::chrono::milliseconds{10}));
            return input_idle;
        }
    }
    unsigned char character = 0;
    DWORD count = 0;
    if (!ReadFile(handle, &character, 1, &count, nullptr)) {
        if (GetLastError() == ERROR_BROKEN_PIPE || GetLastError() == ERROR_HANDLE_EOF) {
            return input_eof;
        }
        input_error();
    }
    return count ? character : input_eof;
#else
    pollfd descriptor{static_cast<int>(input_), POLLIN, 0};
    const auto ready = ::poll(&descriptor, 1, static_cast<int>(timeout.count()));
    if (ready == 0 || (ready < 0 && errno == EINTR)) {
        return input_idle;
    }
    if (ready < 0 || (descriptor.revents & (POLLNVAL | POLLERR))) {
        input_error();
    }
    unsigned char character = 0;
    const auto count = ::read(descriptor.fd, &character, 1);
    if (count < 0) {
        if (errno == EINTR || errno == EAGAIN) {
            return input_idle;
        }
        input_error();
    }
    return count ? character : input_eof;
#endif
}
CliInputResult CliInput::finish_line() {
    CliInputResult result;
    result.kind = discarding_ ? CliInputKind::invalid : CliInputKind::line;
    result.line.swap(pending_);
    discarding_ = false;
    return result;
}
CliInputResult CliInput::poll(std::chrono::milliseconds timeout) {
    if (timeout.count() < 0 || timeout.count() > 1000) {
        throw std::invalid_argument("Input poll must be 0..1000 ms");
    }
    if (eof_) {
        return {CliInputKind::eof, {}};
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (std::size_t count = 0; count < max_characters_per_poll; ++count) {
        const auto remaining = std::max(std::chrono::milliseconds{0},
                                        std::chrono::duration_cast<std::chrono::milliseconds>(
                                            deadline - std::chrono::steady_clock::now()));
        const auto character = read_character(remaining);
        if (character == input_idle) {
            return {};
        }
        if (character == input_eof) {
            eof_ = true;
            return pending_.empty() && !discarding_ ? CliInputResult{CliInputKind::eof, {}}
                                                    : finish_line();
        }
        if (character == '\n' || (console_ && character == '\r')) {
            if (console_) {
                std::cout << std::endl;
            }
            return finish_line();
        }
        if (character == '\r') {
            continue;
        } // Pipe/file CRLF.
        if (console_ && character == '\b' && !discarding_) {
            if (!pending_.empty()) {
                pending_.pop_back();
                std::cout << "\b \b" << std::flush;
            }
            continue;
        }
        if (discarding_) {
            continue;
        }
        if (character < 32 || character >= 127 || pending_.size() == max_line_size) {
            pending_.clear();
            discarding_ = true;
        } else {
            pending_.push_back(static_cast<char>(character));
            if (console_) {
                std::cout << static_cast<char>(character) << std::flush;
            }
        }
    }
    return {};
}
} // namespace send_airplay2::detail
