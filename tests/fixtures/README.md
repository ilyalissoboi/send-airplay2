# Authentication and control-transport fixtures

All keys and payloads here are public, synthetic test material. These are not
captures or pairing credentials, and do not prove receiver interoperability.

`rfc8439-aead.hex` is the ciphertext/tag from RFC 8439 section 2.8.2; the C++ test
provides that section's key, nonce, AAD and plaintext. Numeric vector data is
attributed here and in the test. No RFC implementation source is incorporated.

`control-wire.hex` uses the independent Python `cryptography` ChaCha20Poly1305
implementation. Key bytes are 0..31, plaintext is 1,030 bytes `i % 251`, split
at 1,024 bytes. Nonces are four zeros followed by the 64-bit little-endian record
index. The two-byte little-endian plaintext length is authenticated as AAD.
Both records must match exactly, including the second record's counter.

`control-{read,write}-key.hex` uses independent Python `hmac`/`hashlib` SHA512
extract/expand, with public secret 0..31 and the named control salt/info values.
`hkdf-empty-key.hex` verifies explicitly empty secret/salt/info using the same
HMAC oracle, including the RFC 5869 default salt behavior.

Regenerate with `python tests/fixtures/generate_control.py` in an environment
with `cryptography`. The checked-in results are consumed by CTest; Python and
cryptography are not build/runtime/test dependencies. Record generator versions
when regenerating; initial generation used Python 3.12.14 / cryptography 50.0.1.

## Pair verification

`pair-verify/*.hex` is generated separately by `generate_pair_verify.py`; it
imports no production module and assembles TLV and signing transcripts directly.
Client Ed25519 seed bytes are 0..31, receiver seed 32..63, client X25519 seed
64..95 and receiver X25519 seed 96..127. IDs are `synthetic-controller` and
`synthetic-receiver`; they are not a capture or real credentials.

M1 and M3 are exact sender output oracles. M2 includes the receiver signature
over receiver ephemeral public key + receiver ID + client ephemeral public key.
M3 signs the reverse key order with the controller ID. HKDF-SHA512 uses separate
Python HMAC extract/expand; named ChaCha20-Poly1305 nonces are four zeros +
`PV-Msg02`/`PV-Msg03`, with empty AAD. The generator verifies both signatures.
Read/write keys and the first control records independently fix direction,
length-AAD framing and counter zero. M4 is the literal state acknowledgement.

Negative M2 fixtures are **validly encrypted** but contain a changed signature,
wrong signed ID/transcript, duplicate/missing ID, short signature, malformed
inner TLV or unknown inner tag. They ensure tests exercise identity/schema
verification beyond AEAD tag checking. Python cryptography can share an OpenSSL
backend; independence refers to transcript/encoding/HKDF construction. RFC 7748
and RFC 8032 numeric known answers in the C++ test check the primitive adapters.

Regenerate with `python tests/fixtures/generate_pair_verify.py` using Python
3.12.14 / cryptography 50.0.1 (initial generation). Committed fixtures run without
Python. Existing control fixtures are unchanged.

## PIN pairing

`pair-setup/*.hex` comes from `generate_pair_setup.py`, using Python integer
arithmetic for SRP-6a and hashlib/HMAC for SHA512/HKDF, independent of Botan.
RFC 5054 Appendix A supplies the 3072-bit group (generator 5). PIN `0123`, salt
0..15, controller seed 0..31 and accessory seed 32..63 are public synthetic data.
The test-only client RNG supplies 0x42; Botan 3.12 sets the exponent's high bit.
The independent server exponent is selected to yield a leading-zero shared
integer, proving HAP minimal-integer hashing rather than hashing padded S.

M1/M3/M5 are exact sender-byte oracles; M4 includes the independently calculated
server proof. M6 authenticates the accessory ID/key; negative M6 fixtures are
validly encrypted, isolating signature and inner-schema failures from AEAD.
New credentials use the same identities as the peer-verification fixtures, so
an enrollment-to-verification test independently checks their retained seed/key.
`weak-identity-m6` is validly encrypted and carries a trivial signature accepted
by the primitive with the identity public key. Enrollment must reject that key
before it becomes a reusable credential. Numeric identity, sign-alias, field
boundary and small-order point tests also exercise the shared verification adapter.
The AEAD/Ed25519 generator uses cryptography and may share an OpenSSL backend;
existing RFC tests cover primitives. No production module is imported.

Regenerate with `python tests/fixtures/generate_pair_setup.py` (Python 3.12.14 /
cryptography 50.0.1). Python is not needed to run committed fixtures.

The M6 metadata variants are original public synthetic payloads. Only the type
17 and 159-byte length come from sanitized live Apple TV 4K / tvOS 26.6 error
headers; no receiver metadata contents, identifiers, keys or capture are included.
`metadata-m6` repeats a public marker; `empty-metadata-m6` covers zero bytes,
`max-metadata-m6` covers the 256-byte fragmented bound, and
`oversized-metadata-m6` is one byte beyond it. `duplicate-metadata-m6` separates
two type-17 values with the signature field. `bad-signature-metadata-m6` is
validly encrypted but tampered in the accessory signature. All other fixtures
remain byte-for-byte unchanged. Acceptance must retain the original identity/key
and the independent credential-reload/peer-verification/control-key oracle.
