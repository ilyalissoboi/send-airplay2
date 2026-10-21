# PIN pairing internals

`PairSetup` implements persistent HAP pair-setup. `ReceiverConnection` supplies
receiver I/O and the authentication workflow supplies PIN input and secure
storage. Public callers use [pairing.h](../include/send_airplay2/pairing.h).

## Exchange and trust

| Step | Required transition |
| --- | --- |
| Start | Fresh controller Ed25519 seed/opaque ID; send M1 method 0, state 1 |
| Respond | HTTP 200/state M2 and PIN produce SRP public value/proof in M3 |
| Confirm | Authenticate HTTP 200/state M4 server proof before signed/encrypted M5 |
| Finish | Authenticate HTTP 200/state M6 AEAD and accessory signature before releasing credentials once |

SRP-6a uses the RFC 5054 3072-bit group, generator 5, SHA512 and username
`Pair-Setup`. Botan provides arithmetic and system randomness. The adapter hashes
the minimal big-endian shared integer and verifies server proofs in constant
time. PINs are 4..8 ASCII digits with leading zeros preserved. The finite profile
requires a 16-byte salt, valid bounded public values and nonzero shared secret.

HKDF-SHA512 derives separate encryption/controller/accessory signing keys.
M5/M6 use ChaCha20-Poly1305 nonces four zeros plus `PS-Msg05`/`PS-Msg06`, empty
AAD, and signatures over derived prefix + opaque ID + Ed25519 public key.
Keys must be canonical, non-identity prime-order points before signature
verification. Discovery data is never a trust source.

## Bounds and lifetime

Bodies are at most 2048 bytes; encrypted M6 at most 512; identifiers 1..64.
Schemas reject duplicates, missing fields, wrong lengths/states and unknown tags.
M6 permits one optional opaque type-17 metadata field up to 256 bytes. It is
discarded, never used for identity or stored credentials; ID/key/signature remain
mandatory. This does not introduce general extension tolerance.

Objects are noncopyable/nonmovable and serial. Any exception is terminal;
cancellation, timeout or disconnect closes the exchange. Retry requires a fresh
object. Owned seeds, proofs, derived keys and secret transcripts are cleared on
all exits; callers clear their PINs and returned credentials. Diagnostic fields
are fixed categories and bounded field types/lengths, never plaintext values.

`pair_setup_tests` uses independent Python integer/hash transcript fixtures,
including a leading-zero shared integer, wrong PINs, proof/ciphertext mutation,
weak keys, metadata bounds and one-time release. See
[fixture provenance](../tests/fixtures/README.md) and
[dependency provenance](dependencies.md).
