// SPDX-License-Identifier: Apache-2.0
#include "credential_store.h"
#include "control_crypto.h"
#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <winapifamily.h>
// Credential Manager (wincred.h) exists for desktop apps only. App-partition
// (UWP) builds leave it out, so the binary has no desktop-only credential
// imports and the built-in store reports unsupported, as on other platforms
// without one; packaged hosts supply their own store (D49).
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#define SAP2_HAS_CREDENTIAL_MANAGER 1
#endif
#endif
#ifdef SAP2_HAS_CREDENTIAL_MANAGER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#include <sddl.h>
#include <cwchar>
#endif

namespace send_airplay2::detail {
#ifdef SAP2_HAS_CREDENTIAL_MANAGER
namespace {
[[noreturn]] void unavailable() {
    throw CredentialException(CredentialError::unavailable);
}
struct HandleOwner {
    HANDLE value = nullptr;
    HandleOwner() = default;
    ~HandleOwner() {
        if (value) {
            CloseHandle(value);
        }
    }
    HandleOwner(const HandleOwner&) = delete;
    HandleOwner& operator=(const HandleOwner&) = delete;
    HandleOwner(HandleOwner&&) = delete;
    HandleOwner& operator=(HandleOwner&&) = delete;
};
struct StoredCredential {
    PCREDENTIALW value = nullptr;
    StoredCredential() = default;
    ~StoredCredential() {
        if (value) {
            // CredRead supplies one OS-owned allocation; erase its secret before freeing it.
            if (value->CredentialBlob &&
                value->CredentialBlobSize <= CRED_MAX_CREDENTIAL_BLOB_SIZE) {
                cleanse(value->CredentialBlob, value->CredentialBlobSize);
            }
            CredFree(value);
        }
    }
    StoredCredential(const StoredCredential&) = delete;
    StoredCredential& operator=(const StoredCredential&) = delete;
    StoredCredential(StoredCredential&&) = delete;
    StoredCredential& operator=(StoredCredential&&) = delete;
};
std::wstring user_sid() {
    HandleOwner token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) {
        unavailable();
    }
    DWORD size = 0;
    (void)GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !size || size > 4096) {
        unavailable();
    }
    std::vector<unsigned char> storage(size);
    if (!GetTokenInformation(token.value, TokenUser, storage.data(), size, &size)) {
        unavailable();
    }
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(storage.data())->User.Sid, &sid)) {
        unavailable();
    }
    const auto owner = std::unique_ptr<wchar_t, decltype(&LocalFree)>(sid, LocalFree);
    return std::wstring(sid);
}
/** Serialize cooperating app writers across processes. Credential Manager itself
 * has no create-only write operation. The mutex is scoped by user SID/profile;
 * external applications running as the same user remain in the trust boundary.
 */
class SlotLock {
    HandleOwner mutex_;
    bool acquired_ = false;

public:
    explicit SlotLock(const std::wstring& name) {
        mutex_.value = CreateMutexW(nullptr, FALSE, name.c_str());
        if (!mutex_.value) {
            unavailable();
        }
        const auto result = WaitForSingleObject(mutex_.value, 5000);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            unavailable();
        }
        acquired_ = true;
    }
    ~SlotLock() {
        if (acquired_) {
            ReleaseMutex(mutex_.value);
        }
    }
    SlotLock(const SlotLock&) = delete;
    SlotLock& operator=(const SlotLock&) = delete;
    SlotLock(SlotLock&&) = delete;
    SlotLock& operator=(SlotLock&&) = delete;
};
bool read_slot(const std::wstring& target, StoredCredential& output) {
    if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &output.value)) {
        return true;
    }
    if (GetLastError() == ERROR_NOT_FOUND) {
        return false;
    }
    unavailable();
}
class WindowsCredentialStore final : public CredentialStore {
    std::wstring prefix_;
    std::wstring lock_prefix_;
    std::wstring target(std::string_view profile) const {
        validate_credential_profile(profile);
        return prefix_ + std::wstring(profile.begin(), profile.end());
    }

public:
    explicit WindowsCredentialStore(CredentialNamespace space)
        : prefix_(space == CredentialNamespace::application ? L"send-airplay2/v1/"
                                                            : L"send-airplay2/tests/v1/"),
          lock_prefix_(L"Global\\send-airplay2." + user_sid() +
                       (space == CredentialNamespace::application ? L".v1." : L".tests.v1.")) {}
    std::unique_ptr<PairCredentials> load(std::string_view profile) override {
        const auto name = target(profile);
        StoredCredential stored;
        if (!read_slot(name, stored)) {
            return nullptr;
        }
        const auto& record = *stored.value;
        if (record.Type != CRED_TYPE_GENERIC || record.Persist != CRED_PERSIST_LOCAL_MACHINE ||
            !record.UserName || std::wcscmp(record.UserName, L"send-airplay2-v1") != 0 ||
            !record.CredentialBlob ||
            record.CredentialBlobSize > credential_format::max_blob_size) {
            throw CredentialException(CredentialError::invalid_record);
        }
        CredentialBlob blob;
        blob.size = record.CredentialBlobSize;
        std::copy_n(record.CredentialBlob, blob.size, blob.bytes.begin());
        return decode_credentials(blob);
    }
    void save_new(std::string_view profile, const PairCredentials& credentials) override {
        auto name = target(profile);
        CredentialBlob blob;
        encode_credentials(credentials, blob);
        SlotLock lock(lock_prefix_ + std::wstring(profile.begin(), profile.end()));
        StoredCredential existing;
        if (read_slot(name, existing)) {
            throw CredentialException(CredentialError::already_exists);
        }
        CREDENTIALW record{};
        record.Type = CRED_TYPE_GENERIC;
        record.TargetName = name.data();
        record.CredentialBlobSize = static_cast<DWORD>(blob.size);
        record.CredentialBlob = blob.bytes.data();
        record.Persist = CRED_PERSIST_LOCAL_MACHINE;
        wchar_t username[] = L"send-airplay2-v1";
        record.UserName = username;
        if (!CredWriteW(&record, 0)) {
            unavailable();
        }
    }
    bool erase(std::string_view profile) override {
        const auto name = target(profile);
        SlotLock lock(lock_prefix_ + std::wstring(profile.begin(), profile.end()));
        if (CredDeleteW(name.c_str(), CRED_TYPE_GENERIC, 0)) {
            return true;
        }
        if (GetLastError() == ERROR_NOT_FOUND) {
            return false;
        }
        unavailable();
    }
};
} // namespace
#endif
std::unique_ptr<CredentialStore> native_credential_store(CredentialNamespace space) {
    if (space != CredentialNamespace::application && space != CredentialNamespace::synthetic_test) {
        throw CredentialException(CredentialError::invalid_profile);
    }
#ifdef SAP2_HAS_CREDENTIAL_MANAGER
    return std::make_unique<WindowsCredentialStore>(space);
#else
    (void)space;
    throw CredentialException(CredentialError::unsupported);
#endif
}
} // namespace send_airplay2::detail
