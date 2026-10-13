#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original 16.5-second movie fixtures for lazy AAC HLS and audible joins.

Development FFmpeg encoder only; no tool binary or downloaded media is shipped.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import generate_decoder_fixtures as source


def rewrite_ids(data, order):
    track_index = 0
    def rewrite(kind, body, track_id=0):
        nonlocal track_index
        if kind == b"trak":
            track_id = order[track_index]
            track_index += 1
        if kind == b"tkhd":
            body = bytearray(body)
            identity_at = 20 if body[0] == 1 else 12
            body[identity_at:identity_at + 4] = track_id.to_bytes(4, "big")
            body[1:4] = (int.from_bytes(body[1:4], "big") | 1).to_bytes(3, "big")
        if kind in (b"moov", b"trak", b"mdia", b"minf", b"stbl", b"edts"):
            body = b"".join(rewrite(child, payload, track_id) for child, payload in source.boxes(body))
        return source.box(kind, body)
    return b"".join(rewrite(kind, body) for kind, body in source.boxes(data))


def video_rounding_boundary(data):
    """Independently author 6.484, 6.490, 3.526 s video spans in a 48 kHz clock.

    33 original I-frames; mark only samples 1/14/27 as sync to control HLS cuts.
    moov follows mdat, so changed table sizes do not move source packet bytes.
    """
    durations = [23940] * 12 + [23952] + [23960] * 12 + [24000] + [24178] * 6 + [24180]
    assert len(durations) == 33 and sum(durations) == 792000
    def rewrite(kind, body, track_id=0):
        if kind == b"trak":
            tkhd = next(payload for child, payload in source.boxes(body) if child == b"tkhd")
            identity_at = 20 if tkhd[0] == 1 else 12
            track_id = int.from_bytes(tkhd[identity_at:identity_at + 4], "big")
        if track_id == 11:
            if kind == b"mdhd":
                body = bytearray(body)
                assert body[0] == 0
                body[12:16] = (48000).to_bytes(4, "big")
                body[16:20] = (792000).to_bytes(4, "big")
            elif kind == b"stts":
                body = b"\0" * 4 + len(durations).to_bytes(4, "big")
                body += b"".join((1).to_bytes(4, "big") + duration.to_bytes(4, "big") for duration in durations)
            elif kind == b"stss":
                body = b"\0" * 4 + (3).to_bytes(4, "big") + b"".join(sample.to_bytes(4, "big") for sample in (1, 14, 27))
            elif kind == b"stbl" and not any(child == b"stss" for child, _ in source.boxes(body)):
                # All-sync input omits stss; add an explicit conservative sync table.
                body += source.box(b"stss", b"\0" * 4 + (3).to_bytes(4, "big") +
                                   b"".join(sample.to_bytes(4, "big") for sample in (1, 14, 27)))
        if kind in (b"moov", b"trak", b"mdia", b"minf", b"stbl", b"edts"):
            body = b"".join(rewrite(child, payload, track_id) for child, payload in source.boxes(body))
        return source.box(kind, body)
    return b"".join(rewrite(kind, body) for kind, body in source.boxes(data))


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        temp = Path(temporary)
        wav = temp / "main.wav"
        inputs = [source.wave(wav, 6, frames=792000)]
        video = temp / "video.mp4"
        source.run("-f", "lavfi", "-i", "color=black:s=16x16:r=2:d=16.5", "-c:v", "libx264",
                   "-bf", "0", "-g", "1", "-an", str(video))
        short = temp / "short.wav"
        inputs.append(source.wave(short, 6, frames=144000))
        short_movie = temp / "short.mp4"
        source.run("-i", str(video), "-i", str(short), "-map", "0:v", "-map", "1:a",
                   "-c:v", "copy", "-c:a", "aac", "-b:a", "448k", str(short_movie))
        (directory / "aac-short.mp4").write_bytes(source.rewrite_movie(short_movie.read_bytes(), [11, 42]))
        (directory / "aac-edits.mp4").write_bytes(source.rewrite_movie(short_movie.read_bytes(), [11, 42], edited=True))
        for name, codec in [("aac", "aac"), ("ac3", "ac3"), ("eac3", "eac3"), ("dts", "dca")]:
            movie = temp / f"{name}.mp4"
            source.run("-i", str(video), "-i", str(wav), "-map", "0:v", "-map", "1:a", "-c:v", "copy",
                       "-c:a", codec, "-strict", "-2", "-b:a", "1411200" if name == "dts" else "448k", str(movie))
            (directory / f"{name}.mp4").write_bytes(source.rewrite_movie(movie.read_bytes(), [11, 42]))
            if name == "aac":
                (directory / "aac-rounding.mp4").write_bytes(video_rounding_boundary((directory / "aac.mp4").read_bytes()))
            if name == "dts":
                captions = temp / "captions.srt"
                captions.write_text("1\n00:00:01,000 --> 00:00:02,000\nOriginal caption\n", encoding="utf-8")
                subtitled = temp / "subtitled.mp4"
                source.run("-i", str(video), "-i", str(wav), "-i", str(captions),
                           "-map", "0:v", "-map", "1:a", "-map", "2:s", "-c:v", "copy",
                           "-c:a", "dca", "-strict", "-2", "-b:a", "1411200", "-c:s", "mov_text", str(subtitled))
                (directory / "dts-subtitles.mp4").write_bytes(rewrite_ids(subtitled.read_bytes(), [11, 42, 51]))
                reordered = temp / "reordered.mp4"
                source.run("-f", "lavfi", "-i", "color=black:s=16x16:r=2:d=16.5", "-i", str(wav),
                           "-map", "0:v", "-map", "1:a", "-c:v", "libx264", "-bf", "2", "-g", "12",
                           "-c:a", "dca", "-strict", "-2", "-b:a", "1411200", str(reordered))
                (directory / "dts-bframes.mp4").write_bytes(source.rewrite_movie(reordered.read_bytes(), [11, 42]))
        # Known clocks/cues authored independently; repeats continue beyond two joins.
        dts = temp / "main.mka"
        source.run("-i", str(wav), "-c:a", "dca", "-strict", "-2", "-b:a", "1411200", str(dts))
        vp, vc = source.encoded_track(video)
        ap, ac = source.encoded_track(dts)
        (directory / "dts.mkv").write_bytes(source.matroska([
            (11, "V_MPEG4/ISO/AVC", vp, vc, 0), (42, "A_DTS", ap, ac, 6)]))
    version = subprocess.check_output(["ffmpeg", "-version"], text=True)
    records = [{"file": path.name, "bytes": path.stat().st_size,
                "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
               for path in sorted(directory.iterdir()) if path.suffix in (".mp4", ".mkv")]
    (directory / "provenance.json").write_text(json.dumps({
        "license": "Apache-2.0; original synthesized input and resulting test media",
        "generator": "generate_hls_fixtures.py", "tool_version": version.splitlines()[0],
        "tool_configuration": next(line for line in version.splitlines() if line.startswith("configuration:")),
        "tool_distribution": "development only; not incorporated or redistributed",
        "inputs": inputs, "encoder_commands": source.ENCODER_COMMANDS,
        "identities": {"video": 11, "main": 42}, "frames_48khz": 792000,
        "rounding_video": {"timescale": 48000, "segment_samples": [311232, 311520, 169248],
                           "sync_samples_one_based": [1, 14, 27]},
        "fixtures": records}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "hls")
