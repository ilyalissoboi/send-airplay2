// SPDX-License-Identifier: Apache-2.0
#include "credential_store.h"
#include "control_crypto.h"
#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::array<std::uint8_t, 8> magic{'S', 'A', 'P', '2', 'C', 'R', 'E', 'D'};
constexpr std::uint8_t version = 1;
constexpr std::size_t version_offset = magic.size();
constexpr std::size_t receiver_length_offset = version_offset + 1;
constexpr std::size_t client_length_offset = receiver_length_offset + 1;
[[noreturn]] void invalid_record() {
    throw CredentialException(CredentialError::invalid_record);
}
void valid_id_size(std::size_t size) {
    if (!size || size > pair_verify::max_identifier_size) {
        invalid_record();
    }
}
} // namespace
CredentialException::CredentialException(CredentialError reason)
    : std::runtime_error("Credential operation failed (category " +
                         std::to_string(static_cast<int>(reason)) + ")"),
      reason_(reason) {}
CredentialBlob::~CredentialBlob() {
    cleanse(bytes.data(), bytes.size());
}
void validate_credential_profile(std::string_view profile) {
    const auto alphanumeric = [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    };
    if (profile.empty() || profile.size() > 64 || !alphanumeric(profile.front()) ||
        !std::all_of(profile.begin(), profile.end(), [&](char ch) {
            return alphanumeric(ch) || ch == '.' || ch == '_' || ch == '-';
        })) {
        throw CredentialException(CredentialError::invalid_profile);
    }
}
/// Narrow friend boundary: no public seed/identity accessors are added to credentials.
struct CredentialCodec {
    static void encode(const PairCredentials& input, CredentialBlob& output) {
        valid_id_size(input.receiver_id_.size());
        valid_id_size(input.client_id_.size());
        if (!ed25519_key_valid(input.receiver_key_)) {
            invalid_record();
        }
        CredentialBlob temporary;
        std::copy(magic.begin(), magic.end(), temporary.bytes.begin());
        temporary.bytes[version_offset] = version;
        temporary.bytes[receiver_length_offset] =
            static_cast<std::uint8_t>(input.receiver_id_.size());
        temporary.bytes[client_length_offset] = static_cast<std::uint8_t>(input.client_id_.size());
        auto cursor = temporary.bytes.begin() + credential_format::header_size;
        cursor = std::copy(input.receiver_key_.begin(), input.receiver_key_.end(), cursor);
        cursor =
            std::copy(input.client_seed_.bytes.begin(), input.client_seed_.bytes.end(), cursor);
        cursor = std::copy(input.receiver_id_.begin(), input.receiver_id_.end(), cursor);
        cursor = std::copy(input.client_id_.begin(), input.client_id_.end(), cursor);
        temporary.size = static_cast<std::size_t>(cursor - temporary.bytes.begin());
        // No throwing operations after validation/construction; overwrite wipes old tail too.
        output.bytes = temporary.bytes;
        output.size = temporary.size;
    }
};
void encode_credentials(const PairCredentials& credentials, CredentialBlob& output) {
    CredentialCodec::encode(credentials, output);
}
std::unique_ptr<PairCredentials> decode_credentials(const CredentialBlob& input) {
    constexpr auto fixed_size = credential_format::header_size + 2 * credential_format::key_size;
    if (input.size < fixed_size || input.size > input.bytes.size() ||
        !std::equal(magic.begin(), magic.end(), input.bytes.begin()) ||
        input.bytes[version_offset] != version) {
        invalid_record();
    }
    const auto receiver_length = input.bytes[receiver_length_offset];
    const auto client_length = input.bytes[client_length_offset];
    valid_id_size(receiver_length);
    valid_id_size(client_length);
    if (input.size != fixed_size + receiver_length + client_length) {
        invalid_record();
    }
    PublicKey receiver_key{};
    std::copy_n(input.bytes.begin() + credential_format::header_size, receiver_key.size(),
                receiver_key.begin());
    if (!ed25519_key_valid(receiver_key)) {
        invalid_record();
    }
    const auto receiver_start = input.bytes.begin() + fixed_size;
    const auto client_start = receiver_start + receiver_length;
    Bytes receiver_id(receiver_start, client_start);
    Bytes client_id(client_start, client_start + client_length);
    Secret32 client_seed;
    std::copy_n(input.bytes.begin() + credential_format::header_size + credential_format::key_size,
                client_seed.bytes.size(), client_seed.bytes.begin());
    return std::make_unique<PairCredentials>(std::move(receiver_id), receiver_key,
                                             std::move(client_id), client_seed);
}
} // namespace send_airplay2::detail
