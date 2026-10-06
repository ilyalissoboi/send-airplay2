// SPDX-License-Identifier: Apache-2.0
#include "pair_setup.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace {
constexpr std::uint8_t method_tag = 0;
constexpr std::uint8_t identifier_tag = 1;
constexpr std::uint8_t salt_tag = 2;
constexpr std::uint8_t public_key_tag = 3;
constexpr std::uint8_t proof_tag = 4;
constexpr std::uint8_t encrypted_tag = 5;
constexpr std::uint8_t state_tag = 6;
constexpr std::uint8_t error_tag = 7;
constexpr std::uint8_t signature_tag = 10;

void validate_identifier(const Bytes& value) {
    if (value.empty() || value.size() > pair_setup::max_identifier_size) {
        throw PairSetupException(PairSetupError::invalid_message);
    }
}
std::vector<TlvField> parse_schema(const Bytes& body, std::initializer_list<std::uint8_t> allowed) {
    if (body.size() > pair_setup::max_body_size) {
        throw PairSetupException(PairSetupError::invalid_message);
    }
    std::vector<TlvField> fields;
    try {
        fields = decode_tlv(body);
    } catch (const std::invalid_argument&) {
        throw PairSetupException(PairSetupError::invalid_message);
    }
    std::array<bool, 256> seen{};
    for (const auto& field : fields) {
        if (seen[field.type] ||
            std::find(allowed.begin(), allowed.end(), field.type) == allowed.end()) {
            throw PairSetupException(PairSetupError::invalid_message);
        }
        seen[field.type] = true;
    }
    return fields;
}
const Bytes& required_field(const std::vector<TlvField>& fields, std::uint8_t tag) {
    const auto found = std::find_if(fields.begin(), fields.end(),
                                    [tag](const auto& field) { return field.type == tag; });
    if (found == fields.end()) {
        throw PairSetupException(PairSetupError::invalid_message);
    }
    return found->value;
}
std::vector<TlvField> response_fields(unsigned status, const Bytes& body, std::uint8_t state,
                                      std::initializer_list<std::uint8_t> allowed) {
    if (status != 200) {
        throw PairSetupException(PairSetupError::peer_rejected);
    }
    auto fields = parse_schema(body, allowed);
    if (required_field(fields, state_tag) != Bytes{state}) {
        throw PairSetupException(PairSetupError::invalid_message);
    }
    for (const auto& field : fields) {
        if (field.type == error_tag) {
            if (field.value.size() != 1 || field.value[0] == 0) {
                throw PairSetupException(PairSetupError::invalid_message);
            }
            throw PairSetupException(PairSetupError::peer_rejected);
        }
    }
    return fields;
}
template <std::size_t Size> std::array<std::uint8_t, Size> fixed_field(const Bytes& bytes) {
    if (bytes.size() != Size) {
        throw PairSetupException(PairSetupError::invalid_message);
    }
    std::array<std::uint8_t, Size> value{};
    std::copy(bytes.begin(), bytes.end(), value.begin());
    return value;
}
template <std::size_t Size> Bytes wire_bytes(const std::array<std::uint8_t, Size>& value) {
    return Bytes(value.begin(), value.end());
}
template <std::size_t Size> ControlNonce setup_nonce(const char (&label)[Size]) {
    static_assert(Size == 9, "Pair-setup nonce labels are eight bytes plus terminator");
    ControlNonce nonce{};
    constexpr std::size_t zero_prefix_size = 4;
    std::copy_n(label, Size - 1, nonce.begin() + zero_prefix_size);
    return nonce;
}
Bytes signed_identity(const Secret32& derived, const Bytes& identifier, const PublicKey& key) {
    Bytes message;
    message.reserve(derived.bytes.size() + identifier.size() + key.size());
    message.insert(message.end(), derived.bytes.begin(), derived.bytes.end());
    message.insert(message.end(), identifier.begin(), identifier.end());
    message.insert(message.end(), key.begin(), key.end());
    return message;
}
/// The signing transcript contains a secret-derived prefix. Erase it on every exit.
struct SigningInput {
    Bytes bytes;
    SigningInput(const Secret32& derived, const Bytes& id, const PublicKey& key)
        : bytes(signed_identity(derived, id, key)) {}
    ~SigningInput() {
        cleanse(bytes.data(), bytes.size());
    }
    SigningInput(const SigningInput&) = delete;
    SigningInput& operator=(const SigningInput&) = delete;
    SigningInput(SigningInput&&) = delete;
    SigningInput& operator=(SigningInput&&) = delete;
};
[[noreturn]] void rethrow_crypto(const ControlException& error) {
    throw PairSetupException(error.reason() == ControlError::authentication
                                 ? PairSetupError::authentication
                                 : PairSetupError::backend);
}
} // namespace

PairSetup::PairSetup(Bytes controller_id) : controller_id_(std::move(controller_id)) {
    validate_identifier(controller_id_);
    try {
        generate_ed25519_seed(controller_seed_);
    } catch (const ControlException& error) {
        rethrow_crypto(error);
    }
}
PairSetup::~PairSetup() {
    close();
}
void PairSetup::require_state(State expected) const {
    if (state_ != expected) {
        throw PairSetupException(PairSetupError::unexpected_state);
    }
}
Bytes PairSetup::start() {
    try {
        require_state(State::ready);
        auto m1 = encode_tlv({{method_tag, {0}}, {state_tag, {1}}});
        state_ = State::waiting_m2;
        return m1;
    } catch (...) {
        close();
        throw;
    }
}
Bytes PairSetup::respond(std::string_view pin, unsigned http_status, const Bytes& m2) {
    try {
        require_state(State::waiting_m2);
        const auto fields =
            response_fields(http_status, m2, 2, {state_tag, salt_tag, public_key_tag, error_tag});
        const auto response = srp_.begin(pin, required_field(fields, salt_tag),
                                         required_field(fields, public_key_tag));
        auto m3 = encode_tlv({{state_tag, {3}},
                              {public_key_tag, response.public_key},
                              {proof_tag, wire_bytes(response.proof)}});
        state_ = State::waiting_m4;
        return m3;
    } catch (...) {
        close();
        throw;
    }
}
Bytes PairSetup::confirm(unsigned http_status, const Bytes& m4) {
    try {
        require_state(State::waiting_m4);
        const auto fields = response_fields(http_status, m4, 4, {state_tag, proof_tag, error_tag});
        // This must authenticate the server before any credential/signature is emitted.
        srp_.complete(required_field(fields, proof_tag), session_secret_);
        derive_setup_key(session_secret_, "Pair-Setup-Encrypt-Salt", "Pair-Setup-Encrypt-Info",
                         encryption_key_);
        Secret32 controller_sign;
        derive_setup_key(session_secret_, "Pair-Setup-Controller-Sign-Salt",
                         "Pair-Setup-Controller-Sign-Info", controller_sign);
        const auto public_key = ed25519_public(controller_seed_);
        const SigningInput transcript(controller_sign, controller_id_, public_key);
        const auto signature = ed25519_sign(controller_seed_, transcript.bytes);
        const auto plaintext = encode_tlv({{identifier_tag, controller_id_},
                                           {public_key_tag, wire_bytes(public_key)},
                                           {signature_tag, wire_bytes(signature)}});
        const auto encrypted =
            seal_record(encryption_key_.bytes, setup_nonce("PS-Msg05"), {}, plaintext);
        auto m5 = encode_tlv({{state_tag, {5}}, {encrypted_tag, encrypted}});
        state_ = State::waiting_m6;
        return m5;
    } catch (const ControlException& error) {
        close();
        rethrow_crypto(error);
    } catch (...) {
        close();
        throw;
    }
}
std::unique_ptr<PairCredentials> PairSetup::finish(unsigned http_status, const Bytes& m6) {
    try {
        require_state(State::waiting_m6);
        const auto fields =
            response_fields(http_status, m6, 6, {state_tag, encrypted_tag, error_tag});
        const auto& encrypted = required_field(fields, encrypted_tag);
        if (encrypted.size() < auth_tag_size || encrypted.size() > pair_setup::max_encrypted_size) {
            throw PairSetupException(PairSetupError::invalid_message);
        }
        const auto plaintext =
            open_record(encryption_key_.bytes, setup_nonce("PS-Msg06"), {}, encrypted);
        const auto identity =
            parse_schema(plaintext, {identifier_tag, public_key_tag, signature_tag});
        const auto& receiver_id = required_field(identity, identifier_tag);
        validate_identifier(receiver_id);
        const auto receiver_key = fixed_field<32>(required_field(identity, public_key_tag));
        const auto signature = fixed_field<64>(required_field(identity, signature_tag));
        Secret32 accessory_sign;
        derive_setup_key(session_secret_, "Pair-Setup-Accessory-Sign-Salt",
                         "Pair-Setup-Accessory-Sign-Info", accessory_sign);
        const SigningInput transcript(accessory_sign, receiver_id, receiver_key);
        if (!ed25519_verify(receiver_key, transcript.bytes, signature)) {
            throw PairSetupException(PairSetupError::authentication);
        }
        auto credentials = std::make_unique<PairCredentials>(receiver_id, receiver_key,
                                                             controller_id_, controller_seed_);
        close();
        return credentials;
    } catch (const ControlException& error) {
        close();
        rethrow_crypto(error);
    } catch (...) {
        close();
        throw;
    }
}
void PairSetup::close() noexcept {
    srp_.close();
    controller_seed_.clear();
    session_secret_.clear();
    encryption_key_.clear();
    state_ = State::closed;
}
} // namespace send_airplay2::detail
