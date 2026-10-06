# Private PIN pairing

This slice implements persistent HAP pair-setup in private C++17 code. It does
not connect to a receiver, request/display a PIN, persist credentials or expose a
public pairing ABI. Apple TV 4K / tvOS 26.6 interoperability remains untested.

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
There is no extension negotiation in this private profile.

Objects are non-copyable/non-movable and require serial use. Every method
exception is terminal. `close` handles cancellation, timeout and disconnect;
retry requires a fresh object. Owned controller seeds, SRP keys/proofs, derived
keys and secret signing transcripts are erased on success/failure/destruction.
Botan owns its internal SRP temporaries. Caller-owned PINs, returned messages and
credential owners remain the caller's responsibility; process-wide erasure is
not guaranteed. Errors expose sanitized categories without identities or secrets.

## Validation and remaining gates

`pair_setup_tests` uses committed Python integer/SHA512/HMAC transcript fixtures,
independent of Botan arithmetic, with a leading-zero shared integer. It checks
exact M1/M3/M5, wrong PIN, every server-proof byte and M6 ciphertext byte,
validly encrypted bad signatures/IDs/keys, schemas, bounds, cancellation,
weak public-key/trivial-forgery rejection,
single-use release and secret cleanup. Newly enrolled synthetic credentials also
complete the existing peer-verification oracle and derive expected control keys.
Deterministic randomness injection is test-only; no production seed API exists.
See [fixtures](../tests/fixtures/README.md) and [dependency provenance](dependencies.md).

Windows 11 x64 / MSVC 19.51 static/shared Release passes all nine CTest targets;
the full hardened Debug PIN suite passes in 94.70 seconds. At source commit
`4c7dcc4f6af6f5ebc391097dcc0998262436f761`, all six platform/static/shared jobs
and Linux ASan/UBSan passed in the
[PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37436328684).
Check the actual final PR head as documentation updates also trigger CI.

Subsequent [receiver transport](receiver-transport.md) implements bounded
HTTP/socket correlation, partial writes, deadlines and cancellation privately.
Subsequent [credential storage/CLI authentication](credential-storage.md) implements
the private codec and Windows desktop store plus hidden PIN/reconnect commands.
Next perform explicit receiver PIN/reconnect/revocation tests. UWP support is a packaging gate because the
current vcpkg Botan port excludes that triplet. Public API, packaged Windows and
Android loading, reference playback and native playback remain pending.
