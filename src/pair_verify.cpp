// SPDX-License-Identifier: Apache-2.0
#include "pair_verify.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace {
// HAP TLV tags. The generic codec is schema-neutral; this exchange is deliberately strict.
constexpr std::uint8_t identifier_tag = 1;
constexpr std::uint8_t public_key_tag = 3;
constexpr std::uint8_t encrypted_tag = 5;
constexpr std::uint8_t state_tag = 6;
constexpr std::uint8_t error_tag = 7;
constexpr std::uint8_t signature_tag = 10;

const char* error_message(PairVerifyError reason) noexcept {
    switch (reason) {
    case PairVerifyError::invalid_message:
        return "Invalid pair-verify message";
    case PairVerifyError::authentication:
        return "Pair-verify authentication failed";
    case PairVerifyError::peer_rejected:
        return "Peer rejected pair verification";
    case PairVerifyError::unexpected_state:
        return "Unexpected pair-verify state";
    case PairVerifyError::backend:
        return "Pair-verify cryptographic backend failed";
    }
    return "Unknown pair-verify error";
}

void validate_identifier(const Bytes& identifier) {
    if (identifier.empty() || identifier.size() > pair_verify::max_identifier_size) {
        throw PairVerifyException(PairVerifyError::invalid_message);
    }
}

std::vector<TlvField> parse_schema(const Bytes& body, std::initializer_list<std::uint8_t> allowed) {
    if (body.size() > pair_verify::max_body_size) {
        throw PairVerifyException(PairVerifyError::invalid_message);
    }
    std::vector<TlvField> fields;
    try {
        fields = decode_tlv(body);
    } catch (const std::invalid_argument&) {
        throw PairVerifyException(PairVerifyError::invalid_message);
    }
    std::array<bool, 256> seen{};
    for (const auto& field : fields) {
        // Reject even nonadjacent duplicates; no map overwrite or ambiguous signed fields.
        if (seen[field.type] ||
            std::find(allowed.begin(), allowed.end(), field.type) == allowed.end()) {
            throw PairVerifyException(PairVerifyError::invalid_message);
        }
        seen[field.type] = true;
    }
    return fields;
}
const Bytes& required_field(const std::vector<TlvField>& fields, std::uint8_t type) {
    const auto found = std::find_if(fields.begin(), fields.end(),
                                    [type](const auto& field) { return field.type == type; });
    if (found == fields.end()) {
        throw PairVerifyException(PairVerifyError::invalid_message);
    }
    return found->value;
}
void validate_response(unsigned http_status, const std::vector<TlvField>& fields,
                       std::uint8_t state) {
    if (http_status != 200) {
        throw PairVerifyException(PairVerifyError::peer_rejected);
    }
    if (required_field(fields, state_tag) != Bytes{state}) {
        throw PairVerifyException(PairVerifyError::invalid_message);
    }
    for (const auto& field : fields) {
        if (field.type == error_tag) {
            if (field.value.size() != 1 || field.value[0] == 0) {
                throw PairVerifyException(PairVerifyError::invalid_message);
            }
            throw PairVerifyException(PairVerifyError::peer_rejected);
        }
    }
}
template <std::size_t Size> std::array<std::uint8_t, Size> fixed_field(const Bytes& field) {
    if (field.size() != Size) {
        throw PairVerifyException(PairVerifyError::invalid_message);
    }
    std::array<std::uint8_t, Size> result{};
    std::copy(field.begin(), field.end(), result.begin());
    return result;
}
template <std::size_t Size> Bytes wire_bytes(const std::array<std::uint8_t, Size>& value) {
    return Bytes(value.begin(), value.end());
}
Bytes signed_identity(const PublicKey& first, const Bytes& identifier, const PublicKey& second) {
    Bytes message;
    message.reserve(first.size() + identifier.size() + second.size());
    message.insert(message.end(), first.begin(), first.end());
    message.insert(message.end(), identifier.begin(), identifier.end());
    message.insert(message.end(), second.begin(), second.end());
    return message;
}
template <std::size_t Size> ControlNonce verification_nonce(const char (&label)[Size]) {
    static_assert(Size == 9, "Pair-verify nonce labels have eight bytes plus a terminator");
    // The named nonce is eight bytes, left padded to the RFC 8439 12-byte layout.
    ControlNonce nonce{};
    constexpr std::size_t prefix_size = 4;
    std::copy_n(label, Size - 1, nonce.begin() + prefix_size);
    return nonce;
}
void derive_key(const Secret32& shared, std::string_view salt, std::string_view info,
                Secret32& output) {
    // The existing HKDF adapter accepts a vector; own/erase this temporary copy.
    struct SecretInput {
        Bytes bytes;
        explicit SecretInput(const Secret32& secret)
            : bytes(secret.bytes.begin(), secret.bytes.end()) {}
        ~SecretInput() {
            cleanse(bytes.data(), bytes.size());
        }
        SecretInput(const SecretInput&) = delete;
        SecretInput& operator=(const SecretInput&) = delete;
        SecretInput(SecretInput&&) = delete;
        SecretInput& operator=(SecretInput&&) = delete;
    } input(shared);
    auto derived = derive_control_key(input.bytes, salt, info);
    output.bytes = derived;
    cleanse(derived.data(), derived.size());
}
[[noreturn]] void rethrow_crypto(const ControlException& error) {
    throw PairVerifyException(error.reason() == ControlError::authentication
                                  ? PairVerifyError::authentication
                                  : PairVerifyError::backend);
}
} // namespace

PairVerifyException::PairVerifyException(PairVerifyError reason)
    : std::runtime_error(error_message(reason)), reason_(reason) {}
PairCredentials::PairCredentials(Bytes receiver_id, const PublicKey& receiver_key, Bytes client_id,
                                 const Secret32& client_seed)
    : receiver_id_(std::move(receiver_id)), receiver_key_(receiver_key),
      client_id_(std::move(client_id)), client_seed_(client_seed.bytes) {
    validate_identifier(receiver_id_);
    validate_identifier(client_id_);
}
PairVerifier::PairVerifier(const PairCredentials& credentials) : credentials_(credentials) {
    try {
        generate_x25519_seed(ephemeral_);
    } catch (const ControlException& error) {
        rethrow_crypto(error);
    }
}
PairVerifier::~PairVerifier() {
    close();
}
void PairVerifier::require_state(State expected) const {
    if (state_ != expected) {
        throw PairVerifyException(PairVerifyError::unexpected_state);
    }
}
Bytes PairVerifier::start() {
    try {
        require_state(State::ready);
        client_public_ = x25519_public(ephemeral_);
        auto m1 = encode_tlv({{state_tag, {1}}, {public_key_tag, wire_bytes(client_public_)}});
        state_ = State::waiting_m2;
        return m1;
    } catch (const ControlException& error) {
        close();
        rethrow_crypto(error);
    } catch (...) {
        close();
        throw;
    }
}
Bytes PairVerifier::respond(unsigned http_status, const Bytes& m2) {
    try {
        require_state(State::waiting_m2);
        const auto fields = parse_schema(m2, {state_tag, public_key_tag, encrypted_tag, error_tag});
        validate_response(http_status, fields, 2);
        const auto peer_public = fixed_field<32>(required_field(fields, public_key_tag));
        const auto& encrypted = required_field(fields, encrypted_tag);
        if (encrypted.size() < auth_tag_size ||
            encrypted.size() > pair_verify::max_encrypted_size) {
            throw PairVerifyException(PairVerifyError::invalid_message);
        }
        x25519_shared(ephemeral_, peer_public, shared_);
        derive_key(shared_, "Pair-Verify-Encrypt-Salt", "Pair-Verify-Encrypt-Info", session_key_);
        const auto plaintext =
            open_record(session_key_.bytes, verification_nonce("PV-Msg02"), {}, encrypted);
        const auto identity = parse_schema(plaintext, {identifier_tag, signature_tag});
        const auto& receiver_id = required_field(identity, identifier_tag);
        const auto signature = fixed_field<64>(required_field(identity, signature_tag));
        if (receiver_id != credentials_.receiver_id_ ||
            !ed25519_verify(credentials_.receiver_key_,
                            signed_identity(peer_public, receiver_id, client_public_), signature)) {
            throw PairVerifyException(PairVerifyError::authentication);
        }
        // Only after pinned identity verification may the controller sign its transcript.
        const auto client_signature =
            ed25519_sign(credentials_.client_seed_,
                         signed_identity(client_public_, credentials_.client_id_, peer_public));
        const auto response = encode_tlv({{identifier_tag, credentials_.client_id_},
                                          {signature_tag, wire_bytes(client_signature)}});
        const auto sealed =
            seal_record(session_key_.bytes, verification_nonce("PV-Msg03"), {}, response);
        auto m3 = encode_tlv({{state_tag, {3}}, {encrypted_tag, sealed}});
        ephemeral_.clear();
        session_key_.clear();
        state_ = State::waiting_m4;
        return m3;
    } catch (const ControlException& error) {
        close();
        rethrow_crypto(error);
    } catch (...) {
        close();
        throw;
    }
}
void PairVerifier::finish(unsigned http_status, const Bytes& m4) {
    try {
        require_state(State::waiting_m4);
        const auto fields = parse_schema(m4, {state_tag, error_tag});
        validate_response(http_status, fields, 4);
        derive_key(shared_, "Control-Salt", "Control-Write-Encryption-Key", write_key_);
        derive_key(shared_, "Control-Salt", "Control-Read-Encryption-Key", read_key_);
        shared_.clear();
        state_ = State::verified;
    } catch (const ControlException& error) {
        close();
        rethrow_crypto(error);
    } catch (...) {
        close();
        throw;
    }
}
void PairVerifier::take_control_keys(Secret32& write_key, Secret32& read_key) {
    try {
        require_state(State::verified);
        if (&write_key == &read_key) {
            throw PairVerifyException(PairVerifyError::unexpected_state);
        }
        write_key.bytes = write_key_.bytes;
        read_key.bytes = read_key_.bytes;
        close();
    } catch (...) {
        close();
        throw;
    }
}
void PairVerifier::close() noexcept {
    ephemeral_.clear();
    shared_.clear();
    session_key_.clear();
    write_key_.clear();
    read_key_.clear();
    state_ = State::closed;
}
} // namespace send_airplay2::detail
