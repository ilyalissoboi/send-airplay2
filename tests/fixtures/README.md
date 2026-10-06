# Control-transport fixtures

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
