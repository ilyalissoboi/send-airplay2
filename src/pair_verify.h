// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_PAIR_VERIFY_H
#define SEND_AIRPLAY2_PAIR_VERIFY_H

#include "identity_crypto.h"
#include <cstddef>
#include <stdexcept>

namespace send_airplay2::detail {
namespace pair_verify {
constexpr std::size_t max_identifier_size = 64;
constexpr std::size_t max_body_size = 1024;
constexpr std::size_t max_encrypted_size = 512;
} // namespace pair_verify

enum class PairVerifyError {
    invalid_message,
    authentication,
    peer_rejected,
    unexpected_state,
    backend
};
/// Sanitized categories only: no identity, credential, transcript or backend error data.
class PairVerifyException : public std::runtime_error {
public:
    explicit PairVerifyException(PairVerifyError reason);
    [[nodiscard]] PairVerifyError reason() const noexcept {
        return reason_;
    }

private:
    PairVerifyError reason_;
};

/** Credentials from a separately authenticated provisioning flow/trusted storage.
 * Receiver ID + Ed25519 key are pinned, never taken from discovery or a new M2.
 * IDs are opaque nonempty bytes <= 64 bytes. This object owns a signing-seed
 * copy and erases it on destruction; callers erase source copies. The private
 * credential codec serializes only across trusted storage boundaries; no public
 * export or trust-on-first-use. Non-copyable/non-movable; serial use.
 */
class PairCredentials {
public:
    PairCredentials(Bytes receiver_id, const PublicKey& receiver_key, Bytes client_id,
                    const Secret32& client_seed);
    PairCredentials(const PairCredentials&) = delete;
    PairCredentials& operator=(const PairCredentials&) = delete;
    PairCredentials(PairCredentials&&) = delete;
    PairCredentials& operator=(PairCredentials&&) = delete;

private:
    friend class PairVerifier;
    friend struct CredentialCodec;
    Bytes receiver_id_;
    PublicKey receiver_key_;
    Bytes client_id_;
    Secret32 client_seed_;
};

/** One HAP pair-verify exchange on one connection, not a socket/session API.
 * Borrowed credentials must outlive this object. Non-copyable/non-movable,
 * serial use. Constructor generates fresh ephemeral randomness; start() emits M1,
 * respond() authenticates M2 and emits M3; finish() requires HTTP 200 and state M4.
 * Supply complete, bounded bodies from correlated /pair-verify responses only.
 * The future transport owns HTTP parsing, deadlines, cancellation and disconnects.
 * M4 is a protocol acknowledgement, not another cryptographic proof.
 * Every method exception is terminal and erases ephemeral/session/control keys.
 * On timeout/disconnect call close(); retry uses a new object and fresh randomness.
 */
class PairVerifier {
public:
    explicit PairVerifier(const PairCredentials& credentials);
    ~PairVerifier();
    PairVerifier(const PairVerifier&) = delete;
    PairVerifier& operator=(const PairVerifier&) = delete;
    PairVerifier(PairVerifier&&) = delete;
    PairVerifier& operator=(PairVerifier&&) = delete;
    [[nodiscard]] Bytes start();
    [[nodiscard]] Bytes respond(unsigned http_status, const Bytes& m2);
    void finish(unsigned http_status, const Bytes& m4);
    /// Release once after finish into distinct output owners; sender write/read order.
    /// Outputs remain unchanged on failure. Success closes/wipes this exchange.
    void take_control_keys(Secret32& write_key, Secret32& read_key);
    void close() noexcept;

private:
    friend struct PairVerifyTestAccess;
    enum class State { ready, waiting_m2, waiting_m4, verified, closed };
    void require_state(State expected) const;
    const PairCredentials& credentials_;
    State state_ = State::ready;
    Secret32 ephemeral_;
    PublicKey client_public_{};
    Secret32 shared_;
    Secret32 session_key_;
    Secret32 write_key_;
    Secret32 read_key_;
};
} // namespace send_airplay2::detail
#endif
