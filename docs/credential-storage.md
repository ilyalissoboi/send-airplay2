# Credential storage and authentication CLI

Implemented as private host code. The built-in stores are Windows desktop
Credential Manager, the macOS login keychain (D58) and, on Linux builds with
libsecret, the Secret Service (D59); the library's public ABI does not expose
signing seeds or credential serialization. Linux builds without libsecret
report native storage as unsupported; packaged Windows/UWP and Android hosts
supply their own store (D49). This slice
does not implement playback or establish Apple TV compatibility.

The planned design for other platforms and packaged hosts (built-in stores plus a
host-provided store through the C interface) is recorded as D49 in
[credential-interface.md](credential-interface.md); this page describes what is
implemented today.

## Trust and record contract

`CredentialStore` owns the persistence boundary. Credentials originate in
authenticated PIN M6 or a trusted store; discovery names, addresses, advertised
keys and a new peer-verify M2 never replace pinned identity. There is no plaintext
file fallback, import/export command or automatic re-pairing.

The version-1 envelope is at most 203 bytes, with no padding in its declared size:

| Offset | Bytes | Meaning |
|---|---:|---|
| 0 | 8 | ASCII `SAP2CRED` |
| 8 | 1 | Version, exactly 1 |
| 9 | 1 | Receiver ID length, 1..64 |
| 10 | 1 | Controller ID length, 1..64 |
| 11 | 32 | Pinned receiver Ed25519 public key |
| 43 | 32 | Controller Ed25519 signing seed |
| 75 | Variable | Receiver ID, then controller ID; opaque bytes |

The decoder requires the exact declared length, version and schema, and rejects
noncanonical, identity and non-prime-order receiver keys through the existing
Botan validation adapter. Unknown versions fail closed; migration is not implemented.
There is no standalone encryption/MAC: only the trusted OS store may persist this
plaintext envelope. Fixed blob/PIN owners, temporary seeds and OS-read blobs are
erased on release. This is scoped cleanup, not process-wide erasure or memory locking.

Profiles contain 1..64 lowercase ASCII letters/digits/`._-`, starting with a letter
or digit. A profile selects a local trust slot, independently of receiver addresses.
Lowercase names avoid Credential Manager's case-insensitive target aliases.
Application targets are `send-airplay2/v1/PROFILE`; synthetic tests use a separate
`send-airplay2/tests/v1/PROFILE` namespace unavailable to CLI arguments.

Windows uses generic credentials with `CRED_PERSIST_LOCAL_MACHINE`: the current
user on this computer, including subsequent logons. It does not roam pairing to
another computer or isolate secrets from other applications running as that user.
An application marker and persistence policy are checked before decoding.
`CredRead` failure, malformed entries and unavailable credential sets are explicit
errors; absence alone returns an empty result.

`CredWrite` replaces existing targets, so `save_new` takes a named mutex keyed by
user SID, namespace and profile across Windows sessions, checks absence, and then
writes. Cooperating writers cannot overwrite one another, including malformed
entries. The mutex wait is bounded at five seconds; OS storage calls themselves
are synchronous and are not cancellable. Other same-user applications are inside
the trust boundary and can alter the store independently of this mutex.

## macOS login keychain (D58)

On macOS the built-in store keeps one generic password item per profile in the
user's default keychain (normally the login keychain), through the SecItem API
(`src/keychain_credential_store.cpp`). The item's service is `send-airplay2/v1`
(`send-airplay2/tests/v1` for synthetic tests) and its account is the profile;
the value is the version-1 envelope above, labelled "send-airplay2 credential".

| Behavior | Implementation |
| --- | --- |
| Create only | `SecItemAdd` fails with `errSecDuplicateItem` for an existing service/account pair, including a malformed item, so no lock is needed |
| Absent | `errSecItemNotFound` from `SecItemCopyMatching` or `SecItemDelete` |
| Unavailable | Any other status (locked keychain without UI, access denied, no keychain); native messages are not surfaced |
| Malformed | A value larger than 203 bytes or empty is `invalid_record`; the decoder checks the rest |
| One keychain | Every operation names the default keychain (`kSecUseKeychain` for adds, `kSecMatchSearchList` for reads and deletes); otherwise reads and deletes would search the whole keychain search list while adds go to the default one (TN3137). `SecKeychainCopyDefault` is deprecated with no SecItem equivalent, so its warning is suppressed for that call |
| Secret handling | The value passed to `SecItemAdd` is our own fixed-capacity mutable copy, erased before release; the CFData Security returns on load is immutable and cannot be erased by us, and Security's stored copy is outside our control |

Engineering choice (D58): the **file-based keychain**, not the data protection
keychain. Apple's TN3137 recommends the data protection keychain for new code,
but it requires keychain-access-group entitlements authorized by a provisioning
profile, and for library code the host process's entitlements decide. The CLI,
tests and unsigned hosts have none. A signed macOS host that wants the data
protection keychain can pass its own store through the C interface (D49). The
file-based keychain is "on the road to deprecation" (not deprecated) and uses
per-item access lists: the binary that created an item reads it without a
prompt, and a different or rebuilt binary may get an "allow access" prompt.
Items stay on this Mac; nothing synchronizes. Calls block the calling thread.

Evidence (CI only): at `aa77c6d` the native-store tests ran against the real
login keychain on GitHub's `macos-26-arm64` runner image, static and shared,
and passed: roundtrip, a read from a separately spawned process, overwrite
refusal, idempotent erase, exactly one winner of two racing writers, and a
one-byte malformed item that loads as invalid and stays occupied. They create
only random synthetic profiles in the test service and remove them. Not run: a
developer Mac with prompts, a rebuilt binary reading an earlier item, Mac
hardware casting, and Intel Macs.

## Linux Secret Service (D59)

When CMake finds `libsecret-1` 0.19 or later through pkg-config (option
`SAP2_SECRET_SERVICE`, on by default), Linux builds use the Secret Service as the
built-in store (`src/secret_service_credential_store.cpp`), through libsecret's
synchronous binary password API. Without libsecret, or with
`-DSAP2_SECRET_SERVICE=OFF`, the built-in store stays unsupported. Each profile
is one item in the default collection under the schema
`org.send-airplay2.Credential`, with attributes `namespace` (`v1`, or `tests/v1`
for synthetic tests) and `profile`; the value is the version-1 envelope above,
labelled "send-airplay2 credential".

| Behavior | Implementation |
| --- | --- |
| Create only | libsecret's store **replaces** a matching item, so `save_new` takes a per-user lock, checks that no item exists (a malformed one counts) and then stores |
| Lock | `flock` on `$XDG_RUNTIME_DIR/send-airplay2.<namespace>.<profile>.lock` (namespace `/` becomes `.`), opened `O_NOFOLLOW` with mode 0600, bounded at five seconds; `erase` takes it too. Without a usable `XDG_RUNTIME_DIR` the store is unavailable |
| Absent | No matching item at all, checked with `secret_password_search_sync(SECRET_SEARCH_ALL)` |
| Locked | Lookup and clear skip items in locked collections, so a match that is still present makes `load` and `erase` unavailable (never absent) and keeps `save_new`'s slot occupied; not exercised in CI, whose keyring stays unlocked |
| Unavailable | Any `GError` (no session bus, no Secret Service, locked collection, refused prompt), or the lock; native messages are not surfaced |
| Malformed | An empty value or one larger than 203 bytes is `invalid_record`; the decoder checks the rest |
| Secret handling | Values pass through libsecret's `SecretValue`, which keeps secret data in its non-pageable secure memory and wipes it on release |

The Secret Service needs a session D-Bus and a running, unlocked keyring (for
example GNOME Keyring in a desktop login). Headless or embedded hosts without one
get `unavailable` and should pass their own store (D49). Calls block and are not
cancellable. Other programs of the same user can change the collection without
the lock; they are inside the trust boundary, as on the other platforms.

The native-store tests run on Linux builds with libsecret. They skip with a
message when no Secret Service is reachable, unless
`SAP2_REQUIRE_SECRET_SERVICE=1`. CI's Ubuntu jobs install libsecret and GNOME
Keyring and run the tests through `tests/with_test_keyring.sh` inside
`dbus-run-session`: the script starts a throwaway keyring, initializes its
secrets component, checks that `org.freedesktop.secrets` is on the bus and that
a probe secret round-trips, and provides a private runtime directory if needed.
The sanitizer job does not install libsecret, so it covers the unsupported path.
Detection runs only for Linux targets (`CMAKE_SYSTEM_NAME` `Linux`), so Android
and other Unix builds, including cross-builds, keep the unsupported store.

Evidence (CI only): at `4176b7d` the Ubuntu 24.04 static and shared jobs
(libsecret 0.21.4, GNOME Keyring 46) passed with `SAP2_REQUIRE_SECRET_SERVICE=1`,
so the native-store tests could not skip: roundtrip, a read from a separately
spawned process, overwrite refusal, idempotent erase, one winner of two racing
writers and a malformed item. Earlier runs found and fixed a lock-path bug
that placed the lock file beside `XDG_RUNTIME_DIR` instead of inside it. Not
run: a Linux desktop session, a locked keyring with a prompt, KWallet or other
Secret Service providers.

## CLI workflow

Select the numeric address and service port from discovery. Replace the example
documentation address below with the actual receiver address; for link-local
IPv6 also supply its numeric interface index as `--scope-id`.

```powershell
build/Release/airplay2-cli.exe pair --address 192.0.2.10 --port 7000 --profile living-room
build/Release/airplay2-cli.exe verify --address 192.0.2.10 --port 7000 --profile living-room
build/Release/airplay2-cli.exe forget --profile living-room
```

`pair` refuses an existing or malformed profile before contacting the receiver.
It requires an interactive Windows console and establishes hidden input before
requesting receiver PIN display. Enter 4..8 digits, preserving leading zeros,
then Enter. Backspace edits; Esc, Ctrl+C or Ctrl+Break cancels. Digits are neither
echoed nor accepted through arguments, redirected stdin or environment variables.
Console input mode is restored on success/failure, and queued PIN events are
discarded before restoration. The command assumes sole ownership of console
input while prompting. PIN input has a separate default 60-second deadline;
`--pin-timeout-ms` accepts 1..60000 on `pair` only.

After authenticated M6, the provisioning socket closes, the credential is saved,
and a sanitized saved checkpoint is printed. The command drops the original
credential owner, reloads storage and verifies on a fresh socket, installing
encrypted control transport. A reload/reconnect/authentication failure retains
the saved profile: retry `verify` explicitly; it never silently re-enrolls.
Each network phase has a default 10-second deadline (`--timeout-ms` 1..60000).
Ctrl+C/Break cancellation is checked during bounded socket/console waits.

`verify` requires stored credentials and sends no PIN-display request. It uses
fresh ephemeral keys and never substitutes advertised identity. `forget` deletes
only local credentials, idempotently; it does not contact the receiver or revoke
its pairing. If receiver enrollment succeeds but host saving fails, receiver-side
pairing can remain; recovery may require the receiver's access settings. The
current CLI does not manage receiver-side revocation.

Exit status is 0 on success, 2 for arguments/input errors, and 1 for runtime
failure. Authentication diagnostics use sanitized categories; PIN-enrollment
response/schema failures also report phase, HTTP status, body size and bounded TLV
types/lengths. Decrypted identity diagnostics contain headers only. No PIN, keys,
metadata contents, credential envelope or raw transcript is printed.

## Evidence and next gate

Windows 11 x64 / MSVC Release static/shared builds run the codec/workflow tests,
real isolated synthetic Credential Manager operations, cross-process reload,
concurrent create refusal, malformed slots and real CLI argument/redirection
checks. The independent PIN-to-peer-verification oracle now serializes/reloads
the enrolled credentials before checking exact transcript/control-key bytes.
Portable tests cover truncation, bounds, weak keys, leading-zero PIN handling,
no premature saves, fresh reconnect, cancellation and retained credentials after
reconnect failure. Synthetic entries are cleaned up; no application profile is
used in these tests. The Ed25519 public-key literal is from
[RFC 8032 section 7.1, test 1](https://www.rfc-editor.org/rfc/rfc8032#section-7.1);
other host-record fixture bytes are original public synthetic values.

At source commit `37f5e3fe90136be25d89ede9c150bcd0f582969b`, Windows/Linux/macOS
static/shared and Linux ASan/UBSan passed in the
[PR CI run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37444942542).
Check the actual final PR head after documentation updates too. CI validates
portable workflow/codec behavior and Windows synthetic storage, not hardware
interoperability or other OS store adapters.

Actual PIN display/entry, authenticated enrollment, Windows application credential
save/reload and built-in fresh-socket verification passed on Living Room / Apple
TV 4K / tvOS 26.6 / Windows 11 x64 after the M6 type-17 metadata fix, as reported
by the user. Separate-process `verify` also passed with exit code 0.
Physical-console echo/mode restoration, host/receiver restart, wrong PIN and
revocation remain pending. Record the exact
build/access policy and additional results in
[receiver-validation.md](receiver-validation.md).
Media serving, the file adapter and private URL session/`cast` are now implemented.
The minimum native remote session passed G1; MRP controls are the next gate; see
[session-design.md](session-design.md). Restart/revocation and other host stores
remain separate authentication gates.

## Source and dependency provenance

All new adapters/code are original Apache-2.0 project code. No third-party
implementation was copied and no runtime dependency was added. Windows uses
the OS `Advapi32` credential/token APIs and kernel/console APIs; macOS uses the
system Security and CoreFoundation frameworks (D58); Linux uses the system
libsecret when present (D59); crypto remains OpenSSL/Botan as recorded in
[dependencies.md](dependencies.md).
Primary contracts consulted:

- [CREDENTIALW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/ns-wincred-credentialw)
  and [CredWriteW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credwritew).
- [CredReadW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credreadw)
  and [CredDeleteW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-creddeletew).
- [Console modes](https://learn.microsoft.com/en-us/windows/console/setconsolemode)
  and [low-level input](https://learn.microsoft.com/en-us/windows/console/low-level-console-input-functions).
- [Kernel object namespaces](https://learn.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces).
- [TN3137: On Mac keychain APIs and implementations](https://developer.apple.com/documentation/technotes/tn3137-on-mac-keychains),
  [SecItemAdd](https://developer.apple.com/documentation/security/secitemadd(_:_:)),
  [errSecDuplicateItem](https://developer.apple.com/documentation/security/errsecduplicateitem)
  and [kSecUseDataProtectionKeychain](https://developer.apple.com/documentation/security/ksecusedataprotectionkeychain)
  (read 2026-10-08 through Apple's documentation JSON).
- libsecret reference: [secret_password_store_binary_sync](https://gnome.pages.gitlab.gnome.org/libsecret/func.password_store_binary_sync.html),
  [secret_password_store_sync](https://gnome.pages.gitlab.gnome.org/libsecret/func.password_store_sync.html)
  (matching items are updated),
  [secret_password_lookup_binary_sync](https://gnome.pages.gitlab.gnome.org/libsecret/func.password_lookup_binary_sync.html),
  [secret_password_clear_sync](https://gnome.pages.gitlab.gnome.org/libsecret/func.password_clear_sync.html)
  and [SecretSchema](https://gnome.pages.gitlab.gnome.org/libsecret/struct.Schema.html)
  (read 2026-10-08).
