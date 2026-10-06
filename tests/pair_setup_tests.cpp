// SPDX-License-Identifier: Apache-2.0
// Public synthetic secrets only; see fixtures/README.md.
#include "pair_setup.h"
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

void credential_round_trip(const std::string& directory) {
    active_group = "PIN enrollment to peer verification";
    Exchange exchange(directory);
    exchange.advance(directory, 3);
    auto credentials = exchange.setup.finish(200, fixture(directory, "m6"));
    check(credentials != nullptr && PairSetupTestAccess::wiped(exchange.setup),
          "authenticated credential release and setup erasure");
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
    for (const char* name : {"bad-signature-m6", "wrong-id-m6", "wrong-key-m6"}) {
        Exchange exchange(directory);
        exchange.advance(directory, 3);
        reject(
            exchange.setup, name,
            [&] { (void)exchange.setup.finish(200, fixture(directory, name)); },
            PairSetupError::authentication);
    }
    for (const char* name :
         {"duplicate-id-m6", "missing-id-m6", "empty-id-m6", "long-id-m6", "short-key-m6",
          "short-signature-m6", "unknown-inner-m6", "malformed-inner-m6"}) {
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
        srp_vectors(directory);
        credential_round_trip(directory);
        malformed_srp(directory);
        state_and_schema(directory);
        accessory_authentication(directory);
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << active_group << "]: " << error.what() << '\n';
        return 1;
    }
    return failures == 0 ? 0 : 1;
}
