# SPDX-License-Identifier: Apache-2.0
"""Public synthetic pair-verify oracles; no production code imported.

Requires Python cryptography only to regenerate, not to run CTest. This oracle
assembles TLV/signing transcripts independently and uses standard cryptography
primitives. Its crypto backend may also be OpenSSL; RFC vectors cover primitives.
"""
from pathlib import Path
import hashlib
import hmac
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

root = Path(__file__).parent / "pair-verify"
root.mkdir(exist_ok=True)

def save(name, data):
    (root / f"{name}.hex").write_text(data.hex() + "\n", encoding="ascii")

def public(key):
    return key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)

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
    prk = hmac.new(salt, secret, hashlib.sha512).digest()
    return hmac.new(prk, info + b"\x01", hashlib.sha512).digest()[:32]

client_seed = bytes(range(32))
receiver_seed = bytes(range(32, 64))
client_ephemeral = bytes(range(64, 96))
server_ephemeral = bytes(range(96, 128))
client = Ed25519PrivateKey.from_private_bytes(client_seed)
receiver = Ed25519PrivateKey.from_private_bytes(receiver_seed)
client_exchange = X25519PrivateKey.from_private_bytes(client_ephemeral)
server_exchange = X25519PrivateKey.from_private_bytes(server_ephemeral)
client_public = public(client_exchange)
server_public = public(server_exchange)
shared = client_exchange.exchange(server_exchange.public_key())
assert shared == server_exchange.exchange(client_exchange.public_key())
session_key = hkdf(shared, b"Pair-Verify-Encrypt-Salt", b"Pair-Verify-Encrypt-Info")
receiver_id = b"synthetic-receiver"
client_id = b"synthetic-controller"
server_signature = receiver.sign(server_public + receiver_id + client_public)
client_signature = client.sign(client_public + client_id + server_public)
receiver.public_key().verify(server_signature, server_public + receiver_id + client_public)
client.public_key().verify(client_signature, client_public + client_id + server_public)

def m2(plaintext):
    encrypted = ChaCha20Poly1305(session_key).encrypt(bytes(4) + b"PV-Msg02", plaintext, b"")
    return tlv((6, b"\x02"), (3, server_public), (5, encrypted))

save("client-seed", client_seed)
save("client-ephemeral", client_ephemeral)
save("receiver-key", public(receiver))
save("m1", tlv((6, b"\x01"), (3, client_public)))
save("m2", m2(tlv((1, receiver_id), (10, server_signature))))
encrypted_m3 = ChaCha20Poly1305(session_key).encrypt(
    bytes(4) + b"PV-Msg03", tlv((1, client_id), (10, client_signature)), b"")
save("m3", tlv((6, b"\x03"), (5, encrypted_m3)))
save("m4", tlv((6, b"\x04")))

wrong_id = b"different-receiver"
bad_signature = bytes([server_signature[0] ^ 1]) + server_signature[1:]
save("bad-signature-m2", m2(tlv((1, receiver_id), (10, bad_signature))))
save("wrong-id-m2", m2(tlv((1, wrong_id), (10, receiver.sign(server_public + wrong_id + client_public)))))
save("wrong-transcript-m2", m2(tlv((1, receiver_id), (10, receiver.sign(client_public + receiver_id + server_public)))))
save("duplicate-id-m2", m2(tlv((1, receiver_id), (10, server_signature), (1, receiver_id))))
save("missing-id-m2", m2(tlv((10, server_signature))))
save("short-signature-m2", m2(tlv((1, receiver_id), (10, server_signature[:-1]))))
save("malformed-inner-m2", m2(b"\x01\x40\x01"))
save("unknown-inner-m2", m2(tlv((1, receiver_id), (10, server_signature), (100, b"extension"))))

# Further channels of the same verified session reuse the shared secret with
# their own salt/info. pyatv calls setup_channel(salt, output_info, input_info):
# the event channel passes (Events-Salt, Events-Read..., Events-Write...), so the
# sender writes with the "Read" info; data streams pass Output then Input, with
# the stream's SETUP seed appended to the salt in decimal.
data_stream_seed = 0x0123456789ABCDEF
channel_labels = {
    "events": (b"Events-Salt", b"Events-Read-Encryption-Key", b"Events-Write-Encryption-Key"),
    "datastream": (
        b"DataStream-Salt" + str(data_stream_seed).encode("ascii"),
        b"DataStream-Output-Encryption-Key",
        b"DataStream-Input-Encryption-Key",
    ),
}
for channel, (salt, sender_write_info, sender_read_info) in channel_labels.items():
    save(f"{channel}-write-key", hkdf(shared, salt, sender_write_info))
    save(f"{channel}-read-key", hkdf(shared, salt, sender_read_info))

for direction in ("write", "read"):
    key = hkdf(shared, b"Control-Salt", f"Control-{direction.title()}-Encryption-Key".encode("ascii"))
    save(f"{direction}-key", key)
    # One independent encrypted record proves key direction and initial counter.
    payload = f"synthetic-{direction}".encode("ascii")
    header = len(payload).to_bytes(2, "little")
    save(f"{direction}-wire", header + ChaCha20Poly1305(key).encrypt(bytes(12), payload, header))
