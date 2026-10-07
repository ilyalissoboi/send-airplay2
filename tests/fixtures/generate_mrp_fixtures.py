# SPDX-License-Identifier: Apache-2.0
"""Synthetic oracle: pyatv 0.18.0 pinned schemas and Python protobuf/plistlib.

Run in the already installed reference-tool environment, not the native build.
No network, receiver, credentials, discovery or controller calls are made.
See docs/dependencies.md for the pinned revision and MIT schema provenance.
"""
from pathlib import Path
import plistlib
import struct
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from pyatv.protocols.mrp import protobuf as pb

root = Path(__file__).parent / "mrp"
root.mkdir(exist_ok=True)

def message(kind):
    msg = pb.ProtocolMessage()
    msg.type = kind
    msg.identifier = "SYNTHETIC-REQUEST"
    msg.errorCode = 0
    msg.uniqueIdentifier = "SYNTHETIC-UNIQUE"
    return msg, pb.extract_inner(msg)

def save(name, msg):
    raw = msg.SerializeToString(deterministic=True)
    (root / (name + ".bin")).write_bytes(raw)
    return raw

msg, info = message(pb.DEVICE_INFO_MESSAGE)
info.uniqueIdentifier = "synthetic-controller"
info.name = "send-airplay2"
info.localizedModelName = "iPhone"
info.systemBuildVersion = "20F66"
info.applicationBundleIdentifier = "com.apple.TVRemote"
info.applicationBundleVersion = "344.28"
info.protocolVersion = 1
info.lastSupportedMessageType = 108
info.supportsSystemPairing = True
info.allowsPairing = True
info.systemMediaApplication = "com.apple.TVMusic"
info.supportsACL = True
info.supportsSharedQueue = True
info.supportsExtendedMotion = True
info.sharedQueueVersion = 2
info.deviceClass = pb.DeviceClass.iPhone
info.logicalDeviceCount = 1
save("device", msg)
msg, inner = message(pb.SET_CONNECTION_STATE_MESSAGE)
inner.state = pb.SetConnectionStateMessage.Connected
save("connection", msg)
msg, inner = message(pb.CLIENT_UPDATES_CONFIG_MESSAGE)
inner.artworkUpdates = True
inner.nowPlayingUpdates = False
inner.volumeUpdates = True
inner.keyboardUpdates = True
inner.outputDeviceUpdates = True
save("updates", msg)

path = pb.PlayerPath()
path.origin.type = 1
path.origin.identifier = 7
path.client.bundleIdentifier = "synthetic.airplay"
path.player.identifier = "synthetic-player"
(root / "path.bin").write_bytes(path.SerializeToString(deterministic=True))
msg, inner = message(pb.SEND_COMMAND_MESSAGE)
inner.command = 45
inner.options.playbackPosition = 30.5
inner.playerPath.CopyFrom(path)
seek = save("seek", msg)
msg, inner = message(pb.SEND_COMMAND_RESULT_MESSAGE)
inner.sendError = 0
inner.handlerReturnStatus = 0
save("result", msg)
msg, inner = message(pb.SET_STATE_MESSAGE)
inner.playbackState = pb.PlaybackState.Playing
inner.playerPath.CopyFrom(path)
inner.playbackQueue.location = 0
item = inner.playbackQueue.contentItems.add()
item.identifier = "synthetic-item"
item.metadata.assetURLString = "http://192.0.2.10/synthetic.mp4"
item.metadata.duration = 131.6
item.metadata.elapsedTime = 17.0
item.metadata.playbackRate = 1.0
save("state", msg)

# Independent boundary oracles: sender wall-clock extrapolation cannot prove EOF.
for name, state, elapsed, rate in [
    ("paused-at-end", pb.PlaybackState.Paused, 131.6, 0.0),
    ("paused-before-end", pb.PlaybackState.Paused, 131.59, 0.0),
    ("playing-at-end", pb.PlaybackState.Playing, 131.6, 1.0),
]:
    boundary = pb.ProtocolMessage()
    boundary.CopyFrom(msg)
    boundary_inner = pb.extract_inner(boundary)
    boundary_inner.playbackState = state
    boundary_inner.playbackQueue.contentItems[0].metadata.elapsedTime = elapsed
    boundary_inner.playbackQueue.contentItems[0].metadata.playbackRate = rate
    save(name, boundary)
airplay = pb.ProtocolMessage()
airplay.CopyFrom(msg)
airplay_inner = pb.extract_inner(airplay)
airplay_inner.playerPath.client.bundleIdentifier = "com.apple.TVAirPlay"
airplay_inner.playbackQueue.contentItems[0].metadata.ClearField("assetURLString")
save("unlinked-airplay-state", airplay)
active, active_inner = message(pb.SET_NOW_PLAYING_PLAYER_MESSAGE)
active_inner.playerPath.CopyFrom(airplay_inner.playerPath)
save("active-airplay-player", active)
airplay_inner.playbackQueue.contentItems[0].identifier = "replacement-item"
save("replacement-airplay-state", airplay)
msg, inner = message(pb.UPDATE_CONTENT_ITEM_MESSAGE)
inner.playerPath.CopyFrom(path)
item = inner.contentItems.add()
item.identifier = "synthetic-item"
item.metadata.elapsedTime = 30.5
item.metadata.playbackRate = 0.0
save("content-update", msg)

def variant(value):
    result = bytearray()
    while value > 127:
        result.append((value & 127) | 128)
        value >>= 7
    result.append(value)
    return bytes(result)

payload = plistlib.dumps({"params": {"data": variant(len(seek)) + seek}}, fmt=plistlib.FMT_BINARY)
sequence = 0x0102030405060708
(root / "sync.bin").write_bytes(struct.pack(">I12s4sQI", 32 + len(payload), b"sync", b"comm", sequence, 0) + payload)
(root / "reply.bin").write_bytes(struct.pack(">I12s4sQI", 32, b"rply", b"\0" * 4, sequence, 0))
reply_payload = plistlib.dumps({}, fmt=plistlib.FMT_BINARY)
(root / "reply-payload.bin").write_bytes(struct.pack(">I12s4sQI", 32 + len(reply_payload), b"rply", b"\0" * 4, sequence, 0) + reply_payload)
print("Generated synthetic MRP fixtures")
record_plain = bytes(index % 251 for index in range(2363))
record_header = struct.pack("<H", len(record_plain))
record_wire = record_header + ChaCha20Poly1305(bytes([0x22]) * 32).encrypt(bytes(12), record_plain, record_header)
(root / "large-record.bin").write_bytes(record_wire)
(root / "large-record-plain.bin").write_bytes(record_plain)
