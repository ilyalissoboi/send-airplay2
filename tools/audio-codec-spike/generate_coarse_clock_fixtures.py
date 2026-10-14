# SPDX-License-Identifier: Apache-2.0
"""Retime original D77 Matroska fixtures to an ordinary millisecond clock.

Payloads, CodecDelay and signed DiscardPadding are unchanged. Cue byte offsets
are rebuilt independently, and no external media or encoder is needed.
"""
import hashlib
import json
from pathlib import Path
from fractions import Fraction
import struct
import tempfile

import generate_decoder_fixtures as base
import generate_expanded_decoder_fixtures as expanded

SCALE = 1_000_000


def elements(data):
    offset = 0
    while offset < len(data):
        start = offset
        id_width = next(width for width in range(1, 5) if data[offset] & (1 << (8 - width)))
        tag = data[offset:offset + id_width].hex()
        offset += id_width
        size_width = next(width for width in range(1, 9) if data[offset] & (1 << (8 - width)))
        size = int.from_bytes(data[offset:offset + size_width], "big") & ((1 << (7 * size_width)) - 1)
        offset += size_width
        assert offset + size <= len(data)
        yield tag, data[offset:offset + size], start
        offset += size
    assert offset == len(data)


def rewrite(tag, body, positions=None):
    if tag == "2ad7b1":
        body = SCALE.to_bytes(3, "big")
    elif tag in ("e7", "b3"):
        # Integer nearest-tick rounding, independently of the decoder's clock.
        timestamp = (int.from_bytes(body, "big") + SCALE // 2) // SCALE
        body = timestamp.to_bytes(len(body), "big")
    elif tag == "f1" and positions is not None:
        body = positions[int.from_bytes(body, "big")].to_bytes(len(body), "big")
    elif tag == "4489":
        body = struct.pack(">d", struct.unpack(">d", body)[0] / SCALE)
    elif tag in ("1549a966", "1f43b675", "1c53bb6b", "bb", "b7"):
        body = b"".join(rewrite(child, value, positions) for child, value, _ in elements(body))
    return base.element(tag, body)


def coarse(data):
    top = list(elements(data))
    assert [tag for tag, _, _ in top] == ["1a45dfa3", "18538067"]
    children = list(elements(top[1][1]))
    rewritten = [rewrite(tag, value) for tag, value, _ in children]
    positions, offset = {}, 0
    for (tag, _, original_offset), value in zip(children, rewritten):
        if tag == "1f43b675":
            positions[original_offset] = offset
        offset += len(value)
    for index, (tag, value, _) in enumerate(children):
        if tag == "1c53bb6b":
            updated = rewrite(tag, value, positions)
            assert len(updated) == len(rewritten[index])
            rewritten[index] = updated
    return base.element(top[0][0], top[0][1]) + base.element("18538067", b"".join(rewritten))


def generate(directory):
    sources = directory.parent / "expanded-decoder"
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    for name in ["flac.mkv", "opus.mkv", "vorbis.mkv", "truehd.mkv", "pcm-s16le.mkv",
                 "pcm-s24be.mkv", "pcm-f64le.mkv", "opus-leading-discard.mkv",
                 "pcm-leading-discard.mkv", "flac-51.mkv", "opus-51.mkv", "vorbis-51.mkv",
                 "truehd-51.mkv"]:
        original = (sources / name).read_bytes()
        output = coarse(original)
        expanded.write_fixture(directory / name, output)
        records.append({"file": name, "original_sha256": hashlib.sha256(original).hexdigest(),
                        "sha256": hashlib.sha256(output).hexdigest(), "bytes": len(output)})
    # Original variable PCM packet lengths at a non-integer output-rate ratio.
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        rate, frames = 44100, 264732
        wave = Path(temporary) / "tones.wav"
        input_record = base.wave(wave, 2, frames=frames, rate=rate)
        raw = wave.read_bytes()
        pcm = raw[raw.index(b"data") + 8:]
        packets, start, index = [], 0, 0
        lengths = (997, 1537, 2111, 641)
        while start < frames:
            end = min(start + lengths[index % len(lengths)], frames)
            packets.append((Fraction(start * 48000, rate), pcm[start * 4:end * 4], 0, True))
            start, index = end, index + 1
        original_video = list(elements((sources / "pcm-s16le.mkv").read_bytes()))[1][1]
        tracks = next(body for tag, body, _ in elements(original_video) if tag == "1654ae6b")
        video_entry = list(elements(tracks))[0][1]
        private = next(body for tag, body, _ in elements(video_entry) if tag == "63a2")
        video_packets = []
        for tag, body, _ in elements(original_video):
            if tag != "1f43b675":
                continue
            fields = list(elements(body))
            block = next((value for kind, value, _ in fields if kind == "a3"), None)
            if block and block[0] == 0x8b:
                timestamp = int.from_bytes(fields[0][1], "big")
                assert block[1:3] == b"\0\0"
                video_packets.append((timestamp // 1000000, block[4:]))
        video = (video_packets, private)
        fine = expanded.movie("A_PCM/INT/LIT", packets, b"", video, bit_depth=16, coded_rate=rate)
        for name, data in [("pcm-variable-44100-fine.mkv", fine),
                           ("pcm-variable-44100.mkv", coarse(fine))]:
            expanded.write_fixture(directory / name, data)
            records.append({"file": name, "sha256": hashlib.sha256(data).hexdigest(),
                            "bytes": len(data), "input": input_record})
        # Extend video past ten seconds while keeping the short owned audio.
        long_video = ([(index * 500, video_packets[index % len(video_packets)][1])
                       for index in range(24)], private)
        long_data = expanded.movie("A_PCM/INT/LIT", packets, b"", long_video,
                                   bit_depth=16, coded_rate=rate, presentation_duration_ns=12000000000)
        expanded.write_fixture(directory / "long-video-fine.mkv", long_data)
        records.append({"file": "long-video-fine.mkv", "sha256": hashlib.sha256(long_data).hexdigest(),
                        "bytes": len(long_data), "video_frames": 24})
        output = coarse(long_data)
        expanded.write_fixture(directory / "over-limit-video.mkv", output)
        records.append({"file": "over-limit-video.mkv", "sha256": hashlib.sha256(output).hexdigest(),
                        "bytes": len(output), "video_frames": 24, "hls_unsupported": True})
        gapped = [(pts + (4800 if pts >= 96000 else 0), payload, discard, key)
                  for pts, payload, discard, key in packets]
        output = coarse(expanded.movie("A_PCM/INT/LIT", gapped, b"", video,
                                       bit_depth=16, coded_rate=rate))
        expanded.write_fixture(directory / "discontinuous.mkv", output)
        records.append({"file": "discontinuous.mkv", "sha256": hashlib.sha256(output).hexdigest(),
                        "bytes": len(output), "gap_samples_48khz": 4800})
    provenance = {"license": "Apache-2.0; original D77 payloads and synthesized PCM",
                  "generator": Path(__file__).name, "timestamp_scale_ns": SCALE,
                  "rounding": "nearest integer tick, ties upward",
                  "original_provenance": "../expanded-decoder/provenance.json",
                  "fixtures": records}
    (directory / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "coarse-clock")
