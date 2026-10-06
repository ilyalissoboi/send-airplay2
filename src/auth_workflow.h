// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_AUTH_WORKFLOW_H
#define SEND_AIRPLAY2_AUTH_WORKFLOW_H
#include "credential_store.h"
#include "receiver_stream.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>

namespace send_airplay2::detail {
enum class AuthCommand { pair, verify, forget };
struct AuthOptions {
    AuthCommand command = AuthCommand::verify;
    ReceiverEndpoint endpoint;
    std::string profile;
    std::chrono::milliseconds timeout{10000};
    std::chrono::milliseconds pin_timeout{60000};
};
/** Parse the complete auth CLI before any storage/network/PIN side effects.
 * argv begins with the command. No PIN/credential value is accepted in arguments.
 * Explicit numeric address + profile for pair/verify; forget accepts a profile only.
 * Repeated/unknown options, ports outside 1..65535, invalid numeric addresses and
 * timeouts outside 1..60000 ms are argument errors. PIN timeout applies to pair only.
 */
[[nodiscard]] AuthOptions parse_auth_options(int argc, const char* const* argv);

/** Fixed erasing PIN owner; only terminal digits are accepted, retaining zeros.
 * Noncopyable/nonmovable. append returns false beyond eight digits; deletion/clear
 * erase previous characters. Never print, log or serialize this object.
 */
class PinCode {
public:
    PinCode() = default;
    ~PinCode();
    PinCode(const PinCode&) = delete;
    PinCode& operator=(const PinCode&) = delete;
    PinCode(PinCode&&) = delete;
    PinCode& operator=(PinCode&&) = delete;
    bool append(char digit) noexcept;
    void backspace() noexcept;
    void clear() noexcept;
    [[nodiscard]] bool complete() const noexcept {
        return size_ >= 4 && size_ <= 8;
    }
    [[nodiscard]] std::string_view view() const noexcept {
        return {digits_.data(), size_};
    }

private:
    std::array<char, 8> digits_{};
    std::size_t size_ = 0;
};
class PinPrompt {
public:
    PinPrompt() = default;
    virtual ~PinPrompt() = default;
    PinPrompt(const PinPrompt&) = delete;
    PinPrompt& operator=(const PinPrompt&) = delete;
    PinPrompt(PinPrompt&&) = delete;
    PinPrompt& operator=(PinPrompt&&) = delete;
    /// Enforce interactive, hidden input before requesting PIN display on the receiver.
    virtual void prepare() = 0;
    virtual void read(PinCode& output, const ReceiverOperation& operation) = 0;
};

/** Narrow host seam for testing orchestration separately from protocol crypto.
 * Native implementation owns ReceiverConnection. No extra trust/secret accessors.
 */
class AuthSession {
public:
    AuthSession() = default;
    virtual ~AuthSession() = default;
    AuthSession(const AuthSession&) = delete;
    AuthSession& operator=(const AuthSession&) = delete;
    AuthSession(AuthSession&&) = delete;
    AuthSession& operator=(AuthSession&&) = delete;
    virtual void begin(Bytes controller_id, const ReceiverOperation& operation) = 0;
    virtual std::unique_ptr<PairCredentials> finish(std::string_view pin,
                                                    const ReceiverOperation& operation) = 0;
    virtual void verify(const PairCredentials& credentials, const ReceiverOperation& operation) = 0;
};
class AuthSessionFactory {
public:
    AuthSessionFactory() = default;
    virtual ~AuthSessionFactory() = default;
    AuthSessionFactory(const AuthSessionFactory&) = delete;
    AuthSessionFactory& operator=(const AuthSessionFactory&) = delete;
    AuthSessionFactory(AuthSessionFactory&&) = delete;
    AuthSessionFactory& operator=(AuthSessionFactory&&) = delete;
    virtual std::unique_ptr<AuthSession> connect(const ReceiverEndpoint& endpoint,
                                                 const ReceiverOperation& operation) = 0;
};
[[nodiscard]] std::unique_ptr<AuthSessionFactory> native_auth_sessions();

/** Serial host workflow; caller owns stores/factory/prompt/output and cancellation.
 * Pair refuses an existing profile, enrolls, saves only authenticated M6 credentials,
 * reloads the trusted store, then verifies on a fresh socket. A failed reconnect
 * retains saved credentials and emits a saved checkpoint for explicit retry.
 * Failures before M6/store commit never save. No automatic re-pairing or key replacement.
 * Forget deletes local storage only. Progress/errors contain no PIN/key/transcript.
 * Each network phase has a fresh bounded deadline; time in the PIN UI is separate.
 */
void run_auth_workflow(const AuthOptions& options, CredentialStore& store,
                       AuthSessionFactory& sessions, PinPrompt& prompt, std::ostream& progress,
                       const std::atomic_bool* cancelled = nullptr);
} // namespace send_airplay2::detail
#endif
