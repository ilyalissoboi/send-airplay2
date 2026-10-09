// SPDX-License-Identifier: Apache-2.0
// Compiled on Linux when libsecret is found (CMakeLists.txt). No implementation
// source was copied; the calls follow libsecret's reference documentation
// (docs/credential-storage.md).
#include "secret_service_credential_store.h"

#include <libsecret/secret.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace send_airplay2::detail {
namespace {
[[noreturn]] void unavailable() {
    throw CredentialException(CredentialError::unavailable);
}

const SecretSchema* credential_schema() {
    static const SecretSchema schema = {
        secret_service_schema_name,
        SECRET_SCHEMA_NONE,
        {
            {secret_service_namespace_attribute, SECRET_SCHEMA_ATTRIBUTE_STRING},
            {secret_service_profile_attribute, SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING},
        },
        // Reserved fields, zero-initialized as libsecret requires.
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr};
    return &schema;
}

/// Owns a GError reported by libsecret; native messages are never surfaced.
class ErrorOwner {
public:
    ErrorOwner() = default;
    ~ErrorOwner() {
        if (error_) {
            g_error_free(error_);
        }
    }
    ErrorOwner(const ErrorOwner&) = delete;
    ErrorOwner& operator=(const ErrorOwner&) = delete;
    ErrorOwner(ErrorOwner&&) = delete;
    ErrorOwner& operator=(ErrorOwner&&) = delete;
    [[nodiscard]] GError** receive() noexcept {
        return &error_;
    }
    [[nodiscard]] bool failed() const noexcept {
        return error_ != nullptr;
    }

private:
    GError* error_ = nullptr;
};

/// Owns one SecretValue reference. libsecret keeps secret data in its
/// non-pageable secure memory and wipes it when the last reference goes.
class ValueOwner {
public:
    explicit ValueOwner(SecretValue* value = nullptr) noexcept : value_(value) {}
    ~ValueOwner() {
        if (value_) {
            secret_value_unref(value_);
        }
    }
    ValueOwner(const ValueOwner&) = delete;
    ValueOwner& operator=(const ValueOwner&) = delete;
    ValueOwner(ValueOwner&&) = delete;
    ValueOwner& operator=(ValueOwner&&) = delete;
    [[nodiscard]] SecretValue* get() const noexcept {
        return value_;
    }

private:
    SecretValue* value_;
};

/**
 * Serializes cooperating writers for one namespace and profile across
 * processes of this user: libsecret has no create-only store. The lock file
 * lives in $XDG_RUNTIME_DIR (per-user, 0700, removed at logout); without it the
 * store is unavailable. Other same-user programs are inside the trust boundary
 * and can change the collection without taking this lock.
 */
class SlotLock {
public:
    SlotLock(std::string_view space, std::string_view profile) {
        const char* runtime = std::getenv("XDG_RUNTIME_DIR");
        if (!runtime || runtime[0] != '/') {
            unavailable();
        }
        // One file name per namespace and profile, such as
        // "send-airplay2.tests.v1.<profile>.lock": the namespace's '/' becomes
        // '.', and validated profiles never contain '/'.
        std::string name = "send-airplay2.";
        name += space;
        name += '.';
        name += profile;
        name += ".lock";
        std::replace(name.begin(), name.end(), '/', '.');
        const std::string path = std::string(runtime) + '/' + name;
        descriptor_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (descriptor_ < 0) {
            unavailable();
        }
        constexpr auto wait_limit = std::chrono::seconds(5);
        constexpr auto retry_interval = std::chrono::milliseconds(20);
        const auto deadline = std::chrono::steady_clock::now() + wait_limit;
        while (::flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
            if ((errno != EWOULDBLOCK && errno != EINTR) ||
                std::chrono::steady_clock::now() >= deadline) {
                ::close(descriptor_);
                unavailable();
            }
            std::this_thread::sleep_for(retry_interval);
        }
    }
    ~SlotLock() {
        (void)::flock(descriptor_, LOCK_UN);
        ::close(descriptor_);
    }
    SlotLock(const SlotLock&) = delete;
    SlotLock& operator=(const SlotLock&) = delete;
    SlotLock(SlotLock&&) = delete;
    SlotLock& operator=(SlotLock&&) = delete;

private:
    int descriptor_ = -1;
};

class SecretServiceCredentialStore final : public CredentialStore {
public:
    explicit SecretServiceCredentialStore(CredentialNamespace space)
        : space_(space == CredentialNamespace::application ? "v1" : "tests/v1") {}

    std::unique_ptr<PairCredentials> load(std::string_view profile) override {
        validate_credential_profile(profile);
        const std::string name(profile);
        ValueOwner value(lookup(name));
        if (!value.get()) {
            // Lookup skips items in a locked collection; such an item is
            // present but unreadable, not absent.
            if (has_item(name)) {
                unavailable();
            }
            return nullptr;
        }
        gsize size = 0;
        const auto* bytes = secret_value_get(value.get(), &size);
        if (!bytes || size == 0 || size > credential_format::max_blob_size) {
            throw CredentialException(CredentialError::invalid_record);
        }
        CredentialBlob blob;
        blob.size = size;
        std::copy_n(reinterpret_cast<const std::uint8_t*>(bytes), size, blob.bytes.begin());
        return decode_credentials(blob);
    }

    void save_new(std::string_view profile, const PairCredentials& credentials) override {
        validate_credential_profile(profile);
        const std::string name(profile);
        CredentialBlob blob;
        encode_credentials(credentials, blob);
        const SlotLock lock(space_, name);
        // Any existing item keeps the slot occupied: readable or not, and
        // whether its collection is locked or not.
        if (has_item(name)) {
            throw CredentialException(CredentialError::already_exists);
        }
        // secret_value_new copies the bytes into libsecret's secure memory.
        const ValueOwner value(secret_value_new(reinterpret_cast<const gchar*>(blob.bytes.data()),
                                                static_cast<gssize>(blob.size),
                                                "application/octet-stream"));
        ErrorOwner error;
        const auto stored = secret_password_store_binary_sync(
            credential_schema(), SECRET_COLLECTION_DEFAULT, "send-airplay2 credential", value.get(),
            nullptr, error.receive(), secret_service_namespace_attribute, space_.c_str(),
            secret_service_profile_attribute, name.c_str(), nullptr);
        if (!stored || error.failed()) {
            unavailable();
        }
    }

    bool erase(std::string_view profile) override {
        validate_credential_profile(profile);
        const std::string name(profile);
        const SlotLock lock(space_, name);
        ErrorOwner error;
        const auto removed = secret_password_clear_sync(
            credential_schema(), nullptr, error.receive(), secret_service_namespace_attribute,
            space_.c_str(), secret_service_profile_attribute, name.c_str(), nullptr);
        if (error.failed()) {
            unavailable();
        }
        if (removed != FALSE) {
            return true;
        }
        // Clearing deletes only unlocked items. A match that is still there
        // (in a locked collection) was not removed, which is not "absent".
        if (has_item(name)) {
            unavailable();
        }
        return false;
    }

private:
    /// The item's value, or null when absent; failures are unavailable.
    SecretValue* lookup(const std::string& profile) const {
        ErrorOwner error;
        auto* value = secret_password_lookup_binary_sync(
            credential_schema(), nullptr, error.receive(), secret_service_namespace_attribute,
            space_.c_str(), secret_service_profile_attribute, profile.c_str(), nullptr);
        if (error.failed()) {
            if (value) {
                secret_value_unref(value);
            }
            unavailable();
        }
        return value;
    }

    /// Whether any matching item exists, including items in locked
    /// collections (SECRET_SEARCH_ALL), without reading or unlocking secrets.
    bool has_item(const std::string& profile) const {
        ErrorOwner error;
        GList* items = secret_password_search_sync(
            credential_schema(), SECRET_SEARCH_ALL, nullptr, error.receive(),
            secret_service_namespace_attribute, space_.c_str(), secret_service_profile_attribute,
            profile.c_str(), nullptr);
        const bool found = items != nullptr;
        g_list_free_full(items, g_object_unref);
        if (error.failed()) {
            unavailable();
        }
        return found;
    }

    std::string space_;
};
} // namespace

std::unique_ptr<CredentialStore> secret_service_credential_store(CredentialNamespace space) {
    return std::make_unique<SecretServiceCredentialStore>(space);
}
} // namespace send_airplay2::detail
