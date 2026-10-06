// SPDX-License-Identifier: Apache-2.0
#include "auth_workflow.h"
#include "control_crypto.h"
#include "receiver_connection.h"
#include <charconv>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace send_airplay2::detail {
namespace {
std::uint32_t number(std::string_view value, std::uint32_t limit, bool allow_zero = false) {
    std::uint32_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        (!result && !allow_zero) || result > limit) {
        throw std::invalid_argument("Invalid numeric authentication option");
    }
    return result;
}
void validate_endpoint(const ReceiverEndpoint& endpoint) {
    in_addr ipv4{};
    in6_addr ipv6{};
    if (endpoint.address.size() > 64 || endpoint.address.find('\0') != std::string::npos ||
        !endpoint.port ||
        (inet_pton(AF_INET, endpoint.address.c_str(), &ipv4) != 1 &&
         inet_pton(AF_INET6, endpoint.address.c_str(), &ipv6) != 1) ||
        (endpoint.scope_id && endpoint.address.find(':') == std::string::npos)) {
        throw std::invalid_argument("Address must be numeric IPv4/IPv6; scope is IPv6 only");
    }
}
Bytes new_controller_id() {
    Secret32 randomness;
    generate_ed25519_seed(randomness);
    // A fresh UUID-shaped opaque controller ID, independent of the signing seed.
    randomness.bytes[6] = (randomness.bytes[6] & 0x0f) | 0x40;
    randomness.bytes[8] = (randomness.bytes[8] & 0x3f) | 0x80;
    constexpr char hex[] = "0123456789abcdef";
    Bytes output;
    output.reserve(36);
    for (std::size_t index = 0; index < 16; ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            output.push_back('-');
        }
        output.push_back(static_cast<std::uint8_t>(hex[randomness.bytes[index] >> 4]));
        output.push_back(static_cast<std::uint8_t>(hex[randomness.bytes[index] & 15]));
    }
    return output;
}
class NativeAuthSession final : public AuthSession {
    ReceiverConnection connection_;

public:
    NativeAuthSession(const ReceiverEndpoint& endpoint, const ReceiverOperation& operation)
        : connection_(connect_receiver(endpoint, operation), endpoint.authority()) {}
    void begin(Bytes controller_id, const ReceiverOperation& operation) override {
        connection_.begin_pairing(std::move(controller_id), operation);
    }
    std::unique_ptr<PairCredentials> finish(std::string_view pin,
                                            const ReceiverOperation& operation) override {
        return connection_.finish_pairing(pin, operation);
    }
    void verify(const PairCredentials& credentials, const ReceiverOperation& operation) override {
        connection_.verify(credentials, operation);
    }
};
class NativeAuthFactory final : public AuthSessionFactory {
public:
    std::unique_ptr<AuthSession> connect(const ReceiverEndpoint& endpoint,
                                         const ReceiverOperation& operation) override {
        return std::make_unique<NativeAuthSession>(endpoint, operation);
    }
};
} // namespace
AuthOptions parse_auth_options(int argc, const char* const* argv) {
    if (argc < 1 || !argv || !argv[0]) {
        throw std::invalid_argument("Missing authentication command");
    }
    AuthOptions options;
    const std::string_view command(argv[0]);
    if (command == "pair") {
        options.command = AuthCommand::pair;
    } else if (command == "verify") {
        options.command = AuthCommand::verify;
    } else if (command == "forget") {
        options.command = AuthCommand::forget;
    } else {
        throw std::invalid_argument("Unknown authentication command");
    }
    options.endpoint.port = 7000;
    unsigned seen = 0;
    constexpr unsigned address_bit = 1, profile_bit = 2, port_bit = 4, scope_bit = 8,
                       timeout_bit = 16, pin_timeout_bit = 32;
    for (int index = 1; index < argc; ++index) {
        if (!argv[index] || index + 1 >= argc || !argv[index + 1]) {
            throw std::invalid_argument("Incomplete authentication option");
        }
        const std::string_view argument(argv[index]);
        const std::string_view value(argv[++index]);
        unsigned bit = 0;
        if (argument == "--address") {
            if (value.size() > 64) {
                throw std::invalid_argument("Address is too long");
            }
            bit = address_bit;
            options.endpoint.address = value;
        } else if (argument == "--profile") {
            if (value.size() > 64) {
                throw std::invalid_argument("Profile is too long");
            }
            bit = profile_bit;
            options.profile = value;
        } else if (argument == "--port") {
            bit = port_bit;
            options.endpoint.port = static_cast<std::uint16_t>(number(value, 65535));
        } else if (argument == "--scope-id") {
            bit = scope_bit;
            options.endpoint.scope_id = number(value, UINT32_MAX, true);
        } else if (argument == "--timeout-ms") {
            bit = timeout_bit;
            options.timeout = std::chrono::milliseconds(number(value, 60000));
        } else if (argument == "--pin-timeout-ms") {
            bit = pin_timeout_bit;
            options.pin_timeout = std::chrono::milliseconds(number(value, 60000));
        } else {
            throw std::invalid_argument("Unknown authentication option");
        }
        if (seen & bit) {
            throw std::invalid_argument("Repeated authentication option");
        }
        seen |= bit;
    }
    try {
        validate_credential_profile(options.profile);
    } catch (const CredentialException&) {
        throw std::invalid_argument(
            "Profile must be 1..64 lower-case letters/digits/._- and begin with a letter/digit");
    }
    if (options.command == AuthCommand::forget) {
        if (seen != profile_bit) {
            throw std::invalid_argument("forget accepts --profile only");
        }
    } else {
        if (!(seen & address_bit)) {
            throw std::invalid_argument("Authentication requires --address");
        }
        validate_endpoint(options.endpoint);
        if (options.command != AuthCommand::pair && (seen & pin_timeout_bit)) {
            throw std::invalid_argument("PIN timeout applies to pair only");
        }
    }
    return options;
}
PinCode::~PinCode() {
    clear();
}
bool PinCode::append(char digit) noexcept {
    if (digit < '0' || digit > '9' || size_ == digits_.size()) {
        return false;
    }
    digits_[size_++] = digit;
    return true;
}
void PinCode::backspace() noexcept {
    if (size_) {
        digits_[--size_] = 0;
    }
}
void PinCode::clear() noexcept {
    cleanse(digits_.data(), digits_.size());
    size_ = 0;
}
std::unique_ptr<AuthSessionFactory> native_auth_sessions() {
    return std::make_unique<NativeAuthFactory>();
}
void run_auth_workflow(const AuthOptions& options, CredentialStore& store,
                       AuthSessionFactory& sessions, PinPrompt& prompt, std::ostream& progress,
                       const std::atomic_bool* cancelled) {
    validate_credential_profile(options.profile);
    if (options.command != AuthCommand::pair && options.command != AuthCommand::verify &&
        options.command != AuthCommand::forget) {
        throw std::invalid_argument("Unknown authentication command");
    }
    if (options.command != AuthCommand::forget) {
        validate_endpoint(options.endpoint);
    }
    if (options.command == AuthCommand::pair) {
        (void)ReceiverOperation::after(options.pin_timeout, cancelled);
    }
    const auto operation = [&] { return ReceiverOperation::after(options.timeout, cancelled); };
    operation().check();
    if (options.command == AuthCommand::forget) {
        progress << (store.erase(options.profile)
                         ? "Local credentials deleted; receiver pairing is unchanged.\n"
                         : "No local credentials exist for this profile.\n");
        return;
    }
    auto credentials = store.load(options.profile);
    if (options.command == AuthCommand::pair) {
        if (credentials) {
            throw CredentialException(CredentialError::already_exists);
        }
        prompt.prepare(); // Fail before receiver PIN display if input cannot be hidden/interactive.
        auto deadline = operation();
        auto session = sessions.connect(options.endpoint, deadline);
        session->begin(new_controller_id(), deadline);
        PinCode pin;
        prompt.read(pin, ReceiverOperation::after(options.pin_timeout, cancelled));
        operation().check();
        if (!pin.complete()) {
            throw std::invalid_argument("PIN must contain 4..8 digits");
        }
        credentials = session->finish(pin.view(), operation());
        pin.clear();
        session.reset(); // Provisioning socket cannot be reused for verification.
        if (!credentials) {
            throw CredentialException(CredentialError::invalid_record);
        }
        operation().check();
        store.save_new(options.profile, *credentials);
        progress << "Pairing authenticated; credentials saved. Verifying a fresh connection.\n"
                 << std::flush;
        credentials.reset();
        credentials = store.load(options.profile);
    }
    if (!credentials) {
        throw CredentialException(CredentialError::invalid_record);
    }
    auto deadline = operation();
    auto session = sessions.connect(options.endpoint, deadline);
    session->verify(*credentials, deadline);
    deadline.check();
    progress << "Peer verification succeeded; encrypted control transport established. Playback is "
                "not implemented.\n";
}
} // namespace send_airplay2::detail
