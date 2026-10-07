// SPDX-License-Identifier: Apache-2.0
// Label strings follow the AirPlay 2 HAP channel names used by pyatv 0.18.0
// (MIT; protocol constants only, no source incorporated). See dependencies.md.
#include "channel_keys.h"
#include "control_crypto.h"
#include <stdexcept>
#include <string>
#include <string_view>

namespace send_airplay2::detail {
ChannelKeyLabels control_channel_labels() {
    return {"Control-Salt", "Control-Write-Encryption-Key", "Control-Read-Encryption-Key"};
}

ChannelKeyLabels event_channel_labels() {
    // Named from the receiver's side: what the receiver reads, the sender writes.
    return {"Events-Salt", "Events-Read-Encryption-Key", "Events-Write-Encryption-Key"};
}

ChannelKeyLabels data_stream_labels(std::uint64_t seed) {
    return {"DataStream-Salt" + std::to_string(seed), "DataStream-Output-Encryption-Key",
            "DataStream-Input-Encryption-Key"};
}

void derive_session_key(const Secret32& secret, std::string_view salt, std::string_view info,
                        Secret32& output) {
    // The HKDF adapter accepts a vector; own and erase this temporary copy.
    struct SecretInput {
        Bytes bytes;
        explicit SecretInput(const Secret32& source)
            : bytes(source.bytes.begin(), source.bytes.end()) {}
        ~SecretInput() {
            cleanse(bytes.data(), bytes.size());
        }
        SecretInput(const SecretInput&) = delete;
        SecretInput& operator=(const SecretInput&) = delete;
        SecretInput(SecretInput&&) = delete;
        SecretInput& operator=(SecretInput&&) = delete;
    } input(secret);
    try {
        auto derived = derive_control_key(input.bytes, salt, info);
        output.bytes = derived;
        cleanse(derived.data(), derived.size());
    } catch (...) {
        output.clear();
        throw;
    }
}

void ChannelKeySource::derive(const ChannelKeyLabels& labels, Secret32& sender_write,
                              Secret32& sender_read) const {
    if (!available_) {
        throw std::logic_error("No verified session secret for channel keys");
    }
    if (&sender_write == &sender_read) {
        throw std::logic_error("Channel key outputs must be distinct");
    }
    // Derive into locals first so the caller's owners change only on success.
    Secret32 write_key;
    Secret32 read_key;
    derive_session_key(shared_, labels.salt, labels.sender_write_info, write_key);
    derive_session_key(shared_, labels.salt, labels.sender_read_info, read_key);
    sender_write.bytes = write_key.bytes;
    sender_read.bytes = read_key.bytes;
}

void ChannelKeySource::clear() noexcept {
    shared_.clear();
    available_ = false;
}

void ChannelKeySource::adopt(Secret32& shared) noexcept {
    shared_.bytes = shared.bytes;
    shared.clear();
    available_ = true;
}
} // namespace send_airplay2::detail
