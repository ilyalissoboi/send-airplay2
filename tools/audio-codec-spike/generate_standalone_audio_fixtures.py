#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original tones for native FLAC and RIFF/RF64 WAVE audio-only tests."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import generate_decoder_fixtures as base


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    with tempfile.TemporaryDirectory(dir=directory, ignore_cleanup_errors=True) as temporary:
        temp = Path(temporary)
        cases = [
            ("flac48.flac", 2, 48000, 624144, "flac", 2, []),
            ("flac441.flac", 2, 44100, 573432, "flac", 2, []),
            ("flac51.flac", 6, 48000, 144144, "flac", 6, []),
            ("pcm16.wav", 2, 48000, 624144, "pcm_s16le", 2, []),
            ("pcm24-51.wav", 6, 48000, 144144, "pcm_s24le", 6, []),
            ("pcm32.wav", 2, 48000, 144144, "pcm_s32le", 2, []),
            ("float32.wav", 2, 48000, 144144, "pcm_f32le", 2, []),
            ("float64.wav", 2, 48000, 144144, "pcm_f64le", 2, []),
            ("mono441.wav", 2, 44100, 132432, "pcm_s16le", 1, []),
            ("rf64.wav", 2, 48000, 144144, "pcm_s16le", 2, ["-rf64", "always"]),
        ]
        for name, channels, rate, frames, codec, output_channels, extra in cases:
            source = temp / (name + ".source.wav")
            original = base.wave(source, channels, frames=frames, rate=rate)
            path = directory / name
            base.run("-i", str(source), "-c:a", codec, "-ac", str(output_channels), *extra, str(path))
            records.append({"file": name, "original_tones": original, "output_channels": output_channels})
        raw = (directory / "flac48.flac").read_bytes()
        minimum_block = int.from_bytes(raw[8:10], "big")
        # A valid seek point: first sample, first frame offset, first block count.
        point = struct.pack(">QQH", 0, 0, minimum_block)
        (directory / "seektable.flac").write_bytes(raw[:42] + bytes([3, 0, 0, 18]) + point + raw[42:])
        records.append({"file": "seektable.flac", "derived_from": "flac48.flac", "seek_points": 1})
        for name, delta in [("too-long.flac", 1024), ("too-short.flac", -1024)]:
            changed = bytearray(raw)
            packed = int.from_bytes(changed[18:26], "big")
            changed[18:26] = ((packed & ~((1 << 36) - 1)) | (624144 + delta)).to_bytes(8, "big")
            (directory / name).write_bytes(changed)
            records.append({"file": name, "derived_from": "flac48.flac", "declared_frame_delta": delta})
        raw = (directory / "rf64.wav").read_bytes()
        # Reuse a short WAVE payload, strip only the RF64 ds64 chunk and author RIFF.
        body = raw[8:12] + b"JUNK" + struct.pack("<I", 3) + b"abc\0" + raw[48:]
        # RF64 data marker becomes its concrete short length.
        marker = body.index(b"data")
        data_size = int.from_bytes(raw[28:36], "little")
        body = body[:marker+4] + struct.pack("<I", data_size) + body[marker+8:]
        (directory / "oddchunk.wav").write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
        records.append({"file": "oddchunk.wav", "derived_from": "rf64.wav", "odd_unknown_chunk_bytes": 3})
    for record in records:
        path = directory / record["file"]
        record.update(bytes=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    (directory / "provenance.json").write_text(json.dumps({
        "license": "Apache-2.0 original repository-generated tones and metadata",
        "generator": "generate_standalone_audio_fixtures.py",
        "development_tool": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
        "files": records,
    }, indent=2) + "\n")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "standalone-audio")
