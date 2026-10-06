# Pairing and control-transport foundation

Implemented on `codex/pairing-transport`, based on merged discovery PR #2 at
`02c953a22c9503f1bf17bc14d6814e797cb04a1b`. This slice adds private codecs and
cryptographic adapters. It performs no receiver I/O and does not pair or cast.
See [dependency provenance](dependencies.md) before changing the crypto backend.

## Pairing messages

`pairing_tlv` encodes/decodes complete TLV8 bodies: one-byte type, one-byte length,
then length bytes. Values above 255 bytes are split into adjacent fragments.
Continuation requires a preceding 255-byte fragment and the same adjacent type.
An empty fragment cannot terminate a continuation. Independent equal types must
be separated by another type or an explicit `ff 00` separator. Unknown types and
nonadjacent repeats remain visible; the future state machine must validate its
schema and reject duplicate state/proof/identity fields. This is a deliberately
strict codec policy, not a claim about every receiver's accepted encodings.

Parsing rejects truncated headers/values, nonempty separators, ambiguous repeats,
and limit violations without returning partial fields. Limits are 64 KiB per
body, 4 KiB per logical value and 64 logical fields. Encoding enforces the same
bounds and preserves field order. This is not an HTTP parser or nested TLV schema;
the future request layer must enforce body size/time limits before buffering.

## Authenticated control records

Each record has two bytes of little-endian plaintext length, 0..1024 encrypted
plaintext bytes, and a 16-byte ChaCha20-Poly1305 authentication tag. The exact
length bytes are additional authenticated data. The 12-byte nonce is four zero
bytes followed by an independent 64-bit little-endian counter for each direction.
HKDF-SHA512 derives 32-byte keys; the salt/info selection belongs to the verified
pairing path. Fixtures exercise the researched Control-Salt/Read/Write labels;
the library does not yet negotiate or provision keys.

`ControlWriter` and `ControlReader` own one direction each, cannot be copied or
moved, and are used serially by one connection. They start at counter zero with
fresh directional keys from an authenticated handshake. Do not reconstruct them
with an old key after failure/reconnect or share a key between directions.
`UINT64_MAX` is reserved: exhaustion closes the direction before wraparound.
An empty writer call emits nothing; an authenticated zero-length inbound record
is accepted and consumes one nonce. There is no plaintext fallback.

Each call accepts at most 64 KiB of input. The writer emits as many bounded records
as needed. The reader retains at most one incomplete record (1,042 bytes), supports
fragmented/coalesced reads, and returns plaintext only after authentication.
If a later record in a call fails, that call returns no plaintext. Data returned
by earlier successful calls is already committed. EOF with incomplete data is an
error. Malformed lengths, bad tags/replay, exhaustion, allocation or backend
exceptions permanently close that direction and wipe its owned key/buffer.
The connection layer must close both directions/socket on such a failure.

OpenSSL contexts use RAII. Unauthenticated transient plaintext is wiped on every
exit, including allocation errors, and aggregate plaintext is wiped on failure.
Caller-provided/returned key/plaintext buffers remain the caller's responsibility;
this does not guarantee erasure of all process memory, registers or host copies.
Errors expose categories only, without raw OpenSSL errors or credential bytes.

These codecs do not provide sockets, HTTP/RTSP parsing, deadlines, cancellation,
PIN/SRP proof validation, Ed25519 peer identity verification, X25519 handshake,
credential persistence, revocation handling or a public pairing API. Those remain
explicit gates before enabling a control connection. The socket layer must send
each encrypted byte exactly once in order, retaining bytes after partial writes.

## Verification and next gate

CTest adds `control_tests` to the existing six targets. It checks an RFC 8439 AEAD
known answer, independent HKDF/control-wire fixtures, record size boundaries,
every two-part split and byte-at-a-time reads, malformed lengths, wrong keys,
AAD/nonces/tags, replay/out-of-order records, truncated EOF, terminal failure,
key/buffer cleanup and counter boundaries. TLV tests cover literals, fragmentation,
separators, repeats, limits and 3,000 deterministic parser mutations. Test-only
counter access is outside public headers. See `tests/fixtures/README.md` for oracle
generation; Python is unnecessary to run the committed fixtures.

Local Windows 11 x64 / MSVC 19.51 static and shared Release builds passed all seven
CTest targets against OpenSSL 3.6.5; clang-format and `git diff --check` passed.
Cross-platform/sanitizer CI results must be verified for the published PR head.

Next: implement the PIN pairing/peer-verification state machine using vetted
SRP/signature/key-agreement primitives, credential ownership/storage adapters and
bounded request/response transport. Add independent handshake transcripts and
wrong-PIN/signature/revocation tests, then validate the exact receiver path and a
pyatv playback baseline on the Apple TV 4K / tvOS 26.6 / Windows 11 x64 setup.
No receiver pairing or playback was attempted in this foundation slice.
