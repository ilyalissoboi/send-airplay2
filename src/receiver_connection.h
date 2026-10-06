// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_RECEIVER_CONNECTION_H
#define SEND_AIRPLAY2_RECEIVER_CONNECTION_H
#include "control_records.h"
#include "pair_setup.h"
#include "receiver_stream.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace send_airplay2::detail {
/** One private receiver TCP connection; owns stream, framing and encryption.
 * Noncopyable/nonmovable, serial use, no automatic retries or plaintext fallback.
 * Every method exception closes socket, both record directions and pending pairing.
 * Deadlines include all I/O and are checked around crypto/state-machine calls;
 * synchronous crypto itself cannot be interrupted. Call close when user abandons
 * a PIN prompt. Destructor closes and erases owned pending bodies/keys.
 * Credentials/PIN/source request buffers are borrowed or caller-owned and must
 * be erased by their owners. Discovery addresses do not establish receiver trust.
 */
class ReceiverConnection {
public:
    /// Ownership transfers even when construction fails; stream must be fresh/idle.
    ReceiverConnection(std::unique_ptr<ReceiverStream> stream, std::string authority);
    ~ReceiverConnection();
    ReceiverConnection(const ReceiverConnection&) = delete;
    ReceiverConnection& operator=(const ReceiverConnection&) = delete;
    ReceiverConnection(ReceiverConnection&&) = delete;
    ReceiverConnection& operator=(ReceiverConnection&&) = delete;
    /// /pair-pin-start then setup M1/M2; retains bounded M2 while caller obtains PIN.
    /// The caller supplies a new deadline for finish, excluding time in the PIN UI.
    void begin_pairing(Bytes controller_id, const ReceiverOperation& operation);
    /// Completes M3..M6 on this socket; returns authenticated credentials once and
    /// closes the provisioning socket. Storage and reconnect belong to the host.
    [[nodiscard]] std::unique_ptr<PairCredentials>
    finish_pairing(std::string_view pin, const ReceiverOperation& operation);
    /// Run M1..M4 with trusted credentials, then install fresh record keys at the
    /// response boundary. Credentials are borrowed for this call only.
    void verify(const PairCredentials& credentials, const ReceiverOperation& operation);
    /// Only available after verification. One request in flight, no pipelining.
    /// Non-200 responses are returned to the session caller for semantic handling.
    /// All network/framing/authentication failures are terminal. Caller owns body.
    [[nodiscard]] ReceiverResponse request(const ReceiverRequest& request,
                                           std::size_t response_body_limit,
                                           const ReceiverOperation& operation);
    void close() noexcept;

private:
    friend struct ReceiverConnectionTestAccess;
    enum class State { fresh, pairing, verified, closed };
    void require_state(State expected) const;
    [[nodiscard]] ReceiverResponse exchange(const ReceiverRequest& request,
                                            std::size_t response_body_limit,
                                            const ReceiverOperation& operation);
    std::unique_ptr<ReceiverStream> stream_;
    std::string authority_;
    State state_ = State::fresh;
    std::uint32_t sequence_ = 1;
    std::unique_ptr<PairSetup> setup_;
    ReceiverResponse pending_m2_;
    std::unique_ptr<ControlWriter> writer_;
    std::unique_ptr<ControlReader> reader_;
};
} // namespace send_airplay2::detail
#endif
