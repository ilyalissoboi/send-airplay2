#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original pulse/channel markers encoded with external development FFmpeg."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile

HERE = Path(__file__).parent
spec = importlib.util.spec_from_file_location("owned_tones", HERE.parent / "audio-only-probe/generate_fixture.py")
tones = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tones)

def normalize_info_crc(raw):
    """Author the Info-tag CRC over its actual header extent, not fixed 190 bytes."""
    begin = 0
    if raw[:3] == b"ID3":
        begin = 10 + sum(raw[6+i] << (7 * (3-i)) for i in range(4))
    encoder = raw.find(b"Lavc", begin)
    if encoder < 0:
        return raw, None
    checksum_position = encoder + 34
    table = []
    for byte in range(256):
        entry = byte
        for _ in range(8):
            entry = (entry >> 1) ^ (0xA001 if entry & 1 else 0)
        table.append(entry)
    crc = 0
    for byte in raw[begin:checksum_position]:
        crc = (crc >> 8) ^ table[(crc ^ byte) & 255]
    previous = int.from_bytes(raw[checksum_position:checksum_position+2], "big")
    if crc == previous:
        return raw, None
    changed = bytearray(raw)
    changed[checksum_position:checksum_position+2] = struct.pack(">H", crc)
    return bytes(changed), {"original_sha256": hashlib.sha256(raw).hexdigest(),
                            "original_crc16": previous, "corrected_crc16": crc,
                            "covered_frame_bytes": checksum_position - begin,
                            "changed_bytes": "Only the two-byte Info-tag checksum; encoded audio and trim unchanged"}


def generate():
    directory = HERE / "fixtures"
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        for name, rate, seconds, channels, options in [
            ("cbr48.mp3", 48000, 64, 2, ["-b:a", "192k"]),
            ("vbr441.mp3", 44100, 64, 2, ["-q:a", "3"]),
            ("mono24.mp3", 24000, 13, 1, ["-b:a", "48k"]),
            ("mono12.mp3", 12000, 3, 1, ["-b:a", "24k"]),
            ("bare48.mp3", 48000, 3, 2, ["-b:a", "128k", "-write_xing", "0", "-id3v2_version", "0"]),
        ]:
            tones.SAMPLE_RATE = rate
            tones.DURATION_SECONDS = seconds
            source = Path(temporary) / (name + ".wav")
            tones.generate_wave(source)
            output = directory / name
            subprocess.run(["ffmpeg", "-y", "-nostdin", "-hide_banner", "-loglevel", "error",
                            "-i", str(source), "-c:a", "libmp3lame", "-ac", str(channels),
                            "-map_metadata", "-1", *options, str(output)], check=True)
            original = output.read_bytes()
            normalized, repair = normalize_info_crc(original)
            output.write_bytes(normalized)
            if name == "mono24.mp3" and repair:
                (directory / "invalid-mono-crc.mp3").write_bytes(original)
                records.append({"file": "invalid-mono-crc.mp3", "purpose": "Original development encoder CRC refusal",
                                "derived_from": "mono24.mp3", "crc_metadata": repair})
            # Independent full decode by the development tool checks authored
            # exact gapless length, not a format-duration/bitrate estimate.
            pcm = subprocess.check_output(["ffmpeg", "-nostdin", "-hide_banner", "-loglevel", "error",
                                           "-i", str(output), "-map", "0:a:0", "-f", "f32le", "-"])
            decoded = len(pcm) // (channels * 4)
            if name != "bare48.mp3":
                assert decoded == rate * seconds, (name, decoded)
            records.append({"file": name, "source_rate": rate, "source_frames": rate * seconds,
                            "output_channels": channels, "independent_decoded_frames": decoded,
                            "source_wave_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                            "encoder_options": options, "info_crc_repair": repair})
        # An opaque leading ID3v2.3 block and trailing ID3v1 must not change timing.
        raw = (directory / "bare48.mp3").read_bytes()
        (directory / "id3-bare48.mp3").write_bytes(b"ID3\x03\0\0\0\0\0\x04test" + raw + b"TAG" + bytes(125))
        records.append({"file": "id3-bare48.mp3", "derived_from": "bare48.mp3", "metadata": "ID3v2.3 4-byte opaque body and ID3v1"})
    for record in records:
        path = directory / record["file"]
        record.update(bytes=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    (directory / "provenance.json").write_text(json.dumps({
        "license": "Apache-2.0 original repository-generated pulse/channel tones and metadata",
        "generator": "tools/mp3-probe/generate_fixtures.py",
        "development_encoder_decoder": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
        "tone_intervals": [{"start_seconds": start, "end_seconds": start + 8,
                            "left_hz": tones.channel_tones(start)[0], "right_hz": tones.channel_tones(start)[1]}
                           for start in range(0, 64, 8)],
        "pulse": "0.75 seconds tone, 0.25 seconds silence, 10-ms fades; mono cases downmix original stereo",
        "files": records,
    }, indent=2) + "\n")


if __name__ == "__main__":
    generate()
