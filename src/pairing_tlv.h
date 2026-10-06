// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_PAIRING_TLV_H
#define SEND_AIRPLAY2_PAIRING_TLV_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace send_airplay2::detail {
using Bytes = std::vector<std::uint8_t>;

/// A logical TLV8 value. Unknown types and explicit separators are preserved.
struct TlvField {
    std::uint8_t type = 0;
    Bytes value;
};

namespace pairing_tlv {
constexpr std::uint8_t separator = 0xff;
constexpr std::size_t max_message_size = 65536;
constexpr std::size_t max_value_size = 4096;
constexpr std::size_t max_fields = 64;
} // namespace pairing_tlv

/**
 * Decode a complete, already bounded HTTP body; this is not a streaming parser.
 * A 255-byte fragment can continue only into an immediately adjacent equal type.
 * Separate equal types require a separator/different type. Separators must be empty.
 * Throws std::invalid_argument on malformed/over-limit input; no partial result.
 * Returned fields own their bytes. Pairing state/schema validation is a later layer.
 */
[[nodiscard]] std::vector<TlvField> decode_tlv(const Bytes& body);

/// Canonical fragmentation with the same bounds/ambiguity rules as decode_tlv.
[[nodiscard]] Bytes encode_tlv(const std::vector<TlvField>& fields);
} // namespace send_airplay2::detail
#endif
