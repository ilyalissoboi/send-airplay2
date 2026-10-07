// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_CLI_INPUT_H
#define SEND_AIRPLAY2_CLI_INPUT_H
#include <chrono>
#include <cstdint>
#include <string>

namespace send_airplay2::detail {
enum class CliInputKind { waiting, line, eof, invalid };
struct CliInputResult {
    CliInputKind kind = CliInputKind::waiting;
    std::string line;
};
/** Bounded, single-reader command input. Polls a borrowed native stdin handle
 * (HANDLE on Windows, fd on POSIX); never closes it or changes console modes.
 * Partial lines survive polls; overlong/non-ASCII lines are discarded through
 * newline. EOF returns a final unterminated line once, then eof. Console input
 * supports character entry, backspace and Enter. No blocked reader is detached
 * when a session ends. Other stdin consumers must not run concurrently.
 */
class CliInput {
public:
    CliInput();
    explicit CliInput(std::intptr_t native_input);
    CliInput(const CliInput&) = delete;
    CliInput& operator=(const CliInput&) = delete;
    CliInput(CliInput&&) = delete;
    CliInput& operator=(CliInput&&) = delete;
    /// Consumes at most 256 characters and waits at most timeout (0..1000 ms).
    /// Lines contain at most 256 printable ASCII characters. Throws on invalid
    /// timeout or unavailable native input; allocation errors propagate.
    [[nodiscard]] CliInputResult poll(std::chrono::milliseconds timeout);

private:
    int read_character(std::chrono::milliseconds timeout);
    CliInputResult finish_line();
    std::intptr_t input_;
    bool console_ = false, eof_ = false, discarding_ = false;
    std::string pending_;
};
} // namespace send_airplay2::detail
#endif
