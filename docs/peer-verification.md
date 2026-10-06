# Authenticated peer verification

This slice implements a private HAP pair-verify state machine for **existing,
trusted credentials**, on `codex/peer-verification`, based on merged PR #3 at
`360d74661e7cc703a82b7852f425215189217ad3`. It builds on the TLV8, HKDF/AEAD and
control-record codecs. No receiver I/O, first-time pairing or playback is enabled.
See [provenance](dependencies.md) and [transport contracts](pairing-transport.md).

## Trust and ownership

`PairCredentials` owns a controller Ed25519 signing-seed copy and pins the receiver
ID/public key. Those values must come from an authenticated provisioning flow or
trusted host storage, never mDNS or a new handshake. IDs are opaque nonempty bytes,
at most 64 bytes; the cap is an implementation policy, not a universal receiver
constraint. This slice does not define credential serialization or storage.

`Secret32`, credentials and the verifier prohibit copying/moving. Their owned
secrets are erased at destruction; the verifier clears secrets on terminal failure,
explicit close, and successful one-time key release. Callers erase their source
copies and released keys after creating record owners. OpenSSL handles key objects
with RAII; no locked-memory/all-process erasure guarantee is made.

Each verifier borrows credentials that must outlive it and generates a fresh
X25519 seed with OpenSSL private randomness. Use serially on one connection.
Retries create a new verifier; closing/restarting cannot reuse the old exchange.
No deterministic ephemeral seed is accepted by its ordinary API. Test-only friend
access sets a public synthetic seed and inspects erasure.

## State and wire contract

1. `start()` emits M1: state 1 and the controller X25519 public key.
2. `respond(200, m2)` requires state 2, a 32-byte receiver ephemeral key, and
   16..512 encrypted bytes. X25519 low-order/all-zero shared secrets are rejected.
   HKDF-SHA512 with `Pair-Verify-Encrypt-Salt`/`Pair-Verify-Encrypt-Info` yields the
   encryption key; M2 uses nonce four zeros + `PV-Msg02`, with empty AAD.
3. M2 plaintext must contain the pinned receiver ID and a 64-byte Ed25519 signature
   over receiver ephemeral key + receiver ID + controller ephemeral key. Verify
   this before signing or returning M3. M3 signs controller ephemeral key +
   controller ID + receiver ephemeral key and encrypts it with `PV-Msg03`.
4. `finish(200, m4)` requires state 4 and no error. M4 is an acknowledgement, not
   another signed proof. Only then derive the sender's outbound/inbound keys with
   `Control-Salt` and `Control-Write-Encryption-Key`/`Control-Read-Encryption-Key`.
5. `take_control_keys(write, read)` transfers keys into distinct caller-owned
   secret buffers once and closes/erases the exchange. Outputs stay unchanged on
   failure. A connection must install both directional record owners together.

Responses are complete TLV bodies bounded at 1,024 bytes. The generic TLV fragment
rules still apply. This state machine additionally rejects missing fields,
nonadjacent duplicates, unexpected/unknown tags, separators, wrong states/lengths
and malformed errors. Error tag 7 must be one nonzero byte and causes terminal
peer rejection (including a receiver reporting revoked credentials). This strict
schema is a candidate receiver path; future extensions require explicit tests.

Non-200 responses, authentication/backend errors, malformed messages and method
order errors are terminal. Errors contain categories only. Ephemeral and handshake
encryption keys are erased after M3; the shared secret is erased after deriving
both directions; control keys remain private until one-time release.

The future HTTP/socket layer must correlate responses on the same connection,
enforce status/body limits before buffering, handle partial writes exactly once,
and call `close()` on timeout, cancellation, EOF or disconnect. There is no
plaintext fallback, HTTP parser, network deadline implementation or public pairing
API here. A caller-supplied M4 is not proof that the Apple TV accepted our M3.

## Validation and next gate

`pair_verify_tests` adds an eighth CTest target. RFC 7748/8032 numeric vectors check
X25519 and pure Ed25519. Separately generated Python wire/HMAC fixtures check M1,
M3, read/write key direction and encrypted control-record handoff at every split.
Validly encrypted negative transcripts test ID/signature/order/schema failures.
Tests cover key bounds, noncanonical signatures, low-order peers, HTTP/receiver
rejection, early/aliased/repeated key release, cancellation at every phase, replay,
every truncated/altered M2 byte and 1,000 deterministic random M2 bodies.
See [fixture provenance](../tests/fixtures/README.md).

Windows 11 x64 / MSVC 19.51 static/shared Release builds pass all eight CTest
targets. clang-format dry-run and `git diff --check` pass, and independent fixture
regeneration reproduces the committed bytes. At implementation commit
`7b572a7b24d7242200e0cb1321c366a83932b3da`, all six Windows/Linux/macOS static/shared
jobs and Linux ASan/UBSan passed in the
[PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37427549046) and
[push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37427502668).
Inspect checks for the actual final [PR #4](https://github.com/ilyalissoboi/send-airplay2/pull/4)
head before merging, including documentation-only updates.
No Apple TV 4K / tvOS 26.6 pairing or playback operation was attempted; hardware
revocation, reconnect and interoperability remain NOT RUN.

Next implement first-time PIN/SRP provisioning with a vetted backend and verified
server proofs/signatures, then credential storage and bounded HTTP/socket
transport. Establish the pyatv hardware playback baseline and native authenticated
connection before enabling URL playback. Wrong-PIN validation is pending because
this slice consumes existing credentials and does not implement a PIN flow.

Subsequent development implements private [PIN/SRP provisioning](pin-pairing.md)
with Botan and verified server proofs/signatures. Synthetic wrong-PIN tests now
exist there; receiver validation, trusted storage and bounded HTTP/socket I/O
remain pending.
The shared Ed25519 verification adapter now rejects noncanonical encodings,
identity points and keys outside the prime-order subgroup before accepting a
signature. Botan supplies subgroup validation; OpenSSL alone can accept trivial
forgeries under weak keys. This applies to pinned peer verification as well as
new PIN enrollment; see the weak-key regression fixtures in pin-pairing.md.
