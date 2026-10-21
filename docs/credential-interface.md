# Host credential stores

The library uses either a host-supplied store or a platform built-in store.
There is no plaintext fallback. Public contracts are in
[credentials.h](../include/send_airplay2/credentials.h) and
[pairing.h](../include/send_airplay2/pairing.h).

## Store selection

Pass a `sap2_credential_store` through cast, pairing or removal options to select
host storage. With NULL, the library uses the [OS adapter](credential-storage.md)
when available. UWP compiles out desktop Credential Manager and therefore needs
a host store; the example uses C# `PasswordVault`. Headless services and signed
apps with different storage requirements can provide their own adapters.

Profiles select local trust slots, not receiver addresses. A profile is 1..64
lowercase ASCII letters/digits/`._-`, beginning with a letter or digit. Pairing
refuses an occupied slot, including an invalid record. Forget removes only the
local slot and does not revoke receiver-side pairing.

## Callback contract

The sized table contains `context`, `load`, `save_new` and `erase`. The library
copies the table, but callbacks and context must remain valid until the operation
returns; for a cast that means until `sap2_cast_destroy` returns. Credential load
occurs during start, not create. Callbacks run synchronously on the invoking
thread, not receiver event threads, and must not re-enter that operation.

`load` fills a library-owned buffer and reports its length. `save_new` must be
create-only, never an overwrite. Return the fixed OK, absent, already-exists or
unavailable store results. The library validates records and clears temporary
buffers after use; no allocator crosses the ABI.

The record is opaque and contains a signing seed. Store its exact bytes only in
secure storage. Text-only vaults can encode it as Base64; do not parse, alter,
log or put it into ordinary settings. The maximum version-1 envelope is 203
bytes. Hosts own their own copies and synchronization policy.

## Pairing

`sap2_pair` verifies slot absence, requests receiver PIN display, invokes the
host PIN reader, authenticates enrollment, saves credentials, reloads them and
verifies a fresh connection. Wrong PIN, cancellation and late PIN input have
distinct results. Network phases have bounded deadlines; the synchronous host
PIN callback itself must honor its deadline because native code cannot interrupt it.

Enrollment can succeed at the receiver while a host save fails. Conversely, a
failed reconnect does not erase successfully saved credentials. Retry verification
explicitly; never replace trust from discovery or silently re-pair.

## Packaged hosts

Package identity determines access to the app's vault and brokered file grants.
An in-place package update should preserve that identity and its data; uninstalling
or changing identity is a separate migration decision. CLI credentials are not
automatically available to a packaged app. See the
[UWP store implementation](../tools/uwp-host/PasswordVaultStore.cs).

Platform references: [Windows Credential Manager](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credreadw),
[PasswordVault](https://learn.microsoft.com/en-us/uwp/api/windows.security.credentials.passwordvault),
[credential locker](https://learn.microsoft.com/en-us/windows/apps/develop/security/credential-locker).
