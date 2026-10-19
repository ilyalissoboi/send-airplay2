#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Owned tones encoded by development FFmpeg; no downloaded media/tools shipped."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import generate_decoder_fixtures as base


def page_crc(data):
    crc = 0
    for value in data:
        crc ^= value << 24
        for _ in range(8):
            crc = ((crc << 1) ^ (0x04C11DB7 if crc & 0x80000000 else 0)) & 0xFFFFFFFF
    return crc


def pages(data):
    offset = 0
    while offset < len(data):
        assert data[offset:offset+4] == b"OggS"
        laces = data[offset+26]
        size = 27 + laces + sum(data[offset+27:offset+27+laces])
        yield offset, size
        offset += size
    assert offset == len(data)


def replace_granule(data, offset, size, value):
    result = bytearray(data)
    result[offset+6:offset+14] = struct.pack("<Q", value)
    result[offset+22:offset+26] = b"\0" * 4
    result[offset+22:offset+26] = struct.pack("<I", page_crc(result[offset:offset+size]))
    return bytes(result)


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        for name, rate, frames, channels, codec, extra in [
            ("opus48.opus", 48000, 624144, 2, "libopus", []),
            ("opus-mono.opus", 48000, 144144, 1, "libopus", []),
            ("opus51.opus", 48000, 144144, 6, "libopus", ["-mapping_family", "1"]),
            ("vorbis48.ogg", 48000, 624144, 2, "libvorbis", []),
            ("vorbis441.ogg", 44100, 573432, 2, "libvorbis", []),
            ("vorbis51.ogg", 48000, 144144, 6, "libvorbis", []),
        ]:
            source = Path(temporary) / (name + ".wav")
            tones = base.wave(source, 6 if channels == 6 else 2, frames=frames, rate=rate)
            path = directory / name
            base.run("-i", str(source), "-c:a", codec, "-ac", str(channels), *extra, str(path))
            raw = path.read_bytes()
            # Stable serial, with original checksums recalculated after metadata change.
            changed = bytearray(raw)
            for offset, size in pages(raw):
                changed[offset+14:offset+18] = struct.pack("<I", 0x12345678)
                changed[offset+22:offset+26] = b"\0" * 4
                changed[offset+22:offset+26] = struct.pack("<I", page_crc(changed[offset:offset+size]))
            path.write_bytes(changed)
            records.append({"file": name, "original_tones": tones, "output_channels": channels,
                            "expected_48khz_frames": 624144 if frames > 144144 else 144144})
        raw = (directory / "opus48.opus").read_bytes()
        last, size = list(pages(raw))[-1]
        final_granule = int.from_bytes(raw[last+6:last+14], "little")
        variants = {
            "chained.opus": raw + raw,
            "missing-eos.opus": raw[:last],
            "bad-crc.opus": raw[:-1] + bytes([raw[-1] ^ 1]),
            "bad-end.opus": replace_granule(raw, last, size, final_granule + 4096),
        }
        for name, data in variants.items():
            (directory / name).write_bytes(data)
            records.append({"file": name, "derived_from": "opus48.opus", "purpose": "refusal"})
        raw = (directory / "vorbis48.ogg").read_bytes()
        last, size = list(pages(raw))[-1]
        granule = int.from_bytes(raw[last+6:last+14], "little")
        (directory / "bad-vorbis-end.ogg").write_bytes(replace_granule(raw, last, size, granule + 4096))
        records.append({"file": "bad-vorbis-end.ogg", "derived_from": "vorbis48.ogg",
                        "purpose": "decoded end/coverage refusal", "declared_frame_delta": 4096})
    for record in records:
        path = directory / record["file"]
        record.update(bytes=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    (directory / "provenance.json").write_text(json.dumps({
        "license": "Apache-2.0 original repository-generated tones and metadata",
        "generator": "generate_ogg_audio_fixtures.py",
        "development_tool": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
        "files": records,
    }, indent=2) + "\n")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "ogg-audio")
