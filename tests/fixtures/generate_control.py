# SPDX-License-Identifier: Apache-2.0
"""Regenerate public synthetic oracles. Requires cryptography; not a CTest dependency."""
from pathlib import Path
import hashlib
import hmac
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

root = Path(__file__).parent
key = bytes(range(32))
plaintext = bytes(i % 251 for i in range(1030))
wire = bytearray()
for counter, offset in enumerate(range(0, len(plaintext), 1024)):
    frame = plaintext[offset:offset + 1024]
    header = len(frame).to_bytes(2, "little")
    nonce = bytes(4) + counter.to_bytes(8, "little")
    wire.extend(header + ChaCha20Poly1305(key).encrypt(nonce, frame, header))
(root / "control-wire.hex").write_text(wire.hex() + "\n", encoding="ascii")

# Independent RFC 5869 extract/expand using SHA512; 32 bytes need only block T(1).
salt = b"Control-Salt"
secret = bytes(range(32))
prk = hmac.new(salt, secret, hashlib.sha512).digest()
for direction in ("Write", "Read"):
    info = f"Control-{direction}-Encryption-Key".encode("ascii")
    output = hmac.new(prk, info + b"\x01", hashlib.sha512).digest()[:32]
    (root / f"control-{direction.lower()}-key.hex").write_text(output.hex() + "\n", encoding="ascii")

empty_prk = hmac.new(bytes(64), b"", hashlib.sha512).digest()
empty_key = hmac.new(empty_prk, b"\x01", hashlib.sha512).digest()[:32]
(root / "hkdf-empty-key.hex").write_text(empty_key.hex() + "\n", encoding="ascii")

# RFC 8439 2.8.2 AEAD ciphertext and authentication tag (numeric test data).
rfc_output = (
    "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63"
    "dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692"
    "ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff"
    "4def08e4b7a9de576d26586cec64b6116"
    "1ae10b594f09e26a7e902ecbd0600691"
)
(root / "rfc8439-aead.hex").write_text(rfc_output + "\n", encoding="ascii")
