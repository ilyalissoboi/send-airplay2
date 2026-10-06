// SPDX-License-Identifier: Apache-2.0
#include "control_records.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace send_airplay2::detail {
namespace {
ControlNonce nonce_for_counter(std::uint64_t counter) {
    // Reserve UINT64_MAX rather than allowing the next increment to wrap to zero.
    if (counter == std::numeric_limits<std::uint64_t>::max()) {
        throw ControlException(ControlError::counter_exhausted);
    }
    ControlNonce nonce{};
    for (std::size_t byte = 0; byte < 8; ++byte) {
        nonce[4 + byte] = static_cast<std::uint8_t>(counter >> (8 * byte));
    }
    return nonce;
}

void validate_call(bool closed, std::size_t size) {
    if (closed) {
        throw ControlException(ControlError::closed);
    }
    if (size > control_records::max_call_input) {
        throw ControlException(ControlError::invalid_length);
    }
}

/// Own and wipe one plaintext record, including when aggregate allocation fails.
struct PlaintextRecord {
    Bytes bytes;
    explicit PlaintextRecord(Bytes value) : bytes(std::move(value)) {}
    ~PlaintextRecord() {
        cleanse(bytes.data(), bytes.size());
    }
    PlaintextRecord(const PlaintextRecord&) = delete;
    PlaintextRecord& operator=(const PlaintextRecord&) = delete;
    PlaintextRecord(PlaintextRecord&&) = delete;
    PlaintextRecord& operator=(PlaintextRecord&&) = delete;
};
} // namespace

ControlWriter::ControlWriter(const ControlKey& key) : key_(key) {}
ControlWriter::~ControlWriter() {
    close();
}
void ControlWriter::close() noexcept {
    closed_ = true;
    cleanse(key_.data(), key_.size());
}

Bytes ControlWriter::encrypt(const Bytes& plaintext) {
    try {
        validate_call(closed_, plaintext.size());
        Bytes wire;
        const auto record_count = (plaintext.size() + control_records::max_plaintext - 1) /
                                  control_records::max_plaintext;
        wire.reserve(plaintext.size() +
                     record_count * (control_records::header_size + auth_tag_size));
        for (std::size_t offset = 0; offset < plaintext.size();) {
            const auto length = std::min(control_records::max_plaintext, plaintext.size() - offset);
            const Bytes header{static_cast<std::uint8_t>(length),
                               static_cast<std::uint8_t>(length >> 8)};
            PlaintextRecord record{
                Bytes(plaintext.begin() + static_cast<std::ptrdiff_t>(offset),
                      plaintext.begin() + static_cast<std::ptrdiff_t>(offset + length))};
            const auto encrypted =
                seal_record(key_, nonce_for_counter(counter_), header, record.bytes);
            wire.insert(wire.end(), header.begin(), header.end());
            wire.insert(wire.end(), encrypted.begin(), encrypted.end());
            ++counter_;
            offset += length;
        }
        return wire;
    } catch (...) {
        // A failed allocation could occur after encryption. Never retry this key/counter.
        close();
        throw;
    }
}

ControlReader::ControlReader(const ControlKey& key) : key_(key) {}
ControlReader::~ControlReader() {
    close();
}
void ControlReader::close() noexcept {
    closed_ = true;
    cleanse(key_.data(), key_.size());
    cleanse(pending_.data(), pending_.size());
    pending_size_ = 0;
}

Bytes ControlReader::feed(const Bytes& wire) {
    Bytes output;
    try {
        validate_call(closed_, wire.size());
        // Reserve once so accumulating plaintext never leaves copies in freed capacity.
        output.reserve(wire.size() + pending_size_);
        std::size_t cursor = 0;
        while (cursor < wire.size()) {
            if (pending_size_ < control_records::header_size) {
                pending_[pending_size_++] = wire[cursor++];
                if (pending_size_ < control_records::header_size) {
                    continue;
                }
            }
            const auto length = static_cast<std::size_t>(pending_[0]) |
                                (static_cast<std::size_t>(pending_[1]) << 8);
            if (length > control_records::max_plaintext) {
                throw ControlException(ControlError::invalid_length);
            }
            const auto required = control_records::header_size + length + auth_tag_size;
            const auto available = std::min(required - pending_size_, wire.size() - cursor);
            std::copy_n(wire.data() + cursor, available, pending_.data() + pending_size_);
            cursor += available;
            pending_size_ += available;
            if (pending_size_ != required) {
                continue;
            }
            const Bytes header{pending_[0], pending_[1]};
            const Bytes encrypted(pending_.begin() + control_records::header_size,
                                  pending_.begin() + static_cast<std::ptrdiff_t>(required));
            PlaintextRecord record{
                open_record(key_, nonce_for_counter(counter_), header, encrypted)};
            output.insert(output.end(), record.bytes.begin(), record.bytes.end());
            ++counter_;
            pending_size_ = 0;
        }
        return output;
    } catch (...) {
        cleanse(output.data(), output.size());
        close();
        throw;
    }
}

void ControlReader::finish() {
    if (closed_) {
        throw ControlException(ControlError::closed);
    }
    const bool truncated = pending_size_ != 0;
    close();
    if (truncated) {
        throw ControlException(ControlError::invalid_length);
    }
}
} // namespace send_airplay2::detail
