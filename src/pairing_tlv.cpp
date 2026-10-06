// SPDX-License-Identifier: Apache-2.0
#include "pairing_tlv.h"
#include <algorithm>
#include <stdexcept>

namespace send_airplay2::detail {
namespace {
void validate_value(std::uint8_t type, std::size_t size) {
    if (size > pairing_tlv::max_value_size || (type == pairing_tlv::separator && size != 0)) {
        throw std::invalid_argument("Invalid pairing TLV value length");
    }
}
} // namespace

std::vector<TlvField> decode_tlv(const Bytes& body) {
    if (body.size() > pairing_tlv::max_message_size) {
        throw std::invalid_argument("Pairing TLV body exceeds limit");
    }
    std::vector<TlvField> fields;
    std::size_t cursor = 0;
    std::size_t previous_fragment_length = 0;
    while (cursor < body.size()) {
        if (body.size() - cursor < pairing_tlv::header_size) {
            throw std::invalid_argument("Truncated pairing TLV header");
        }
        const auto type = body[cursor++];
        const auto length = body[cursor++];
        if (length > body.size() - cursor) {
            throw std::invalid_argument("Truncated pairing TLV value");
        }
        validate_value(type, length);
        if (!fields.empty() && fields.back().type == type && type != pairing_tlv::separator) {
            if (previous_fragment_length != pairing_tlv::max_fragment_size || length == 0) {
                throw std::invalid_argument("Ambiguous repeated pairing TLV type");
            }
        } else {
            if (fields.size() == pairing_tlv::max_fields) {
                throw std::invalid_argument("Too many pairing TLV fields");
            }
            fields.push_back({type, {}});
        }
        auto& value = fields.back().value;
        validate_value(type, value.size() + length);
        value.insert(value.end(), body.begin() + static_cast<std::ptrdiff_t>(cursor),
                     body.begin() + static_cast<std::ptrdiff_t>(cursor + length));
        previous_fragment_length = length;
        cursor += length;
    }
    return fields;
}

Bytes encode_tlv(const std::vector<TlvField>& fields) {
    if (fields.size() > pairing_tlv::max_fields) {
        throw std::invalid_argument("Too many pairing TLV fields");
    }
    Bytes body;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto& field = fields[index];
        validate_value(field.type, field.value.size());
        if (index != 0 && fields[index - 1].type == field.type &&
            field.type != pairing_tlv::separator) {
            throw std::invalid_argument("Separate equal TLV types require a separator");
        }
        const auto fragments =
            std::max<std::size_t>(1, (field.value.size() + pairing_tlv::max_fragment_size - 1) /
                                         pairing_tlv::max_fragment_size);
        const auto encoded_size = field.value.size() + pairing_tlv::header_size * fragments;
        if (encoded_size > pairing_tlv::max_message_size - body.size()) {
            throw std::invalid_argument("Pairing TLV body exceeds limit");
        }
        std::size_t offset = 0;
        // Even an empty value has a two-byte header; exact multiples need no empty tail.
        do {
            const auto length =
                std::min(pairing_tlv::max_fragment_size, field.value.size() - offset);
            body.push_back(field.type);
            body.push_back(static_cast<std::uint8_t>(length));
            body.insert(body.end(), field.value.begin() + static_cast<std::ptrdiff_t>(offset),
                        field.value.begin() + static_cast<std::ptrdiff_t>(offset + length));
            offset += length;
        } while (offset < field.value.size());
    }
    return body;
}
} // namespace send_airplay2::detail
