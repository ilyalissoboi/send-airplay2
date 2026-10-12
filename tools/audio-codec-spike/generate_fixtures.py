#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate project-owned channel tones; external ffmpeg is a development tool.

No third-party media is downloaded. The tool binary is never packaged. Commit
the generated compressed fixtures and the version/configuration/hash record.
"""
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile


def generate(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    rate, frames, channels = 48000, 4800, 6
    # Native WAVE order: FL, FR, FC, LFE, BL, BR. Distinct tones identify inputs.
    frequencies = [310, 470, 630, 80, 790, 950]
    pcm = bytearray()
    for frame in range(frames):
        for frequency in frequencies:
            sample = round(4096 * math.sin(2 * math.pi * frequency * frame / rate))
            pcm.extend(struct.pack("<h", sample))
    pcm_guid = bytes.fromhex("0100000000001000800000aa00389b71")
    fmt = struct.pack("<HHIIHHHHI", 0xFFFE, channels, rate, rate * channels * 2,
                      channels * 2, 16, 22, 16, 0x3F) + pcm_guid
    body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(pcm)) + pcm
    wave = b"RIFF" + struct.pack("<I", len(body)) + body
    version = subprocess.check_output(["ffmpeg", "-version"], text=True)
    variants = [
        ("aac-51.m4a", ["-c:a", "aac", "-b:a", "384k"]),
        ("ac3-51.mka", ["-c:a", "ac3", "-b:a", "448k"]),
        ("eac3-51.mka", ["-c:a", "eac3", "-b:a", "448k"]),
        ("dts-core-51.mka", ["-c:a", "dca", "-strict", "-2", "-b:a", "1411200"]),
    ]
    records = []
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        source = Path(temporary) / "owned-51.wav"
        source.write_bytes(wave)
        for name, codec in variants:
            command = ["ffmpeg", "-y", "-hide_banner", "-loglevel", "error",
                       "-i", str(source), "-map_metadata", "-1"] + codec + [str(directory / name)]
            subprocess.run(command, check=True)
            data = (directory / name).read_bytes()
            records.append({"file": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                            "encoder_arguments": codec})
    record = {"license": "Apache-2.0; original synthesized input and resulting test media",
              "generator": "generate_fixtures.py", "tool_version": version.splitlines()[0],
              "tool_configuration": next(line for line in version.splitlines() if line.startswith("configuration:")),
              "tool_distribution": "development use only; not incorporated or redistributed",
              "input": {"channels": channels, "sample_rate": rate, "frames": frames,
                        "channel_mask": "0x3f", "frequencies_hz": frequencies,
                        "sha256": hashlib.sha256(wave).hexdigest()}, "fixtures": records}
    (directory / "provenance.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures")
