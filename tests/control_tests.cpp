// SPDX-License-Identifier: Apache-2.0
// Public synthetic keys only. Independent fixture provenance: fixtures/README.md.
#include "control_records.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <type_traits>

using namespace send_airplay2::detail;
namespace send_airplay2::detail {
// Test-only access keeps counter injection out of the production constructors.
struct ControlRecordTestAccess {
    static void counter(ControlWriter& writer, std::uint64_t value) {
        writer.counter_ = value;
    }
    static void counter(ControlReader& reader, std::uint64_t value) {
        reader.counter_ = value;
    }
    static bool wiped(const ControlReader& reader) {
        return reader.closed_ && reader.pending_size_ == 0 &&
               std::all_of(reader.key_.begin(), reader.key_.end(),
                           [](auto byte) { return byte == 0; }) &&
               std::all_of(reader.pending_.begin(), reader.pending_.end(),
                           [](auto byte) { return byte == 0; });
    }
    static bool wiped(const ControlWriter& writer) {
        return writer.closed_ && std::all_of(writer.key_.begin(), writer.key_.end(),
                                             [](auto byte) { return byte == 0; });
    }
};
} // namespace send_airplay2::detail

namespace {
int failures = 0;
void check(bool passed, const char* message) {
    if (!passed) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
template <typename Function> void expect_control_error(Function action, ControlError reason) {
    try {
        action();
        check(false, "Expected control error");
    } catch (const ControlException& error) {
        check(error.reason() == reason, "Correct control error category");
    }
}
template <typename Function> void expect_bad_tlv(Function action) {
    try {
        action();
        check(false, "Expected malformed TLV rejection");
    } catch (const std::invalid_argument&) {
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
Bytes fixture(const std::string& directory, const char* name) {
    std::ifstream input(directory + '/' + name);
    std::string hex;
    if (!(input >> hex)) {
        throw std::runtime_error("Missing control fixture");
    }
    return unhex(hex);
}
ControlKey synthetic_key() {
    ControlKey key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(index);
    }
    return key;
}
Bytes synthetic_plaintext(std::size_t length) {
    Bytes plaintext(length);
    for (std::size_t index = 0; index < length; ++index) {
        plaintext[index] = static_cast<std::uint8_t>(index % 251);
    }
    return plaintext;
}
Bytes slice(const Bytes& bytes, std::size_t first, std::size_t last) {
    return Bytes(bytes.begin() + static_cast<std::ptrdiff_t>(first),
                 bytes.begin() + static_cast<std::ptrdiff_t>(last));
}

void crypto_vectors(const std::string& directory) {
    // RFC 8439 section 2.8.2: independent known-answer ciphertext AND tag.
    ControlKey key{};
    for (std::size_t index = 0; index < key.size(); ++index) {
        key[index] = static_cast<std::uint8_t>(0x80 + index);
    }
    const ControlNonce nonce{7, 0, 0, 0, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47};
    const auto aad = unhex("50515253c0c1c2c3c4c5c6c7");
    const std::string text = "Ladies and Gentlemen of the class of '99: If I could offer you "
                             "only one tip for the future, sunscreen would be it.";
    const Bytes plaintext(text.begin(), text.end());
    const auto expected = fixture(directory, "rfc8439-aead.hex");
    check(seal_record(key, nonce, aad, plaintext) == expected, "RFC AEAD seal known answer");
    check(open_record(key, nonce, aad, expected) == plaintext, "RFC AEAD open known answer");
    auto corrupt = expected;
    corrupt.back() ^= 1;
    expect_control_error([&] { (void)open_record(key, nonce, aad, corrupt); },
                         ControlError::authentication);
    auto wrong_aad = aad;
    wrong_aad[0] ^= 1;
    expect_control_error([&] { (void)open_record(key, nonce, wrong_aad, expected); },
                         ControlError::authentication);
    auto wrong_nonce = nonce;
    wrong_nonce[0] ^= 1;
    expect_control_error([&] { (void)open_record(key, wrong_nonce, aad, expected); },
                         ControlError::authentication);
    expect_control_error([&] { (void)open_record(key, nonce, {}, Bytes(15)); },
                         ControlError::invalid_length);
    expect_control_error([&] { (void)seal_record(key, nonce, {}, Bytes(65537)); },
                         ControlError::invalid_length);
    expect_control_error([&] { (void)seal_record(key, nonce, Bytes(65537), {}); },
                         ControlError::invalid_length);
    const auto empty = seal_record(key, nonce, {}, {});
    check(empty.size() == auth_tag_size && open_record(key, nonce, {}, empty).empty(),
          "Empty AEAD value");

    const auto secret_key = synthetic_key();
    const Bytes secret(secret_key.begin(), secret_key.end());
    const auto write = derive_control_key(secret, "Control-Salt", "Control-Write-Encryption-Key");
    const auto read = derive_control_key(secret, "Control-Salt", "Control-Read-Encryption-Key");
    check(Bytes(write.begin(), write.end()) == fixture(directory, "control-write-key.hex"),
          "Independent HKDF write key");
    check(Bytes(read.begin(), read.end()) == fixture(directory, "control-read-key.hex"),
          "Independent HKDF read key");
    check(write != read, "Directional HKDF keys differ");
    expect_control_error([&] { (void)derive_control_key(Bytes(65537), "salt", "info"); },
                         ControlError::invalid_length);
    expect_control_error([&] { (void)derive_control_key(secret, "salt", std::string(1025, 'x')); },
                         ControlError::invalid_length);
    const auto empty_key = derive_control_key({}, {}, {});
    check(Bytes(empty_key.begin(), empty_key.end()) == fixture(directory, "hkdf-empty-key.hex"),
          "Independent empty-input HKDF key");
}

void record_vectors_and_fragmentation(const std::string& directory) {
    const auto key = synthetic_key();
    const auto plaintext = synthetic_plaintext(1030);
    const auto expected = fixture(directory, "control-wire.hex");
    ControlWriter writer(key);
    check(writer.encrypt({}).empty(), "Empty write consumes no counter");
    check(writer.encrypt(plaintext) == expected, "Independent two-record wire oracle");
    // Every byte split covers partial length fields, ciphertext and authentication tags.
    for (std::size_t split = 0; split <= expected.size(); ++split) {
        ControlReader reader(key);
        auto decoded = reader.feed(slice(expected, 0, split));
        const auto remainder = reader.feed(slice(expected, split, expected.size()));
        decoded.insert(decoded.end(), remainder.begin(), remainder.end());
        check(decoded == plaintext, "Every two-part stream split");
        reader.finish();
    }
    ControlReader byte_reader(key);
    Bytes decoded;
    for (const auto byte : expected) {
        const auto fragment = byte_reader.feed({byte});
        decoded.insert(decoded.end(), fragment.begin(), fragment.end());
    }
    check(decoded == plaintext, "One-byte incremental reads");
    byte_reader.finish();
    expect_control_error([&] { (void)byte_reader.feed({}); }, ControlError::closed);

    for (const auto length : {std::size_t{1}, std::size_t{1023}, std::size_t{1024},
                              std::size_t{1025}, std::size_t{65536}}) {
        ControlWriter boundary_writer(key);
        ControlReader reader(key);
        const auto payload = synthetic_plaintext(length);
        const auto wire = boundary_writer.encrypt(payload);
        Bytes result;
        for (std::size_t offset = 0; offset < wire.size(); offset += 4096) {
            const auto part =
                reader.feed(slice(wire, offset, std::min(offset + 4096, wire.size())));
            result.insert(result.end(), part.begin(), part.end());
        }
        check(result == payload, "Record/call length boundaries");
        reader.finish();
    }
}

void record_failures(const std::string& directory) {
    const auto key = synthetic_key();
    ControlWriter writer(key);
    const auto wire = writer.encrypt({1, 2, 3, 4});
    for (std::size_t index = 2; index < wire.size(); ++index) {
        auto corrupt = wire;
        corrupt[index] ^= 1;
        ControlReader reader(key);
        expect_control_error([&] { (void)reader.feed(corrupt); }, ControlError::authentication);
        check(ControlRecordTestAccess::wiped(reader), "Failure wipes key and pending data");
        expect_control_error([&] { (void)reader.feed(wire); }, ControlError::closed);
    }
    auto corrupt_length = wire;
    corrupt_length[0] = 3;
    ControlReader length_reader(key);
    expect_control_error([&] { (void)length_reader.feed(corrupt_length); },
                         ControlError::authentication);
    ControlReader oversized(key);
    expect_control_error([&] { (void)oversized.feed({1, 4}); }, ControlError::invalid_length);
    ControlReader oversized_call(key);
    expect_control_error([&] { (void)oversized_call.feed(Bytes(65537)); },
                         ControlError::invalid_length);
    ControlWriter oversized_writer(key);
    expect_control_error([&] { (void)oversized_writer.encrypt(Bytes(65537)); },
                         ControlError::invalid_length);
    check(ControlRecordTestAccess::wiped(oversized_writer), "Failed writer wipes key");

    auto wrong_key = key;
    wrong_key[0] ^= 1;
    ControlReader wrong_key_reader(wrong_key);
    expect_control_error([&] { (void)wrong_key_reader.feed(wire); }, ControlError::authentication);
    ControlReader replay_reader(key);
    check(replay_reader.feed(wire) == Bytes({1, 2, 3, 4}), "First record accepted");
    expect_control_error([&] { (void)replay_reader.feed(wire); }, ControlError::authentication);
    const auto next_wire = writer.encrypt({5});
    ControlReader reordered(key);
    expect_control_error([&] { (void)reordered.feed(next_wire); }, ControlError::authentication);
    ControlReader skipped(key);
    ControlRecordTestAccess::counter(skipped, 1);
    expect_control_error([&] { (void)skipped.feed(wire); }, ControlError::authentication);

    // A complete authenticated first record followed by a bad second record fails
    // atomically for this call. Earlier successful feed calls are already committed.
    auto two_records = fixture(directory, "control-wire.hex");
    two_records.back() ^= 1;
    ControlReader atomic_reader(key);
    expect_control_error([&] { (void)atomic_reader.feed(two_records); },
                         ControlError::authentication);
    check(ControlRecordTestAccess::wiped(atomic_reader), "Multi-record failure is terminal");

    for (std::size_t cutoff = 1; cutoff < wire.size(); ++cutoff) {
        ControlReader truncated(key);
        check(truncated.feed(slice(wire, 0, cutoff)).empty(), "No plaintext for partial record");
        expect_control_error([&] { truncated.finish(); }, ControlError::invalid_length);
    }
    ControlWriter exhausted_writer(key);
    ControlRecordTestAccess::counter(exhausted_writer, std::numeric_limits<std::uint64_t>::max());
    expect_control_error([&] { (void)exhausted_writer.encrypt({1}); },
                         ControlError::counter_exhausted);
    ControlReader exhausted_reader(key);
    ControlRecordTestAccess::counter(exhausted_reader, std::numeric_limits<std::uint64_t>::max());
    expect_control_error([&] { (void)exhausted_reader.feed(wire); },
                         ControlError::counter_exhausted);
    // The final permitted counter works once, then cannot wrap or restart.
    ControlWriter final_writer(key);
    ControlReader final_reader(key);
    const auto final_counter = std::numeric_limits<std::uint64_t>::max() - 1;
    ControlRecordTestAccess::counter(final_writer, final_counter);
    ControlRecordTestAccess::counter(final_reader, final_counter);
    const auto final_wire = final_writer.encrypt({42});
    check(final_reader.feed(final_wire) == Bytes({42}), "Final permitted nonce");
    expect_control_error([&] { (void)final_writer.encrypt({42}); },
                         ControlError::counter_exhausted);
    expect_control_error([&] { (void)final_reader.feed(final_wire); },
                         ControlError::counter_exhausted);
    writer.close();
    expect_control_error([&] { (void)writer.encrypt({}); }, ControlError::closed);
}

void tlv_cases() {
    const auto message = decode_tlv({0, 1, 0, 6, 1, 1, 0x80, 0});
    check(message.size() == 3 && message[0].value == Bytes({0}) && message[2].value.empty(),
          "Pairing M1 + unknown empty type");
    check(encode_tlv(message) == Bytes({0, 1, 0, 6, 1, 1, 0x80, 0}), "Literal TLV known answer");
    check(decode_tlv({}).empty() && encode_tlv({}).empty(), "Empty TLV message");
    for (const auto length : {std::size_t{0}, std::size_t{1}, std::size_t{254}, std::size_t{255},
                              std::size_t{256}, std::size_t{510}, std::size_t{4096}}) {
        const std::vector<TlvField> fields{{3, synthetic_plaintext(length)}, {6, {2}}};
        const auto decoded = decode_tlv(encode_tlv(fields));
        check(decoded.size() == 2 && decoded[0].value == fields[0].value &&
                  decoded[1].value == Bytes({2}),
              "TLV fragment boundaries");
    }
    const Bytes repeated{1, 1, 42, 255, 0, 1, 1, 43};
    const auto separated = decode_tlv(repeated);
    check(separated.size() == 3 && encode_tlv(separated) == repeated,
          "Separator preserves repeated identities");
    const auto nonadjacent = decode_tlv({1, 1, 42, 6, 1, 2, 1, 1, 43});
    check(nonadjacent.size() == 3, "Nonadjacent duplicates remain distinct for schema validation");
    for (const auto& malformed :
         {Bytes{1}, Bytes{1, 2, 42}, Bytes{1, 1, 42, 1, 1, 43}, Bytes{255, 1, 0}}) {
        expect_bad_tlv([&] { (void)decode_tlv(malformed); });
    }
    expect_bad_tlv([] { (void)encode_tlv({{3, Bytes(4097)}}); });
    expect_bad_tlv([] { (void)encode_tlv({{1, {42}}, {1, {43}}}); });
    expect_bad_tlv([] { (void)encode_tlv({{255, {42}}}); });
    expect_bad_tlv([] { (void)decode_tlv(Bytes(65537)); });
    std::vector<TlvField> too_many;
    for (unsigned index = 0; index < 65; ++index) {
        too_many.push_back({static_cast<std::uint8_t>(index % 2), {}});
    }
    expect_bad_tlv([&] { (void)encode_tlv(too_many); });
    Bytes excessive_fields;
    for (const auto& field : too_many) {
        excessive_fields.insert(excessive_fields.end(), {field.type, 0});
    }
    expect_bad_tlv([&] { (void)decode_tlv(excessive_fields); });
    std::vector<TlvField> excessive_body;
    for (unsigned index = 0; index < 16; ++index) {
        excessive_body.push_back({static_cast<std::uint8_t>(index % 2), Bytes(4096)});
    }
    expect_bad_tlv([&] { (void)encode_tlv(excessive_body); });
    auto large_value = encode_tlv({{3, Bytes(4096)}});
    auto excessive_value = large_value;
    ++excessive_value[excessive_value.size() - 17];
    excessive_value.push_back(0);
    expect_bad_tlv([&] { (void)decode_tlv(excessive_value); });
    large_value[large_value.size() - 17] = 255; // Turn the final fragment into an overrun.
    expect_bad_tlv([&] { (void)decode_tlv(large_value); });

    // Deterministic malformed corpus: accepted inputs must canonicalize without data loss.
    std::mt19937 random(8439);
    const auto mutation_seed = encode_tlv({{3, synthetic_plaintext(510)}, {6, {2}}});
    for (unsigned sample = 0; sample < 3000; ++sample) {
        Bytes input(random() % 600);
        for (auto& byte : input) {
            byte = static_cast<std::uint8_t>(random());
        }
        if (sample % 2 == 0) {
            input = mutation_seed;
            input[random() % input.size()] ^= static_cast<std::uint8_t>(1 + random() % 255);
            if (sample % 5 == 0) {
                input.resize(random() % input.size());
            }
        }
        std::vector<TlvField> fields;
        try {
            fields = decode_tlv(input);
        } catch (const std::invalid_argument&) {
            continue;
        }
        // Encoder rejection after a successful decode is a test failure, not malformed input.
        const auto again = decode_tlv(encode_tlv(fields));
        check(again.size() == fields.size(), "TLV mutation canonicalization field count");
        for (std::size_t index = 0; index < std::min(fields.size(), again.size()); ++index) {
            check(again[index].type == fields[index].type &&
                      again[index].value == fields[index].value,
                  "TLV mutation data preserved");
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    static_assert(!std::is_copy_constructible_v<ControlReader> &&
                  !std::is_move_constructible_v<ControlReader>);
    static_assert(!std::is_copy_constructible_v<ControlWriter> &&
                  !std::is_move_constructible_v<ControlWriter>);
    try {
        if (argc != 2) {
            throw std::runtime_error("Expected fixture directory");
        }
        crypto_vectors(argv[1]);
        record_vectors_and_fragmentation(argv[1]);
        record_failures(argv[1]);
        tlv_cases();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected test exception: " << error.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
