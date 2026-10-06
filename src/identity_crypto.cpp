// SPDX-License-Identifier: Apache-2.0
#include "identity_crypto.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstddef>
#include <memory>

namespace send_airplay2::detail {
namespace {
constexpr std::size_t max_signed_message = 65536;
using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using KeyContext = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;
using DigestContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

void require_success(int result) {
    if (result != 1) {
        throw ControlException(ControlError::backend);
    }
}
Key private_key(int type, const Secret32& seed) {
    Key key(EVP_PKEY_new_raw_private_key(type, nullptr, seed.bytes.data(), seed.bytes.size()),
            EVP_PKEY_free);
    if (!key) {
        throw ControlException(ControlError::backend);
    }
    return key;
}
Key public_key(int type, const PublicKey& bytes) {
    Key key(EVP_PKEY_new_raw_public_key(type, nullptr, bytes.data(), bytes.size()), EVP_PKEY_free);
    if (!key) {
        throw ControlException(ControlError::backend);
    }
    return key;
}
PublicKey export_public(int type, const Secret32& seed) {
    const auto key = private_key(type, seed);
    PublicKey output{};
    auto length = output.size();
    require_success(EVP_PKEY_get_raw_public_key(key.get(), output.data(), &length));
    if (length != output.size()) {
        throw ControlException(ControlError::backend);
    }
    return output;
}
DigestContext signature_context(const Key& key, bool signing, std::size_t message_size) {
    if (message_size > max_signed_message) {
        throw ControlException(ControlError::invalid_length);
    }
    DigestContext context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context) {
        throw ControlException(ControlError::backend);
    }
    // Ed25519 performs its own hashing and requires a null digest and one-shot API.
    require_success(
        signing ? EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get())
                : EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()));
    return context;
}
} // namespace

Secret32::~Secret32() {
    clear();
}
void Secret32::clear() noexcept {
    cleanse(bytes.data(), bytes.size());
}
void generate_x25519_seed(Secret32& seed) {
    if (RAND_priv_bytes(seed.bytes.data(), static_cast<int>(seed.bytes.size())) != 1) {
        seed.clear();
        throw ControlException(ControlError::backend);
    }
}
PublicKey x25519_public(const Secret32& seed) {
    return export_public(EVP_PKEY_X25519, seed);
}
void generate_ed25519_seed(Secret32& seed) {
    if (RAND_priv_bytes(seed.bytes.data(), static_cast<int>(seed.bytes.size())) != 1) {
        seed.clear();
        throw ControlException(ControlError::backend);
    }
}
void x25519_shared(const Secret32& seed, const PublicKey& peer, Secret32& shared) {
    if (&seed == &shared) {
        throw ControlException(ControlError::invalid_length);
    }
    shared.clear();
    try {
        const auto key = private_key(EVP_PKEY_X25519, seed);
        const auto peer_key = public_key(EVP_PKEY_X25519, peer);
        KeyContext context(EVP_PKEY_CTX_new(key.get(), nullptr), EVP_PKEY_CTX_free);
        if (!context) {
            throw ControlException(ControlError::backend);
        }
        require_success(EVP_PKEY_derive_init(context.get()));
        require_success(EVP_PKEY_derive_set_peer(context.get(), peer_key.get()));
        auto length = shared.bytes.size();
        // OpenSSL rejects the low-order peers that yield an all-zero secret.
        if (EVP_PKEY_derive(context.get(), shared.bytes.data(), &length) != 1) {
            throw ControlException(ControlError::authentication);
        }
        if (length != shared.bytes.size()) {
            throw ControlException(ControlError::backend);
        }
        constexpr PublicKey zero{};
        if (CRYPTO_memcmp(shared.bytes.data(), zero.data(), zero.size()) == 0) {
            throw ControlException(ControlError::authentication);
        }
    } catch (...) {
        shared.clear();
        throw;
    }
}
PublicKey ed25519_public(const Secret32& seed) {
    return export_public(EVP_PKEY_ED25519, seed);
}
Signature ed25519_sign(const Secret32& seed, const Bytes& message) {
    const auto key = private_key(EVP_PKEY_ED25519, seed);
    const auto context = signature_context(key, true, message.size());
    Signature signature{};
    auto length = signature.size();
    require_success(
        EVP_DigestSign(context.get(), signature.data(), &length, message.data(), message.size()));
    if (length != signature.size()) {
        throw ControlException(ControlError::backend);
    }
    return signature;
}
bool ed25519_verify(const PublicKey& bytes, const Bytes& message, const Signature& signature) {
    const auto key = public_key(EVP_PKEY_ED25519, bytes);
    const auto context = signature_context(key, false, message.size());
    const auto result = EVP_DigestVerify(context.get(), signature.data(), signature.size(),
                                         message.data(), message.size());
    if (result < 0) {
        throw ControlException(ControlError::backend);
    }
    return result == 1;
}
} // namespace send_airplay2::detail
