# SPDX-License-Identifier: Apache-2.0
"""Regenerate public synthetic session message oracles with the standard library.

Bodies mirror the reference sender's construction (pyatv 0.18.0 and the
unmerged fix robkochman/pyatv@8144c77c, MIT; shapes and constants only):
RTSP requests serialize dict bodies with plistlib's default sort_keys=True,
and /command payloads serialize the inner command with sort_keys=False.
Identifiers are fixed synthetic values; nothing is a capture. Not a CTest
dependency.
"""
from pathlib import Path
import plistlib

root = Path(__file__).parent / "session"
root.mkdir(exist_ok=True)

# Synthetic identity and identifiers (locally administered MAC-style ID).
DEVICE_ID = "02:11:22:33:44:55"
SESSION_UUID = "00000000-0000-4000-8000-0000000000A1"
CORRELATION_UUID = "00000000-0000-4000-8000-0000000000A2"
CLIENT_UUID = "00000000-0000-4000-8000-0000000000A3"
CHANNEL_UUID = "00000000-0000-4000-8000-0000000000A4"
ITEM_UUID = "00000000-0000-4000-8000-0000000000A5"
TIMING_PORT = 49152
DATA_STREAM_SEED = 0x0123456789ABCDEF
MEDIA_URL = "http://192.0.2.10:49153/synthetic-token/media"  # RFC 5737 address.

# Reference identity of the URL playback (stream) session.
MODEL = "iPhone14,3"
OS_NAME = "iPhone OS"
OS_VERSION = "16.5"
OS_BUILD = "20F66"
SOURCE_VERSION = "690.7.1"
NAME = "send-airplay2"  # Our own display name instead of the reference "pyatv".

URL_CLIENT_TYPE = "A6B27562-B43A-4F2D-B75F-82391E250194"
DATA_STREAM_CLIENT_TYPE = "1910A70F-DBC0-4242-AF95-115DB30604E1"


def rtsp_body(value) -> bytes:
    return plistlib.dumps(value, fmt=plistlib.FMT_BINARY)  # sort_keys=True


def write(name: str, data: bytes) -> None:
    (root / f"{name}.hex").write_text(data.hex() + "\n", encoding="ascii")


# Base SETUP of the stream session (fork: adds sessionCorrelationUUID).
write("base-setup", rtsp_body({
    "deviceID": DEVICE_ID,
    "sessionUUID": SESSION_UUID,
    "sessionCorrelationUUID": CORRELATION_UUID,
    "timingPort": TIMING_PORT,
    "timingProtocol": "NTP",
    "isMultiSelectAirPlay": True,
    "groupContainsGroupLeader": False,
    "macAddress": DEVICE_ID,
    "model": MODEL,
    "name": NAME,
    "osBuildVersion": OS_BUILD,
    "osName": OS_NAME,
    "osVersion": OS_VERSION,
    "senderSupportsRelay": False,
    "sourceVersion": SOURCE_VERSION,
    "statsCollectionEnabled": False,
}))

# Base SETUP of the remote-control-only session (pyatv ap2_session).
write("remote-control-setup", rtsp_body({
    "isRemoteControlOnly": True,
    "osName": OS_NAME,
    "sourceVersion": "550.10",
    "timingProtocol": "None",
    "model": MODEL,
    "deviceID": DEVICE_ID,
    "osVersion": OS_VERSION,
    "osBuildVersion": OS_BUILD,
    "macAddress": DEVICE_ID,
    "sessionUUID": SESSION_UUID,
    "name": NAME,
}))

# Type-130 control stream for URL playback (fork: controlType 1).
write("url-stream-setup", rtsp_body({"streams": [{
    "clientUUID": CLIENT_UUID,
    "clientTypeUUID": URL_CLIENT_TYPE,
    "channelID": f"{DEVICE_ID}-RCS-1",
    "controlType": 1,
    "type": 130,
}]}))

# Type-130 data stream for remote control (pyatv: controlType 2).
write("data-stream-setup", rtsp_body({"streams": [{
    "controlType": 2,
    "channelID": CHANNEL_UUID,
    "seed": DATA_STREAM_SEED,
    "clientUUID": CLIENT_UUID,
    "type": 130,
    "wantsDedicatedSocket": True,
    "clientTypeUUID": DATA_STREAM_CLIENT_TYPE,
}]}))

# Receiver responses: shapes observed (sanitized) on tvOS 26.6, values synthetic.
write("base-setup-response", rtsp_body({"eventPort": 49213}))
write("url-stream-setup-response", rtsp_body({"streams": [{"type": 130, "streamID": 1}]}))
write("data-stream-setup-response",
      rtsp_body({"streams": [{"type": 130, "streamID": 2, "dataPort": 49214}]}))


def command(name: str, value) -> None:
    inner = plistlib.dumps(value, fmt=plistlib.FMT_BINARY, sort_keys=False)
    write(f"command-{name}", rtsp_body({"params": {"data": inner}}))


# The validated start sequence, in order.
command("insert", {
    "type": "insertPlayQueueItem",
    "item": {
        "uuid": ITEM_UUID,
        "mediaType": "file",
        "Content-Location": MEDIA_URL,
        "Start-Position-Seconds": 0.0,
    },
})
command("date-range", {
    "type": "setProperty",
    "value": True,
    "property": "isInterestedInDateRange",
    "item": {"uuid": ITEM_UUID},
})
command("item-end", {"type": "setProperty", "value": 1, "property": "actionAtItemEnd"})
command("rate", {"type": "setRate", "rate": 1.0})


def event(name: str, value) -> None:
    inner = plistlib.dumps(value, fmt=plistlib.FMT_BINARY, sort_keys=False)
    write(f"event-{name}", rtsp_body({"params": {"data": inner}}))


# Receiver events: playback state in params, the alternate "name" form, and
# an event type the session only counts.
event("state-params", {"type": "playbackState", "params": {"playbackState": "Playing"}})
event("state-name", {"type": "playbackState", "name": "Loading"})
# macOS HLS EOF (26.7.1): no final position, but an explicit root reason.
event("state-ended", {"type": "playbackState", "name": "stopped", "reason": "ended"})
# CMTime-shaped duration observed on tvOS 26.6; all values are synthetic.
event("state-duration", {"type": "playbackState", "params": {
    "playbackState": "Playing", "duration": {
        "value": 6580, "timescale": 50, "flags": 1, "epoch": 0}}})
event("notification", {"type": "notification", "params": {"kind": "synthetic"}})
# Some receiver events are a bare dict, not wrapped in params.data (updateInfo
# on tvOS 26.6 and in the reference sender's log).
write("event-bare", rtsp_body({"type": "updateInfo", "value": {"kind": "synthetic"}}))
