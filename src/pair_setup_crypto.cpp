// SPDX-License-Identifier: Apache-2.0
#include "pair_setup_crypto.h"
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

namespace send_airplay2::detail {
namespace {
constexpr std::size_t srp_integer_size = 384;
constexpr std::size_t srp_salt_size = 16;
constexpr std::size_t min_pin_digits = 4;
constexpr std::size_t max_pin_digits = 8;
constexpr std::size_t max_hash_input_size = 65536;
constexpr std::string_view username = "Pair-Setup";
using HashContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using BigNumber = std::unique_ptr<BIGNUM, decltype(&BN_free)>;

const char* error_message(PairSetupError reason) noexcept {
    switch (reason) {
    case PairSetupError::invalid_message:
        return "Invalid pair-setup message";
    case PairSetupError::authentication:
        return "Pair-setup authentication failed";
    case PairSetupError::peer_rejected:
        return "Peer rejected pair setup";
    case PairSetupError::unexpected_state:
        return "Unexpected pair-setup state";
    case PairSetupError::backend:
        return "Pair-setup cryptographic backend failed";
    }
    return "Unknown pair-setup error";
}
void require_success(bool success) {
    if (!success) {
        throw PairSetupException(PairSetupError::backend);
    }
}
HashInput view(const Bytes& bytes) {
    return {bytes.data(), bytes.size()};
}
HashInput view(const Digest64& bytes) {
    return {bytes.data(), bytes.size()};
}
HashInput minimal_integer(const std::uint8_t* bytes, std::size_t length) {
    // HAP proof/session-key hashes use minimal integer encodings. u/k padding is
    // performed inside Botan's SRP implementation, not by this transcript helper.
    std::size_t offset = 0;
    while (offset + 1 < length && bytes[offset] == 0) {
        ++offset;
    }
    return {bytes + offset, length - offset};
}
Bytes modulus_bytes() {
    // RFC 5054's 3072-bit SRP group uses the RFC 3526 prime, with generator five.
    BigNumber prime(BN_get_rfc3526_prime_3072(nullptr), BN_free);
    require_success(prime != nullptr);
    Bytes bytes(srp_integer_size);
    require_success(BN_bn2binpad(prime.get(), bytes.data(), static_cast<int>(bytes.size())) ==
                    static_cast<int>(bytes.size()));
    return bytes;
}
struct PinInput {
    std::array<char, max_pin_digits + 1> bytes{}; // Terminating NUL required by the C FFI.
    explicit PinInput(std::string_view pin) {
        if (pin.size() < min_pin_digits || pin.size() > max_pin_digits ||
            !std::all_of(pin.begin(), pin.end(),
                         [](char digit) { return digit >= '0' && digit <= '9'; })) {
            throw PairSetupException(PairSetupError::invalid_message);
        }
        std::copy(pin.begin(), pin.end(), bytes.begin());
    }
    ~PinInput() {
        cleanse(bytes.data(), bytes.size());
    }
    PinInput(const PinInput&) = delete;
    PinInput& operator=(const PinInput&) = delete;
    PinInput(PinInput&&) = delete;
    PinInput& operator=(PinInput&&) = delete;
};
} // namespace

PairSetupException::PairSetupException(PairSetupError reason)
    : std::runtime_error(error_message(reason)), reason_(reason) {}
Secret64::~Secret64() {
    clear();
}
void Secret64::clear() noexcept {
    cleanse(bytes.data(), bytes.size());
}
void hash_sha512(Digest64& output, std::initializer_list<HashInput> inputs) {
    HashContext context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    require_success(context != nullptr);
    require_success(EVP_DigestInit_ex(context.get(), EVP_sha512(), nullptr) == 1);
    for (const auto& input : inputs) {
        if (input.size > max_hash_input_size || (input.size != 0 && input.data == nullptr)) {
            throw PairSetupException(PairSetupError::invalid_message);
        }
        if (input.size != 0) {
            require_success(EVP_DigestUpdate(context.get(), input.data, input.size) == 1);
        }
    }
    unsigned length = 0;
    require_success(EVP_DigestFinal_ex(context.get(), output.data(), &length) == 1 &&
                    length == output.size());
}
void derive_setup_key(const Secret64& secret, std::string_view salt, std::string_view info,
                      Secret32& output) {
    struct SecretInput {
        Bytes bytes;
        explicit SecretInput(const Secret64& source)
            : bytes(source.bytes.begin(), source.bytes.end()) {}
        ~SecretInput() {
            cleanse(bytes.data(), bytes.size());
        }
        SecretInput(const SecretInput&) = delete;
        SecretInput& operator=(const SecretInput&) = delete;
        SecretInput(SecretInput&&) = delete;
        SecretInput& operator=(SecretInput&&) = delete;
    } input(secret);
    auto key = derive_control_key(input.bytes, salt, info);
    output.bytes = key;
    cleanse(key.data(), key.size());
}
SrpClient::SrpClient() {
    if (botan_rng_init(&rng_, "system") != 0) {
        if (rng_ != nullptr) {
            (void)botan_rng_destroy(rng_);
            rng_ = nullptr;
        }
        throw PairSetupException(PairSetupError::backend);
    }
}
SrpClient::~SrpClient() {
    close();
}
SrpResponse SrpClient::begin(std::string_view pin, const Bytes& salt, const Bytes& server_public) {
    try {
        if (closed_ || waiting_proof_) {
            throw PairSetupException(PairSetupError::unexpected_state);
        }
        PinInput password(pin);
        if (salt.size() != srp_salt_size || server_public.empty() ||
            server_public.size() > srp_integer_size) {
            throw PairSetupException(PairSetupError::invalid_message);
        }
        const auto modulus = modulus_bytes();
        Bytes padded_server(srp_integer_size, 0);
        std::copy(server_public.begin(), server_public.end(),
                  padded_server.end() - static_cast<std::ptrdiff_t>(server_public.size()));
        if (std::all_of(padded_server.begin(), padded_server.end(),
                        [](auto byte) { return byte == 0; }) ||
            padded_server >= modulus) {
            throw PairSetupException(PairSetupError::authentication);
        }
        struct SharedInteger {
            std::array<std::uint8_t, srp_integer_size> bytes{};
            ~SharedInteger() {
                cleanse(bytes.data(), bytes.size());
            }
            SharedInteger() = default;
            SharedInteger(const SharedInteger&) = delete;
            SharedInteger& operator=(const SharedInteger&) = delete;
            SharedInteger(SharedInteger&&) = delete;
            SharedInteger& operator=(SharedInteger&&) = delete;
        } shared;
        SrpResponse response{Bytes(srp_integer_size), {}};
        auto public_length = response.public_key.size();
        auto shared_length = shared.bytes.size();
        const auto result = botan_srp6_client_agree(
            "Pair-Setup", password.bytes.data(), "modp/srp/3072", "SHA-512", salt.data(),
            salt.size(), padded_server.data(), padded_server.size(), rng_,
            response.public_key.data(), &public_length, shared.bytes.data(), &shared_length);
        require_success(result == 0 && public_length == srp_integer_size &&
                        shared_length == srp_integer_size);
        constexpr std::array<std::uint8_t, srp_integer_size> zero{};
        if (CRYPTO_memcmp(shared.bytes.data(), zero.data(), zero.size()) == 0) {
            throw PairSetupException(PairSetupError::authentication);
        }
        hash_sha512(key_.bytes, {minimal_integer(shared.bytes.data(), shared.bytes.size())});
        Digest64 modulus_hash{}, generator_hash{}, user_hash{};
        const std::uint8_t generator = 5;
        hash_sha512(modulus_hash, {view(modulus)});
        hash_sha512(generator_hash, {{&generator, 1}});
        hash_sha512(user_hash,
                    {{reinterpret_cast<const std::uint8_t*>(username.data()), username.size()}});
        for (std::size_t index = 0; index < modulus_hash.size(); ++index) {
            modulus_hash[index] ^= generator_hash[index];
        }
        const auto public_view =
            minimal_integer(response.public_key.data(), response.public_key.size());
        const auto server_view = minimal_integer(padded_server.data(), padded_server.size());
        hash_sha512(response.proof, {view(modulus_hash), view(user_hash), view(salt), public_view,
                                     server_view, view(key_.bytes)});
        hash_sha512(expected_proof_.bytes, {public_view, view(response.proof), view(key_.bytes)});
        waiting_proof_ = true;
        return response;
    } catch (...) {
        close();
        throw;
    }
}
void SrpClient::complete(const Bytes& server_proof, Secret64& key) {
    try {
        if (closed_ || !waiting_proof_) {
            throw PairSetupException(PairSetupError::unexpected_state);
        }
        if (server_proof.size() != expected_proof_.bytes.size()) {
            throw PairSetupException(PairSetupError::invalid_message);
        }
        if (CRYPTO_memcmp(server_proof.data(), expected_proof_.bytes.data(), server_proof.size()) !=
            0) {
            throw PairSetupException(PairSetupError::authentication);
        }
        key.bytes = key_.bytes;
        close();
    } catch (...) {
        close();
        throw;
    }
}
void SrpClient::close() noexcept {
    key_.clear();
    expected_proof_.clear();
    if (rng_ != nullptr) {
        (void)botan_rng_destroy(rng_);
        rng_ = nullptr;
    }
    waiting_proof_ = false;
    closed_ = true;
}
} // namespace send_airplay2::detail
