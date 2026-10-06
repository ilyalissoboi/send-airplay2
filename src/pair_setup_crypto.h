// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_PAIR_SETUP_CRYPTO_H
#define SEND_AIRPLAY2_PAIR_SETUP_CRYPTO_H
#include "identity_crypto.h"
#include <botan/ffi.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace send_airplay2::detail {
using Digest64 = std::array<std::uint8_t, 64>;
enum class PairSetupError {
    invalid_message,
    authentication,
    peer_rejected,
    unexpected_state,
    backend
};
class PairSetupException : public std::runtime_error {
public:
    explicit PairSetupException(PairSetupError reason);
    [[nodiscard]] PairSetupError reason() const noexcept {
        return reason_;
    }

private:
    PairSetupError reason_;
};
/// Erasing owner for the SHA512 SRP session key and expected proof; serial use only.
struct Secret64 {
    Digest64 bytes{};
    ~Secret64();
    Secret64() = default;
    Secret64(const Secret64&) = delete;
    Secret64& operator=(const Secret64&) = delete;
    Secret64(Secret64&&) = delete;
    Secret64& operator=(Secret64&&) = delete;
    void clear() noexcept;
};
/// Borrowed bytes for one hash call, never retained. Empty views may have null data.
struct HashInput {
    const std::uint8_t* data;
    std::size_t size;
};
/// SHA512 transcript hashing; each input <= 64 KiB. Writes directly to caller output.
void hash_sha512(Digest64& output, std::initializer_list<HashInput> inputs);
/// HAP HKDF-SHA512 with owned temporary secret copies erased on all exits.
void derive_setup_key(const Secret64& secret, std::string_view salt, std::string_view info,
                      Secret32& output);

struct SrpResponse {
    Bytes public_key; // Fixed 384-byte, big-endian SRP A on the wire.
    Digest64 proof;   // HAP client proof, not the raw shared secret.
};
/** One SRP-6a client agreement, fixed RFC 5054 3072-bit group/g=5 and SHA512.
 * Botan supplies arithmetic and system randomness. begin accepts 4..8 ASCII PIN
 * digits, 16 salt bytes and 1..384 bytes of big-endian B. It preserves leading PIN
 * zeros and never retains the PIN. Caller erases PIN/source copies and proof data.
 * complete authenticates the 64-byte server proof before releasing the session key
 * once. Every method failure closes/wipes; output stays unchanged on failure.
 * Non-copyable/non-movable, serial use. No network, plaintext fallback or retry.
 */
class SrpClient {
public:
    SrpClient();
    ~SrpClient();
    SrpClient(const SrpClient&) = delete;
    SrpClient& operator=(const SrpClient&) = delete;
    SrpClient(SrpClient&&) = delete;
    SrpClient& operator=(SrpClient&&) = delete;
    [[nodiscard]] SrpResponse begin(std::string_view pin, const Bytes& salt,
                                    const Bytes& server_public);
    void complete(const Bytes& server_proof, Secret64& key);
    void close() noexcept;

private:
    friend struct PairSetupTestAccess;
    botan_rng_t rng_ = nullptr;
    Secret64 key_;
    Secret64 expected_proof_;
    bool waiting_proof_ = false;
    bool closed_ = false;
};
} // namespace send_airplay2::detail
#endif
