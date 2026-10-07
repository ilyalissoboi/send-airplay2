// SPDX-License-Identifier: Apache-2.0
#include "host_credentials.h"
#include "send_airplay2/pairing.h"
#include "send_airplay2/playback.h"
#include "control_crypto.h"

#include <array>
#include <exception>
#include <ostream>
#include <stdexcept>
#include <utility>

namespace send_airplay2::detail {
namespace {
static_assert(SAP2_CREDENTIAL_RECORD_MAX == credential_format::max_blob_size,
              "the public record bound must match the version-1 envelope");

/// The host ended PIN entry (cancel or an unexpected result).
class PinCancelled : public std::runtime_error {
public:
    PinCancelled() : std::runtime_error("PIN entry cancelled") {}
};
/// read_pin returned after the PIN deadline.
class PinTimeout : public std::runtime_error {
public:
    PinTimeout() : std::runtime_error("PIN entry timed out") {}
};

/// Erases a fixed PIN buffer on every exit path.
struct PinBuffer {
    std::array<char, SAP2_MAX_PIN_DIGITS> digits{};
    PinBuffer() = default;
    ~PinBuffer() {
        cleanse(digits.data(), digits.size());
    }
    PinBuffer(const PinBuffer&) = delete;
    PinBuffer& operator=(const PinBuffer&) = delete;
    PinBuffer(PinBuffer&&) = delete;
    PinBuffer& operator=(PinBuffer&&) = delete;
};

/// Progress text from run_auth_workflow is CLI output; the C interface drops it.
class DiscardBuffer final : public std::streambuf {
protected:
    int_type overflow(int_type character) override {
        return traits_type::not_eof(character);
    }
};
} // namespace

HostCredentialStore::HostCredentialStore(const sap2_credential_store& callbacks) noexcept
    : callbacks_(callbacks) {}

std::unique_ptr<PairCredentials> HostCredentialStore::load(std::string_view profile) {
    const std::string name(profile);
    CredentialBlob record; // Erased on destruction, whatever the host wrote.
    std::size_t length = 0;
    const auto result = callbacks_.load(callbacks_.context, name.c_str(), record.bytes.data(),
                                        record.bytes.size(), &length);
    if (result == SAP2_STORE_ABSENT) {
        return nullptr;
    }
    if (result != SAP2_STORE_OK) {
        throw CredentialException(CredentialError::unavailable);
    }
    if (length == 0 || length > record.bytes.size()) {
        throw CredentialException(CredentialError::invalid_record);
    }
    record.size = length;
    return decode_credentials(record); // Rejects malformed or weak records.
}

void HostCredentialStore::save_new(std::string_view profile, const PairCredentials& credentials) {
    const std::string name(profile);
    CredentialBlob record;
    encode_credentials(credentials, record);
    const auto result =
        callbacks_.save_new(callbacks_.context, name.c_str(), record.bytes.data(), record.size);
    if (result == SAP2_STORE_EXISTS) {
        throw CredentialException(CredentialError::already_exists);
    }
    if (result != SAP2_STORE_OK) {
        throw CredentialException(CredentialError::unavailable);
    }
}

bool HostCredentialStore::erase(std::string_view profile) {
    const std::string name(profile);
    const auto result = callbacks_.erase(callbacks_.context, name.c_str());
    if (result == SAP2_STORE_ABSENT) {
        return false;
    }
    if (result != SAP2_STORE_OK) {
        throw CredentialException(CredentialError::unavailable);
    }
    return true;
}

bool valid_credential_store(const sap2_credential_store& callbacks) noexcept {
    return callbacks.struct_size >= sizeof(sap2_credential_store) && callbacks.load &&
           callbacks.save_new && callbacks.erase;
}

std::unique_ptr<CredentialStore> select_credential_store(const sap2_credential_store* host) {
    if (host) {
        return std::make_unique<HostCredentialStore>(*host);
    }
    return native_credential_store();
}

CallbackPinPrompt::CallbackPinPrompt(PinReader read_pin, void* context) noexcept
    : read_pin_(read_pin), context_(context) {}

void CallbackPinPrompt::read(PinCode& output, const ReceiverOperation& operation) {
    PinBuffer buffer;
    std::size_t length = 0;
    const auto result = read_pin_(context_, buffer.digits.data(), buffer.digits.size(), &length);
    if (result != SAP2_OK) {
        throw PinCancelled();
    }
    // The callback cannot be interrupted; its deadline is checked on return.
    if (std::chrono::steady_clock::now() >= operation.deadline) {
        throw PinTimeout();
    }
    if (length < SAP2_MIN_PIN_DIGITS || length > SAP2_MAX_PIN_DIGITS) {
        throw std::invalid_argument("PIN must contain 4..8 digits");
    }
    for (std::size_t index = 0; index < length; ++index) {
        if (!output.append(buffer.digits[index])) {
            output.clear();
            throw std::invalid_argument("PIN must contain only digits");
        }
    }
}

CastResult pair_profile(const PairSettings& settings, CredentialStore& store,
                        AuthSessionFactory& sessions, PinPrompt& prompt) noexcept {
    try {
        AuthOptions options;
        options.command = AuthCommand::pair;
        options.endpoint = settings.endpoint;
        options.profile = settings.profile;
        options.timeout = settings.timeout;
        options.pin_timeout = settings.pin_timeout;
        DiscardBuffer discard;
        std::ostream progress(&discard);
        run_auth_workflow(options, store, sessions, prompt, progress);
        return CastResult::ok;
    } catch (const PinCancelled&) {
        return CastResult::cancelled;
    } catch (const PinTimeout&) {
        return CastResult::pin_timeout;
    } catch (...) {
        unsigned rejected_status = 0;
        return cast_start_result(std::current_exception(), rejected_status);
    }
}

CastResult forget_profile(std::string_view profile, CredentialStore& store) noexcept {
    try {
        validate_credential_profile(profile);
        return store.erase(profile) ? CastResult::ok : CastResult::profile_not_found;
    } catch (...) {
        unsigned rejected_status = 0;
        return cast_start_result(std::current_exception(), rejected_status);
    }
}
} // namespace send_airplay2::detail
