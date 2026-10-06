// SPDX-License-Identifier: Apache-2.0
#include "control_crypto.h"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace send_airplay2::detail {
namespace {
constexpr std::size_t max_crypto_input = 65536;
constexpr std::size_t max_hkdf_info_size = 1024;
using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;
using KeyContext = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;

const char* error_message(ControlError reason) noexcept {
    switch (reason) {
    case ControlError::invalid_length:
        return "Invalid control record length";
    case ControlError::authentication:
        return "Control record authentication failed";
    case ControlError::counter_exhausted:
        return "Control record counter exhausted";
    case ControlError::closed:
        return "Control record direction is closed";
    case ControlError::backend:
        return "Cryptographic backend failed";
    }
    return "Unknown control error";
}

void require_success(int result) {
    if (result != 1) {
        throw ControlException(ControlError::backend);
    }
}

void validate_lengths(std::size_t data, std::size_t aad) {
    if (data > max_crypto_input || aad > max_crypto_input) {
        throw ControlException(ControlError::invalid_length);
    }
}

/// EVP can produce unauthenticated plaintext during Update. Wipe it on every exit.
struct SensitiveBytes {
    Bytes value;
    explicit SensitiveBytes(std::size_t size) : value(size) {}
    ~SensitiveBytes() {
        cleanse(value.data(), value.size());
    }
    SensitiveBytes(const SensitiveBytes&) = delete;
    SensitiveBytes& operator=(const SensitiveBytes&) = delete;
};
} // namespace

ControlException::ControlException(ControlError reason)
    : std::runtime_error(error_message(reason)), reason_(reason) {}

void cleanse(void* data, std::size_t size) noexcept {
    if (size != 0) {
        OPENSSL_cleanse(data, size);
    }
}

Bytes seal_record(const ControlKey& key, const ControlNonce& nonce, const Bytes& aad,
                  const Bytes& plaintext) {
    validate_lengths(plaintext.size(), aad.size());
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) {
        throw ControlException(ControlError::backend);
    }
    require_success(EVP_EncryptInit_ex(context.get(), EVP_chacha20_poly1305(), nullptr, key.data(),
                                       nonce.data()));
    int written = 0;
    if (!aad.empty()) {
        require_success(EVP_EncryptUpdate(context.get(), nullptr, &written, aad.data(),
                                          static_cast<int>(aad.size())));
    }
    Bytes output(plaintext.size() + auth_tag_size);
    int encrypted = 0;
    if (!plaintext.empty()) {
        require_success(EVP_EncryptUpdate(context.get(), output.data(), &encrypted,
                                          plaintext.data(), static_cast<int>(plaintext.size())));
    }
    require_success(EVP_EncryptFinal_ex(context.get(), output.data() + encrypted, &written));
    if (static_cast<std::size_t>(encrypted + written) != plaintext.size()) {
        throw ControlException(ControlError::backend);
    }
    require_success(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_GET_TAG,
                                        static_cast<int>(auth_tag_size),
                                        output.data() + plaintext.size()));
    return output;
}

Bytes open_record(const ControlKey& key, const ControlNonce& nonce, const Bytes& aad,
                  const Bytes& ciphertext_and_tag) {
    if (ciphertext_and_tag.size() < auth_tag_size) {
        throw ControlException(ControlError::invalid_length);
    }
    const auto length = ciphertext_and_tag.size() - auth_tag_size;
    validate_lengths(length, aad.size());
    CipherContext context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context) {
        throw ControlException(ControlError::backend);
    }
    require_success(EVP_DecryptInit_ex(context.get(), EVP_chacha20_poly1305(), nullptr, key.data(),
                                       nonce.data()));
    int written = 0;
    if (!aad.empty()) {
        require_success(EVP_DecryptUpdate(context.get(), nullptr, &written, aad.data(),
                                          static_cast<int>(aad.size())));
    }
    // Extra room keeps the empty-input finalization pointer valid as well.
    SensitiveBytes plaintext(length + auth_tag_size);
    int decrypted = 0;
    if (length != 0) {
        require_success(EVP_DecryptUpdate(context.get(), plaintext.value.data(), &decrypted,
                                          ciphertext_and_tag.data(), static_cast<int>(length)));
    }
    std::array<std::uint8_t, auth_tag_size> tag{};
    std::copy_n(ciphertext_and_tag.data() + length, auth_tag_size, tag.data());
    require_success(EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_TAG,
                                        static_cast<int>(tag.size()), tag.data()));
    if (EVP_DecryptFinal_ex(context.get(), plaintext.value.data() + decrypted, &written) != 1) {
        throw ControlException(ControlError::authentication);
    }
    if (static_cast<std::size_t>(decrypted + written) != length) {
        throw ControlException(ControlError::backend);
    }
    return Bytes(plaintext.value.begin(),
                 plaintext.value.begin() + static_cast<std::ptrdiff_t>(length));
}

ControlKey derive_control_key(const Bytes& secret, std::string_view salt, std::string_view info) {
    validate_lengths(secret.size(), salt.size());
    if (info.size() > max_hkdf_info_size) {
        throw ControlException(ControlError::invalid_length);
    }
    KeyContext context(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr), EVP_PKEY_CTX_free);
    if (!context) {
        throw ControlException(ControlError::backend);
    }
    require_success(EVP_PKEY_derive_init(context.get()));
    require_success(EVP_PKEY_CTX_hkdf_mode(context.get(), EVP_PKEY_HKDEF_MODE_EXTRACT_AND_EXPAND));
    require_success(EVP_PKEY_CTX_set_hkdf_md(context.get(), EVP_sha512()));
    // OpenSSL distinguishes an explicitly empty key from an unset (null) key.
    const unsigned char empty_input = 0;
    require_success(EVP_PKEY_CTX_set1_hkdf_key(context.get(),
                                               secret.empty() ? &empty_input : secret.data(),
                                               static_cast<int>(secret.size())));
    require_success(EVP_PKEY_CTX_set1_hkdf_salt(
        context.get(),
        salt.empty() ? &empty_input : reinterpret_cast<const unsigned char*>(salt.data()),
        static_cast<int>(salt.size())));
    require_success(EVP_PKEY_CTX_add1_hkdf_info(
        context.get(),
        info.empty() ? &empty_input : reinterpret_cast<const unsigned char*>(info.data()),
        static_cast<int>(info.size())));
    ControlKey output{};
    auto length = output.size();
    if (EVP_PKEY_derive(context.get(), output.data(), &length) != 1 || length != output.size()) {
        cleanse(output.data(), output.size());
        throw ControlException(ControlError::backend);
    }
    return output;
}
} // namespace send_airplay2::detail
