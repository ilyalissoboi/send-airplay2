# Built-in credential storage

Built-in stores persist authenticated pairing records. They never trust
discovery names, addresses or advertised keys as replacement identities.
Applications can instead use the [host-store interface](credential-interface.md).

## Record and profile contract

The version-1 envelope has exact-length encoding and at most 203 bytes:

| Offset | Bytes | Meaning |
| --- | ---: | --- |
| 0 | 8 | ASCII `SAP2CRED` |
| 8 | 1 | Version 1 |
| 9 | 1 | Receiver ID length, 1..64 |
| 10 | 1 | Controller ID length, 1..64 |
| 11 | 32 | Pinned receiver Ed25519 public key |
| 43 | 32 | Controller Ed25519 signing seed |
| 75 | Variable | Receiver ID followed by controller ID |

Unknown versions, invalid lengths and weak/noncanonical receiver keys refuse.
The envelope is not independently encrypted or MACed: only trusted secure storage
may persist it. Secret owners clear their buffers, without promising process-wide
erasure. Profiles are lowercase 1..64-character trust-slot names as defined in
[credentials.h](../include/send_airplay2/credentials.h).

## Platform adapters

| Platform | Store | Availability |
| --- | --- | --- |
| Windows desktop | Generic Credential Manager entry `send-airplay2/v1/PROFILE` | Current user, local-machine persistence; excluded from UWP |
| macOS | Generic password in default/login keychain, service `send-airplay2/v1`, account profile | Security/CoreFoundation frameworks |
| Linux | Secret Service schema `org.send-airplay2.Credential` | Requires libsecret, session D-Bus and usable unlocked keyring |
| Other / packaged hosts | Host callbacks | No built-in plaintext fallback |

Windows create-only saves use a bounded named mutex, then check absence before
`CredWrite`. macOS `SecItemAdd` supplies atomic duplicate refusal. Linux uses a
per-user lock in `XDG_RUNTIME_DIR`, then checks absence before storing because
libsecret's ordinary store operation replaces matches. Missing/unusable runtime
storage reports unavailable, not a fabricated absence. Malformed slots stay occupied.

The macOS adapter uses the default file-based keychain so ordinary unsigned
hosts do not need access-group entitlements. A host needing the data protection
keychain supplies its own adapter. Reads by another/rebuilt binary can trigger
OS access prompts. Linux locked collections are not treated as absent. OS store
calls are synchronous and not cancellable; other same-user processes remain
inside the platform trust boundary.

Synthetic tests use separate namespaces and random profiles, and clean up their
own entries. CI exercises keychain and unlocked Secret Service adapters, not
every desktop prompt, provider or receiver pairing scenario.

## CLI

```powershell
build/Release/airplay2-cli.exe pair --address 192.0.2.10 --profile living-room
build/Release/airplay2-cli.exe verify --address 192.0.2.10 --profile living-room
build/Release/airplay2-cli.exe forget --profile living-room
```

Pairing requires the CLI's interactive Windows console. PINs are 4..8 digits,
including leading zeros; they are neither echoed nor accepted through arguments,
environment variables or redirected stdin. Escape/Ctrl+C cancels. Network and
PIN deadlines are separately bounded. A successful save is followed by reload
and fresh verification. Forget is idempotent local removal, not receiver revocation.

References: [CREDENTIALW](https://learn.microsoft.com/en-us/windows/win32/api/wincred/ns-wincred-credentialw),
[Apple TN3137](https://developer.apple.com/documentation/technotes/tn3137-on-mac-keychains),
[libsecret](https://gnome.pages.gitlab.gnome.org/libsecret/).
