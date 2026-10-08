# Common credential interface (D49)

Status: **decided; step 1 implemented in D50 (unit/CI-tested only).** Snapshot
2026-10-08 (Asia/Tokyo). This record
revises part of D46 so the library can eventually authenticate on every target
host (Windows desktop, packaged Windows/UWP, Linux, macOS, Android). It keeps
the three kinds of statement separate: user decisions, engineering proposals and
researched platform facts. Nothing here is implemented or tested yet; the
current behavior remains [credential-storage.md](credential-storage.md).

## Why D46 needs revising

D46 loads credentials by profile name from a store inside the library, so pairing
secrets never cross the C interface. Only the Windows desktop Credential Manager
adapter exists; every other platform reports `SAP2_ERROR_UNSUPPORTED`. Research
for the packaged Windows host (below) showed that this cannot reach the main
target, Screenbox, which runs as an AppContainer UWP app: Credential Manager is
desktop-only there, and the store such an app can use is reachable most simply
from its own managed code. Android has the same shape (Keystore from Kotlin/Java).
Separately, each packaged app's storage is its own, so pairing must be possible
from the host; a profile paired with the CLI does not appear in another store.

## User decisions (D49, 2026-10-08)

The user approved recording this common credential design:

1. **One credential seam, two kinds of store.** The private `CredentialStore`
   interface (`load`, `save_new`, `erase`) stays the only boundary. Behind it:
   - **built-in adapters** where native access is straightforward: Windows desktop
     Credential Manager (exists), macOS Keychain and Linux Secret Service
     (libsecret) later;
   - **a host-provided store** passed through the C interface, for hosts whose
     secure storage is only practical from managed code: UWP `PasswordVault`
     (C#), Android Keystore-backed storage (Kotlin), headless Linux services
     with their own vault.
2. **Profiles stay the key.** Hosts keep passing a profile name. The library uses
   the host store when one is supplied, otherwise the platform's built-in store,
   otherwise `SAP2_ERROR_UNSUPPORTED`.
3. **D46 revised for host stores only.** With a host store, the library's encoded
   credential record crosses into host code, which must write it straight into
   that platform's secure storage. Built-in stores keep D46's guarantee that
   secrets never cross the interface.
4. **Pairing joins the C interface** (with local profile removal), so a host can
   enroll into its own store. Without it, only CLI-paired Windows desktop
   profiles can ever be used.
5. **`PasswordVault` is host-supplied, not a built-in adapter.** This avoids a
   C++/WinRT dependency in the library and leaves the locker's roaming and
   20-credential trade-offs (below) to the host that chooses it.
6. **UWP builds exclude the Credential Manager adapter at compile time,** so the
   UWP library binary carries no desktop-only `advapi32` credential imports.
7. **No silent plaintext fallback** on any platform: no store means
   `SAP2_ERROR_UNSUPPORTED`. This rule already applies.

## Host-store contract

Proposed with D49 and implemented in D50 as
[`credentials.h`](../include/send_airplay2/credentials.h) and
[`pairing.h`](../include/send_airplay2/pairing.h); see
[public-api.md](public-api.md#credential-stores-and-pairing-api-version-2-d50).

- **Callback table** `sap2_credential_store`: `struct_size`, `context`, `load`,
  `save_new`, `erase`, each taking the profile name. Supplied through the options
  of the calls that need credentials (cast start, pairing, removal).
- **Lifetime.** The library copies the table (the function pointers and the
  `context` value) when the call that receives it validates its options; the
  host's own struct can be discarded after that call returns. The callbacks and
  whatever `context` points to must stay valid for as long as the library may
  call them:
  - for a cast, from `sap2_cast_create()` until `sap2_cast_destroy()` returns,
    because credentials are loaded later, inside `sap2_cast_start()`;
  - for one-shot calls such as pairing and profile removal, until that call
    returns.
  The library never calls the store after those points. A C# binding therefore
  keeps its delegates and any `GCHandle` for `context` alive until the handle is
  destroyed (or the one-shot call returns), and releases them only then.
- **Opaque record.** The host stores the library's encoded record without parsing
  it. It contains the controller's Ed25519 signing seed, so it belongs only in
  secure storage, never in plain files, logs or app settings. Hosts whose store
  holds text (such as `PasswordVault`) encode it themselves, for example as Base64.
- **No allocation across the boundary.** `load` fills a library-owned buffer of
  the maximum record size (203 bytes for version 1) and reports the length;
  `save_new` reads from a library-owned buffer. The library erases its buffers
  after every call, and discards whatever a failed or absent `load` wrote.
- **Fixed results:** ok, absent, already exists (`save_new` never overwrites),
  unavailable. The library maps them to the existing credential results.
- **Threading.** Callbacks run synchronously on the thread that called the
  library function, during start, pairing and removal only, never on session
  worker threads (keeps D28).
- **Roaming is the host's decision.** A host store may sync or roam credentials
  (the UWP locker can). The host decides whether that is acceptable and documents
  it. Built-in stores do not roam: Windows uses `CRED_PERSIST_LOCAL_MACHINE`.

## Researched platform facts (Windows)

Verified on 2026-10-08 against the installed Windows SDK 10.0.28000.0, Microsoft
Learn and the built library. These are documentation and build facts, not
runtime tests.

| Fact | Evidence |
| --- | --- |
| `CredReadW`/`CredWriteW`/`CredDeleteW`/`CredEnumerateW` are desktop-only | SDK `wincred.h` declares them only under `WINAPI_PARTITION_DESKTOP \| WINAPI_PARTITION_SYSTEM`; Microsoft Learn lists `CredReadW` as "[desktop apps only]" |
| `PasswordVault` is available to UWP apps | SDK `windows.security.credentials.idl`: `UniversalApiContract` 1.0, activatable; Windows 10 10240 and later |
| An AppContainer app sees only its own locker; a regular desktop app can access all of the user's lockers, including AppContainer apps' lockers | `PasswordVault` reference page |
| At most 20 credentials per app; credentials roam with the user's Microsoft account; use the locker "only for passwords and not for larger data blobs" | Credential locker guide |
| `PasswordCredential.Password` is a string | SDK IDL (`HSTRING`) |
| Lockers are keyed by package identity: changing it (a fork, or UWP to WinUI 3) loses stored credentials | Windows App SDK issue #4393 |
| Screenbox is an AppContainer UWP app | Its manifest: `Windows.Universal`, entry point `Screenbox.App`, no `runFullTrust` |
| `send_airplay2.dll` imports `CredReadW`, `CredWriteW`, `CredDeleteW` and `CredFree` | `dumpbin /imports` on the D48 shared build |
| The Store certification kit checks import tables for APIs not allowed for the app type | Kit documentation (Windows 8.x era) and a quoted user report of `CredWriteW` failing it; not run here |

The version-1 record is at most 203 bytes (272 characters as Base64), which is
password-sized. An earlier estimate of 139 bytes in conversation was wrong.

**Measured in the packaged UWP test app (D53):** Credential Manager does **not**
work at runtime inside a sideloaded AppContainer app (removing an absent profile
and loading an existing one both report `credential_store`), and pairing into a
`PasswordVault` host store works, with the credential kept across in-place
package updates; see [uwp-host.md](uwp-host.md). **Still unverified:** whether the
locker roams on Windows 11 and under which setting (Windows backup's "Remember my
preferences" appears related, unconfirmed), and whether a desktop tool can
provision a specific app's locker. None of these changes the decisions above.

## Other platforms (planned, not researched in depth)

| Platform | Planned store | Notes |
| --- | --- | --- |
| macOS | Built-in Keychain adapter (Security framework, C API) | Generic password items, local to the login keychain |
| Linux desktop | Built-in Secret Service adapter (libsecret over D-Bus) | Needs a session bus and an unlocked keyring; report unavailable otherwise |
| Linux headless or embedded | Host store | No desktop keyring is assumed |
| Android | Host store (Kotlin) | Keystore key encrypting the record in app storage |
| Windows packaged (UWP) | Host store (C# `PasswordVault`) | 20-credential limit and roaming as above |

Each adapter needs its own research, provenance record and tests before
implementation; dependencies such as libsecret are recorded in
[dependencies.md](dependencies.md) when added.

## Order of work

1. **Done (D50, unit/CI-tested):** host-store callbacks and pairing in the C
   interface, with test stores, so CI covers them on Windows, Linux and macOS.
   A real pairing through `sap2_pair()` into the built-in store passed on
   2026-10-08 (D51); pairing into a host store on hardware comes with step 2.
2. **Done (D52, D53):** the C# binding, and a packaged UWP test app with a C#
   `PasswordVault` host store, receiver-tested on one Apple TV and Windows host.
3. The UWP library build without the Credential Manager adapter, linked against
   the app C runtime (D53 found the desktop-built DLLs depend on the desktop C
   runtime, which a clean UWP device may not have).
4. Built-in macOS Keychain and Linux Secret Service adapters.

## Sources

- [CredReadW function (wincred.h)](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credreadw)
- [PasswordVault class](https://learn.microsoft.com/en-us/uwp/api/windows.security.credentials.passwordvault)
- [Credential locker for Windows apps](https://learn.microsoft.com/en-us/windows/apps/develop/security/credential-locker)
- [Windows App SDK issue #4393](https://github.com/microsoft/WindowsAppSDK/issues/4393)
- [Windows App Certification Kit tests (Windows 8.x era)](https://learn.microsoft.com/it-it/previous-versions/windows/apps/dn629257(v=win.10))
- [About sync settings on Windows devices](https://support.microsoft.com/help/4026102)
- [Screenbox Package.appxmanifest](https://github.com/huynhsontung/Screenbox/blob/main/Screenbox/Package.appxmanifest)
