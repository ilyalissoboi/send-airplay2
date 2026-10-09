// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_SECRET_SERVICE_CREDENTIAL_STORE_H
#define SEND_AIRPLAY2_SECRET_SERVICE_CREDENTIAL_STORE_H
#include "credential_store.h"
#include <memory>

namespace send_airplay2::detail {
/// libsecret schema name stored on every item; lookups match it.
inline constexpr const char* secret_service_schema_name = "org.send-airplay2.Credential";
/// Schema attributes: the namespace ("v1", or "tests/v1" for the test
/// namespace) and the profile name. Attributes are not secret.
inline constexpr const char* secret_service_namespace_attribute = "namespace";
inline constexpr const char* secret_service_profile_attribute = "profile";

/**
 * Linux built-in store (D59): one item per profile in the Secret Service's
 * default collection, through libsecret's synchronous binary password API.
 * Needs a session D-Bus with a running, unlocked Secret Service (for example
 * GNOME Keyring); otherwise every call reports unavailable, and headless hosts
 * should pass their own store. libsecret's store replaces a matching item, so
 * save_new and erase take a per-user advisory lock (flock on a file in
 * $XDG_RUNTIME_DIR, at most five seconds) around check-then-store. Calls block
 * the calling thread and are not cancellable.
 */
[[nodiscard]] std::unique_ptr<CredentialStore>
secret_service_credential_store(CredentialNamespace space);
} // namespace send_airplay2::detail
#endif
