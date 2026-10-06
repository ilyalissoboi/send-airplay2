# SPDX-License-Identifier: Apache-2.0
"""Public synthetic HAP pair-setup oracles, independent of production code.

SRP uses Python integer arithmetic and SHA-512, not Botan. The group is RFC
5054 Appendix A (3072-bit, generator 5). Fixed client randomness matches the
test-only Botan RNG callback; production always uses the system RNG.
Requires cryptography only for regeneration, not CTest.
"""
from pathlib import Path
import hashlib
import hmac
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

root = Path(__file__).parent / "pair-setup"
root.mkdir(exist_ok=True)
N = int("""
FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1
29024E088A67CC74020BBEA63B139B22514A08798E3404DD
EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245
E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED
EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D
C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F
83655D23DCA3AD961C62F356208552BB9ED529077096966D
670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B
E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C
9DE2BCBF6955817183995497CEA956AE515D2261898FA051
015728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA
64ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E
4C7ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD
2EE6BF12FFA06D98A0864D87602733EC86A64521F2B18177
B200CBBE117577A615D6C770988C0BAD946E208E24FA074E
5AB3143DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF
""".replace("\n", ""), 16)
assert N.bit_length() == 3072
g = 5
username, pin = b"Pair-Setup", b"0123"
salt = bytes(range(16))

def save(name, data):
    (root / f"{name}.hex").write_text(data.hex() + "\n", encoding="ascii")

def integer(value, padded=False):
    return value.to_bytes(384 if padded else max(1, (value.bit_length() + 7) // 8), "big")

def H(*values):
    return hashlib.sha512(b"".join(values)).digest()

def tlv(*fields):
    output = bytearray()
    for tag, value in fields:
        if not value:
            output.extend((tag, 0))
        for offset in range(0, len(value), 255):
            fragment = value[offset:offset + 255]
            output.extend((tag, len(fragment)))
            output.extend(fragment)
    return bytes(output)

def hkdf(secret, salt, info):
    return hmac.new(hmac.new(salt, secret, hashlib.sha512).digest(),
                    info + b"\x01", hashlib.sha512).digest()[:32]

x = int.from_bytes(H(salt, H(username + b":" + pin)), "big")
v = pow(g, x, N)
k = int.from_bytes(H(integer(N, True), integer(g, True)), "big")
# Botan 3.12 chooses a 256-bit exponent with its highest bit set.
a = int.from_bytes(bytes([0xC2]) + bytes([0x42]) * 31, "big")
A = pow(g, a, N)
# Exercise minimal-S hashing with an independently selected server exponent.
for b in range(1, 20000):
    B = (k * v + pow(g, b, N)) % N
    u = int.from_bytes(H(integer(A, True), integer(B, True)), "big")
    S = pow((A * pow(v, u, N)) % N, b, N)
    if integer(S, True)[0] == 0:
        break
else:
    raise AssertionError("No leading-zero shared integer found")
assert S == pow((B - k * v) % N, a + u * x, N)
K = H(integer(S))
xor = bytes(left ^ right for left, right in zip(H(integer(N)), H(integer(g))))
proof = H(xor, H(username), salt, integer(A), integer(B), K)
server_proof = H(integer(A), proof, K)
client_seed = bytes(range(32))
receiver_seed = bytes(range(32, 64))
client = Ed25519PrivateKey.from_private_bytes(client_seed)
receiver = Ed25519PrivateKey.from_private_bytes(receiver_seed)

def public(key):
    return key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)

client_id, receiver_id = b"synthetic-controller", b"synthetic-receiver"
encryption = hkdf(K, b"Pair-Setup-Encrypt-Salt", b"Pair-Setup-Encrypt-Info")
client_sign = hkdf(K, b"Pair-Setup-Controller-Sign-Salt", b"Pair-Setup-Controller-Sign-Info")
receiver_sign = hkdf(K, b"Pair-Setup-Accessory-Sign-Salt", b"Pair-Setup-Accessory-Sign-Info")
signature = receiver.sign(receiver_sign + receiver_id + public(receiver))

def m6(plaintext):
    return tlv((6, b"\x06"), (5, ChaCha20Poly1305(encryption).encrypt(
        bytes(4) + b"PS-Msg06", plaintext, b"")))

for name, data in {"salt": salt, "modulus": integer(N), "server-public": integer(B, True),
                   "zero-shared-public": integer(k * v % N, True), "client-public": integer(A, True),
                   "proof": proof, "server-proof": server_proof, "session-key": K,
                   "client-seed": client_seed}.items():
    save(name, data)
save("m1", tlv((0, b"\x00"), (6, b"\x01")))
save("m2", tlv((6, b"\x02"), (2, salt), (3, integer(B, True))))
save("m3", tlv((6, b"\x03"), (3, integer(A, True)), (4, proof)))
save("m4", tlv((6, b"\x04"), (4, server_proof)))
client_signature = client.sign(client_sign + client_id + public(client))
save("m5", tlv((6, b"\x05"), (5, ChaCha20Poly1305(encryption).encrypt(
    bytes(4) + b"PS-Msg05", tlv((1, client_id), (3, public(client)), (10, client_signature)), b""))))
save("m6", m6(tlv((1, receiver_id), (3, public(receiver)), (10, signature))))
save("bad-signature-m6", m6(tlv((1, receiver_id), (3, public(receiver)),
                               (10, bytes([signature[0] ^ 1]) + signature[1:]))))
save("wrong-id-m6", m6(tlv((1, b"different-receiver"), (3, public(receiver)), (10, signature))))
save("wrong-key-m6", m6(tlv((1, receiver_id), (3, public(client)), (10, signature))))
save("duplicate-id-m6", m6(tlv((1, receiver_id), (3, public(receiver)), (10, signature), (1, receiver_id))))
save("missing-id-m6", m6(tlv((3, public(receiver)), (10, signature))))
save("empty-id-m6", m6(tlv((1, b""), (3, public(receiver)), (10, signature))))
save("long-id-m6", m6(tlv((1, b"a" * 65), (3, public(receiver)), (10, signature))))
save("short-key-m6", m6(tlv((1, receiver_id), (3, public(receiver)[:-1]), (10, signature))))
save("short-signature-m6", m6(tlv((1, receiver_id), (3, public(receiver)), (10, signature[:-1]))))
save("unknown-inner-m6", m6(tlv((1, receiver_id), (3, public(receiver)), (10, signature), (100, b"x"))))
save("malformed-inner-m6", m6(b"\x01\x40\x01"))
identity_key = bytes([1]) + bytes(31)
forged_signature = bytes([1]) + bytes(63)
# The primitive can accept this without any private key. Enrollment must reject
# the weak public key even when both the AEAD and signature equation succeed.
Ed25519PublicKey.from_public_bytes(identity_key).verify(
    forged_signature, receiver_sign + receiver_id + identity_key)
save("weak-identity-m6", m6(tlv((1, receiver_id), (3, identity_key), (10, forged_signature))))
print(f"Generated pair-setup fixtures; server exponent {b}, leading-zero S covered")
