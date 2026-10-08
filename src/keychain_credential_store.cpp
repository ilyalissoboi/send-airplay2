// SPDX-License-Identifier: Apache-2.0
// Compiled on Apple platforms only (CMakeLists.txt). No implementation source
// was copied; the SecItem calls follow Apple's Keychain Services reference and
// TN3137 (docs/credential-storage.md).
#include "keychain_credential_store.h"
#include "control_crypto.h"

#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace send_airplay2::detail {
namespace {
[[noreturn]] void unavailable() {
    throw CredentialException(CredentialError::unavailable);
}

/// Owns one Core Foundation reference; CFRelease on destruction. Noncopyable so
/// a reference is released exactly once.
template <typename Reference> class CfOwner {
public:
    explicit CfOwner(Reference value = nullptr) noexcept : value_(value) {}
    ~CfOwner() {
        if (value_) {
            CFRelease(value_);
        }
    }
    CfOwner(const CfOwner&) = delete;
    CfOwner& operator=(const CfOwner&) = delete;
    CfOwner(CfOwner&&) = delete;
    CfOwner& operator=(CfOwner&&) = delete;
    [[nodiscard]] Reference get() const noexcept {
        return value_;
    }
    /// For out-parameters of Copy functions; the slot must be empty.
    [[nodiscard]] Reference* receive() noexcept {
        return &value_;
    }

private:
    Reference value_;
};

CFStringRef create_string(std::string_view text) {
    const auto value =
        CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
                                static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false);
    if (!value) {
        unavailable();
    }
    return value;
}

CFMutableDictionaryRef create_dictionary() {
    const auto dictionary = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!dictionary) {
        unavailable();
    }
    return dictionary;
}

/**
 * The user's default keychain (normally the login keychain). Without an explicit
 * keychain, SecItem reads and deletes search every keychain in the search list
 * while SecItemAdd writes to the default one (TN3137), so a same-named item in
 * the System or a custom keychain could be read or deleted instead. Every
 * operation therefore names this keychain. SecKeychainCopyDefault belongs to the
 * deprecated SecKeychain API and has no SecItem equivalent, so its deprecation
 * warning is suppressed for this one call.
 */
SecKeychainRef copy_default_keychain() {
    SecKeychainRef keychain = nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    const auto status = SecKeychainCopyDefault(&keychain);
#pragma clang diagnostic pop
    if (status != errSecSuccess || !keychain) {
        if (keychain) {
            CFRelease(keychain);
        }
        unavailable();
    }
    return keychain;
}

/// Restricts a read or delete to `keychain` (kSecMatchSearchList).
void restrict_search(CFMutableDictionaryRef query, SecKeychainRef keychain) {
    const void* keychains[] = {keychain};
    const CfOwner<CFArrayRef> search(
        CFArrayCreate(kCFAllocatorDefault, keychains, 1, &kCFTypeArrayCallBacks));
    if (!search.get()) {
        unavailable();
    }
    CFDictionarySetValue(query, kSecMatchSearchList, search.get());
}

/**
 * Our copy of the record for SecItemAdd, erased before release. The capacity is
 * fixed at the record size, so appending cannot reallocate and leave an
 * unerased buffer behind. Security keeps its own copy of what it stores, which
 * is outside this library's control. Noncopyable: one owner erases once.
 */
class ErasingData {
public:
    explicit ErasingData(const CredentialBlob& blob)
        : data_(CFDataCreateMutable(kCFAllocatorDefault, static_cast<CFIndex>(blob.size))) {
        if (!data_) {
            unavailable();
        }
        CFDataAppendBytes(data_, blob.bytes.data(), static_cast<CFIndex>(blob.size));
    }
    ~ErasingData() {
        cleanse(CFDataGetMutableBytePtr(data_), static_cast<std::size_t>(CFDataGetLength(data_)));
        CFRelease(data_);
    }
    ErasingData(const ErasingData&) = delete;
    ErasingData& operator=(const ErasingData&) = delete;
    ErasingData(ErasingData&&) = delete;
    ErasingData& operator=(ErasingData&&) = delete;
    [[nodiscard]] CFDataRef get() const noexcept {
        return data_;
    }

private:
    CFMutableDataRef data_;
};

/// Generic password items are unique per service and account (errSecDuplicateItem).
/// The dictionary retains the strings it is given.
void add_item_keys(CFMutableDictionaryRef query, std::string_view service,
                   std::string_view profile) {
    const CfOwner<CFStringRef> service_name(create_string(service));
    const CfOwner<CFStringRef> account(create_string(profile));
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, service_name.get());
    CFDictionarySetValue(query, kSecAttrAccount, account.get());
}

class KeychainCredentialStore final : public CredentialStore {
public:
    explicit KeychainCredentialStore(CredentialNamespace space)
        : service_(space == CredentialNamespace::application ? "send-airplay2/v1"
                                                             : "send-airplay2/tests/v1") {}

    std::unique_ptr<PairCredentials> load(std::string_view profile) override {
        validate_credential_profile(profile);
        const CfOwner<SecKeychainRef> keychain(copy_default_keychain());
        const CfOwner<CFMutableDictionaryRef> query(create_dictionary());
        add_item_keys(query.get(), service_, profile);
        restrict_search(query.get(), keychain.get());
        CFDictionarySetValue(query.get(), kSecReturnData, kCFBooleanTrue);
        CFDictionarySetValue(query.get(), kSecMatchLimit, kSecMatchLimitOne);
        CfOwner<CFTypeRef> result;
        const auto status = SecItemCopyMatching(query.get(), result.receive());
        if (status == errSecItemNotFound) {
            return nullptr;
        }
        if (status != errSecSuccess || !result.get() ||
            CFGetTypeID(result.get()) != CFDataGetTypeID()) {
            unavailable();
        }
        const auto data = static_cast<CFDataRef>(result.get());
        const auto size = CFDataGetLength(data);
        if (size <= 0 || static_cast<std::size_t>(size) > credential_format::max_blob_size) {
            throw CredentialException(CredentialError::invalid_record);
        }
        // The returned CFData is immutable and owned by Core Foundation, so its
        // copy of the record cannot be erased here; the blob below is.
        CredentialBlob blob;
        blob.size = static_cast<std::size_t>(size);
        std::copy_n(CFDataGetBytePtr(data), blob.size, blob.bytes.begin());
        return decode_credentials(blob);
    }

    void save_new(std::string_view profile, const PairCredentials& credentials) override {
        validate_credential_profile(profile);
        CredentialBlob blob;
        encode_credentials(credentials, blob);
        const CfOwner<SecKeychainRef> keychain(copy_default_keychain());
        // Declared before the dictionary that retains it, so the dictionary is
        // released first and the bytes are erased last.
        const ErasingData value(blob);
        const CfOwner<CFMutableDictionaryRef> item(create_dictionary());
        add_item_keys(item.get(), service_, profile);
        const CfOwner<CFStringRef> label(create_string("send-airplay2 credential"));
        CFDictionarySetValue(item.get(), kSecUseKeychain, keychain.get());
        CFDictionarySetValue(item.get(), kSecValueData, value.get());
        CFDictionarySetValue(item.get(), kSecAttrLabel, label.get());
        // SecItemAdd refuses an existing service/account pair, so creation is
        // atomic across processes without an extra lock.
        const auto status = SecItemAdd(item.get(), nullptr);
        if (status == errSecDuplicateItem) {
            throw CredentialException(CredentialError::already_exists);
        }
        if (status != errSecSuccess) {
            unavailable();
        }
    }

    bool erase(std::string_view profile) override {
        validate_credential_profile(profile);
        const CfOwner<SecKeychainRef> keychain(copy_default_keychain());
        const CfOwner<CFMutableDictionaryRef> query(create_dictionary());
        add_item_keys(query.get(), service_, profile);
        restrict_search(query.get(), keychain.get());
        const auto status = SecItemDelete(query.get());
        if (status == errSecItemNotFound) {
            return false;
        }
        if (status != errSecSuccess) {
            unavailable();
        }
        return true;
    }

private:
    std::string service_;
};
} // namespace

std::unique_ptr<CredentialStore> keychain_credential_store(CredentialNamespace space) {
    return std::make_unique<KeychainCredentialStore>(space);
}
} // namespace send_airplay2::detail
