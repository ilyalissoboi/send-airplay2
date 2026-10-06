# Credential storage and authentication CLI

Implemented as private host code on Windows desktop. The library's public ABI
does not expose signing seeds or credential serialization. Linux/macOS builds
exercise the portable codec/workflow but native storage reports unsupported;
Android and packaged Windows/UWP storage/loading are unvalidated. This slice
does not implement playback or establish Apple TV compatibility.

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
failure. Authentication diagnostics use sanitized categories; no PIN, keys,
credential envelope or raw transcript is printed.

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

Actual interactive PIN entry, console echo/mode restoration on a physical
terminal, Apple TV first pairing, restart reconnect, wrong PIN and revocation
remain NOT RUN. Complete these on Living Room / Apple TV 4K / tvOS 26.6 and record
the exact build/access policy in [receiver-validation.md](receiver-validation.md).
The next implementation slice is bounded media serving once the authentication
gate is assessed; authenticated playback/session setup remains a separate step.

## Source and dependency provenance

All new adapters/code are original Apache-2.0 project code. No third-party
implementation was copied and no runtime dependency was added. Windows uses
the OS `Advapi32` credential/token APIs and kernel/console APIs; crypto remains
OpenSSL/Botan as recorded in [dependencies.md](dependencies.md).
Primary contracts consulted:

- [CREDENTIALW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/ns-wincred-credentialw)
  and [CredWriteW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credwritew).
- [CredReadW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-credreadw)
  and [CredDeleteW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/nf-wincred-creddeletew).
- [Console modes](https://learn.microsoft.com/en-us/windows/console/setconsolemode)
  and [low-level input](https://learn.microsoft.com/en-us/windows/console/low-level-console-input-functions).
- [Kernel object namespaces](https://learn.microsoft.com/en-us/windows/win32/termserv/kernel-object-namespaces).
