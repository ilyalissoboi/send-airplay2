// SPDX-License-Identifier: Apache-2.0
#include "auth_cli.h"
#include "auth_workflow.h"
#include "control_crypto.h"
#include "pair_setup.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace send_airplay2::detail {
namespace {
#ifdef _WIN32
std::atomic_bool cli_cancelled{false};
static_assert(std::atomic_bool::is_always_lock_free, "Console cancellation must not take a lock");
BOOL WINAPI cancel_handler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        cli_cancelled.store(true);
        return TRUE;
    }
    return FALSE; // Keep the system's normal close/logoff/shutdown handling.
}
class CancellationGuard {
public:
    CancellationGuard() {
        cli_cancelled.store(false);
        if (!SetConsoleCtrlHandler(cancel_handler, TRUE)) {
            throw CredentialException(CredentialError::unavailable);
        }
    }
    ~CancellationGuard() {
        SetConsoleCtrlHandler(cancel_handler, FALSE);
    }
    CancellationGuard(const CancellationGuard&) = delete;
    CancellationGuard& operator=(const CancellationGuard&) = delete;
    CancellationGuard(CancellationGuard&&) = delete;
    CancellationGuard& operator=(CancellationGuard&&) = delete;
};

/** Console mode owner. Redirected input is refused before receiver enrollment.
 * read consumes bounded key events without echo; each wait checks cancellation.
 * Queued PIN events are discarded before restoring the caller's exact input mode.
 * One auth command/console reader per process; no PIN is copied to a line buffer.
 */
class ConsolePinPrompt final : public PinPrompt {
    HANDLE input_ = INVALID_HANDLE_VALUE; // Borrowed standard handle; never closed here.
    DWORD original_mode_ = 0;
    bool prepared_ = false;

    void restore() noexcept {
        if (prepared_) {
            FlushConsoleInputBuffer(input_);
            SetConsoleMode(input_, original_mode_);
            prepared_ = false;
        }
    }

public:
    ~ConsolePinPrompt() override {
        restore();
    }
    void prepare() override {
        if (prepared_) {
            return;
        }
        input_ = GetStdHandle(STD_INPUT_HANDLE);
        if (input_ == INVALID_HANDLE_VALUE || !GetConsoleMode(input_, &original_mode_)) {
            throw std::invalid_argument(
                "pair requires an interactive Windows console with hidden PIN input");
        }
        const DWORD mode = (original_mode_ | ENABLE_EXTENDED_FLAGS | ENABLE_PROCESSED_INPUT) &
                           ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_QUICK_EDIT_MODE |
                             ENABLE_VIRTUAL_TERMINAL_INPUT);
        if (!SetConsoleMode(input_, mode)) {
            throw CredentialException(CredentialError::unavailable);
        }
        prepared_ = true;
        FlushConsoleInputBuffer(input_);
    }
    void read(PinCode& output, const ReceiverOperation& operation) override {
        if (!prepared_) {
            throw CredentialException(CredentialError::unavailable);
        }
        std::cout << "Enter the receiver's 4..8 digit PIN (hidden), then Enter; Esc cancels: "
                  << std::flush;
        try {
            for (;;) {
                operation.check();
                const auto ready = WaitForSingleObject(input_, 20);
                if (ready == WAIT_TIMEOUT) {
                    continue;
                }
                if (ready != WAIT_OBJECT_0) {
                    throw CredentialException(CredentialError::unavailable);
                }
                // Erase the event too: KEY_EVENT_RECORD contains the typed digit.
                struct InputEvent {
                    INPUT_RECORD record{};
                    ~InputEvent() {
                        cleanse(&record, sizeof(record));
                    }
                    InputEvent() = default;
                    InputEvent(const InputEvent&) = delete;
                    InputEvent& operator=(const InputEvent&) = delete;
                    InputEvent(InputEvent&&) = delete;
                    InputEvent& operator=(InputEvent&&) = delete;
                } event;
                DWORD count = 0;
                if (!ReadConsoleInputW(input_, &event.record, 1, &count) || count != 1) {
                    throw CredentialException(CredentialError::unavailable);
                }
                operation.check();
                if (event.record.EventType != KEY_EVENT || !event.record.Event.KeyEvent.bKeyDown) {
                    continue;
                }
                const auto& key = event.record.Event.KeyEvent;
                if (key.wVirtualKeyCode == VK_ESCAPE || key.uChar.UnicodeChar == 3 ||
                    (key.wVirtualKeyCode == 'C' &&
                     (key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)))) {
                    cli_cancelled.store(true);
                    operation.check();
                }
                if (key.wVirtualKeyCode == VK_RETURN) {
                    if (!output.complete()) {
                        throw std::invalid_argument("PIN must contain 4..8 digits");
                    }
                    restore();
                    std::cout << '\n';
                    return;
                }
                if (key.wVirtualKeyCode == VK_BACK) {
                    for (WORD repeat = 0; repeat < key.wRepeatCount && repeat < 8; ++repeat) {
                        output.backspace();
                    }
                    continue;
                }
                // Ignore modifiers/navigation, which have no character. Reject every
                // printable non-digit and overflow rather than silently changing a PIN.
                if (!key.uChar.UnicodeChar) {
                    continue;
                }
                for (WORD repeat = 0; repeat < key.wRepeatCount; ++repeat) {
                    if (key.uChar.UnicodeChar < L'0' || key.uChar.UnicodeChar > L'9' ||
                        !output.append(static_cast<char>(key.uChar.UnicodeChar))) {
                        throw std::invalid_argument("PIN must contain 4..8 digits");
                    }
                }
            }
        } catch (...) {
            output.clear();
            restore();
            std::cout << '\n';
            throw;
        }
    }
};
#else
class ConsolePinPrompt final : public PinPrompt {
public:
    void prepare() override {
        throw CredentialException(CredentialError::unsupported);
    }
    void read(PinCode&, const ReceiverOperation&) override {
        throw CredentialException(CredentialError::unsupported);
    }
};
#endif
} // namespace
const char* describe_credential_error(CredentialError error) {
    switch (error) {
    case CredentialError::already_exists:
        return "Profile already exists; use verify or explicitly forget before pairing again.";
    case CredentialError::invalid_profile:
        return "Invalid credential profile.";
    case CredentialError::invalid_record:
        return "Stored credential is missing or malformed; no automatic re-pairing was attempted.";
    case CredentialError::unsupported:
        return "Native credential storage currently supports Windows desktop only.";
    case CredentialError::unavailable:
        return "Windows credential storage or console is unavailable; no plaintext fallback is "
               "used.";
    }
    return "Credential operation failed.";
}
int run_auth_cli(int argc, const char* const* argv) {
    try {
        const auto options = parse_auth_options(argc, argv);
        auto store = native_credential_store();
        auto sessions = native_auth_sessions();
        ConsolePinPrompt prompt;
#ifdef _WIN32
        CancellationGuard cancellation;
        run_auth_workflow(options, *store, *sessions, prompt, std::cout, &cli_cancelled);
#else
        run_auth_workflow(options, *store, *sessions, prompt, std::cout);
#endif
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "Arguments: " << error.what() << '\n';
        return 2;
    } catch (const CredentialException& error) {
        std::cerr << "Credentials: " << describe_credential_error(error.reason()) << '\n';
    } catch (const TransportException& error) {
        std::cerr << "Receiver: " << error.what() << '\n';
    } catch (const ControlException& error) {
        std::cerr << "Authentication: " << error.what() << '\n';
    } catch (const PairSetupException& error) {
        std::cerr << "PIN enrollment: " << error.what() << '\n';
    } catch (const PairVerifyException& error) {
        std::cerr << "Peer verification: " << error.what() << '\n';
    } catch (const std::exception&) {
        std::cerr << "Authentication failed due to a host/backend error.\n";
    }
    return 1;
}
} // namespace send_airplay2::detail
