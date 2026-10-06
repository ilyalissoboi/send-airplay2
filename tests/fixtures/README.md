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
