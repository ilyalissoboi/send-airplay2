// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_PROTOBUF_WIRE_H
#define SEND_AIRPLAY2_PROTOBUF_WIRE_H
#include "binary_plist.h"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace send_airplay2::detail::protobuf_wire {
constexpr std::size_t max_message_size = 65536;
constexpr std::size_t max_fields = 2048;
/// A borrowed field; data remains valid only while the input buffer lives.
/// Unknown varint, fixed64, length-delimited and fixed32 fields are skipped.
/// Groups, field zero, overflow and truncated input are rejected.
struct Field {
    std::uint32_t number = 0;
    unsigned wire = 0;
    std::uint64_t integer = 0;
    std::string_view data;
};
using Fields = std::vector<Field>;
[[nodiscard]] Fields decode(std::string_view input);
/// Reject duplicate singular fields and wrong wire types. Missing returns null.
[[nodiscard]] const Field* find(const Fields& fields, std::uint32_t number, unsigned wire);
/// Read at offset, advancing only through a complete uint64 varint; throws
/// std::invalid_argument on truncation/overflow. No partial result is returned.
[[nodiscard]] std::uint64_t varint(std::string_view input, std::size_t& offset);
void append_varint(Bytes& output, std::uint64_t value);
void integer(Bytes& output, std::uint32_t field, std::uint64_t value);
void data(Bytes& output, std::uint32_t field, std::string_view value);
void data(Bytes& output, std::uint32_t field, const Bytes& value);
void real64(Bytes& output, std::uint32_t field, double value);
[[nodiscard]] double real64(const Field& field);
[[nodiscard]] float real32(const Field& field);
[[nodiscard]] std::string_view view(const Bytes& bytes) noexcept;
} // namespace send_airplay2::detail::protobuf_wire
#endif
