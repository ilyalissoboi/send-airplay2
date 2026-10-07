# Private PIN pairing

This slice implements persistent HAP pair-setup in private C++17 code. It does
not connect to a receiver, request/display a PIN, persist credentials or expose a
public pairing ABI. The Windows CLI now connects this flow to receiver I/O and
storage. Live Apple TV 4K / tvOS 26.6 enrollment exposed an M6 metadata schema
failure; the user confirmed pairing and built-in fresh-socket verification with
the compatibility fix. See
[receiver observations](receiver-validation.md) for the current hardware gate.

## Authentication and ownership

`PairSetup` generates a fresh Ed25519 controller seed. The caller supplies a
unique opaque controller ID, handles `/pair-pin-start`, then correlates complete
HTTP `/pair-setup` responses on one connection. Only HTTP 200 with the expected
state and no peer error can advance the exchange. Discovery advertisements are
not an identity trust source. No transient-pairing or plaintext fallback exists.

| Call | Input | Output and trust gate |
|---|---|---|
| `start` | Fresh object | M1: method 0, state 1 |
| `respond` | M2, PIN | M3: client SRP public value and proof |
| `confirm` | M4 | Verify server SRP proof before emitting encrypted/signed M5 |
| `finish` | M6 | Verify AEAD and accessory Ed25519 signature; release credentials once |

SRP-6a uses the RFC 5054 3072-bit group, generator 5, SHA512 and username
`Pair-Setup`. Botan performs SRP arithmetic and draws fresh system randomness.
The adapter hashes the minimal big-endian shared integer and assembles HAP
proofs, with constant-time server-proof comparison. Botan pads the public values
for the SRP multiplier/scrambling hashes. PINs are 4..8 ASCII digits; leading
zeros are preserved. The fixed profile rejects salts other than 16 bytes,
empty/oversized public values, B outside 1..N-1 and a zero shared integer.
These constraints are an implementation profile, not firmware-tested claims.

HKDF-SHA512 derives separate encryption/controller/accessory signing keys.
M5/M6 use ChaCha20-Poly1305 with four zero bytes plus `PS-Msg05`/`PS-Msg06`
and empty AAD. Each signature authenticates the derived signing prefix, opaque
identifier and Ed25519 public key in that order. Public keys must be canonical,
non-identity points in the prime-order subgroup, checked through Botan before
OpenSSL signature verification. M6 must authenticate before
any receiver ID/key is admitted to `PairCredentials`; those credentials can
then be used by the existing pinned-identity peer verifier.

Bodies are bounded to 2,048 bytes; encrypted M6 to 512 bytes; identifiers to
1..64 bytes. Fragmented TLV values are supported, but duplicate fields,
separators, unknown tags, missing fields and wrong fixed lengths are rejected.
The one recognized optional M6 identity field is type 17 (receiver metadata),
bounded to 0..256 bytes after fragment reassembly. Its content is opaque: it is
neither parsed nor used for trust, profile naming or stored credentials. It is
discarded and erased with the decrypted identity buffer and decoded field copies
on success/failure. ID, key and signature remain mandatory, and duplicate metadata
or any other unknown tag is rejected. The 512-byte encrypted bound still applies.
There is no general extension negotiation in this private profile.

Objects are non-copyable/non-movable and require serial use. Every method
exception is terminal. `close` handles cancellation, timeout and disconnect;
retry requires a fresh object. Owned controller seeds, SRP keys/proofs, derived
keys and secret signing transcripts are erased on success/failure/destruction.
Botan owns its internal SRP temporaries. Caller-owned PINs, returned messages and
credential owners remain the caller's responsibility; process-wide erasure is
not guaranteed. Response/schema errors add the M2/M4/M6 phase, HTTP status, body
size and bounded wire TLV types/lengths, without identities or secrets. Only valid
outer state/error codes may be printed; decrypted identity diagnostics report
headers only (`source=identity`). Malformed/over-limit input is summarized without
repairing it, copying its values or changing acceptance rules.

## Validation and remaining gates

`pair_setup_tests` uses committed Python integer/SHA512/HMAC transcript fixtures,
independent of Botan arithmetic, with a leading-zero shared integer. It checks
exact M1/M3/M5, wrong PIN, every server-proof byte and M6 ciphertext byte,
validly encrypted bad signatures/IDs/keys, schemas, bounds, cancellation,
weak public-key/trivial-forgery rejection,
single-use release and secret cleanup. Newly enrolled synthetic credentials also
complete the existing peer-verification oracle and derive expected control keys.
Optional metadata fixtures cover absent, empty, observed-size (159 bytes),
fragmented maximum (256 bytes), over-limit (257 bytes), duplicates and bad
signatures. Successful variants serialize/reload credentials and satisfy the
same independent peer-verification/control-key oracle. Diagnostics tests cover
each phase, malformed/over-limit bodies and payload/PIN suppression.
Deterministic randomness injection is test-only; no production seed API exists.
See [fixtures](../tests/fixtures/README.md) and [dependency provenance](dependencies.md).

The original PIN slice passed all nine then-existing CTest targets in Windows
11 x64 / MSVC 19.51 static/shared Release; its full hardened Debug PIN suite
passed in 94.70 seconds. At source commit
`4c7dcc4f6af6f5ebc391097dcc0998262436f761`, all six platform/static/shared jobs
and Linux ASan/UBSan passed in the
[PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37436328684).
Check the actual final PR head as documentation updates also trigger CI.

For the M6 metadata fix at source commit
`f61698fa930893c139efcb5f5d2b0a40a93d0cae`, Windows Release static/shared each
passed all 12 CTests. Windows/Linux/macOS static/shared and Linux ASan/UBSan
passed in the [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37450506346)
and [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37450491552).
CI is codec/workflow evidence; live receiver results are recorded separately.

Subsequent [receiver transport](receiver-transport.md) implements bounded
HTTP/socket correlation, partial writes, deadlines and cancellation privately.
Subsequent [credential storage/CLI authentication](credential-storage.md) implements
the private codec and Windows desktop store plus hidden PIN/reconnect commands.
Live PIN enrollment and separate-process verification passed after the M6
metadata fix; see the receiver record. Next assess restart, wrong PIN and revocation.
UWP support is a packaging gate because the
current vcpkg Botan port excludes that triplet. Public API, packaged Windows and
Android loading remain pending. Reference playback passed with the unmerged
pyatv fix; native-only G1 passed with the minimum remote session. See receiver-validation.md.
