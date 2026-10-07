// SPDX-License-Identifier: Apache-2.0
// All seeds/identifiers are public synthetic data. See fixtures/README.md.
#include "control_records.h"
#include "pair_verify.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace send_airplay2::detail;
namespace send_airplay2::detail {
// Deterministic injection and erasure inspection stay out of the public/private API.
struct PairVerifyTestAccess {
    static void ephemeral(PairVerifier& verifier, const ControlKey& seed) {
        verifier.ephemeral_.bytes = seed;
    }
    static bool wiped(const PairVerifier& verifier) {
        return verifier.state_ == PairVerifier::State::closed && zero(verifier.ephemeral_) &&
               zero(verifier.shared_) && zero(verifier.session_key_) && zero(verifier.write_key_) &&
               zero(verifier.read_key_);
    }
    /// After M4 the ephemeral seed and handshake key are gone; the shared secret
    /// is retained until release so that take_session_keys can hand it over.
    static bool handshake_wiped(const PairVerifier& verifier) {
        return zero(verifier.ephemeral_) && zero(verifier.session_key_);
    }
    static bool holds_shared_secret(const PairVerifier& verifier) {
        return !zero(verifier.shared_);
    }

private:
    static bool zero(const Secret32& secret) {
        return std::all_of(secret.bytes.begin(), secret.bytes.end(),
                           [](auto byte) { return byte == 0; });
    }
};
} // namespace send_airplay2::detail

namespace {
int failures = 0;
const char* active_group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << active_group << "]: " << scenario << '\n';
        ++failures;
    }
}
Bytes unhex(std::string_view hex) {
    if (hex.size() % 2 != 0) {
        throw std::runtime_error("Odd fixture length");
    }
    Bytes output;
    for (std::size_t offset = 0; offset < hex.size(); offset += 2) {
        output.push_back(
            static_cast<std::uint8_t>(std::stoul(std::string(hex.substr(offset, 2)), nullptr, 16)));
    }
    return output;
}
Bytes fixture(const std::string& directory, const char* name) {
    std::ifstream input(directory + '/' + name + ".hex");
    std::string hex;
    if (!(input >> hex)) {
        throw std::runtime_error(std::string("Missing pair-verify fixture: ") + name);
    }
    return unhex(hex);
}
template <std::size_t Size> std::array<std::uint8_t, Size> fixed(const Bytes& bytes) {
    if (bytes.size() != Size) {
        throw std::runtime_error("Wrong fixed fixture length");
    }
    std::array<std::uint8_t, Size> output{};
    std::copy(bytes.begin(), bytes.end(), output.begin());
    return output;
}
Bytes text(std::string_view value) {
    return Bytes(value.begin(), value.end());
}

struct Exchange {
    Secret32 client_seed;
    PairCredentials credentials;
    PairVerifier verifier;
    explicit Exchange(const std::string& directory)
        : client_seed(fixed<32>(fixture(directory, "client-seed"))),
          credentials(text("synthetic-receiver"), fixed<32>(fixture(directory, "receiver-key")),
                      text("synthetic-controller"), client_seed),
          verifier(credentials) {
        PairVerifyTestAccess::ephemeral(verifier,
                                        fixed<32>(fixture(directory, "client-ephemeral")));
    }
    void start(const std::string& directory) {
        check(verifier.start() == fixture(directory, "m1"), "independent M1 bytes");
    }
    void respond(const std::string& directory) {
        start(directory);
        check(verifier.respond(200, fixture(directory, "m2")) == fixture(directory, "m3"),
              "independent M3 bytes/signature");
    }
};

template <typename Function>
void expect_error(PairVerifier& verifier, const std::string& scenario, Function action,
                  PairVerifyError reason) {
    try {
        action();
        check(false, scenario + ": expected rejection");
    } catch (const PairVerifyException& error) {
        check(error.reason() == reason, scenario + ": wrong error category " +
                                            std::to_string(static_cast<int>(error.reason())));
    }
    check(PairVerifyTestAccess::wiped(verifier),
          scenario + ": terminal state and wiped owned keys");
}
template <typename Function>
void expect_any_rejection(PairVerifier& verifier, const std::string& scenario, Function action) {
    try {
        action();
        check(false, scenario + ": unexpectedly accepted");
    } catch (const PairVerifyException&) {
    }
    check(PairVerifyTestAccess::wiped(verifier), scenario + ": wiped owned keys");
}

void x25519_tests() {
    active_group = "RFC 7748 X25519";
    // RFC 7748 section 6.1: independent Alice/Bob public and shared key bytes.
    Secret32 alice(
        fixed<32>(unhex("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a")));
    const auto alice_public =
        fixed<32>(unhex("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"));
    const auto bob_public =
        fixed<32>(unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"));
    check(x25519_public(alice) == alice_public, "Alice public vector");
    Secret32 shared;
    x25519_shared(alice, bob_public, shared);
    check(shared.bytes ==
              fixed<32>(unhex("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742")),
          "shared vector");
    for (const auto peer : {PublicKey{}, PublicKey{1}}) {
        shared.bytes.fill(0xaa);
        try {
            x25519_shared(alice, peer, shared);
            check(false, "low-order peer rejected");
        } catch (const ControlException& error) {
            check(error.reason() == ControlError::authentication, "low-order category");
        }
        check(shared.bytes == ControlKey{}, "failed shared output wiped");
    }
    const auto original = alice.bytes;
    try {
        x25519_shared(alice, bob_public, alice);
        check(false, "aliased output rejected");
    } catch (const ControlException& error) {
        check(error.reason() == ControlError::invalid_length, "aliased output category");
    }
    check(alice.bytes == original, "alias rejection preserves source");
}
void ed25519_tests() {
    active_group = "RFC 8032 Ed25519";
    // RFC 8032 section 7.1 test 1: pure Ed25519 on the empty message.
    Secret32 seed(
        fixed<32>(unhex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60")));
    const auto key =
        fixed<32>(unhex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a"));
    auto signature =
        fixed<64>(unhex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
                        "5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"));
    check(ed25519_public(seed) == key, "public vector");
    check(ed25519_sign(seed, {}) == signature, "signature vector");
    check(ed25519_verify(key, {}, signature), "valid vector");
    check(!ed25519_verify(key, {1}, signature), "changed message rejected");
    signature[63] |= 0x80;
    check(!ed25519_verify(key, {}, signature), "noncanonical signature rejected");
    const Bytes boundary(65536, 0x42);
    check(ed25519_verify(key, boundary, ed25519_sign(seed, boundary)), "64 KiB signing boundary");
    for (const bool signing : {true, false}) {
        try {
            if (signing) {
                (void)ed25519_sign(seed, Bytes(65537));
            } else {
                (void)ed25519_verify(key, Bytes(65537), signature);
            }
            check(false, "oversize signed message rejected");
        } catch (const ControlException& error) {
            check(error.reason() == ControlError::invalid_length, "signed message limit category");
        }
    }
    seed.clear();
    check(seed.bytes == ControlKey{}, "explicit seed erasure");
}
void transcript_tests(const std::string& directory) {
    active_group = "independent transcript and record handoff";
    Exchange exchange(directory);
    exchange.respond(directory);
    exchange.verifier.finish(200, fixture(directory, "m4"));
    check(PairVerifyTestAccess::handshake_wiped(exchange.verifier),
          "transient handshake keys wiped after M4");
    check(PairVerifyTestAccess::holds_shared_secret(exchange.verifier),
          "shared secret retained until key release");
    Secret32 write, read;
    exchange.verifier.take_control_keys(write, read);
    check(write.bytes == fixed<32>(fixture(directory, "write-key")), "sender write key oracle");
    check(read.bytes == fixed<32>(fixture(directory, "read-key")), "sender read key oracle");
    check(write.bytes != read.bytes, "directional keys differ");
    check(PairVerifyTestAccess::wiped(exchange.verifier), "release erases exchange");
    ControlWriter writer(write.bytes);
    check(writer.encrypt(text("synthetic-write")) == fixture(directory, "write-wire"),
          "sender encrypted record oracle");
    const auto inbound = fixture(directory, "read-wire");
    for (std::size_t split = 0; split <= inbound.size(); ++split) {
        ControlReader reader(read.bytes);
        auto plaintext = reader.feed(
            Bytes(inbound.begin(), inbound.begin() + static_cast<std::ptrdiff_t>(split)));
        const auto tail =
            reader.feed(Bytes(inbound.begin() + static_cast<std::ptrdiff_t>(split), inbound.end()));
        plaintext.insert(plaintext.end(), tail.begin(), tail.end());
        reader.finish();
        check(plaintext == text("synthetic-read"),
              "receiver record split " + std::to_string(split));
    }
    const auto saved_write = write.bytes;
    const auto saved_read = read.bytes;
    expect_error(
        exchange.verifier, "second key release",
        [&] { exchange.verifier.take_control_keys(write, read); },
        PairVerifyError::unexpected_state);
    check(write.bytes == saved_write && read.bytes == saved_read,
          "release failure preserves caller output");
}
template <typename Function> void expect_logic_error(const std::string& scenario, Function action) {
    try {
        action();
        check(false, scenario + ": expected std::logic_error");
    } catch (const std::logic_error&) {
    }
}
/// Sentinel key bytes for checking that failed releases leave outputs alone.
ControlKey filled_key(std::uint8_t value) {
    ControlKey key{};
    key.fill(value);
    return key;
}

void channel_label_tests() {
    active_group = "channel key labels";
    // Literal receiver-defined names; the event infos are reversed for the sender.
    const auto control = control_channel_labels();
    check(control.salt == "Control-Salt" &&
              control.sender_write_info == "Control-Write-Encryption-Key" &&
              control.sender_read_info == "Control-Read-Encryption-Key",
          "control labels");
    const auto events = event_channel_labels();
    check(events.salt == "Events-Salt" &&
              events.sender_write_info == "Events-Read-Encryption-Key" &&
              events.sender_read_info == "Events-Write-Encryption-Key",
          "event labels are reversed from the sender's view");
    const auto stream = data_stream_labels(0x0123456789ABCDEF);
    check(stream.salt == "DataStream-Salt81985529216486895" &&
              stream.sender_write_info == "DataStream-Output-Encryption-Key" &&
              stream.sender_read_info == "DataStream-Input-Encryption-Key",
          "data stream labels");
    check(data_stream_labels(0).salt == "DataStream-Salt0", "zero seed in decimal");
    check(data_stream_labels(UINT64_MAX).salt == "DataStream-Salt18446744073709551615",
          "seed is unsigned decimal");
}

void session_channel_key_tests(const std::string& directory) {
    active_group = "session channel keys";
    Exchange exchange(directory);
    exchange.respond(directory);
    exchange.verifier.finish(200, fixture(directory, "m4"));
    Secret32 write, read;
    ChannelKeySource channels;
    exchange.verifier.take_session_keys(write, read, channels);
    check(write.bytes == fixed<32>(fixture(directory, "write-key")) &&
              read.bytes == fixed<32>(fixture(directory, "read-key")),
          "session release yields the same control keys");
    check(channels.available() && PairVerifyTestAccess::wiped(exchange.verifier),
          "secret moved to the session; exchange wiped");

    const std::pair<const char*, ChannelKeyLabels> channels_under_test[] = {
        {"events", event_channel_labels()},
        {"datastream", data_stream_labels(0x0123456789ABCDEF)},
    };
    for (const auto& [name, labels] : channels_under_test) {
        Secret32 sender_write, sender_read;
        channels.derive(labels, sender_write, sender_read);
        const std::string prefix(name);
        check(sender_write.bytes == fixed<32>(fixture(directory, (prefix + "-write-key").c_str())),
              prefix + " sender write key oracle");
        check(sender_read.bytes == fixed<32>(fixture(directory, (prefix + "-read-key").c_str())),
              prefix + " sender read key oracle");
        check(sender_write.bytes != write.bytes, prefix + " keys differ from control keys");
    }

    Secret32 same;
    expect_logic_error("aliased channel outputs",
                       [&] { channels.derive(event_channel_labels(), same, same); });
    check(channels.available(), "rejected derivation keeps the secret");
    channels.clear();
    check(!channels.available(), "clear releases the secret");
    Secret32 untouched_write(filled_key(0x11));
    Secret32 untouched_read(filled_key(0x22));
    expect_logic_error("derive after clear", [&] {
        channels.derive(event_channel_labels(), untouched_write, untouched_read);
    });
    check(untouched_write.bytes == filled_key(0x11) && untouched_read.bytes == filled_key(0x22),
          "failed derivation leaves outputs unchanged");
}

void session_release_error_tests(const std::string& directory) {
    active_group = "session key release errors";
    // Fill a destination from one exchange, then refuse to overwrite it.
    ChannelKeySource occupied;
    {
        Exchange first(directory);
        first.respond(directory);
        first.verifier.finish(200, fixture(directory, "m4"));
        Secret32 write, read;
        first.verifier.take_session_keys(write, read, occupied);
    }
    Exchange second(directory);
    second.respond(directory);
    second.verifier.finish(200, fixture(directory, "m4"));
    Secret32 write(filled_key(0x33));
    Secret32 read(filled_key(0x44));
    expect_error(
        second.verifier, "occupied channel key source",
        [&] { second.verifier.take_session_keys(write, read, occupied); },
        PairVerifyError::unexpected_state);
    check(write.bytes == filled_key(0x33) && read.bytes == filled_key(0x44),
          "failed release leaves outputs unchanged");
    Secret32 events_write, events_read;
    occupied.derive(event_channel_labels(), events_write, events_read);
    check(events_write.bytes == fixed<32>(fixture(directory, "events-write-key")),
          "failed release leaves the existing source intact");

    Exchange aliased(directory);
    aliased.respond(directory);
    aliased.verifier.finish(200, fixture(directory, "m4"));
    ChannelKeySource fresh;
    Secret32 output;
    expect_error(
        aliased.verifier, "aliased session outputs",
        [&] { aliased.verifier.take_session_keys(output, output, fresh); },
        PairVerifyError::unexpected_state);
    check(!fresh.available(), "aliased release moves no secret");
}

void credential_tests() {
    active_group = "credential bounds";
    Secret32 seed;
    for (const auto length : {std::size_t{1}, std::size_t{64}}) {
        PairCredentials credentials(Bytes(length, 0), PublicKey{}, Bytes(length, 0), seed);
        PairVerifier verifier(credentials);
        check(!verifier.start().empty(),
              "opaque identifiers accepted at length " + std::to_string(length));
    }
    for (const auto length : {std::size_t{0}, std::size_t{65}}) {
        for (const bool receiver : {true, false}) {
            try {
                PairCredentials credentials(receiver ? Bytes(length) : Bytes{1}, PublicKey{},
                                            receiver ? Bytes{1} : Bytes(length), seed);
                check(false, "identifier length rejected " + std::to_string(length));
            } catch (const PairVerifyException& error) {
                check(error.reason() == PairVerifyError::invalid_message,
                      "credential bounds category");
            }
        }
    }
}
void authentication_tests(const std::string& directory) {
    active_group = "peer authentication";
    for (const auto name : {"bad-signature-m2", "wrong-id-m2", "wrong-transcript-m2"}) {
        Exchange exchange(directory);
        exchange.start(directory);
        expect_error(
            exchange.verifier, name,
            [&] { (void)exchange.verifier.respond(200, fixture(directory, name)); },
            PairVerifyError::authentication);
    }
    for (const auto name : {"duplicate-id-m2", "missing-id-m2", "short-signature-m2",
                            "malformed-inner-m2", "unknown-inner-m2"}) {
        Exchange exchange(directory);
        exchange.start(directory);
        expect_error(
            exchange.verifier, name,
            [&] { (void)exchange.verifier.respond(200, fixture(directory, name)); },
            PairVerifyError::invalid_message);
    }
    Exchange replay(directory);
    // A new ephemeral transcript cannot accept an old connection's encrypted M2.
    auto different_seed = fixed<32>(fixture(directory, "client-ephemeral"));
    different_seed[10] ^= 1;
    PairVerifyTestAccess::ephemeral(replay.verifier, different_seed);
    (void)replay.verifier.start();
    expect_error(
        replay.verifier, "M2 replay across ephemeral keys",
        [&] { (void)replay.verifier.respond(200, fixture(directory, "m2")); },
        PairVerifyError::authentication);
    Secret32 seed(fixed<32>(fixture(directory, "client-seed")));
    auto wrong_key = fixed<32>(fixture(directory, "receiver-key"));
    wrong_key[0] ^= 1;
    PairCredentials credentials(text("synthetic-receiver"), wrong_key, text("synthetic-controller"),
                                seed);
    PairVerifier wrong_peer(credentials);
    PairVerifyTestAccess::ephemeral(wrong_peer, fixed<32>(fixture(directory, "client-ephemeral")));
    (void)wrong_peer.start();
    expect_error(
        wrong_peer, "wrong pinned receiver key",
        [&] { (void)wrong_peer.respond(200, fixture(directory, "m2")); },
        PairVerifyError::authentication);
}
void schema_tests(const std::string& directory) {
    active_group = "M2 envelope and bounds";
    const auto valid = decode_tlv(fixture(directory, "m2"));
    const std::vector<std::pair<const char*, Bytes>> cases{
        {"empty body", {}},
        {"truncated header", {6}},
        {"wrong state", {6, 1, 4}},
        {"empty state", {6, 0}},
        {"multibyte state", {6, 2, 2, 2}},
        {"missing public/encrypted fields", {6, 1, 2}},
        {"unknown field", {6, 1, 2, 100, 1, 1}},
        {"separator", {6, 1, 2, 255, 0}},
        {"oversize body", Bytes(1025)}};
    for (const auto& test : cases) {
        Exchange exchange(directory);
        exchange.start(directory);
        expect_error(
            exchange.verifier, test.first,
            [&] { (void)exchange.verifier.respond(200, test.second); },
            PairVerifyError::invalid_message);
    }
    for (const auto type : {std::uint8_t{3}, std::uint8_t{5}, std::uint8_t{6}}) {
        const auto field = *std::find_if(valid.begin(), valid.end(),
                                         [type](const auto& value) { return value.type == type; });
        auto duplicate_wire = fixture(directory, "m2");
        const auto duplicate_field = encode_tlv({field});
        duplicate_wire.insert(duplicate_wire.end(), duplicate_field.begin(), duplicate_field.end());
        Exchange exchange(directory);
        exchange.start(directory);
        expect_error(
            exchange.verifier, "duplicate outer tag " + std::to_string(type),
            [&] { (void)exchange.verifier.respond(200, duplicate_wire); },
            PairVerifyError::invalid_message);
    }
    for (const auto length : {std::size_t{0}, std::size_t{31}, std::size_t{33}}) {
        auto fields = valid;
        fields[1].value.resize(length);
        Exchange exchange(directory);
        exchange.start(directory);
        expect_error(
            exchange.verifier, "public key length " + std::to_string(length),
            [&] { (void)exchange.verifier.respond(200, encode_tlv(fields)); },
            PairVerifyError::invalid_message);
    }
    for (const auto length : {std::size_t{15}, std::size_t{513}}) {
        auto fields = valid;
        fields[2].value.resize(length);
        Exchange exchange(directory);
        exchange.start(directory);
        expect_error(
            exchange.verifier, "encrypted value length " + std::to_string(length),
            [&] { (void)exchange.verifier.respond(200, encode_tlv(fields)); },
            PairVerifyError::invalid_message);
    }
    // Inclusive encrypted-data bounds pass the schema, then fail authentication.
    for (const auto length : {std::size_t{16}, std::size_t{512}}) {
        auto fields = valid;
        fields[2].value.assign(length, 0);
        Exchange boundary(directory);
        boundary.start(directory);
        expect_error(
            boundary.verifier, "encrypted boundary length " + std::to_string(length),
            [&] { (void)boundary.verifier.respond(200, encode_tlv(fields)); },
            PairVerifyError::authentication);
    }
    auto low_order = valid;
    low_order[1].value.assign(32, 0);
    Exchange exchange(directory);
    exchange.start(directory);
    expect_error(
        exchange.verifier, "low-order exchange peer",
        [&] { (void)exchange.verifier.respond(200, encode_tlv(low_order)); },
        PairVerifyError::authentication);
}
void final_response_tests(const std::string& directory) {
    active_group = "M4 acknowledgement/revocation";
    const std::vector<std::pair<const char*, Bytes>> invalid{
        {"empty M4", {}},
        {"old M2 state", {6, 1, 2}},
        {"missing M4 state", {7, 1, 2}},
        {"duplicate M4 state", {6, 1, 4, 6, 1, 4}},
        {"unknown M4 field", {6, 1, 4, 100, 0}},
        {"zero error", {6, 1, 4, 7, 1, 0}},
        {"multibyte error", {6, 1, 4, 7, 2, 2, 2}}};
    for (const auto& test : invalid) {
        Exchange exchange(directory);
        exchange.respond(directory);
        expect_error(
            exchange.verifier, test.first, [&] { exchange.verifier.finish(200, test.second); },
            PairVerifyError::invalid_message);
    }
    for (const auto state : {std::uint8_t{2}, std::uint8_t{4}}) {
        Exchange exchange(directory);
        exchange.start(directory);
        if (state == 4) {
            (void)exchange.verifier.respond(200, fixture(directory, "m2"));
        }
        const Bytes rejected{6, 1, state, 7, 1, 2};
        expect_error(
            exchange.verifier, "peer authentication error at M" + std::to_string(state),
            [&] {
                if (state == 2) {
                    (void)exchange.verifier.respond(200, rejected);
                } else {
                    exchange.verifier.finish(200, rejected);
                }
            },
            PairVerifyError::peer_rejected);
    }
    for (const auto status : {204U, 401U, 403U, 500U}) {
        Exchange m2(directory);
        m2.start(directory);
        expect_error(
            m2.verifier, "M2 HTTP " + std::to_string(status),
            [&] { (void)m2.verifier.respond(status, fixture(directory, "m2")); },
            PairVerifyError::peer_rejected);
        Exchange m4(directory);
        m4.respond(directory);
        expect_error(
            m4.verifier, "M4 HTTP " + std::to_string(status),
            [&] { m4.verifier.finish(status, fixture(directory, "m4")); },
            PairVerifyError::peer_rejected);
    }
}
void lifecycle_tests(const std::string& directory) {
    active_group = "state/key release lifecycle";
    for (int phase = 0; phase < 3; ++phase) {
        Exchange exchange(directory);
        if (phase >= 1) {
            exchange.start(directory);
        }
        if (phase >= 2) {
            (void)exchange.verifier.respond(200, fixture(directory, "m2"));
        }
        Secret32 write, read;
        write.bytes.fill(0xa5);
        read.bytes.fill(0x5a);
        expect_error(
            exchange.verifier, "early key release phase " + std::to_string(phase),
            [&] { exchange.verifier.take_control_keys(write, read); },
            PairVerifyError::unexpected_state);
        check(std::all_of(write.bytes.begin(), write.bytes.end(),
                          [](auto byte) { return byte == 0xa5; }),
              "early release preserves write output");
        check(std::all_of(read.bytes.begin(), read.bytes.end(),
                          [](auto byte) { return byte == 0x5a; }),
              "early release preserves read output");
    }
    Exchange repeated(directory);
    repeated.start(directory);
    expect_error(
        repeated.verifier, "repeat M1", [&] { (void)repeated.verifier.start(); },
        PairVerifyError::unexpected_state);
    Exchange replay(directory);
    replay.respond(directory);
    expect_error(
        replay.verifier, "repeat M2",
        [&] { (void)replay.verifier.respond(200, fixture(directory, "m2")); },
        PairVerifyError::unexpected_state);
    Exchange premature(directory);
    expect_error(
        premature.verifier, "M4 before M1",
        [&] { premature.verifier.finish(200, fixture(directory, "m4")); },
        PairVerifyError::unexpected_state);
    Exchange alias(directory);
    alias.respond(directory);
    alias.verifier.finish(200, fixture(directory, "m4"));
    Secret32 output;
    expect_error(
        alias.verifier, "aliased key outputs",
        [&] { alias.verifier.take_control_keys(output, output); },
        PairVerifyError::unexpected_state);
    for (int phase = 0; phase < 4; ++phase) {
        Exchange exchange(directory);
        if (phase >= 1) {
            exchange.start(directory);
        }
        if (phase >= 2) {
            (void)exchange.verifier.respond(200, fixture(directory, "m2"));
        }
        if (phase >= 3) {
            exchange.verifier.finish(200, fixture(directory, "m4"));
        }
        exchange.verifier.close();
        exchange.verifier.close();
        check(PairVerifyTestAccess::wiped(exchange.verifier),
              "idempotent cancellation at phase " + std::to_string(phase));
        expect_error(
            exchange.verifier, "restart closed phase " + std::to_string(phase),
            [&] { (void)exchange.verifier.start(); }, PairVerifyError::unexpected_state);
    }
    Secret32 seed(fixed<32>(fixture(directory, "client-seed")));
    PairCredentials credentials(text("synthetic-receiver"),
                                fixed<32>(fixture(directory, "receiver-key")),
                                text("synthetic-controller"), seed);
    PairVerifier first(credentials), second(credentials);
    check(first.start() != second.start(), "fresh connection public keys differ");
}
void mutation_tests(const std::string& directory) {
    active_group = "truncated/altered M2 and deterministic mutations";
    const auto valid = fixture(directory, "m2");
    for (std::size_t index = 0; index < valid.size(); ++index) {
        Exchange truncated(directory);
        truncated.start(directory);
        expect_any_rejection(truncated.verifier, "M2 EOF cutoff " + std::to_string(index), [&] {
            (void)truncated.verifier.respond(
                200, Bytes(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(index)));
        });
        Exchange altered(directory);
        altered.start(directory);
        auto wire = valid;
        wire[index] ^= 1;
        expect_any_rejection(altered.verifier, "M2 altered byte " + std::to_string(index),
                             [&] { (void)altered.verifier.respond(200, wire); });
    }
    std::mt19937 random(1776);
    for (int sample = 0; sample < 1000; ++sample) {
        Bytes wire(random() % 513);
        for (auto& byte : wire) {
            byte = static_cast<std::uint8_t>(random());
        }
        Exchange exchange(directory);
        exchange.start(directory);
        expect_any_rejection(exchange.verifier, "mutation sample " + std::to_string(sample),
                             [&] { (void)exchange.verifier.respond(200, wire); });
    }
}
} // namespace

int main(int argc, char** argv) {
    static_assert(!std::is_copy_constructible_v<Secret32> &&
                  !std::is_move_constructible_v<Secret32>);
    static_assert(!std::is_copy_constructible_v<PairCredentials> &&
                  !std::is_move_constructible_v<PairCredentials>);
    static_assert(!std::is_copy_constructible_v<PairVerifier> &&
                  !std::is_move_constructible_v<PairVerifier>);
    try {
        if (argc != 2) {
            throw std::runtime_error("Expected pair-verify fixture directory");
        }
        const std::string directory = argv[1];
        x25519_tests();
        ed25519_tests();
        transcript_tests(directory);
        channel_label_tests();
        session_channel_key_tests(directory);
        session_release_error_tests(directory);
        credential_tests();
        authentication_tests(directory);
        schema_tests(directory);
        final_response_tests(directory);
        lifecycle_tests(directory);
        mutation_tests(directory);
    } catch (const std::exception& error) {
        std::cerr << "Unexpected test exception [" << active_group << "]: " << error.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
