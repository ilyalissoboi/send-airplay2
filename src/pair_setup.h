// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_PAIR_SETUP_H
#define SEND_AIRPLAY2_PAIR_SETUP_H
#include "pair_setup_crypto.h"
#include "pair_verify.h"
#include <cstddef>
#include <memory>
#include <string_view>

namespace send_airplay2::detail {
namespace pair_setup {
constexpr std::size_t max_body_size = 2048;
constexpr std::size_t max_encrypted_size = 512;
constexpr std::size_t max_identifier_size = 64;
} // namespace pair_setup

/** Private persistent HAP pair-setup exchange; no socket or PIN-display API.
 * Caller provides a unique opaque controller ID (1..64 bytes), and owns the
 * correlated /pair-pin-start and /pair-setup request lifecycle on one connection.
 * start emits M1; respond consumes M2 plus a 4..8 digit PIN and emits M3;
 * confirm authenticates the server's M4 SRP proof before emitting signed M5;
 * finish authenticates M6 and returns trusted credential ownership once.
 * Both SRP proof and accessory Ed25519 signature are mandatory; no TOFU or
 * transient-pairing fallback. Supply HTTP 200 and complete bounded TLV bodies.
 * Non-copyable/non-movable, serial use. Every method exception is terminal and
 * wipes owned secrets. close handles timeout/cancellation/disconnect; retry creates
 * a fresh object. Caller erases PIN/source copies and stores returned credentials
 * through a trusted host adapter. Receiver interoperability remains untested.
 */
class PairSetup {
public:
    explicit PairSetup(Bytes controller_id);
    ~PairSetup();
    PairSetup(const PairSetup&) = delete;
    PairSetup& operator=(const PairSetup&) = delete;
    PairSetup(PairSetup&&) = delete;
    PairSetup& operator=(PairSetup&&) = delete;
    [[nodiscard]] Bytes start();
    [[nodiscard]] Bytes respond(std::string_view pin, unsigned http_status, const Bytes& m2);
    [[nodiscard]] Bytes confirm(unsigned http_status, const Bytes& m4);
    [[nodiscard]] std::unique_ptr<PairCredentials> finish(unsigned http_status, const Bytes& m6);
    void close() noexcept;

private:
    friend struct PairSetupTestAccess;
    enum class State { ready, waiting_m2, waiting_m4, waiting_m6, closed };
    void require_state(State expected) const;
    Bytes controller_id_;
    Secret32 controller_seed_;
    SrpClient srp_;
    Secret64 session_secret_;
    Secret32 encryption_key_;
    State state_ = State::ready;
};
} // namespace send_airplay2::detail
#endif
