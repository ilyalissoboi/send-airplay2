// SPDX-License-Identifier: Apache-2.0
// Fixtures come from CPython plistlib (fixtures/generate_plist.py); malformed and
// literal documents are assembled here from the bplist00 layout, not the encoder.
#include "binary_plist.h"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace send_airplay2::detail;

namespace {
int failures = 0;
const char* active_test = "test setup";
template <typename Function> void run_case(const char* name, Function action) {
    active_test = name;
    try {
        action();
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << active_test << "]: unexpected exception: " << error.what() << '\n';
        ++failures;
    }
}
void check(bool passed, std::string_view message) {
    if (!passed) {
        std::cerr << "FAIL [" << active_test << "]: " << message << '\n';
        ++failures;
    }
}

Bytes unhex(const std::string& text) {
    Bytes output;
    if (text.size() % 2 != 0) {
        throw std::runtime_error("Odd fixture hex length");
    }
    for (std::size_t offset = 0; offset < text.size(); offset += 2) {
        output.push_back(
            static_cast<std::uint8_t>(std::stoul(text.substr(offset, 2), nullptr, 16)));
    }
    return output;
}
Bytes fixture(const std::string& directory, const std::string& name) {
    std::ifstream input(directory + '/' + name + ".hex");
    std::string hex;
    if (!(input >> hex)) {
        throw std::runtime_error("Missing plist fixture: " + name);
    }
    return unhex(hex);
}
Bytes ascii_bytes(std::string_view text) {
    return Bytes(text.begin(), text.end());
}

/// Path of the first structural difference, for readable failure messages.
std::string first_difference(const PlistValue& expected, const PlistValue& actual,
                             const std::string& path) {
    if (expected.kind() != actual.kind()) {
        return path + " (kind differs)";
    }
    if (expected.kind() == PlistKind::array) {
        const auto& left = expected.as_array();
        const auto& right = actual.as_array();
        if (left.size() != right.size()) {
            return path + " (array size differs)";
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            auto found = first_difference(left[index], right[index],
                                          path + '[' + std::to_string(index) + ']');
            if (!found.empty()) {
                return found;
            }
        }
        return {};
    }
    if (expected.kind() == PlistKind::dictionary) {
        const auto& left = expected.as_dictionary();
        const auto& right = actual.as_dictionary();
        if (left.size() != right.size()) {
            return path + " (dictionary size differs)";
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            if (left[index].key != right[index].key) {
                return path + " (key " + std::to_string(index) + " differs)";
            }
            auto found = first_difference(left[index].value, right[index].value,
                                          path + '.' + left[index].key);
            if (!found.empty()) {
                return found;
            }
        }
        return {};
    }
    return expected == actual ? std::string{} : path + " (value differs)";
}

void check_decodes_to(const std::string& scenario, const Bytes& document,
                      const PlistValue& expected) {
    const auto decoded = decode_binary_plist(document);
    const auto difference = first_difference(expected, decoded, "root");
    check(difference.empty() && decoded == expected,
          scenario + ": decoded value differs at " + difference);
}

void check_encodes_to(const std::string& scenario, const PlistValue& value, const Bytes& expected) {
    const auto encoded = encode_binary_plist(value);
    if (encoded == expected) {
        return;
    }
    std::size_t offset = 0;
    while (offset < encoded.size() && offset < expected.size() &&
           encoded[offset] == expected[offset]) {
        ++offset;
    }
    check(false, scenario + ": encoding differs at byte " + std::to_string(offset) + " (encoded " +
                     std::to_string(encoded.size()) + " bytes, expected " +
                     std::to_string(expected.size()) + ")");
}

/// Expects std::invalid_argument; any other outcome is a failure.
template <typename Function> void expect_rejected(const std::string& scenario, Function action) {
    try {
        action();
        check(false, scenario + ": expected rejection");
    } catch (const std::invalid_argument&) {
    } catch (const std::exception& error) {
        check(false, scenario + ": wrong exception type: " + error.what());
    }
}
void expect_decode_rejected(const std::string& scenario, const Bytes& document) {
    expect_rejected(scenario, [&] { (void)decode_binary_plist(document); });
}

// ---- Independent document assembly from the bplist00 layout ----

/// Trailer field offsets within the final 32 bytes.
constexpr std::size_t trailer_size = 32;
constexpr std::size_t trailer_offset_width = 6;
constexpr std::size_t trailer_reference_width = 7;
constexpr std::size_t trailer_object_count = 8;
constexpr std::size_t trailer_top_object = 16;
constexpr std::size_t trailer_table_offset = 24;

void append_big_endian(Bytes& output, std::uint64_t value, std::size_t width) {
    for (std::size_t index = width; index > 0; --index) {
        output.push_back(static_cast<std::uint8_t>(value >> (8 * (index - 1))));
    }
}
void set_trailer_field(Bytes& document, std::size_t field_offset, std::uint64_t value) {
    const auto first = document.size() - trailer_size + field_offset;
    for (std::size_t index = 0; index < 8; ++index) {
        document[first + index] = static_cast<std::uint8_t>(value >> (8 * (7 - index)));
    }
}

/// Header, the objects in order, their offset table, then the trailer. Object
/// bytes must already contain references of `reference_width` bytes.
Bytes build_document(const std::vector<Bytes>& objects, std::uint64_t top_object = 0,
                     std::uint8_t reference_width = 1, std::uint8_t offset_width = 1) {
    Bytes document = ascii_bytes("bplist00");
    std::vector<std::uint64_t> offsets;
    for (const auto& object : objects) {
        offsets.push_back(document.size());
        document.insert(document.end(), object.begin(), object.end());
    }
    const auto table_offset = document.size();
    for (const auto offset : offsets) {
        if (offset_width < 8 && offset >= (std::uint64_t{1} << (8 * offset_width))) {
            throw std::logic_error("Test document offset does not fit its width");
        }
        append_big_endian(document, offset, offset_width);
    }
    document.insert(document.end(), 6, std::uint8_t{0});
    document.push_back(offset_width);
    document.push_back(reference_width);
    append_big_endian(document, objects.size(), 8);
    append_big_endian(document, top_object, 8);
    append_big_endian(document, table_offset, 8);
    return document;
}

/// `count` nested one-element arrays ending in an empty array; the innermost
/// value sits at depth count - 1.
std::vector<Bytes> nested_arrays(std::size_t count) {
    std::vector<Bytes> objects;
    for (std::size_t index = 0; index + 1 < count; ++index) {
        objects.push_back({0xa1, static_cast<std::uint8_t>(index + 1)});
    }
    objects.push_back({0xa0});
    return objects;
}

// ---- Expected values for the plistlib fixtures ----

Bytes counting_bytes(std::size_t count, std::size_t modulus) {
    Bytes bytes(count);
    for (std::size_t index = 0; index < count; ++index) {
        bytes[index] = static_cast<std::uint8_t>(index % modulus);
    }
    return bytes;
}

PlistValue expected_scalars() {
    return PlistDictionary{
        {"true", true},
        {"false", false},
        {"zero", 0},
        {"u8-max", 255},
        {"u16-min", 256},
        {"u16-max", 65535},
        {"u32-min", 65536},
        {"u32-max", std::int64_t{4294967295}},
        {"i64-min-positive", std::int64_t{4294967296}},
        {"i64-max", std::numeric_limits<std::int64_t>::max()},
        {"minus-one", -1},
        {"i64-min", std::numeric_limits<std::int64_t>::min()},
        {"real", 1.5},
        {"negative-real", -0.25},
        // 2026-10-06T00:00:00Z: 9,409 days after 2001-01-01, times 86,400 s.
        {"date", PlistDate{812937600.0}},
        {"ascii", "file"},
        // U+2713 CHECK MARK, U+00FC, U+1F600 (outside the BMP: a surrogate pair).
        {"unicode", "Living Room \xe2\x9c\x93 \xc3\xbc \xf0\x9f\x98\x80"},
        {"data", counting_bytes(20, 256)},
        {"empty-string", ""},
        {"empty-data", Bytes{}},
        {"empty-array", PlistArray{}},
        {"empty-dict", PlistDictionary{}},
    };
}

PlistValue expected_shared_scalars() {
    return PlistDictionary{
        {"type", "setProperty"},
        {"property", "type"},
        {"value", true},
        {"item", PlistDictionary{{"uuid", "type"}}},
        {"list", PlistArray{"a", "a", 1, 1, true, 1.0, ascii_bytes("a"), "setProperty"}},
    };
}

PlistValue expected_wide_tables() {
    PlistArray numbers;
    for (int number = 1000; number < 1300; ++number) {
        numbers.emplace_back(number);
    }
    return PlistDictionary{
        {"numbers", std::move(numbers)},
        {"long-string", std::string(300, 'x')},
        {"large-data", counting_bytes(70000, 251)},
    };
}

PlistValue expected_insert_command() {
    return PlistDictionary{
        {"type", "insertPlayQueueItem"},
        {"item",
         PlistDictionary{
             {"uuid", "00000000-0000-4000-8000-000000000001"},
             {"mediaType", "file"},
             {"Content-Location", "http://192.0.2.10:49152/synthetic-token/media"},
             {"Start-Position-Seconds", 0.0},
         }},
    };
}

PlistValue expected_playback_state_event() {
    return PlistDictionary{
        {"type", "playbackState"},
        {"params", PlistDictionary{{"playbackState", "Playing"}}},
    };
}

/// The /command and event envelope: the inner plist nested as data.
PlistValue wrapped(const Bytes& inner_document) {
    return PlistDictionary{{"params", PlistDictionary{{"data", inner_document}}}};
}

// ---- Scenario groups ----

void plistlib_fixture_tests(const std::string& directory) {
    run_case("plistlib fixture: scalars", [&] {
        const auto document = fixture(directory, "scalars");
        check_decodes_to("scalars", document, expected_scalars());
        check_encodes_to("scalars", expected_scalars(), document);
    });
    run_case("plistlib fixture: shared scalars", [&] {
        const auto document = fixture(directory, "shared-scalars");
        check_decodes_to("shared-scalars", document, expected_shared_scalars());
        check_encodes_to("shared-scalars", expected_shared_scalars(), document);
    });
    run_case("plistlib fixture: wide tables", [&] {
        const auto document = fixture(directory, "wide-tables");
        // Confirm the fixture exercises 2-byte references and 4-byte offsets.
        const auto trailer = document.size() - trailer_size;
        check(document[trailer + trailer_reference_width] == 2, "fixture reference width is 2");
        check(document[trailer + trailer_offset_width] == 4, "fixture offset width is 4");
        check_decodes_to("wide-tables", document, expected_wide_tables());
        check_encodes_to("wide-tables", expected_wide_tables(), document);
    });
    run_case("plistlib fixture: insertPlayQueueItem command", [&] {
        const auto inner = fixture(directory, "command-insert-inner");
        const auto outer = fixture(directory, "command-insert");
        check_decodes_to("command inner", inner, expected_insert_command());
        check_encodes_to("command inner", expected_insert_command(), inner);
        check_decodes_to("command envelope", outer, wrapped(inner));
        check_encodes_to("command envelope", wrapped(inner), outer);
    });
    run_case("plistlib fixture: playback state event", [&] {
        const auto inner = fixture(directory, "event-playback-state-inner");
        const auto outer = fixture(directory, "event-playback-state");
        check_encodes_to("event envelope", wrapped(inner), outer);
        // Unwrap the way the session layer will: params.data, then the event.
        const auto envelope = decode_binary_plist(outer);
        const auto* params = envelope.find("params");
        const auto* data = params == nullptr ? nullptr : params->find("data");
        check(data != nullptr && data->kind() == PlistKind::data, "envelope holds params.data");
        if (data != nullptr) {
            const auto event = decode_binary_plist(data->as_data());
            check(event == expected_playback_state_event(), "nested event decodes");
            const auto* state = event.find("params")->find("playbackState");
            check(state != nullptr && state->as_string() == "Playing", "state is Playing");
        }
    });
}

void literal_layout_tests() {
    run_case("literal layout: single true", [] {
        // Header, object 0x09 at offset 8, a 1-byte offset table holding 8, and a
        // trailer: widths 1/1, one object, top 0, table at offset 9.
        Bytes expected = ascii_bytes("bplist00");
        expected.push_back(0x09);
        expected.push_back(0x08);
        expected.insert(expected.end(), {0, 0, 0, 0, 0, 0, 1, 1});
        append_big_endian(expected, 1, 8);
        append_big_endian(expected, 0, 8);
        append_big_endian(expected, 9, 8);
        check_encodes_to("single true", PlistValue(true), expected);
        check_decodes_to("single true", expected, PlistValue(true));
    });
    run_case("literal layout: integer widths", [] {
        // 1/2/4-byte integers are unsigned; 8-byte integers are signed.
        check_decodes_to("u8", build_document({{0x10, 0xff}}), PlistValue(255));
        check_decodes_to("u16", build_document({{0x11, 0xff, 0xff}}), PlistValue(65535));
        check_decodes_to("u32", build_document({{0x12, 0xff, 0xff, 0xff, 0xff}}),
                         PlistValue(std::int64_t{4294967295}));
        check_decodes_to("i64",
                         build_document({{0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}),
                         PlistValue(-1));
        check_encodes_to("negative uses 8 bytes", PlistValue(-2),
                         build_document({{0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe}}));
    });
    run_case("literal layout: extended lengths", [] {
        // 15 bytes no longer fit the marker nibble: 0x4f, then integer 0x10 0x0f.
        Bytes object{0x4f, 0x10, 0x0f};
        const auto payload = counting_bytes(15, 256);
        object.insert(object.end(), payload.begin(), payload.end());
        check_encodes_to("15-byte data", PlistValue(payload), build_document({object}));
        Bytes inline_object{0x4e};
        const auto inline_payload = counting_bytes(14, 256);
        inline_object.insert(inline_object.end(), inline_payload.begin(), inline_payload.end());
        check_encodes_to("14-byte data", PlistValue(inline_payload),
                         build_document({inline_object}));
    });
}

void accepted_variant_tests() {
    run_case("accepted: 4-byte real", [] {
        // 0x22 marker, IEEE binary32 2.5 = 0x40200000.
        check_decodes_to("float32", build_document({{0x22, 0x40, 0x20, 0x00, 0x00}}),
                         PlistValue(2.5));
    });
    run_case("accepted: 3-byte table widths", [] {
        // Widths need not be powers of two; references and offsets are big-endian.
        const auto document =
            build_document({{0xa1, 0x00, 0x00, 0x01}, {0x09}}, 0, /*reference_width=*/3,
                           /*offset_width=*/3);
        check_decodes_to("3-byte widths", document, PlistValue(PlistArray{true}));
    });
    run_case("accepted: repeated references", [] {
        // 300 references to one object exceed the two-object table; still valid.
        Bytes array{0xaf, 0x11, 0x01, 0x2c};
        array.insert(array.end(), 300, std::uint8_t{1});
        // The string's offset (312) needs a 2-byte offset table.
        const auto decoded = decode_binary_plist(
            build_document({array, {0x51, 'a'}}, 0, /*reference_width=*/1, /*offset_width=*/2));
        check(decoded.as_array().size() == 300 && decoded.as_array()[299].as_string() == "a",
              "300 shared elements decode");
    });
    run_case("accepted: maximum nesting", [] {
        const auto decoded =
            decode_binary_plist(build_document(nested_arrays(binary_plist::max_depth + 1)));
        check(decoded.kind() == PlistKind::array, "depth 32 decodes");
    });
    run_case("accepted: payload just under budget", [] {
        // 40 references to a 100,000-byte data object: 4,000,000 bytes decoded.
        Bytes array{0xaf, 0x10, 40};
        array.insert(array.end(), 40, std::uint8_t{1});
        Bytes data{0x4f, 0x12, 0x00, 0x01, 0x86, 0xa0};
        data.insert(data.end(), 100000, std::uint8_t{0x5a});
        const auto decoded = decode_binary_plist(build_document({array, data}));
        check(decoded.as_array().size() == 40, "40 shared data values decode");
    });
}

void malformed_document_tests() {
    const auto valid = build_document({{0x09}});
    run_case("malformed: framing", [&] {
        expect_decode_rejected("empty", Bytes{});
        expect_decode_rejected("shorter than header and trailer", Bytes(39, 0));
        auto bad_magic = valid;
        bad_magic[7] = '1'; // "bplist01"
        expect_decode_rejected("version 01", bad_magic);
        expect_decode_rejected("over document size limit",
                               Bytes(binary_plist::max_document_size + 1, 0));
    });
    run_case("malformed: trailer fields", [&] {
        for (const std::uint8_t width : {std::uint8_t{0}, std::uint8_t{9}}) {
            auto document = valid;
            document[document.size() - trailer_size + trailer_offset_width] = width;
            expect_decode_rejected("offset width " + std::to_string(width), document);
            document = valid;
            document[document.size() - trailer_size + trailer_reference_width] = width;
            expect_decode_rejected("reference width " + std::to_string(width), document);
        }
        auto document = valid;
        set_trailer_field(document, trailer_object_count, 0);
        expect_decode_rejected("zero objects", document);
        document = valid;
        set_trailer_field(document, trailer_object_count, binary_plist::max_objects + 1);
        expect_decode_rejected("object count over limit", document);
        document = valid;
        set_trailer_field(document, trailer_top_object, 1);
        expect_decode_rejected("top object out of range", document);
        document = valid;
        set_trailer_field(document, trailer_table_offset, 8);
        expect_decode_rejected("offset table not adjacent to trailer", document);
        document = valid;
        set_trailer_field(document, trailer_table_offset,
                          std::numeric_limits<std::uint64_t>::max());
        expect_decode_rejected("offset table offset overflows", document);
    });
    run_case("malformed: object offsets", [] {
        auto document = build_document({{0x09}});
        document[9] = 0x00; // Offset into the header.
        expect_decode_rejected("offset in header", document);
        document[9] = 0x09; // Offset of the offset table itself.
        expect_decode_rejected("offset in offset table", document);
    });
    run_case("malformed: unsupported markers", [] {
        Bytes sixteen_byte_integer(17, 0x00);
        sixteen_byte_integer[0] = 0x14;
        const std::vector<std::pair<const char*, Bytes>> objects{
            {"null", {0x00}},
            {"fill", {0x0f}},
            {"16-byte integer", sixteen_byte_integer},
            {"2-byte real", {0x21, 0x00, 0x00}},
            {"non-standard date width", {0x32, 0x00, 0x00, 0x00, 0x00}},
            {"UID", {0x80, 0x01}},
            {"set", {0xc0}},
            {"reserved type 7", {0x70}},
        };
        for (const auto& [name, object] : objects) {
            expect_decode_rejected(name, build_document({object}));
        }
    });
    run_case("malformed: truncated objects", [] {
        expect_decode_rejected("8-byte integer with 4 bytes", build_document({{0x13, 1, 2, 3, 4}}));
        expect_decode_rejected("string longer than region",
                               build_document({{0x55, 'a', 'b', 'c'}}));
        expect_decode_rejected("length marker not an integer", build_document({{0x5f, 0x55}}));
        expect_decode_rejected(
            "negative extended length",
            build_document({{0x5f, 0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}}));
        expect_decode_rejected("extended length past region",
                               build_document({{0x4f, 0x12, 0x7f, 0, 0, 0}}));
        expect_decode_rejected("UTF-16 length past region", build_document({{0x62, 0x00, 0x41}}));
        expect_decode_rejected("array references past region", build_document({{0xa3, 0x00}}));
        // The dictionary is the last object, so its value reference would lie
        // past the object region.
        expect_decode_rejected("dictionary value references missing",
                               build_document({{0x51, 'k'}, {0xd1, 0x00}}, 1));
    });
    run_case("malformed: text", [] {
        expect_decode_rejected("non-ASCII byte in ASCII string", build_document({{0x51, 0x80}}));
        expect_decode_rejected("lone high surrogate", build_document({{0x61, 0xd8, 0x00}}));
        expect_decode_rejected("lone low surrogate", build_document({{0x61, 0xdc, 0x00}}));
        expect_decode_rejected("high surrogate before non-surrogate",
                               build_document({{0x62, 0xd8, 0x00, 0x00, 0x41}}));
    });
    run_case("malformed: references", [] {
        expect_decode_rejected("reference out of range", build_document({{0xa1, 0x05}}));
        expect_decode_rejected("array containing itself", build_document({{0xa1, 0x00}}));
        expect_decode_rejected("indirect cycle", build_document({{0xa1, 0x01}, {0xa1, 0x00}}));
        expect_decode_rejected("non-string key",
                               build_document({{0xd1, 0x01, 0x01}, {0x10, 0x01}}));
        // {"a": 1, "a": 2}: objects 1 = "a", 2 = 1, 3 = 2.
        expect_decode_rejected(
            "duplicate key",
            build_document(
                {{0xd2, 0x01, 0x01, 0x02, 0x03}, {0x51, 'a'}, {0x10, 0x01}, {0x10, 0x02}}));
    });
    run_case("malformed: resource budgets", [] {
        expect_decode_rejected("nesting over limit",
                               build_document(nested_arrays(binary_plist::max_depth + 2)));
        // 25 levels of [next, next]: 2^26 - 1 values from 26 objects.
        std::vector<Bytes> doubling;
        for (std::uint8_t index = 0; index < 25; ++index) {
            doubling.push_back(
                {0xa2, static_cast<std::uint8_t>(index + 1), static_cast<std::uint8_t>(index + 1)});
        }
        doubling.push_back({0x09});
        expect_decode_rejected("shared-object expansion", build_document(doubling));
        // 50 references to a 100,000-byte data object: 5,000,000 bytes > 4 MiB.
        Bytes array{0xaf, 0x10, 50};
        array.insert(array.end(), 50, std::uint8_t{1});
        Bytes data{0x4f, 0x12, 0x00, 0x01, 0x86, 0xa0};
        data.insert(data.end(), 100000, std::uint8_t{0x5a});
        expect_decode_rejected("shared payload expansion", build_document({array, data}));
    });
}

void encoder_rejection_tests() {
    run_case("encoder: invalid UTF-8", [] {
        const std::vector<std::pair<const char*, std::string>> invalid{
            {"lone continuation", "\x80"},         {"overlong NUL", "\xc0\x80"},
            {"encoded surrogate", "\xed\xa0\x80"}, {"above U+10FFFF", "\xf4\x90\x80\x80"},
            {"truncated sequence", "\xe2\x82"},
        };
        for (const auto& example : invalid) {
            // C++17 lambdas cannot capture structured bindings; name a reference.
            const std::string& text = example.second;
            expect_rejected(std::string("value: ") + example.first,
                            [&] { (void)encode_binary_plist(PlistValue(text)); });
            expect_rejected(std::string("key: ") + example.first, [&] {
                (void)encode_binary_plist(PlistValue(PlistDictionary{{text, 1}}));
            });
        }
    });
    run_case("encoder: duplicate key", [] {
        expect_rejected("duplicate key", [] {
            (void)encode_binary_plist(PlistValue(PlistDictionary{{"a", 1}, {"a", 2}}));
        });
    });
    run_case("encoder: nesting limit", [] {
        auto build = [](std::size_t levels) {
            PlistValue value = PlistArray{};
            for (std::size_t level = 0; level < levels; ++level) {
                value = PlistArray{value};
            }
            return value;
        };
        // max_depth wrappings put the innermost array at depth max_depth.
        const auto deepest = build(binary_plist::max_depth);
        check(decode_binary_plist(encode_binary_plist(deepest)) == deepest, "depth 32 round-trips");
        expect_rejected("depth 33",
                        [&] { (void)encode_binary_plist(build(binary_plist::max_depth + 1)); });
    });
    run_case("encoder: object and size limits", [] {
        PlistArray numbers;
        for (std::size_t number = 0; number + 1 < binary_plist::max_objects; ++number) {
            numbers.emplace_back(static_cast<std::int64_t>(number));
        }
        const PlistValue at_limit = numbers; // The array plus 16,383 integers.
        check(decode_binary_plist(encode_binary_plist(at_limit)) == at_limit,
              "16,384 objects round-trip");
        numbers.emplace_back(std::int64_t{-1});
        expect_rejected("16,385 objects", [&] { (void)encode_binary_plist(PlistValue(numbers)); });
        expect_rejected("document over size limit", [] {
            (void)encode_binary_plist(PlistValue(Bytes(binary_plist::max_document_size, 0)));
        });
    });
}

void accessor_tests() {
    run_case("accessors", [] {
        const PlistValue value = PlistDictionary{{"key", "value"}};
        check(value.find("key") != nullptr && value.find("key")->as_string() == "value",
              "find returns the entry");
        check(value.find("missing") == nullptr, "find misses absent key");
        check(PlistValue(1).find("key") == nullptr, "find on non-dictionary is null");
        try {
            (void)value.as_integer();
            check(false, "kind mismatch throws");
        } catch (const std::logic_error&) {
        }
        check(PlistValue(0.0) != PlistValue(-0.0), "reals compare by bit pattern");
        check(PlistValue(1) != PlistValue(true), "integer and boolean differ");
    });
}

/// Every single-byte change and truncation must decode or throw
/// std::invalid_argument: never crash, hang or throw another type.
void mutation_tests(const std::string& directory) {
    for (const char* name :
         {"scalars", "shared-scalars", "command-insert", "event-playback-state"}) {
        run_case("mutations", [&] {
            const auto original = fixture(directory, name);
            auto probe = [&](const Bytes& document, const std::string& description) {
                try {
                    (void)decode_binary_plist(document);
                } catch (const std::invalid_argument&) {
                } catch (const std::exception& error) {
                    check(false, std::string(name) + " " + description + ": " + error.what());
                }
            };
            for (std::size_t offset = 0; offset < original.size(); ++offset) {
                for (const std::uint8_t mask :
                     {std::uint8_t{0x01}, std::uint8_t{0x80}, std::uint8_t{0xff}}) {
                    auto document = original;
                    document[offset] ^= mask;
                    probe(document,
                          "offset " + std::to_string(offset) + " xor " + std::to_string(mask));
                }
            }
            for (std::size_t length = 0; length < original.size(); ++length) {
                probe(
                    Bytes(original.begin(), original.begin() + static_cast<std::ptrdiff_t>(length)),
                    "truncated to " + std::to_string(length));
            }
            std::mt19937 generator(0x504c4953); // Fixed seed: reproducible failures.
            std::uniform_int_distribution<std::size_t> position(0, original.size() - 1);
            std::uniform_int_distribution<int> byte(0, 255);
            for (int iteration = 0; iteration < 2000; ++iteration) {
                auto document = original;
                for (int change = 0; change < 4; ++change) {
                    document[position(generator)] = static_cast<std::uint8_t>(byte(generator));
                }
                probe(document, "random mutation " + std::to_string(iteration));
            }
        });
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: plist_tests FIXTURE_DIRECTORY\n";
        return 2;
    }
    const std::string directory = argv[1];
    plistlib_fixture_tests(directory);
    literal_layout_tests();
    accepted_variant_tests();
    malformed_document_tests();
    encoder_rejection_tests();
    accessor_tests();
    mutation_tests(directory);
    if (failures != 0) {
        std::cerr << failures << " plist test failure(s)\n";
        return 1;
    }
    std::cout << "plist tests passed\n";
    return 0;
}
