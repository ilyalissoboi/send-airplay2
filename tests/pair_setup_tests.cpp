// SPDX-License-Identifier: Apache-2.0
// Public synthetic secrets only; see fixtures/README.md.
#include "pair_setup.h"
#include "credential_store.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace send_airplay2::detail;
namespace send_airplay2::detail {
struct PairSetupTestAccess {
    static void deterministic(SrpClient& client) {
        (void)botan_rng_destroy(client.rng_);
        client.rng_ = nullptr;
        if (botan_rng_init_custom(&client.rng_, "synthetic-test-only", nullptr, random_bytes,
                                  nullptr, nullptr) != 0) {
            throw std::runtime_error("Cannot initialize test RNG");
        }
    }
    static void deterministic(PairSetup& setup, const PublicKey& seed) {
        deterministic(setup.srp_);
        setup.controller_seed_.bytes = seed;
    }
    static bool wiped(const SrpClient& client) {
        return client.closed_ && client.rng_ == nullptr && zero(client.key_) &&
               zero(client.expected_proof_);
    }
    static bool wiped(const PairSetup& setup) {
        return setup.state_ == PairSetup::State::closed && wiped(setup.srp_) &&
               zero(setup.controller_seed_) && zero(setup.session_secret_) &&
               zero(setup.encryption_key_);
    }

private:
    static int random_bytes(void*, std::uint8_t* output, std::size_t length) {
        std::fill_n(output, length, std::uint8_t{0x42});
        return 0;
    }
    template <typename Secret> static bool zero(const Secret& secret) {
        return std::all_of(secret.bytes.begin(), secret.bytes.end(),
                           [](auto byte) { return byte == 0; });
    }
};
struct PairVerifyTestAccess {
    static void ephemeral(PairVerifier& verifier, const PublicKey& seed) {
        verifier.ephemeral_.bytes = seed;
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
Bytes fixture(const std::string& directory, const char* name) {
    std::ifstream input(directory + '/' + name + ".hex");
    std::string hex;
    if (!(input >> hex) || hex.size() % 2 != 0) {
        throw std::runtime_error(std::string("Bad/missing fixture: ") + name);
    }
    Bytes output;
    for (std::size_t offset = 0; offset < hex.size(); offset += 2) {
        output.push_back(static_cast<std::uint8_t>(std::stoul(hex.substr(offset, 2), nullptr, 16)));
    }
    return output;
}
template <std::size_t Size> std::array<std::uint8_t, Size> fixed(const Bytes& input) {
    if (input.size() != Size) {
        throw std::runtime_error("Wrong fixture size");
    }
    std::array<std::uint8_t, Size> output{};
    std::copy(input.begin(), input.end(), output.begin());
    return output;
}
Bytes text(std::string_view input) {
    return Bytes(input.begin(), input.end());
}
struct Exchange {
    PairSetup setup{text("synthetic-controller")};
    explicit Exchange(const std::string& directory) {
        PairSetupTestAccess::deterministic(setup, fixed<32>(fixture(directory, "client-seed")));
    }
    void advance(const std::string& directory, int phase) {
        if (phase >= 1) {
            check(setup.start() == fixture(directory, "m1"), "independent M1");
        }
        if (phase >= 2) {
            check(setup.respond("0123", 200, fixture(directory, "m2")) == fixture(directory, "m3"),
                  "independent M3/SRP proof");
        }
        if (phase >= 3) {
            check(setup.confirm(200, fixture(directory, "m4")) == fixture(directory, "m5"),
                  "independent M5/signature/ciphertext");
        }
    }
};
template <typename Owner, typename Action>
void reject(Owner& owner, const std::string& scenario, Action action, PairSetupError expected) {
    try {
        action();
        check(false, scenario + ": accepted");
    } catch (const PairSetupException& error) {
        check(error.reason() == expected, scenario + ": wrong error category");
    }
    check(PairSetupTestAccess::wiped(owner), scenario + ": terminal erasure");
}

void srp_vectors(const std::string& directory) {
    active_group = "SRP independent vectors";
    SrpClient client;
    PairSetupTestAccess::deterministic(client);
    const auto response =
        client.begin("0123", fixture(directory, "salt"), fixture(directory, "server-public"));
    check(response.public_key == fixture(directory, "client-public"), "A, fixed random exponent");
    check(response.proof == fixed<64>(fixture(directory, "proof")),
          "SHA512 client proof, minimal shared integer");
    Secret64 key;
    client.complete(fixture(directory, "server-proof"), key);
    check(key.bytes == fixed<64>(fixture(directory, "session-key")), "authenticated session key");
    check(PairSetupTestAccess::wiped(client), "successful SRP erasure");
    reject(
        client, "single-use key release",
        [&] { client.complete(fixture(directory, "server-proof"), key); },
        PairSetupError::unexpected_state);
    for (std::size_t index = 0; index < 64; ++index) {
        SrpClient corrupted;
        PairSetupTestAccess::deterministic(corrupted);
        (void)corrupted.begin("0123", fixture(directory, "salt"),
                              fixture(directory, "server-public"));
        auto proof = fixture(directory, "server-proof");
        proof[index] ^= 1;
        key.bytes.fill(0xaa);
        reject(
            corrupted, "server proof byte " + std::to_string(index),
            [&] { corrupted.complete(proof, key); }, PairSetupError::authentication);
        check(
            std::all_of(key.bytes.begin(), key.bytes.end(), [](auto byte) { return byte == 0xaa; }),
            "failure leaves caller key unchanged");
    }
    SrpClient early;
    reject(
        early, "key before agreement",
        [&] { early.complete(fixture(directory, "server-proof"), key); },
        PairSetupError::unexpected_state);
}

void credential_round_trip(const std::string& directory, const char* response_name) {
    active_group = response_name;
    Exchange exchange(directory);
    exchange.advance(directory, 3);
    auto credentials = exchange.setup.finish(200, fixture(directory, response_name));
    check(credentials != nullptr && PairSetupTestAccess::wiped(exchange.setup),
          "authenticated credential release and setup erasure");
    CredentialBlob stored;
    encode_credentials(*credentials, stored);
    credentials.reset();
    credentials = decode_credentials(stored);
    // A newly owned seed/identity must reproduce the independent verify transcript.
    const std::string verify_directory = directory + "/../pair-verify";
    PairVerifier verifier(*credentials);
    PairVerifyTestAccess::ephemeral(verifier,
                                    fixed<32>(fixture(verify_directory, "client-ephemeral")));
    check(verifier.start() == fixture(verify_directory, "m1"), "subsequent verify M1");
    check(verifier.respond(200, fixture(verify_directory, "m2")) == fixture(verify_directory, "m3"),
          "new credentials authenticate verify and sign M3");
    verifier.finish(200, fixture(verify_directory, "m4"));
    Secret32 write, read;
    verifier.take_control_keys(write, read);
    check(write.bytes == fixed<32>(fixture(verify_directory, "write-key")) &&
              read.bytes == fixed<32>(fixture(verify_directory, "read-key")),
          "verified control keys match independent oracle");
    reject(
        exchange.setup, "credentials released once",
        [&] { (void)exchange.setup.finish(200, fixture(directory, "m6")); },
        PairSetupError::unexpected_state);
}

void malformed_srp(const std::string& directory) {
    active_group = "PIN and SRP bounds";
    for (const std::string& pin :
         {std::string{}, std::string{"123"}, std::string{"123456789"}, std::string{"12a3"},
          std::string{"12\0"
                      "3",
                      4}}) {
        SrpClient client;
        reject(
            client, "invalid PIN length/content",
            [&] {
                (void)client.begin(pin, fixture(directory, "salt"),
                                   fixture(directory, "server-public"));
            },
            PairSetupError::invalid_message);
    }
    for (const std::size_t length : {15u, 17u}) {
        SrpClient client;
        reject(
            client, "salt length " + std::to_string(length),
            [&] { (void)client.begin("0123", Bytes(length), fixture(directory, "server-public")); },
            PairSetupError::invalid_message);
    }
    for (const auto& public_key : {Bytes{}, Bytes(385, 1)}) {
        SrpClient client;
        reject(
            client, "B length " + std::to_string(public_key.size()),
            [&] { (void)client.begin("0123", fixture(directory, "salt"), public_key); },
            PairSetupError::invalid_message);
    }
    auto above_modulus = fixture(directory, "modulus");
    // N ends in eight 0xff bytes: increment the preceding byte to obtain N + 2^64.
    constexpr std::size_t trailing_modulus_bytes = 8;
    ++above_modulus[above_modulus.size() - trailing_modulus_bytes - 1];
    for (const auto& public_key : {Bytes{0}, fixture(directory, "modulus"), above_modulus,
                                   fixture(directory, "zero-shared-public")}) {
        SrpClient client;
        reject(
            client, "invalid B/shared zero",
            [&] { (void)client.begin("0123", fixture(directory, "salt"), public_key); },
            PairSetupError::authentication);
    }
    Exchange wrong_pin(directory);
    wrong_pin.advance(directory, 1);
    (void)wrong_pin.setup.respond("0124", 200, fixture(directory, "m2"));
    reject(
        wrong_pin.setup, "wrong PIN cannot emit M5",
        [&] { (void)wrong_pin.setup.confirm(200, fixture(directory, "m4")); },
        PairSetupError::authentication);
}

void state_and_schema(const std::string& directory) {
    active_group = "response schema and lifecycle";
    for (int phase = 0; phase <= 3; ++phase) {
        Exchange exchange(directory);
        exchange.advance(directory, phase);
        exchange.setup.close();
        exchange.setup.close();
        check(PairSetupTestAccess::wiped(exchange.setup), "cancel phase " + std::to_string(phase));
        reject(
            exchange.setup, "reuse canceled exchange", [&] { (void)exchange.setup.start(); },
            PairSetupError::unexpected_state);
    }
    for (int phase = 1; phase <= 3; ++phase) {
        for (const bool http_error : {false, true}) {
            Exchange exchange(directory);
            exchange.advance(directory, phase);
            const auto body = encode_tlv({{6, {static_cast<std::uint8_t>(phase * 2)}}, {7, {2}}});
            reject(
                exchange.setup, "rejection phase " + std::to_string(phase),
                [&] {
                    const unsigned status = http_error ? 403 : 200;
                    if (phase == 1) {
                        (void)exchange.setup.respond("0123", status, body);
                    }
                    if (phase == 2) {
                        (void)exchange.setup.confirm(status, body);
                    }
                    if (phase == 3) {
                        (void)exchange.setup.finish(status, body);
                    }
                },
                PairSetupError::peer_rejected);
        }
    }
    for (const auto& body : {Bytes{}, Bytes(2049), Bytes{6, 2, 2}, encode_tlv({{6, {1}}}),
                             Bytes{6, 1, 2, 6, 1, 2}, encode_tlv({{6, {2}}, {100, {1}}}),
                             encode_tlv({{6, {2}}, {255, {}}}), encode_tlv({{6, {2}}, {7, {0}}})}) {
        Exchange exchange(directory);
        exchange.advance(directory, 1);
        reject(
            exchange.setup, "malformed M2",
            [&] { (void)exchange.setup.respond("0123", 200, body); },
            PairSetupError::invalid_message);
    }
    for (const std::size_t length : {0u, 63u, 65u}) {
        Exchange exchange(directory);
        exchange.advance(directory, 2);
        reject(
            exchange.setup, "M4 proof length " + std::to_string(length),
            [&] { (void)exchange.setup.confirm(200, encode_tlv({{6, {4}}, {4, Bytes(length)}})); },
            PairSetupError::invalid_message);
    }
    Exchange out_of_order(directory);
    reject(
        out_of_order.setup, "M6 before start",
        [&] { (void)out_of_order.setup.finish(200, fixture(directory, "m6")); },
        PairSetupError::unexpected_state);
    Exchange duplicate(directory);
    duplicate.advance(directory, 2);
    reject(
        duplicate.setup, "replayed M2",
        [&] { (void)duplicate.setup.respond("0123", 200, fixture(directory, "m2")); },
        PairSetupError::unexpected_state);
}

void accessory_authentication(const std::string& directory) {
    active_group = "accessory identity authentication";
    for (const char* name : {"bad-signature-m6", "bad-signature-metadata-m6", "wrong-id-m6",
                             "wrong-key-m6", "weak-identity-m6"}) {
        Exchange exchange(directory);
        exchange.advance(directory, 3);
        reject(
            exchange.setup, name,
            [&] { (void)exchange.setup.finish(200, fixture(directory, name)); },
            PairSetupError::authentication);
    }
    for (const char* name :
         {"duplicate-id-m6", "missing-id-m6", "empty-id-m6", "long-id-m6", "short-key-m6",
          "short-signature-m6", "unknown-inner-m6", "malformed-inner-m6", "oversized-metadata-m6",
          "duplicate-metadata-m6"}) {
        Exchange exchange(directory);
        exchange.advance(directory, 3);
        reject(
            exchange.setup, name,
            [&] { (void)exchange.setup.finish(200, fixture(directory, name)); },
            PairSetupError::invalid_message);
    }
    const auto fields = decode_tlv(fixture(directory, "m6"));
    const auto encrypted = fields.at(1).value;
    for (std::size_t index = 0; index < encrypted.size(); ++index) {
        Exchange exchange(directory);
        exchange.advance(directory, 3);
        auto changed = encrypted;
        changed[index] ^= 1;
        reject(
            exchange.setup, "ciphertext byte " + std::to_string(index),
            [&] { (void)exchange.setup.finish(200, encode_tlv({{6, {6}}, {5, changed}})); },
            PairSetupError::authentication);
    }
    for (const std::size_t length : {0u, 15u, 513u}) {
        Exchange exchange(directory);
        exchange.advance(directory, 3);
        reject(
            exchange.setup, "encrypted length " + std::to_string(length),
            [&] { (void)exchange.setup.finish(200, encode_tlv({{6, {6}}, {5, Bytes(length)}})); },
            PairSetupError::invalid_message);
    }
}

void weak_identity_keys() {
    active_group = "Ed25519 identity strength";
    // RFC 8032's field is p = 2^255 - 19. These are public numeric encodings,
    // including aliases of the identity and a canonical point of order two.
    PublicKey field_prime;
    field_prime.fill(0xff);
    field_prime.front() = 0xed;
    field_prime.back() = 0x7f;
    auto identity_alias = field_prime;
    identity_alias.front() = 0xee; // y = p + 1.
    auto order_two = field_prime;
    order_two.front() = 0xec; // y = p - 1.
    PublicKey signed_identity{1};
    signed_identity.back() = 0x80;
    const Signature trivial_forgery{1}; // R = identity, S = zero; no private seed.
    std::size_t index = 0;
    for (const auto& key :
         {PublicKey{}, PublicKey{1}, signed_identity, field_prime, identity_alias, order_two}) {
        check(!ed25519_verify(key, text("synthetic-transcript"), trivial_forgery),
              "weak/noncanonical key " + std::to_string(index++));
    }
}

void response_diagnostics(const std::string& directory) {
    active_group = "sanitized response diagnostics";
    const auto metadata_only = [](const PairSetupException& error, PairSetupPhase phase,
                                  const char* expected) {
        check(error.phase() == phase, "correlated response phase");
        check(std::string(error.what()) == expected, "literal bounded diagnostic");
    };
    Exchange m2(directory);
    m2.advance(directory, 1);
    const auto private_marker = text("private-payload-0123");
    try {
        (void)m2.setup.respond("0123", 200, encode_tlv({{6, {2}}, {100, private_marker}}));
        check(false, "M2 unknown field accepted");
    } catch (const PairSetupException& error) {
        metadata_only(error, PairSetupPhase::m2,
                      "Invalid pair-setup message [phase=M2 HTTP=200 body_bytes=25 "
                      "TLV=[6:1(state=2),100:20]]");
        check(std::string(error.what()).find("private-payload") == std::string::npos &&
                  std::string(error.what()).find("0123") == std::string::npos,
              "payload/PIN marker never appears in diagnostics");
    }
    check(PairSetupTestAccess::wiped(m2.setup), "M2 diagnostics preserve terminal cleanup");
    Exchange m4(directory);
    m4.advance(directory, 2);
    try {
        (void)m4.setup.confirm(200, encode_tlv({{6, {4}}}));
        check(false, "missing M4 proof accepted");
    } catch (const PairSetupException& error) {
        metadata_only(
            error, PairSetupPhase::m4,
            "Invalid pair-setup message [phase=M4 HTTP=200 body_bytes=3 TLV=[6:1(state=4)]]");
    }
    check(PairSetupTestAccess::wiped(m4.setup), "M4 diagnostics preserve terminal cleanup");
    Exchange m6(directory);
    m6.advance(directory, 3);
    try {
        (void)m6.setup.finish(403, encode_tlv({{6, {6}}, {7, {2}}}));
        check(false, "M6 peer rejection accepted");
    } catch (const PairSetupException& error) {
        metadata_only(error, PairSetupPhase::m6,
                      "Peer rejected pair setup [phase=M6 HTTP=403 body_bytes=6 "
                      "TLV=[6:1(state=6),7:1(error=2)]]");
    }
    check(PairSetupTestAccess::wiped(m6.setup), "M6 diagnostics preserve terminal cleanup");
    Exchange inner(directory);
    inner.advance(directory, 3);
    try {
        (void)inner.setup.finish(200, fixture(directory, "unknown-inner-m6"));
        check(false, "unknown M6 identity field accepted");
    } catch (const PairSetupException& error) {
        check(error.phase() == PairSetupPhase::m6 &&
                  std::string(error.what()).find("source=identity") != std::string::npos &&
                  std::string(error.what()).find("100:1") != std::string::npos,
              "M6 identity-schema context is retained through outer cleanup");
        check(std::string(error.what()).find("synthetic-receiver") == std::string::npos,
              "decrypted receiver identity is never logged");
    }
    check(PairSetupTestAccess::wiped(inner.setup), "inner diagnostics preserve terminal cleanup");
    const PairSetupException identity(PairSetupError::invalid_message, PairSetupPhase::m6, 200,
                                      Bytes{6, 1, 6}, PairSetupRegion::identity);
    check(std::string(identity.what()).find("state=") == std::string::npos,
          "inner payload values are never interpreted as outer protocol codes");
    const PairSetupException header(PairSetupError::invalid_message, PairSetupPhase::m2, 200,
                                    Bytes{6});
    check(std::string(header.what()).find("truncated-header") != std::string::npos,
          "truncated header metadata is bounded");
    const PairSetupException value(PairSetupError::invalid_message, PairSetupPhase::m2, 200,
                                   Bytes{6, 2, 2});
    check(std::string(value.what()).find("truncated-value") != std::string::npos,
          "truncated value metadata is bounded");
    const PairSetupException oversized(PairSetupError::invalid_message, PairSetupPhase::m2, 200,
                                       Bytes(2049, 'X'));
    check(std::string(oversized.what()).find("TLV=over-limit") != std::string::npos,
          "oversized bodies are not inspected");
    Bytes empty_fields;
    for (unsigned index = 0; index < 65; ++index) {
        empty_fields.insert(empty_fields.end(), {100, 0});
    }
    const PairSetupException many(PairSetupError::invalid_message, PairSetupPhase::m2, 200,
                                  empty_fields);
    check(std::string(many.what()).find("summary-limit") != std::string::npos,
          "diagnostic fragment count is bounded independently of acceptance");
}
} // namespace

int main(int argc, char** argv) {
    static_assert(!std::is_copy_constructible_v<Secret64> &&
                  !std::is_move_constructible_v<Secret64>);
    static_assert(!std::is_copy_constructible_v<SrpClient> &&
                  !std::is_move_constructible_v<SrpClient>);
    static_assert(!std::is_copy_constructible_v<PairSetup> &&
                  !std::is_move_constructible_v<PairSetup>);
    try {
        if (argc != 2) {
            throw std::runtime_error("Expected fixture directory");
        }
        const std::string directory = argv[1];
        weak_identity_keys();
        response_diagnostics(directory);
        srp_vectors(directory);
        for (const auto* response_name :
             {"m6", "empty-metadata-m6", "metadata-m6", "max-metadata-m6"}) {
            credential_round_trip(directory, response_name);
        }
        malformed_srp(directory);
        state_and_schema(directory);
        accessory_authentication(directory);
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << active_group << "]: " << error.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
