// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_CONTROL_CRYPTO_H
#define SEND_AIRPLAY2_CONTROL_CRYPTO_H

#include "pairing_tlv.h"
#include <array>
#include <stdexcept>
#include <string_view>

namespace send_airplay2::detail {
using ControlKey = std::array<std::uint8_t, 32>;
using ControlNonce = std::array<std::uint8_t, 12>;
constexpr std::size_t auth_tag_size = 16;

/// Sanitized error categories; never embed keys, plaintext or OpenSSL error queues.
enum class ControlError { invalid_length, authentication, counter_exhausted, closed, backend };
class ControlException : public std::runtime_error {
public:
    explicit ControlException(ControlError reason);
    [[nodiscard]] ControlError reason() const noexcept {
        return reason_;
    }

private:
    ControlError reason_;
};

/// Wipe transient/key buffers; callers also own responsibility for source-key copies.
void cleanse(void* data, std::size_t size) noexcept;

/** RFC 8439 AEAD. Values/AAD are bounded at 64 KiB; output owns ciphertext + tag.
 * Caller must use a unique nonce for each message under a given key.
 * open_record returns plaintext only after tag verification, wiping failed output.
 * These are private primitives, not a receiver-authentication API.
 */
[[nodiscard]] Bytes seal_record(const ControlKey& key, const ControlNonce& nonce, const Bytes& aad,
                                const Bytes& plaintext);
[[nodiscard]] Bytes open_record(const ControlKey& key, const ControlNonce& nonce, const Bytes& aad,
                                const Bytes& ciphertext_and_tag);

/// HKDF extract-and-expand, SHA512, 32 output bytes; secret/salt <= 64 KiB, info <= 1 KiB.
[[nodiscard]] ControlKey derive_control_key(const Bytes& secret, std::string_view salt,
                                            std::string_view info);
} // namespace send_airplay2::detail
#endif
