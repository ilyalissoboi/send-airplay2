# TLV8 and authenticated records

Private protocol codecs live in `src/pairing_tlv*`, `control_crypto*` and
`control_records*`. They are composed by the pairing and receiver connection
layers; applications use the public [pairing API](public-api.md).

## TLV8

Each field is a one-byte type, one-byte length and that many value bytes.
Values larger than 255 bytes use adjacent fragments of the same type. A
continuation requires a preceding 255-byte fragment; an empty fragment cannot
terminate it. Independent equal types need another field or `ff 00` separator
between them. Unknown types and nonadjacent repeats remain visible to the
message-specific schema validator.

The codec rejects truncated fields, nonempty separators, ambiguous repeats and
limit violations without returning partial fields. Limits are 64 KiB per body,
4 KiB per logical value and 64 fields. Pairing state machines impose smaller
message-specific limits and reject duplicate trust-bearing fields.

## Encrypted records

A control record contains a two-byte little-endian plaintext length, ciphertext
and a 16-byte ChaCha20-Poly1305 tag. The length bytes are authenticated as AAD.
The nonce is four zero bytes followed by the direction's 64-bit little-endian
counter. HKDF-SHA512 derives independent 32-byte directional keys after a verified
handshake.

Outgoing chunks are at most 1024 plaintext bytes. Control/event readers use that
default; the MRP data channel has a separate bounded 16-KiB incoming record
allowance. Never broaden one channel implicitly because another needs it.

Writer/reader owners are serial, noncopyable and nonmovable. They start at counter
zero only with fresh keys. `UINT64_MAX` is reserved; exhaustion fails before
wraparound. An empty write emits nothing; an authenticated empty incoming record
still consumes a nonce. Partial socket writes retain ciphertext rather than
encrypting it again.

Calls accept at most 64 KiB of input. Readers support split/coalesced records,
retain bounded incomplete input and release plaintext only after authentication.
If a later record in one call fails, that call returns no plaintext; earlier
successful calls are already committed. Incomplete EOF, bad tags, replay,
allocation/backend failure or invalid length permanently closes that direction.
The connection must close both directions and reconnect with fresh verification.

Owned keys and transient plaintext are cleared on failure/destruction. Caller-held
buffers remain the caller's responsibility. There is no plaintext fallback.
Independent fixture generators and RFC vectors are described in
[tests/fixtures/README.md](../tests/fixtures/README.md); provenance is in
[dependencies.md](dependencies.md).
