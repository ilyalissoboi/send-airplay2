// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_CONTROL_RECORDS_H
#define SEND_AIRPLAY2_CONTROL_RECORDS_H

#include "control_crypto.h"

namespace send_airplay2::detail {
namespace control_records {
constexpr std::size_t max_plaintext = 1024;
constexpr std::size_t header_size = 2;
constexpr std::size_t max_wire_record = header_size + max_plaintext + auth_tag_size;
constexpr std::size_t max_call_input = 65536;
} // namespace control_records

/**
 * One freshly keyed outbound direction, owned by one control connection.
 * Non-copyable/non-movable: duplicating key/counter state would reuse nonces.
 * Not thread safe; serialize access. Keys must come from successful peer verification,
 * be distinct by direction, and never be reused in a replacement connection.
 * No socket I/O or plaintext fallback. Every exception permanently closes the direction.
 * Returned wire bytes must be sent exactly once, in order; partial writes must retain
 * the same bytes rather than re-encrypting. The socket layer owns timeout/cancellation.
 */
class ControlWriter {
public:
    explicit ControlWriter(const ControlKey& key);
    ~ControlWriter();
    ControlWriter(const ControlWriter&) = delete;
    ControlWriter& operator=(const ControlWriter&) = delete;
    ControlWriter(ControlWriter&&) = delete;
    ControlWriter& operator=(ControlWriter&&) = delete;

    /// Split <= 64 KiB into records; empty input emits nothing and consumes no nonce.
    [[nodiscard]] Bytes encrypt(const Bytes& plaintext);
    void close() noexcept;

private:
    friend struct ControlRecordTestAccess;
    ControlKey key_;
    std::uint64_t counter_ = 0;
    bool closed_ = false;
};

/**
 * Fresh inbound direction with the same ownership/key rules as ControlWriter.
 * Retains at most one incomplete wire record, regardless of stream lifetime.
 * A successful feed returns owned plaintext from complete, authenticated records.
 * On any exception, no plaintext from that call is returned and the direction closes.
 * Earlier successful calls cannot be rolled back. Do not log returned plaintext.
 */
class ControlReader {
public:
    explicit ControlReader(const ControlKey& key);
    ~ControlReader();
    ControlReader(const ControlReader&) = delete;
    ControlReader& operator=(const ControlReader&) = delete;
    ControlReader(ControlReader&&) = delete;
    ControlReader& operator=(ControlReader&&) = delete;

    /// Accept <= 64 KiB wire bytes per call; a partial record returns no bytes for it.
    [[nodiscard]] Bytes feed(const Bytes& wire);
    /// Signal EOF. Reject any truncated header/ciphertext/tag; close on clean EOF too.
    void finish();
    void close() noexcept;

private:
    friend struct ControlRecordTestAccess;
    ControlKey key_;
    std::uint64_t counter_ = 0;
    bool closed_ = false;
    std::array<std::uint8_t, control_records::max_wire_record> pending_{};
    std::size_t pending_size_ = 0;
};
} // namespace send_airplay2::detail
#endif
