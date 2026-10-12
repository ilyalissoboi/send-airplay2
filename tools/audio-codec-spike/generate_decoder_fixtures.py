#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original finite movie fixtures. FFmpeg CLI is a development-only encoder.

Container IDs, edits and Matroska clocks are authored here, independently of
the native parser/adapter. No downloaded media or tool executable is packaged.
"""
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ENCODER_COMMANDS = []


def run(*args):
    ENCODER_COMMANDS.append([Path(arg).name if Path(arg).is_absolute() else arg for arg in args])
    subprocess.run(["ffmpeg", "-y", "-hide_banner", "-loglevel", "error", *args], check=True)


def wave(path, channels, frames=144000, rate=48000):
    frequencies = [310, 470, 630, 80, 790, 950] if channels == 6 else [173, 227]
    pcm = bytearray()
    for frame in range(frames):
        for frequency in frequencies:
            pcm.extend(struct.pack("<h", round(4096 * math.sin(2 * math.pi * frequency * frame / rate))))
    fmt = struct.pack("<HHIIHHHHI", 0xFFFE, channels, rate, rate * channels * 2,
                      channels * 2, 16, 22, 16, 0x3F if channels == 6 else 3)
    fmt += bytes.fromhex("0100000000001000800000aa00389b71")
    body = b"WAVEfmt " + struct.pack("<I", len(fmt)) + fmt + b"data" + struct.pack("<I", len(pcm)) + pcm
    path.write_bytes(b"RIFF" + struct.pack("<I", len(body)) + body)
    return {"channels": channels, "rate": rate, "frames": frames, "frequencies_hz": frequencies,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def boxes(data):
    offset = 0
    while offset < len(data):
        size = int.from_bytes(data[offset:offset + 4], "big")
        assert size >= 8 and offset + size <= len(data)
        yield data[offset + 4:offset + 8], data[offset + 8:offset + size]
        offset += size


def box(kind, body):
    return struct.pack(">I", len(body) + 8) + kind + body


def rewrite_movie(data, order, edited=False, disabled=False, unsupported=False, protected=False):
    track_index = 0
    moov = next(body for kind, body in boxes(data) if kind == b"moov")
    mvhd = next(body for kind, body in boxes(moov) if kind == b"mvhd")
    assert mvhd[0] == 0
    movie_scale = int.from_bytes(mvhd[12:16], "big")
    assert movie_scale % 4 == 0

    def rewrite(kind, body, track_id=0):
        nonlocal track_index
        if kind == b"trak":
            track_id = order[track_index]
            track_index += 1
        if kind == b"tkhd":
            body = bytearray(body)
            assert body[0] == 0
            body[12:16] = track_id.to_bytes(4, "big")
            body[1:4] = (int.from_bytes(body[1:4], "big") | 1).to_bytes(3, "big")
            if disabled and track_id == 42:
                body[1:4] = b"\0\0\0"
            return box(kind, body)
        if kind == b"elst" and track_id == 42 and edited:
            assert body[0] == 0 and int.from_bytes(body[4:8], "big") == 1
            original_media_start = int.from_bytes(body[12:16], "big", signed=True)
            # Retained movie clock, audio 48000 Hz: initial silence 250 ms,
            # then source offset 500 ms after the existing AAC priming edit.
            entries = struct.pack(">IiHH", movie_scale // 4, -1, 1, 0)
            entries += struct.pack(">IiHH", movie_scale * 5 // 2, original_media_start + 24000, 1, 0)
            return box(kind, b"\0\0\0\0" + struct.pack(">I", 2) + entries)
        if kind == b"stsd" and track_id == 42 and (unsupported or protected):
            body = bytearray(body)
            assert body[12:16] == b"mp4a"
            body[12:16] = b"enca" if protected else b"zzzz"
            return box(kind, body)
        if kind in (b"moov", b"trak", b"mdia", b"minf", b"stbl", b"edts"):
            body = b"".join(rewrite(child, payload, track_id) for child, payload in boxes(body))
        return box(kind, body)

    # moov follows mdat, so changing edit/metadata sizes leaves packet offsets intact.
    return b"".join(rewrite(kind, body) for kind, body in boxes(data))


def unhex_dump(text):
    data = bytearray()
    for line in text.splitlines():
        if ": " not in line:
            continue
        words = line.split(": ", 1)[1].split("  ", 1)[0]
        data.extend(bytes.fromhex(re.sub(r"\s", "", words)))
    return bytes(data)


def encoded_track(path):
    result = subprocess.check_output(["ffprobe", "-v", "error", "-show_packets", "-show_streams",
                                      "-show_data", "-of", "json", str(path)], text=True)
    result = json.loads(result)
    stream = result["streams"][0]
    packets = [(round(float(packet["pts_time"]) * 1000), unhex_dump(packet["data"]))
               for packet in result["packets"] if float(packet["pts_time"]) >= 0]
    return packets, unhex_dump(stream.get("extradata", ""))


def vint(value):
    for width in range(1, 9):
        if value < (1 << (7 * width)) - 1:
            return ((1 << (7 * width)) | value).to_bytes(width, "big")
    raise ValueError("EBML size too large")


def element(tag, data):
    return bytes.fromhex(tag) + vint(len(data)) + data


def uint(tag, value, width=None):
    return element(tag, value.to_bytes(width or max(1, (value.bit_length() + 7) // 8), "big"))


def matroska(tracks, delay=False, spoof=False):
    header = b"".join([uint("4286", 1), uint("42f7", 1), uint("42f2", 4), uint("42f3", 8),
                       element("4282", b"matroska"), uint("4287", 4), uint("4285", 2)])
    info = uint("2ad7b1", 1000000) + element("4489", struct.pack(">d", 3020.0))
    info += element("4d80", b"sap2-owned-fixture") + element("5741", b"sap2-owned-fixture")
    entries, packets = [], []
    for track in tracks:
        number, codec, payloads, private, channels = track
        entry = uint("d7", number) + uint("73c5", number + 1000) + uint("83", 1 if not channels else 2)
        entry += uint("b9", 1) + uint("88", 1 if number == 42 else 0) + element("86", codec.encode())
        if private:
            entry += element("63a2", private)
        if channels:
            entry += element("e1", element("b5", struct.pack(">d", 48000.0)) + uint("9f", channels))
        else:
            entry += element("e0", uint("b0", 16) + uint("ba", 16))
        if delay and number == 42:
            entry += uint("56aa", 20000000) + uint("56bb", 80000000)
        entries.append(element("ae", entry))
        packets.extend((pts + (20 if delay and number == 42 else 0), number, data)
                       for pts, data in payloads)
    packets.sort(key=lambda packet: packet[:2])
    prefix = element("1549a966", info) + element("1654ae6b", b"".join(entries))
    if spoof:
        tags = []
        for track, fake in [(7, b"2a"), (42, b"7")]:
            tag = element("63c0", uint("63c5", track + 1000))
            tag += element("67c8", element("45a3", b"SAP2_MATROSKA_TRACK_NUMBER") + element("4487", fake))
            tags.append(element("7373", tag))
        prefix += element("1254c367", b"".join(tags))
    clusters = [element("1f43b675", uint("e7", pts) +
                        element("a3", vint(number) + b"\0\0\x80" + data))
                for pts, number, data in packets]

    def cues(start):
        position, parts = start, []
        for (pts, number, _), cluster in zip(packets, clusters):
            point = uint("b3", pts, 8) + element("b7", uint("f7", number) + uint("f1", position, 8))
            parts.append(element("bb", point))
            position += len(cluster)
        return element("1c53bb6b", b"".join(parts))

    cue_data = cues(0)
    cue_data = cues(len(prefix) + len(cue_data))
    return element("1a45dfa3", header) + element("18538067", prefix + cue_data + b"".join(clusters))


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output(["ffmpeg", "-version"], text=True)
    records = []
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        temp = Path(temporary)
        main, commentary = temp / "main.wav", temp / "commentary.wav"
        sources = [wave(main, 6), wave(commentary, 2)]
        video = temp / "video.mp4"
        run("-f", "lavfi", "-i", "color=black:s=16x16:r=2:d=3", "-c:v", "libx264",
            "-bf", "0", "-g", "1", "-an", str(video))
        for reverse in (False, True):
            target = temp / "movie.mp4"
            inputs = [main, commentary] if reverse else [commentary, main]
            run("-i", str(video), "-i", str(inputs[0]), "-i", str(inputs[1]), "-map", "0:v",
                "-map", "1:a", "-map", "2:a", "-map_metadata", "-1", "-c:v", "copy",
                "-c:a", "aac", "-b:a", "384k", str(target))
            order = [11, 42, 7] if reverse else [11, 7, 42]
            name = "aac-main-first.mp4" if reverse else "aac-commentary-first.mp4"
            (directory / name).write_bytes(rewrite_movie(target.read_bytes(), order))
            if not reverse:
                for name, options in [("aac-edits.mp4", {"edited": True}),
                                      ("aac-disabled.mp4", {"disabled": True}),
                                      ("aac-unknown.mp4", {"unsupported": True}),
                                      ("aac-protected.mp4", {"protected": True})]:
                    (directory / name).write_bytes(rewrite_movie(target.read_bytes(), order, **options))
        video_packets, video_private = encoded_track(video)
        comment = temp / "comment.mka"
        run("-i", str(commentary), "-c:a", "aac", "-b:a", "128k", str(comment))
        comment_packets, comment_private = encoded_track(comment)
        for name, codec, codec_id in [("dts", "dca", "A_DTS"), ("ac3", "ac3", "A_AC3"),
                                      ("eac3", "eac3", "A_EAC3")]:
            encoded = temp / "main.mka"
            run("-i", str(main), "-c:a", codec, "-strict", "-2", "-b:a",
                "1411200" if name == "dts" else "448k", str(encoded))
            payloads, private = encoded_track(encoded)
            tracks = [(11, "V_MPEG4/ISO/AVC", video_packets, video_private, 0),
                      (7, "A_AAC", comment_packets, comment_private, 2),
                      (42, codec_id, payloads, private, 6)]
            (directory / f"{name}-commentary-first.mkv").write_bytes(matroska(tracks))
            movie = temp / "compressed.mp4"
            # Encode directly into MP4's audio clock instead of importing MKA's
            # millisecond-quantized packet timestamps into a sample-granular clock.
            run("-i", str(video), "-i", str(comment), "-i", str(main),
                "-map", "0:v", "-map", "1:a", "-map", "2:a", "-c:v", "copy",
                "-c:a:0", "copy", "-c:a:1", codec, "-b:a:1",
                "1411200" if name == "dts" else "448k", "-strict", "-2", str(movie))
            (directory / f"{name}-commentary-first.mp4").write_bytes(rewrite_movie(movie.read_bytes(), [11, 7, 42]))
            if name == "ac3":
                stereo = temp / "stereo.mka"
                run("-i", str(commentary), "-c:a", "ac3", "-b:a", "192k", str(stereo))
                stereo_packets, _ = encoded_track(stereo)
                assert len(stereo_packets) == len(payloads)
                changed = [(pts, data if pts < 1500 else stereo_packets[index][1])
                           for index, (pts, data) in enumerate(payloads)]
                changed_main = (42, codec_id, changed, private, 6)
                (directory / "ac3-format-change.mkv").write_bytes(matroska([tracks[0], tracks[1], changed_main]))
            if name == "dts":
                (directory / "dts-main-first.mkv").write_bytes(matroska([tracks[0], tracks[2], tracks[1]]))
                (directory / "dts-delay.mkv").write_bytes(matroska(tracks, delay=True))
                (directory / "dts-spoofed-tags.mkv").write_bytes(matroska(tracks, spoof=True))
                large_main = (0x10000002A, *tracks[2][1:])
                (directory / "dts-large-id.mkv").write_bytes(matroska([tracks[0], tracks[1], large_main]))
        resampled = temp / "resampled.mp4"
        run("-i", str(video), "-i", str(main), "-map", "0:v", "-map", "1:a",
            "-c:v", "copy", "-c:a", "aac", "-ar", "44100", "-b:a", "384k", str(resampled))
        (directory / "aac-44100.mp4").write_bytes(rewrite_movie(resampled.read_bytes(), [11, 42]))
    for path in sorted(directory.iterdir()):
        if path.suffix in (".mp4", ".mkv"):
            records.append({"file": path.name, "bytes": path.stat().st_size,
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    record = {"license": "Apache-2.0; original synthesized input and resulting test media",
              "generator": "generate_decoder_fixtures.py", "tool_version": version.splitlines()[0],
              "tool_configuration": next(line for line in version.splitlines() if line.startswith("configuration:")),
              "tool_distribution": "development use only; not incorporated or redistributed",
              "encoder_commands": ENCODER_COMMANDS,
              "inputs": sources, "identities": {"video": 11, "commentary": 7, "main": 42},
              "matroska_timestamp_scale_ns": 1000000, "delay_case_ns": 20000000,
              "preroll_case_ns": 80000000, "fixtures": records}
    (directory / "provenance.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "decoder")
