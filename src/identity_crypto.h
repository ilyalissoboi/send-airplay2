// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_IDENTITY_CRYPTO_H
#define SEND_AIRPLAY2_IDENTITY_CRYPTO_H

#include "control_crypto.h"
#include <array>
#include <cstdint>

namespace send_airplay2::detail {
using PublicKey = std::array<std::uint8_t, 32>;
using Signature = std::array<std::uint8_t, 64>;

/** Private 32-byte seed/shared key with explicit lifetime and erasure.
 * Non-copyable/non-movable; importing a source copy is explicit. Callers must
 * erase their own copies too. Serial use only; no locked-memory guarantee.
 */
struct Secret32 {
    ControlKey bytes{};
    Secret32() = default;
    explicit Secret32(const ControlKey& source) : bytes(source) {}
    ~Secret32();
    Secret32(const Secret32&) = delete;
    Secret32& operator=(const Secret32&) = delete;
    Secret32(Secret32&&) = delete;
    Secret32& operator=(Secret32&&) = delete;
    void clear() noexcept;
};

/// Replace the seed with fresh OpenSSL private randomness. Wipe on backend failure.
void generate_x25519_seed(Secret32& seed);
[[nodiscard]] PublicKey x25519_public(const Secret32& seed);
/// Derive into a distinct output owner. Reject low-order/all-zero shared secrets.
/// On failure the output is cleared, except alias rejection leaves the seed intact.
/// Source seed copies remain caller-owned.
void x25519_shared(const Secret32& seed, const PublicKey& peer, Secret32& shared);
[[nodiscard]] PublicKey ed25519_public(const Secret32& seed);
/// Pure Ed25519, no external hash/prehash. Messages are bounded at 64 KiB.
[[nodiscard]] Signature ed25519_sign(const Secret32& seed, const Bytes& message);
/// Invalid signatures return false; allocation/backend failures throw ControlException.
[[nodiscard]] bool ed25519_verify(const PublicKey& key, const Bytes& message,
                                  const Signature& signature);
} // namespace send_airplay2::detail
#endif
