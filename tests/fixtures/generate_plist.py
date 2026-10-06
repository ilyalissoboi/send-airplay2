# SPDX-License-Identifier: Apache-2.0
"""Regenerate public synthetic binary-plist oracles with the standard library.

Python's plistlib is an independent bplist00 writer; nothing here imports this
project. Dictionaries keep insertion order (sort_keys=False), matching the C++
encoder's ordered dictionaries. Not a CTest dependency.
"""
from datetime import datetime
from pathlib import Path
import plistlib

root = Path(__file__).parent / "plist"
root.mkdir(exist_ok=True)


def write(name: str, value) -> bytes:
    encoded = plistlib.dumps(value, fmt=plistlib.FMT_BINARY, sort_keys=False)
    (root / f"{name}.hex").write_text(encoded.hex() + "\n", encoding="ascii")
    return encoded


# Every supported type and each integer width boundary (1/2/4/8-byte encodings).
write(
    "scalars",
    {
        "true": True,
        "false": False,
        "zero": 0,
        "u8-max": 255,
        "u16-min": 256,
        "u16-max": 65535,
        "u32-min": 65536,
        "u32-max": 4294967295,
        "i64-min-positive": 4294967296,
        "i64-max": 9223372036854775807,
        "minus-one": -1,
        "i64-min": -9223372036854775808,
        "real": 1.5,
        "negative-real": -0.25,
        "date": datetime(2026, 10, 6),
        "ascii": "file",
        # BMP and astral characters force UTF-16BE with a surrogate pair.
        "unicode": "Living Room ✓ ü \U0001F600",
        "data": bytes(range(20)),
        "empty-string": "",
        "empty-data": b"",
        "empty-array": [],
        "empty-dict": {},
    },
)

# Repeated scalars share one object; equal-looking values of different types do not.
write(
    "shared-scalars",
    {
        "type": "setProperty",
        "property": "type",
        "value": True,
        "item": {"uuid": "type"},
        "list": ["a", "a", 1, 1, True, 1.0, b"a", "setProperty"],
    },
)

# More than 255 objects (2-byte references), a 300-byte string (0x11 length
# marker) and a 70,000-byte data value (4-byte offsets, 0x12 length marker).
write(
    "wide-tables",
    {
        "numbers": list(range(1000, 1300)),
        "long-string": "x" * 300,
        "large-data": bytes(index % 251 for index in range(70000)),
    },
)

# The /command body shape used for URL playback on tvOS 26.6 by the reference
# sender: a queue command plist nested as data inside {"params": {"data": ...}}.
# Synthetic UUID and a documentation-range (RFC 5737) URL; not a capture.
inner = write(
    "command-insert-inner",
    {
        "type": "insertPlayQueueItem",
        "item": {
            "uuid": "00000000-0000-4000-8000-000000000001",
            "mediaType": "file",
            "Content-Location": "http://192.0.2.10:49152/synthetic-token/media",
            "Start-Position-Seconds": 0.0,
        },
    },
)
write("command-insert", {"params": {"data": inner}})

# A receiver-style playback state event, wrapped the same way.
event = write(
    "event-playback-state-inner",
    {"type": "playbackState", "params": {"playbackState": "Playing"}},
)
write("event-playback-state", {"params": {"data": event}})

# Seconds from the 2001-01-01 epoch to the date above, for the C++ expectation.
print("date seconds:", (datetime(2026, 10, 6) - datetime(2001, 1, 1)).total_seconds())
