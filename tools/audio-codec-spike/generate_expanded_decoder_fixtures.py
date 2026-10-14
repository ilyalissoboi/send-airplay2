#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Owned video fixtures for the expanded private decoder, with sample clocks.

The CLI encodes original tones only. Container identities, nanosecond clocks,
BlockGroup padding and expected audible lengths are authored independently here.
"""
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import generate_decoder_fixtures as base

RATE = 48000
FRAMES = 288144


def write_fixture(path, data):
    # Stage a new file before replacement; readers never observe a truncated
    # fixture when Windows indexing has the previous version open.
    with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + ".", delete=False) as file:
        temporary = Path(file.name)
        file.write(data)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def encoded(path):
    result = json.loads(subprocess.check_output([
        "ffprobe", "-v", "error", "-show_streams", "-show_packets", "-show_data",
        "-of", "json", str(path)], text=True))
    stream = result["streams"][0]
    clock = Fraction(stream["time_base"])
    packets = []
    for packet in result["packets"]:
        payload = base.unhex_dump(packet["data"])
        if not payload:
            continue
        discard = next((side.get("discard_padding", 0)
                        for side in packet.get("side_data_list", [])
                        if side["side_data_type"] == "Skip Samples"), 0)
        packets.append((round(int(packet["pts"]) * clock * RATE), payload, discard,
                        "K" in packet["flags"]))
    return packets, base.unhex_dump(stream.get("extradata", ""))


def signed_element(tag, value):
    return base.element(tag, value.to_bytes(8, "big", signed=True))


def movie(codec, packets, private, video, delay_samples=0, bit_depth=None,
          padding_override=None, declared_depth=None, timestamp_scale_ns=1, channels=2):
    """One cluster per packet; cues address original audio TrackNumber 42."""
    header = b"".join([base.uint("4286", 1), base.uint("42f7", 1), base.uint("42f2", 4),
                       base.uint("42f3", 8), base.element("4282", b"matroska"),
                       base.uint("4287", 4), base.uint("4285", 2)])
    # Nanosecond TimestampScale avoids millisecond rounding hiding trim errors.
    info = base.uint("2ad7b1", timestamp_scale_ns) + base.element(
        "4489", struct.pack(">d", 6500000000.0 / timestamp_scale_ns))
    info += base.element("4d80", b"sap2-owned-expanded") + base.element("5741", b"sap2-owned-expanded")
    video_packets, video_private = video
    entry = base.uint("d7", 11) + base.uint("73c5", 1011) + base.uint("83", 1)
    entry += base.element("86", b"V_MPEG4/ISO/AVC") + base.element("63a2", video_private)
    entry += base.uint("23e383", 500000000)  # The owned 2-fps video's sample duration.
    entry += base.element("e0", base.uint("b0", 16) + base.uint("ba", 16))
    entries = base.element("ae", entry)
    entry = base.uint("d7", 42) + base.uint("73c5", 1042) + base.uint("83", 2)
    entry += base.uint("b9", 1) + base.uint("88", 1) + base.element("86", codec.encode())
    if private:
        entry += base.element("63a2", private)
    audio = base.element("b5", struct.pack(">d", RATE)) + base.uint("9f", channels)
    if bit_depth:
        audio += base.uint("6264", declared_depth or bit_depth)
    entry += base.element("e1", audio)
    if delay_samples:
        entry += base.uint("56aa", round(Fraction(delay_samples, RATE) * 1e9))
        entry += base.uint("56bb", 80000000)
    entries += base.element("ae", entry)
    prefix = base.element("1549a966", info) + base.element("1654ae6b", entries)
    ordered = [(round(Fraction(pts + delay_samples, RATE) * 1e9), 42, payload, discard, key)
               for pts, payload, discard, key in packets]
    ordered += [(pts * 1000000, 11, payload, 0, True) for pts, payload in video_packets]
    ordered = [(round(Fraction(pts, timestamp_scale_ns)), number, payload, discard, key)
               for pts, number, payload, discard, key in ordered]
    ordered.sort(key=lambda value: value[:2])
    clusters = []
    for index, (pts, number, payload, discard, key) in enumerate(ordered):
        assert pts >= 0
        block = base.vint(number) + b"\0\0" + bytes([0x80 if key else 0]) + payload
        if number == 42 and padding_override and index == next(
                i for i, value in enumerate(ordered) if value[1] == 42):
            discard = padding_override
        if discard:
            group = base.element("a1", block)
            group += signed_element("75a2", round(Fraction(discard, RATE) * 1e9))
            block = base.element("a0", group)
        else:
            block = base.element("a3", block)
        clusters.append(base.element("1f43b675", base.uint("e7", pts) + block))

    def cues(start):
        parts, position = [], start
        for (pts, number, _, _, key), cluster in zip(ordered, clusters):
            if key:
                point = base.uint("b3", pts, 8) + base.element(
                    "b7", base.uint("f7", number) + base.uint("f1", position, 8))
                parts.append(base.element("bb", point))
            position += len(cluster)
        return base.element("1c53bb6b", b"".join(parts))

    cue_data = cues(0)
    cue_data = cues(len(prefix) + len(cue_data))
    return base.element("1a45dfa3", header) + base.element(
        "18538067", prefix + cue_data + b"".join(clusters))


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output(["ffmpeg", "-version"], text=True)
    records = []
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        temp = Path(temporary)
        source = temp / "tones.wav"
        inputs = [base.wave(source, 2, frames=FRAMES)]
        video = temp / "video.mp4"
        base.run("-f", "lavfi", "-i", "color=black:s=16x16:r=2:d=6.003",
                 "-c:v", "libx264", "-bf", "0", "-g", "1", "-an", str(video))
        video_data = base.encoded_track(video)
        for name, codec, container, identifier in [
                ("flac", "flac", "flac", "A_FLAC"),
                ("opus", "opus", "opus", "A_OPUS"),
                ("vorbis", "vorbis", "ogg", "A_VORBIS"),
                ("truehd", "truehd", "truehd", "A_TRUEHD")]:
            path = temp / (name + ".encoded")
            base.run("-i", str(source), "-c:a", codec, "-strict", "-2", "-f", container, str(path))
            packets, private = encoded(path)
            delay = -packets[0][0]
            if name == "flac":
                private = b"fLaC\x80\0\0\x22" + private
            if name == "vorbis":
                # This native encoder rounds the owned input to 6.004 seconds.
                # Author the original 6.003-second presentation, independently
                # of the rounded Ogg granule, with 48 extra discarded samples.
                pts, payload, discard, key = packets[-1]
                assert pts + 1024 - discard == FRAMES + 48
                packets[-1] = (pts, payload, discard + 48, key)
            write_fixture(directory / (name + ".mkv"), movie(
                identifier, packets, private, video_data, delay))
            records.append({"file": name + ".mkv", "audible_frames": FRAMES})
            if name == "flac":
                write_fixture(directory / "flac-coarse-clock.mkv", movie(
                    identifier, packets, private, video_data, delay, timestamp_scale_ns=1000000))
                records.append({"file": "flac-coarse-clock.mkv", "timestamp_scale_ns": 1000000,
                                "audible_frames": FRAMES, "hls_unsupported": True})
            if name == "opus":
                write_fixture(directory / "opus-leading-discard.mkv", movie(
                    identifier, packets, private, video_data, delay, padding_override=-216))
                records.append({"file": "opus-leading-discard.mkv", "leading_discard": 96,
                                "block_skip_samples": 216, "codec_delay_samples": 120})
            if name in ("flac", "opus"):
                path = temp / (name + ".mp4")
                base.run("-i", str(video), "-i", str(source), "-map", "0:v", "-map", "1:a",
                         "-c:v", "copy", "-c:a", codec, "-strict", "-2", str(path))
                write_fixture(directory / path.name, base.rewrite_movie(path.read_bytes(), [11, 42]))
                records.append({"file": path.name, "audible_frames": FRAMES})
        surround = temp / "surround.wav"
        inputs.append(base.wave(surround, 6, frames=FRAMES))
        for name, codec, container, identifier in [
                ("flac", "flac", "flac", "A_FLAC"),
                ("opus", "libopus", "opus", "A_OPUS"),
                ("vorbis", "libvorbis", "ogg", "A_VORBIS"),
                ("truehd", "truehd", "truehd", "A_TRUEHD")]:
            path = temp / (name + "-51.encoded")
            base.run("-i", str(surround), "-c:a", codec, "-strict", "-2", "-f", container, str(path))
            packets, private = encoded(path)
            if name == "flac":
                private = b"fLaC\x80\0\0\x22" + private
            filename = name + "-51.mkv"
            write_fixture(directory / filename, movie(
                identifier, packets, private, video_data, -packets[0][0], channels=6))
            records.append({"file": filename, "audible_frames": FRAMES, "channels": 6,
                            "development_encoder": codec})
        for codec in ["pcm_s16le", "pcm_s16be", "pcm_s24le", "pcm_s24be", "pcm_s32le",
                      "pcm_s32be", "pcm_f32le", "pcm_f32be", "pcm_f64le", "pcm_f64be"]:
            path = temp / (codec.replace("_", "-") + ".mov")
            base.run("-i", str(video), "-i", str(source), "-map", "0:v", "-map", "1:a",
                     "-c:v", "copy", "-c:a", codec, str(path))
            write_fixture(directory / path.name, base.rewrite_movie(path.read_bytes(), [11, 42]))
            records.append({"file": path.name, "audible_frames": FRAMES, "lossless": True})
        # PCM is packed directly from known integer/float samples, independently
        # of the backend decoder. Variable packet sizes include a short final block.
        raw = source.read_bytes()
        pcm = raw[raw.index(b"data") + 8:]
        values = struct.unpack("<" + "h" * (len(pcm) // 2), pcm)
        for family, widths, endian in [("s", (16, 24, 32), "le"), ("s", (16, 24, 32), "be"),
                                      ("f", (32, 64), "le"), ("f", (32, 64), "be")]:
            for bits in widths:
                name = f"pcm-{family}{bits}{endian}.mkv"
                order = "little" if endian == "le" else "big"
                if family == "s":
                    payload = b"".join((value << (bits - 16)).to_bytes(
                        bits // 8, order, signed=True) for value in values)
                    identifier = "A_PCM/INT/LIT" if endian == "le" else "A_PCM/INT/BIG"
                else:
                    payload = b"".join(struct.pack(("<" if endian == "le" else ">") +
                                                   ("f" if bits == 32 else "d"), value / 32768)
                                       for value in values)
                    # Matroska IEEE float is little endian only.
                    if endian == "be":
                        continue
                    identifier = "A_PCM/FLOAT/IEEE"
                stride = bits // 8 * 2
                packets = [(start, payload[start * stride:min(start + 1537, FRAMES) * stride], 0, True)
                           for start in range(0, FRAMES, 1537)]
                write_fixture(directory / name, movie(identifier, packets, b"", video_data,
                                                    bit_depth=bits))
                records.append({"file": name, "audible_frames": FRAMES, "lossless": True})
                if family == "s" and bits == 16 and endian == "le":
                    write_fixture(directory / "pcm-leading-discard.mkv", movie(
                        identifier, packets, b"", video_data, bit_depth=bits, padding_override=-96))
                    records.append({"file": "pcm-leading-discard.mkv", "leading_discard": 96})
                    write_fixture(directory / "pcm-over-limit-discard.mkv", movie(
                        identifier, packets, b"", video_data, bit_depth=bits, padding_override=-65537))
                    records.append({"file": "pcm-over-limit-discard.mkv", "unsupported": True,
                                    "block_skip_samples": 65537})
                    write_fixture(directory / "pcm-unsupported-depth.mkv", movie(
                        identifier, packets, b"", video_data, bit_depth=bits, declared_depth=8))
                    records.append({"file": "pcm-unsupported-depth.mkv", "unsupported": True})
    for record in records:
        path = directory / record["file"]
        record.update(bytes=path.stat().st_size, sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    provenance = {"license": "Apache-2.0; original synthesized inputs and resulting test media",
                  "generator": Path(__file__).name, "tool_version": version.splitlines()[0],
                  "tool_configuration": next(line for line in version.splitlines()
                                             if line.startswith("configuration:")),
                  "tool_distribution": "development only; executable not redistributed",
                  "encoder_commands": base.ENCODER_COMMANDS, "inputs": inputs,
                  "rate": RATE, "original_frames": FRAMES,
                  "video_frames": 13, "video_duration_samples_48khz": 312000,
                  "identities": {"video": 11, "main": 42}, "timestamp_scale_ns": 1,
                  "fixtures": records}
    (directory / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "expanded-decoder")
