# Peer verification

`PairVerify` reconnects using previously authenticated credentials. It does not
enroll a new identity or trust mDNS metadata. `PairCredentials` pins the receiver
ID/public key and owns the controller signing seed.

## Wire contract

1. A fresh verifier generates an X25519 seed and emits M1 with its public key.
2. HTTP 200/state M2 must carry a 32-byte receiver ephemeral key and 16..512
   encrypted bytes. Reject low-order/all-zero X25519 secrets. Derive the handshake
   encryption key with `Pair-Verify-Encrypt-Salt`/`Pair-Verify-Encrypt-Info`;
   M2 uses four zero nonce bytes plus `PV-Msg02`, with empty AAD.
3. Authenticate the pinned receiver ID and signature over receiver ephemeral
   key + receiver ID + controller ephemeral key before emitting M3. M3 signs
   controller ephemeral key + controller ID + receiver ephemeral key and uses
   nonce `PV-Msg03` after the same four zero bytes.
4. Require HTTP 200/state M4 with no error before releasing directional keys.
   M4 is an acknowledgement, not an independent signature. The connection must
   correlate responses and reject unexpected trailing input at the key transition.
5. `Control-Salt` and the Write/Read encryption info labels derive the sender's
   outbound/inbound keys. Install both directions at counter zero together.

Complete responses are bounded to 1024 bytes. Schema violations, peer errors,
authentication/backend failure and invalid call order are terminal. Key release
is single-use. A session-key release can move the shared secret into a private
`ChannelKeySource` for event/data-channel derivation without exposing it.

## Ownership

Credentials must outlive their verifier. Secret/credential/verifier owners are
noncopyable/nonmovable and clear owned secrets on failure, close and destruction.
Handshake encryption/ephemeral keys are cleared after M3; the shared secret is
erased or moved at successful release. Outputs stay unchanged on failure.

Every reconnect uses fresh randomness and a new verifier. Never reuse record
keys after a broken connection or replace pinned identity with advertised values.
The [receiver transport](receiver-transport.md) enforces deadlines, cancellation,
response framing and connection closure around this state machine.

RFC 7748/8032 and independently authored synthetic transcripts provide the test
oracles. See [fixtures](../tests/fixtures/README.md) and
[provenance](dependencies.md).
