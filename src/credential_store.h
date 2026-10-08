// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_CREDENTIAL_STORE_H
#define SEND_AIRPLAY2_CREDENTIAL_STORE_H
#include "pair_verify.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace send_airplay2::detail {
enum class CredentialError {
    invalid_record,
    invalid_profile,
    already_exists,
    unavailable,
    unsupported
};
/// Sanitized categories only; never expose credential bytes or native diagnostics.
class CredentialException : public std::runtime_error {
public:
    explicit CredentialException(CredentialError reason);
    [[nodiscard]] CredentialError reason() const noexcept {
        return reason_;
    }

private:
    CredentialError reason_;
};
namespace credential_format {
constexpr std::size_t header_size = 11; // 8 magic bytes, version, receiver/client ID lengths.
constexpr std::size_t key_size = 32;
constexpr std::size_t max_blob_size =
    header_size + 2 * key_size + 2 * pair_verify::max_identifier_size;
} // namespace credential_format
/** Fixed erasing owner for a plaintext credential envelope, not a file format.
 * Noncopyable/nonmovable; serial use. Contains the signing seed and must only
 * cross trusted secret-store boundaries. OS storage supplies confidentiality/trust.
 * No standalone encryption/MAC, export command, process-isolation or memory locking.
 */
struct CredentialBlob {
    std::array<std::uint8_t, credential_format::max_blob_size> bytes{};
    std::size_t size = 0;
    CredentialBlob() = default;
    ~CredentialBlob();
    CredentialBlob(const CredentialBlob&) = delete;
    CredentialBlob& operator=(const CredentialBlob&) = delete;
    CredentialBlob(CredentialBlob&&) = delete;
    CredentialBlob& operator=(CredentialBlob&&) = delete;
};
/// Encode authenticated credentials into a distinct erasing owner; unchanged on error.
/// IDs are opaque 1..64 bytes; receiver key must be canonical and in the prime-order subgroup.
void encode_credentials(const PairCredentials& credentials, CredentialBlob& output);
/// Import only from trusted storage. Exact size/version/schema; rejects weak pinned keys.
/// Returned credentials own their seed copy; source blob remains caller-owned/erasing.
[[nodiscard]] std::unique_ptr<PairCredentials> decode_credentials(const CredentialBlob& input);
/// Profiles are 1..64 lower-case ASCII letters/digits/._-, beginning with a letter/digit.
/// They select a local trust slot, never an address/discovery identity or filesystem path.
void validate_credential_profile(std::string_view profile);

/** Host secret-store boundary; serial use, no plaintext persistence fallback.
 * load returns null only for absence; malformed/unavailable storage throws.
 * save_new refuses any existing slot, including malformed records; the host adapter
 * must serialize cooperating writers. External same-user applications are trusted.
 * erase deletes only the local entry, returning false for absence, and does not
 * revoke the receiver's pairing. Every returned credential has independent ownership.
 */
class CredentialStore {
public:
    CredentialStore() = default;
    virtual ~CredentialStore() = default;
    CredentialStore(const CredentialStore&) = delete;
    CredentialStore& operator=(const CredentialStore&) = delete;
    CredentialStore(CredentialStore&&) = delete;
    CredentialStore& operator=(CredentialStore&&) = delete;
    virtual std::unique_ptr<PairCredentials> load(std::string_view profile) = 0;
    virtual void save_new(std::string_view profile, const PairCredentials& credentials) = 0;
    virtual bool erase(std::string_view profile) = 0;
};
enum class CredentialNamespace { application, synthetic_test };
/// Built-in store: Windows desktop Credential Manager (current user, same
/// machine), the macOS login keychain (keychain_credential_store.h) or, on
/// Linux builds with libsecret, the Secret Service
/// (secret_service_credential_store.h). Other platforms, and UWP builds, throw
/// unsupported; hosts there supply a store.
/// Test namespace is separate and cannot be selected through CLI arguments.
[[nodiscard]] std::unique_ptr<CredentialStore>
native_credential_store(CredentialNamespace space = CredentialNamespace::application);
} // namespace send_airplay2::detail
#endif
