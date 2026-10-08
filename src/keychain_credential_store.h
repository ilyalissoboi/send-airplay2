// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_KEYCHAIN_CREDENTIAL_STORE_H
#define SEND_AIRPLAY2_KEYCHAIN_CREDENTIAL_STORE_H
#include "credential_store.h"
#include <memory>

namespace send_airplay2::detail {
/**
 * macOS built-in store (D58): one generic password item per profile in the
 * user's default (login, file-based) keychain, through the SecItem API. The
 * service is "send-airplay2/v1" ("send-airplay2/tests/v1" for the test
 * namespace) and the account is the profile name. The data protection keychain
 * is not used: it needs keychain-access-group entitlements from a provisioning
 * profile, which a library, the CLI or an unsigned host does not have (TN3137);
 * such hosts can pass their own store instead. Items do not synchronize.
 * Blocks the calling thread for keychain access, which may show a macOS access
 * prompt when a different binary (such as a rebuilt one) reads an item.
 */
[[nodiscard]] std::unique_ptr<CredentialStore> keychain_credential_store(CredentialNamespace space);
} // namespace send_airplay2::detail
#endif
