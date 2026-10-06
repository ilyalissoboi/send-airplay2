// SPDX-License-Identifier: Apache-2.0
// Format reference: Apple's CFBinaryPList layout as documented by CPython plistlib.
// Original implementation; no third-party source is incorporated.
#include "binary_plist.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace send_airplay2::detail {
namespace {
static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8,
              "binary plist reals and dates are IEEE 754 binary64");
static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4,
              "binary plist 4-byte reals are IEEE 754 binary32");

constexpr std::string_view magic = "bplist00";

/// Object markers: the high nibble selects the type, the low nibble holds a
/// small length, a width exponent or an exact subtype.
namespace marker {
constexpr std::uint8_t boolean_false = 0x08;
constexpr std::uint8_t boolean_true = 0x09;
constexpr std::uint8_t integer = 0x1;
constexpr std::uint8_t real = 0x2;
constexpr std::uint8_t date = 0x33; // Always an 8-byte real.
constexpr std::uint8_t data = 0x4;
constexpr std::uint8_t ascii_string = 0x5;
constexpr std::uint8_t utf16_string = 0x6; // Length counts UTF-16 code units.
constexpr std::uint8_t array = 0xa;
constexpr std::uint8_t dictionary = 0xd;
/// A low nibble of 0xf means the length follows as an integer object.
constexpr std::uint8_t extended_length = 0x0f;
constexpr std::uint8_t max_inline_length = 0x0e;
} // namespace marker

/// Trailer layout (32 bytes, big-endian): 6 unused/sort-version bytes, offset
/// width, reference width, object count, top object index, offset table offset.
namespace trailer_layout {
constexpr std::size_t offset_width = 6;
constexpr std::size_t reference_width = 7;
constexpr std::size_t object_count = 8;
constexpr std::size_t top_object = 16;
constexpr std::size_t offset_table_offset = 24;
constexpr std::size_t field_size = 8;
} // namespace trailer_layout

constexpr std::size_t max_table_width = 8;
constexpr std::size_t utf16_unit_size = 2;

std::uint64_t bits_of(double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

double double_from_bits(std::uint64_t bits) {
    double value = 0;
    std::memcpy(&value, &bits, sizeof value);
    return value;
}

/// Smallest of 1, 2, 4 or 8 bytes that holds `value` unsigned.
std::size_t width_for(std::uint64_t value) {
    if (value <= 0xff) {
        return 1;
    }
    if (value <= 0xffff) {
        return 2;
    }
    if (value <= 0xffffffff) {
        return 4;
    }
    return 8;
}

/// Width exponent for the integer marker's low nibble: 1, 2, 4, 8 -> 0, 1, 2, 3.
std::uint8_t width_exponent(std::size_t width) {
    std::uint8_t exponent = 0;
    while ((std::size_t{1} << exponent) < width) {
        ++exponent;
    }
    return exponent;
}

void append_big_endian(Bytes& output, std::uint64_t value, std::size_t width) {
    for (std::size_t index = width; index > 0; --index) {
        output.push_back(static_cast<std::uint8_t>(value >> (8 * (index - 1))));
    }
}

std::uint64_t read_big_endian(const std::uint8_t* input, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < width; ++index) {
        value = (value << 8) | input[index];
    }
    return value;
}

/// Two's-complement reinterpretation without implementation-defined conversion.
std::int64_t signed_from_bits(std::uint64_t bits) {
    constexpr auto max_positive =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (bits <= max_positive) {
        return static_cast<std::int64_t>(bits);
    }
    return -static_cast<std::int64_t>(~bits) - 1;
}

std::uint64_t bits_from_signed(std::int64_t value) {
    if (value >= 0) {
        return static_cast<std::uint64_t>(value);
    }
    return ~static_cast<std::uint64_t>(-(value + 1));
}

bool is_ascii(std::string_view text) {
    return std::all_of(text.begin(), text.end(),
                       [](char byte) { return static_cast<unsigned char>(byte) < 0x80; });
}

/// Strict UTF-8 to UTF-16: rejects overlong forms, surrogate code points,
/// values above U+10FFFF and truncated sequences.
std::vector<std::uint16_t> utf16_from_utf8(std::string_view text) {
    std::vector<std::uint16_t> units;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const auto lead = static_cast<unsigned char>(text[cursor]);
        std::size_t continuation_count = 0;
        std::uint32_t code_point = 0;
        std::uint32_t minimum = 0;
        if (lead < 0x80) {
            code_point = lead;
        } else if ((lead & 0xe0) == 0xc0) {
            continuation_count = 1;
            code_point = lead & 0x1f;
            minimum = 0x80;
        } else if ((lead & 0xf0) == 0xe0) {
            continuation_count = 2;
            code_point = lead & 0x0f;
            minimum = 0x800;
        } else if ((lead & 0xf8) == 0xf0) {
            continuation_count = 3;
            code_point = lead & 0x07;
            minimum = 0x10000;
        } else {
            throw std::invalid_argument("Invalid UTF-8 lead byte in plist string");
        }
        if (continuation_count > text.size() - cursor - 1) {
            throw std::invalid_argument("Truncated UTF-8 sequence in plist string");
        }
        for (std::size_t index = 1; index <= continuation_count; ++index) {
            const auto byte = static_cast<unsigned char>(text[cursor + index]);
            if ((byte & 0xc0) != 0x80) {
                throw std::invalid_argument("Invalid UTF-8 continuation in plist string");
            }
            code_point = (code_point << 6) | (byte & 0x3f);
        }
        if (code_point < minimum || code_point > 0x10ffff ||
            (code_point >= 0xd800 && code_point <= 0xdfff)) {
            throw std::invalid_argument("Invalid UTF-8 code point in plist string");
        }
        if (code_point >= 0x10000) {
            const auto offset = code_point - 0x10000;
            units.push_back(static_cast<std::uint16_t>(0xd800 | (offset >> 10)));
            units.push_back(static_cast<std::uint16_t>(0xdc00 | (offset & 0x3ff)));
        } else {
            units.push_back(static_cast<std::uint16_t>(code_point));
        }
        cursor += continuation_count + 1;
    }
    return units;
}

void append_utf8(std::string& output, std::uint32_t code_point) {
    if (code_point < 0x80) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point < 0x800) {
        output.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else if (code_point < 0x10000) {
        output.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
    }
}

/// Strict UTF-16BE to UTF-8: surrogates must form high-low pairs.
std::string utf8_from_utf16_big_endian(const std::uint8_t* input, std::size_t unit_count) {
    std::string output;
    for (std::size_t index = 0; index < unit_count; ++index) {
        const auto unit = static_cast<std::uint32_t>(
            read_big_endian(input + index * utf16_unit_size, utf16_unit_size));
        if (unit >= 0xdc00 && unit <= 0xdfff) {
            throw std::invalid_argument("Unpaired low surrogate in plist string");
        }
        if (unit < 0xd800 || unit > 0xdbff) {
            append_utf8(output, unit);
            continue;
        }
        if (index + 1 == unit_count) {
            throw std::invalid_argument("Unpaired high surrogate in plist string");
        }
        const auto low = static_cast<std::uint32_t>(
            read_big_endian(input + (index + 1) * utf16_unit_size, utf16_unit_size));
        if (low < 0xdc00 || low > 0xdfff) {
            throw std::invalid_argument("Unpaired high surrogate in plist string");
        }
        append_utf8(output, 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00));
        ++index;
    }
    return output;
}

/// Writes objects in plistlib's order so that output matches it byte for byte.
class Encoder {
public:
    Bytes encode(const PlistValue& root) {
        number(root, 0);
        return write_document();
    }

private:
    /// An object is either a value from the tree or a dictionary key string.
    struct ObjectSource {
        const PlistValue* value = nullptr;
        const std::string* key = nullptr;
    };
    /// Identity for sharing: kind plus a canonical byte form of the scalar.
    using ScalarIdentity = std::pair<PlistKind, std::string>;

    static bool is_container(const PlistValue& value) {
        return value.kind() == PlistKind::array || value.kind() == PlistKind::dictionary;
    }

    static ScalarIdentity scalar_identity(const PlistValue& value) {
        std::string canonical;
        switch (value.kind()) {
        case PlistKind::boolean:
            canonical = value.as_boolean() ? "1" : "0";
            break;
        case PlistKind::integer:
            canonical = std::to_string(value.as_integer());
            break;
        case PlistKind::real:
            canonical = std::to_string(bits_of(value.as_real()));
            break;
        case PlistKind::date:
            canonical = std::to_string(bits_of(value.as_date().seconds));
            break;
        case PlistKind::string:
            canonical = value.as_string();
            break;
        case PlistKind::data:
            canonical.assign(value.as_data().begin(), value.as_data().end());
            break;
        case PlistKind::array:
        case PlistKind::dictionary:
            throw std::logic_error("Containers have no scalar identity");
        }
        return {value.kind(), std::move(canonical)};
    }

    void add_object(ObjectSource source) {
        if (objects_.size() == binary_plist::max_objects) {
            throw std::invalid_argument("Plist exceeds object limit");
        }
        objects_.push_back(source);
    }

    /// Depth-first numbering; equal scalars reuse the first object's number.
    void number(const PlistValue& value, std::size_t depth) {
        if (depth > binary_plist::max_depth) {
            throw std::invalid_argument("Plist exceeds nesting limit");
        }
        if (!is_container(value)) {
            if (value.kind() == PlistKind::string) {
                utf16_from_utf8(value.as_string()); // Validate before writing.
            }
            auto identity = scalar_identity(value);
            if (scalar_numbers_.count(identity) == 0) {
                scalar_numbers_.emplace(std::move(identity), objects_.size());
                add_object({&value, nullptr});
            }
            return;
        }
        container_numbers_.emplace(&value, objects_.size());
        add_object({&value, nullptr});
        if (value.kind() == PlistKind::array) {
            for (const auto& element : value.as_array()) {
                number(element, depth + 1);
            }
            return;
        }
        const auto& entries = value.as_dictionary();
        require_unique_keys(entries);
        for (const auto& entry : entries) {
            number_key(entry.key);
        }
        for (const auto& entry : entries) {
            number(entry.value, depth + 1);
        }
    }

    void number_key(const std::string& key) {
        utf16_from_utf8(key);
        ScalarIdentity identity{PlistKind::string, key};
        if (scalar_numbers_.count(identity) == 0) {
            scalar_numbers_.emplace(std::move(identity), objects_.size());
            add_object({nullptr, &key});
        }
    }

    static void require_unique_keys(const PlistDictionary& entries) {
        std::vector<std::string_view> keys;
        keys.reserve(entries.size());
        for (const auto& entry : entries) {
            keys.emplace_back(entry.key);
        }
        std::sort(keys.begin(), keys.end());
        if (std::adjacent_find(keys.begin(), keys.end()) != keys.end()) {
            throw std::invalid_argument("Duplicate plist dictionary key");
        }
    }

    std::size_t number_of(const PlistValue& value) const {
        if (is_container(value)) {
            return container_numbers_.at(&value);
        }
        return scalar_numbers_.at(scalar_identity(value));
    }

    std::size_t number_of_key(const std::string& key) const {
        return scalar_numbers_.at({PlistKind::string, key});
    }

    Bytes write_document() {
        output_.assign(magic.begin(), magic.end());
        reference_width_ = width_for(objects_.size());
        std::vector<std::size_t> offsets;
        offsets.reserve(objects_.size());
        for (const auto& source : objects_) {
            offsets.push_back(output_.size());
            if (source.key != nullptr) {
                write_string(*source.key);
            } else {
                write_value(*source.value);
            }
            require_document_size(output_.size());
        }
        const auto offset_table_offset = output_.size();
        const auto offset_width = width_for(offset_table_offset);
        require_document_size(offset_table_offset + offsets.size() * offset_width +
                              binary_plist::trailer_size);
        for (const auto offset : offsets) {
            append_big_endian(output_, offset, offset_width);
        }
        write_trailer(offset_width, offset_table_offset);
        return std::move(output_);
    }

    static void require_document_size(std::size_t size) {
        if (size > binary_plist::max_document_size) {
            throw std::invalid_argument("Plist exceeds document size limit");
        }
    }

    void write_trailer(std::size_t offset_width, std::size_t offset_table_offset) {
        // Five unused bytes and a zero sort version.
        output_.insert(output_.end(), trailer_layout::offset_width, std::uint8_t{0});
        output_.push_back(static_cast<std::uint8_t>(offset_width));
        output_.push_back(static_cast<std::uint8_t>(reference_width_));
        append_big_endian(output_, objects_.size(), trailer_layout::field_size);
        append_big_endian(output_, 0, trailer_layout::field_size); // Root is object 0.
        append_big_endian(output_, offset_table_offset, trailer_layout::field_size);
    }

    void write_integer(std::int64_t value) {
        const auto width =
            value < 0 ? max_table_width : width_for(static_cast<std::uint64_t>(value));
        output_.push_back(
            static_cast<std::uint8_t>((marker::integer << 4) | width_exponent(width)));
        append_big_endian(output_, bits_from_signed(value), width);
    }

    /// Small lengths share the marker byte; larger ones follow as an integer.
    void write_marker_with_length(std::uint8_t type, std::size_t length) {
        if (length <= marker::max_inline_length) {
            output_.push_back(static_cast<std::uint8_t>((type << 4) | length));
            return;
        }
        output_.push_back(static_cast<std::uint8_t>((type << 4) | marker::extended_length));
        write_integer(static_cast<std::int64_t>(length));
    }

    void write_string(const std::string& text) {
        if (is_ascii(text)) {
            write_marker_with_length(marker::ascii_string, text.size());
            output_.insert(output_.end(), text.begin(), text.end());
            return;
        }
        const auto units = utf16_from_utf8(text);
        write_marker_with_length(marker::utf16_string, units.size());
        for (const auto unit : units) {
            append_big_endian(output_, unit, utf16_unit_size);
        }
    }

    void write_value(const PlistValue& value) {
        switch (value.kind()) {
        case PlistKind::boolean:
            output_.push_back(value.as_boolean() ? marker::boolean_true : marker::boolean_false);
            break;
        case PlistKind::integer:
            write_integer(value.as_integer());
            break;
        case PlistKind::real:
            output_.push_back(static_cast<std::uint8_t>((marker::real << 4) | width_exponent(8)));
            append_big_endian(output_, bits_of(value.as_real()), sizeof(double));
            break;
        case PlistKind::date:
            output_.push_back(marker::date);
            append_big_endian(output_, bits_of(value.as_date().seconds), sizeof(double));
            break;
        case PlistKind::string:
            write_string(value.as_string());
            break;
        case PlistKind::data:
            write_marker_with_length(marker::data, value.as_data().size());
            output_.insert(output_.end(), value.as_data().begin(), value.as_data().end());
            break;
        case PlistKind::array:
            write_marker_with_length(marker::array, value.as_array().size());
            for (const auto& element : value.as_array()) {
                append_big_endian(output_, number_of(element), reference_width_);
            }
            break;
        case PlistKind::dictionary:
            // All key references precede all value references.
            write_marker_with_length(marker::dictionary, value.as_dictionary().size());
            for (const auto& entry : value.as_dictionary()) {
                append_big_endian(output_, number_of_key(entry.key), reference_width_);
            }
            for (const auto& entry : value.as_dictionary()) {
                append_big_endian(output_, number_of(entry.value), reference_width_);
            }
            break;
        }
    }

    std::vector<ObjectSource> objects_;
    std::map<ScalarIdentity, std::size_t> scalar_numbers_;
    std::unordered_map<const PlistValue*, std::size_t> container_numbers_;
    std::size_t reference_width_ = 1;
    Bytes output_;
};

/// Validates the trailer and tables up front, then decodes from the top object.
class Decoder {
public:
    Decoder(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {
        read_trailer();
        in_progress_.assign(object_count_, false);
    }

    PlistValue decode() {
        return decode_object(top_object_, 0);
    }

private:
    void read_trailer() {
        if (size_ > binary_plist::max_document_size) {
            throw std::invalid_argument("Plist exceeds document size limit");
        }
        if (size_ < binary_plist::header_size + binary_plist::trailer_size) {
            throw std::invalid_argument("Plist is too short");
        }
        if (std::memcmp(data_, magic.data(), magic.size()) != 0) {
            throw std::invalid_argument("Missing bplist00 header");
        }
        const auto* trailer = data_ + size_ - binary_plist::trailer_size;
        offset_width_ = trailer[trailer_layout::offset_width];
        reference_width_ = trailer[trailer_layout::reference_width];
        if (offset_width_ == 0 || offset_width_ > max_table_width || reference_width_ == 0 ||
            reference_width_ > max_table_width) {
            throw std::invalid_argument("Invalid plist table width");
        }
        const auto object_count =
            read_big_endian(trailer + trailer_layout::object_count, trailer_layout::field_size);
        const auto top_object =
            read_big_endian(trailer + trailer_layout::top_object, trailer_layout::field_size);
        const auto table_offset = read_big_endian(trailer + trailer_layout::offset_table_offset,
                                                  trailer_layout::field_size);
        if (object_count == 0 || object_count > binary_plist::max_objects) {
            throw std::invalid_argument("Invalid plist object count");
        }
        if (top_object >= object_count) {
            throw std::invalid_argument("Plist top object is out of range");
        }
        object_count_ = static_cast<std::size_t>(object_count);
        top_object_ = static_cast<std::size_t>(top_object);
        // Writers place the offset table directly before the trailer; demand that
        // exact layout so every byte between header and trailer is accounted for.
        const auto trailer_offset = size_ - binary_plist::trailer_size;
        const auto table_size = object_count_ * offset_width_;
        if (table_offset < binary_plist::header_size || table_size > trailer_offset ||
            table_offset != trailer_offset - table_size) {
            throw std::invalid_argument("Invalid plist offset table position");
        }
        objects_end_ = static_cast<std::size_t>(table_offset);
    }

    std::size_t object_offset(std::size_t index) const {
        const auto offset =
            read_big_endian(data_ + objects_end_ + index * offset_width_, offset_width_);
        if (offset < binary_plist::header_size || offset >= objects_end_) {
            throw std::invalid_argument("Plist object offset is out of range");
        }
        return static_cast<std::size_t>(offset);
    }

    /// Objects live in [header_size, objects_end_); nothing may read past it.
    void require_available(std::size_t cursor, std::size_t count) const {
        if (cursor > objects_end_ || count > objects_end_ - cursor) {
            throw std::invalid_argument("Truncated plist object");
        }
    }

    std::uint64_t read_field(std::size_t& cursor, std::size_t width) const {
        require_available(cursor, width);
        const auto value = read_big_endian(data_ + cursor, width);
        cursor += width;
        return value;
    }

    /// Integer objects: 1/2/4 bytes are unsigned, 8 bytes signed. The 16-byte
    /// form only carries values above INT64_MAX, which the model cannot hold.
    std::int64_t read_integer(std::uint8_t width_code, std::size_t& cursor) const {
        if (width_code > 3) {
            throw std::invalid_argument("Unsupported plist integer width");
        }
        const auto width = std::size_t{1} << width_code;
        const auto bits = read_field(cursor, width);
        return width == max_table_width ? signed_from_bits(bits) : static_cast<std::int64_t>(bits);
    }

    std::size_t read_length(std::uint8_t length_code, std::size_t& cursor) const {
        if (length_code != marker::extended_length) {
            return length_code;
        }
        require_available(cursor, 1);
        const auto length_marker = data_[cursor++];
        if ((length_marker >> 4) != marker::integer) {
            throw std::invalid_argument("Invalid plist length marker");
        }
        const auto length = read_integer(length_marker & 0x0f, cursor);
        if (length < 0 || static_cast<std::uint64_t>(length) > objects_end_) {
            throw std::invalid_argument("Invalid plist object length");
        }
        return static_cast<std::size_t>(length);
    }

    std::size_t read_reference(std::size_t& cursor) const {
        const auto reference = read_field(cursor, reference_width_);
        if (reference >= object_count_) {
            throw std::invalid_argument("Plist object reference is out of range");
        }
        return static_cast<std::size_t>(reference);
    }

    void count_value() {
        if (++decoded_values_ > binary_plist::max_decoded_values) {
            throw std::invalid_argument("Plist exceeds decoded value budget");
        }
    }

    void count_payload(std::size_t bytes) {
        if (bytes > binary_plist::max_decoded_payload_bytes - decoded_payload_bytes_) {
            throw std::invalid_argument("Plist exceeds decoded payload budget");
        }
        decoded_payload_bytes_ += bytes;
    }

    PlistValue decode_object(std::size_t index, std::size_t depth) {
        if (depth > binary_plist::max_depth) {
            throw std::invalid_argument("Plist exceeds nesting limit");
        }
        count_value();
        auto cursor = object_offset(index);
        const auto object_marker = data_[cursor++];
        const auto type = static_cast<std::uint8_t>(object_marker >> 4);
        const auto info = static_cast<std::uint8_t>(object_marker & 0x0f);
        if (object_marker == marker::boolean_false || object_marker == marker::boolean_true) {
            return PlistValue(object_marker == marker::boolean_true);
        }
        if (object_marker == marker::date) {
            return PlistValue(PlistDate{double_from_bits(read_field(cursor, sizeof(double)))});
        }
        switch (type) {
        case marker::integer:
            return PlistValue(read_integer(info, cursor));
        case marker::real:
            return decode_real(info, cursor);
        case marker::data:
        case marker::ascii_string:
        case marker::utf16_string: {
            // Read the length first: it advances `cursor` past an extended length.
            const auto length = read_length(info, cursor);
            return decode_bytes_or_text(type, length, cursor);
        }
        case marker::array:
        case marker::dictionary: {
            const auto count = read_length(info, cursor);
            return decode_container(index, type, count, cursor, depth);
        }
        default:
            throw std::invalid_argument("Unsupported plist object type");
        }
    }

    PlistValue decode_real(std::uint8_t width_code, std::size_t& cursor) const {
        if (width_code == 2) {
            const auto bits = static_cast<std::uint32_t>(read_field(cursor, sizeof(float)));
            float value = 0;
            std::memcpy(&value, &bits, sizeof value);
            return PlistValue(static_cast<double>(value));
        }
        if (width_code == 3) {
            return PlistValue(double_from_bits(read_field(cursor, sizeof(double))));
        }
        throw std::invalid_argument("Unsupported plist real width");
    }

    PlistValue decode_bytes_or_text(std::uint8_t type, std::size_t length, std::size_t cursor) {
        const auto byte_count = type == marker::utf16_string ? length * utf16_unit_size : length;
        if (type == marker::utf16_string && length > objects_end_ / utf16_unit_size) {
            throw std::invalid_argument("Truncated plist object");
        }
        require_available(cursor, byte_count);
        count_payload(byte_count);
        const auto* first = data_ + cursor;
        if (type == marker::data) {
            return PlistValue(Bytes(first, first + byte_count));
        }
        if (type == marker::utf16_string) {
            return PlistValue(utf8_from_utf16_big_endian(first, length));
        }
        std::string text(reinterpret_cast<const char*>(first), byte_count);
        if (!is_ascii(text)) {
            throw std::invalid_argument("Non-ASCII byte in plist ASCII string");
        }
        return PlistValue(std::move(text));
    }

    /// Marks the container while its children decode, so a reference back to
    /// any ancestor is reported as a cycle instead of recursing forever.
    PlistValue decode_container(std::size_t index, std::uint8_t type, std::size_t count,
                                std::size_t cursor, std::size_t depth) {
        // References may repeat (["a", "a"] shares one string), so only the
        // reference bytes, not the object count, bound `count`.
        const auto reference_count = type == marker::dictionary ? 2 * count : count;
        if (reference_count > (objects_end_ - std::min(cursor, objects_end_)) / reference_width_) {
            throw std::invalid_argument("Truncated plist container");
        }
        if (in_progress_[index]) {
            throw std::invalid_argument("Plist contains a reference cycle");
        }
        in_progress_[index] = true;
        auto result = type == marker::array ? decode_array(count, cursor, depth)
                                            : decode_dictionary(count, cursor, depth);
        in_progress_[index] = false;
        return result;
    }

    PlistValue decode_array(std::size_t count, std::size_t cursor, std::size_t depth) {
        PlistArray elements;
        elements.reserve(count);
        for (std::size_t element = 0; element < count; ++element) {
            elements.push_back(decode_object(read_reference(cursor), depth + 1));
        }
        return PlistValue(std::move(elements));
    }

    /// Layout: `count` key references, then `count` value references.
    PlistValue decode_dictionary(std::size_t count, std::size_t cursor, std::size_t depth) {
        auto value_cursor = cursor + count * reference_width_;
        PlistDictionary entries;
        entries.reserve(count);
        for (std::size_t entry = 0; entry < count; ++entry) {
            auto key = decode_object(read_reference(cursor), depth + 1);
            if (key.kind() != PlistKind::string) {
                throw std::invalid_argument("Plist dictionary key is not a string");
            }
            auto value = decode_object(read_reference(value_cursor), depth + 1);
            entries.push_back({key.as_string(), std::move(value)});
        }
        std::vector<std::string_view> keys;
        keys.reserve(entries.size());
        for (const auto& entry : entries) {
            keys.emplace_back(entry.key);
        }
        std::sort(keys.begin(), keys.end());
        if (std::adjacent_find(keys.begin(), keys.end()) != keys.end()) {
            throw std::invalid_argument("Duplicate plist dictionary key");
        }
        return PlistValue(std::move(entries));
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t offset_width_ = 0;
    std::size_t reference_width_ = 0;
    std::size_t object_count_ = 0;
    std::size_t top_object_ = 0;
    std::size_t objects_end_ = 0;
    std::vector<bool> in_progress_;
    std::size_t decoded_values_ = 0;
    std::size_t decoded_payload_bytes_ = 0;
};

template <typename Expected, typename Variant>
const Expected& get_kind(const Variant& value, const char* kind_name) {
    if (const auto* result = std::get_if<Expected>(&value)) {
        return *result;
    }
    throw std::logic_error(std::string("Plist value is not ") + kind_name);
}
} // namespace

PlistValue::PlistValue(bool value) : value_(value) {}
PlistValue::PlistValue(std::int64_t value) : value_(value) {}
PlistValue::PlistValue(int value) : value_(static_cast<std::int64_t>(value)) {}
PlistValue::PlistValue(double value) : value_(value) {}
PlistValue::PlistValue(PlistDate value) : value_(value) {}
PlistValue::PlistValue(std::string value) : value_(std::move(value)) {}
PlistValue::PlistValue(const char* value) : value_(std::string(value)) {}
PlistValue::PlistValue(Bytes value) : value_(std::move(value)) {}
PlistValue::PlistValue(PlistArray value) : value_(std::move(value)) {}
PlistValue::PlistValue(PlistDictionary value) : value_(std::move(value)) {}

// Variant alternatives are declared in PlistKind order.
PlistKind PlistValue::kind() const noexcept {
    return static_cast<PlistKind>(value_.index());
}

bool PlistValue::as_boolean() const {
    return get_kind<bool>(value_, "a boolean");
}
std::int64_t PlistValue::as_integer() const {
    return get_kind<std::int64_t>(value_, "an integer");
}
double PlistValue::as_real() const {
    return get_kind<double>(value_, "a real");
}
PlistDate PlistValue::as_date() const {
    return get_kind<PlistDate>(value_, "a date");
}
const std::string& PlistValue::as_string() const {
    return get_kind<std::string>(value_, "a string");
}
const Bytes& PlistValue::as_data() const {
    return get_kind<Bytes>(value_, "data");
}
const PlistArray& PlistValue::as_array() const {
    return get_kind<PlistArray>(value_, "an array");
}
const PlistDictionary& PlistValue::as_dictionary() const {
    return get_kind<PlistDictionary>(value_, "a dictionary");
}

const PlistValue* PlistValue::find(std::string_view key) const noexcept {
    const auto* entries = std::get_if<PlistDictionary>(&value_);
    if (entries == nullptr) {
        return nullptr;
    }
    for (const auto& entry : *entries) {
        if (entry.key == key) {
            return &entry.value;
        }
    }
    return nullptr;
}

bool operator==(const PlistValue& left, const PlistValue& right) {
    if (left.kind() != right.kind()) {
        return false;
    }
    switch (left.kind()) {
    case PlistKind::boolean:
        return left.as_boolean() == right.as_boolean();
    case PlistKind::integer:
        return left.as_integer() == right.as_integer();
    case PlistKind::real:
        return bits_of(left.as_real()) == bits_of(right.as_real());
    case PlistKind::date:
        return bits_of(left.as_date().seconds) == bits_of(right.as_date().seconds);
    case PlistKind::string:
        return left.as_string() == right.as_string();
    case PlistKind::data:
        return left.as_data() == right.as_data();
    case PlistKind::array:
        return left.as_array() == right.as_array();
    case PlistKind::dictionary:
        return left.as_dictionary() == right.as_dictionary();
    }
    return false;
}

bool operator==(const PlistEntry& left, const PlistEntry& right) {
    return left.key == right.key && left.value == right.value;
}

Bytes encode_binary_plist(const PlistValue& root) {
    return Encoder().encode(root);
}

PlistValue decode_binary_plist(const std::uint8_t* data, std::size_t size) {
    if (data == nullptr) {
        throw std::invalid_argument("Missing plist document");
    }
    return Decoder(data, size).decode();
}

PlistValue decode_binary_plist(const Bytes& document) {
    return decode_binary_plist(document.data(), document.size());
}
} // namespace send_airplay2::detail
