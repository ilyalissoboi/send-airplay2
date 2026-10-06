// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_CHANNEL_KEYS_H
#define SEND_AIRPLAY2_CHANNEL_KEYS_H

#include "identity_crypto.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace send_airplay2::detail {
/**
 * HKDF-SHA512 labels for one encrypted channel of a verified AirPlay session.
 * Infos are named from the sender's side: the sender encrypts with the key from
 * `sender_write_info` and decrypts with the key from `sender_read_info`. Some
 * receiver-defined names read the other way round; the label factories below
 * hide that, so callers never swap directions themselves.
 */
struct ChannelKeyLabels {
    std::string salt;
    std::string sender_write_info;
    std::string sender_read_info;
};

/// The pair-verify control connection: salt "Control-Salt".
[[nodiscard]] ChannelKeyLabels control_channel_labels();

/// The event channel announced by the base SETUP's `eventPort`, salt
/// "Events-Salt". Its infos are named from the receiver's side, so the sender
/// writes with "Events-Read-Encryption-Key".
[[nodiscard]] ChannelKeyLabels event_channel_labels();

/// A type-130 data stream (remote control). The salt is "DataStream-Salt"
/// followed by the stream's 64-bit SETUP seed in unsigned decimal; the sender
/// writes with "DataStream-Output-Encryption-Key".
[[nodiscard]] ChannelKeyLabels data_stream_labels(std::uint64_t seed);

/**
 * HKDF-SHA512 extract-and-expand of a 32-byte secret into 32 bytes.
 * `output` must be a distinct owner; it is cleared if derivation throws
 * ControlException (backend failure). Temporary secret copies are erased.
 */
void derive_session_key(const Secret32& secret, std::string_view salt, std::string_view info,
                        Secret32& output);

/**
 * Owns the verified pair-verify shared secret for one receiver session, so that
 * further channels of that session (event channel, data streams) can derive
 * their keys. Only derived keys leave this object; the secret itself is never
 * returned. Filled once by PairVerifier::take_session_keys.
 *
 * Non-copyable and non-movable to avoid secret copies. clear() and destruction
 * erase the secret. Not thread-safe: the owner serializes use.
 */
class ChannelKeySource {
public:
    ChannelKeySource() = default;
    ~ChannelKeySource() = default; // Secret32 erases itself.
    ChannelKeySource(const ChannelKeySource&) = delete;
    ChannelKeySource& operator=(const ChannelKeySource&) = delete;
    ChannelKeySource(ChannelKeySource&&) = delete;
    ChannelKeySource& operator=(ChannelKeySource&&) = delete;

    [[nodiscard]] bool available() const noexcept {
        return available_;
    }

    /**
     * Derive the sender's write and read keys for one channel into distinct
     * owners. Throws std::logic_error when no secret is held or the outputs
     * alias, and ControlException on backend failure. Outputs are unchanged
     * on any failure.
     */
    void derive(const ChannelKeyLabels& labels, Secret32& sender_write,
                Secret32& sender_read) const;

    void clear() noexcept;

private:
    friend class PairVerifier;
    /// Copies the secret in and clears `shared`. Requires !available().
    void adopt(Secret32& shared) noexcept;

    Secret32 shared_;
    bool available_ = false;
};
} // namespace send_airplay2::detail
#endif
