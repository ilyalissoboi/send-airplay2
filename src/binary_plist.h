// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_BINARY_PLIST_H
#define SEND_AIRPLAY2_BINARY_PLIST_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace send_airplay2::detail {
using Bytes = std::vector<std::uint8_t>;

namespace binary_plist {
constexpr std::size_t header_size = 8;   // "bplist00": magic and format version.
constexpr std::size_t trailer_size = 32; // Table sizes, counts and offsets.
/// Session messages are a few KiB; receiver /info is under 2 KiB on tvOS 26.6.
constexpr std::size_t max_document_size = 1u << 20;
constexpr std::size_t max_objects = 16384;
/// Container nesting: the root is depth 0, its children depth 1, and so on.
constexpr std::size_t max_depth = 32;
/// Objects may be shared, so decoding can materialize more values than the
/// object table holds. These budgets bound that expansion.
constexpr std::size_t max_decoded_values = 65536;
constexpr std::size_t max_decoded_payload_bytes = 4u << 20;
} // namespace binary_plist

enum class PlistKind { boolean, integer, real, date, string, data, array, dictionary };

/// Seconds relative to 2001-01-01T00:00:00Z, the binary plist date epoch.
struct PlistDate {
    double seconds = 0;
};

class PlistValue;
struct PlistEntry;
using PlistArray = std::vector<PlistValue>;
/// Ordered entries. Encoding preserves this order; keys must be unique.
using PlistDictionary = std::vector<PlistEntry>;

/**
 * One value of the supported bplist00 subset. Strings hold UTF-8. Integers are
 * signed 64-bit: the 16-byte form used for values above INT64_MAX is not
 * supported. A default-constructed value is boolean false.
 */
class PlistValue {
public:
    PlistValue() = default;
    PlistValue(bool value);
    PlistValue(std::int64_t value);
    PlistValue(int value);
    PlistValue(double value);
    PlistValue(PlistDate value);
    PlistValue(std::string value);
    PlistValue(const char* value);
    PlistValue(Bytes value);
    PlistValue(PlistArray value);
    PlistValue(PlistDictionary value);

    [[nodiscard]] PlistKind kind() const noexcept;

    /// Typed accessors throw std::logic_error when the kind differs.
    [[nodiscard]] bool as_boolean() const;
    [[nodiscard]] std::int64_t as_integer() const;
    [[nodiscard]] double as_real() const;
    [[nodiscard]] PlistDate as_date() const;
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const Bytes& as_data() const;
    [[nodiscard]] const PlistArray& as_array() const;
    [[nodiscard]] const PlistDictionary& as_dictionary() const;

    /// Value for `key` in a dictionary, or nullptr when the key is absent or this
    /// is not a dictionary. The pointer is valid while this value is unchanged.
    [[nodiscard]] const PlistValue* find(std::string_view key) const noexcept;

    /// Structural equality. Reals and dates compare by bit pattern, so -0.0 and
    /// 0.0 differ and a NaN equals the same NaN.
    friend bool operator==(const PlistValue& left, const PlistValue& right);
    friend bool operator!=(const PlistValue& left, const PlistValue& right) {
        return !(left == right);
    }

private:
    std::variant<bool, std::int64_t, double, PlistDate, std::string, Bytes, PlistArray,
                 PlistDictionary>
        value_;
};

struct PlistEntry {
    std::string key;
    PlistValue value;
};

bool operator==(const PlistEntry& left, const PlistEntry& right);

/**
 * Encode `root` as a complete bplist00 document.
 *
 * The layout matches CPython plistlib's binary writer with sort_keys=False:
 * objects are numbered depth-first (dictionary keys before values), equal
 * scalars of the same kind share one object, and integers, lengths, references
 * and offsets use the smallest of 1, 2, 4 or 8 bytes. Strings are written as
 * ASCII when possible and as UTF-16BE otherwise. One deliberate difference:
 * reals are shared by bit pattern, so 0.0 and -0.0 stay distinct.
 *
 * Throws std::invalid_argument for invalid UTF-8, duplicate dictionary keys, or
 * input exceeding the binary_plist depth, object or document-size limits.
 */
[[nodiscard]] Bytes encode_binary_plist(const PlistValue& root);

/**
 * Decode a complete bplist00 document; this is not a streaming parser.
 *
 * Accepts booleans, integers of 1/2/4/8 bytes (1-4 unsigned, 8 signed), 4- and
 * 8-byte reals, dates, data, ASCII and UTF-16BE strings, arrays and dictionaries
 * with string keys. Rejects null, fill, UID, set and 16-byte integer objects,
 * malformed tables, out-of-range references, cycles, duplicate dictionary keys,
 * invalid ASCII/UTF-16 text and anything beyond the binary_plist limits.
 * Shared objects are decoded into independent copies.
 *
 * Throws std::invalid_argument on any violation; no partial result is returned.
 */
[[nodiscard]] PlistValue decode_binary_plist(const std::uint8_t* data, std::size_t size);
[[nodiscard]] PlistValue decode_binary_plist(const Bytes& document);
} // namespace send_airplay2::detail
#endif
