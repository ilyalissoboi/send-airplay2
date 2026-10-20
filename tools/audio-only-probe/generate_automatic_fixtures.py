#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate owned 64-second channel markers for the automatic audio-only gate.

Requires external development FFmpeg/ffprobe, including libopus/libvorbis.
Output stays in a fresh caller directory; no downloaded or private media.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

import generate_fixture as tones

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def encode(ffmpeg, source, output, options):
    subprocess.run([ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error",
                    "-i", str(source), "-map", "0:a:0", "-vn", "-map_metadata", "-1",
                    *options, str(output)], check=True, capture_output=True)


def generate(directory):
    ffmpeg, ffprobe = shutil.which("ffmpeg"), shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:
        raise RuntimeError("FFmpeg and ffprobe must be on PATH")
    if directory.exists() and any(directory.iterdir()):
        raise RuntimeError("Use a fresh directory to preserve existing evidence")
    directory.mkdir(parents=True, exist_ok=True)
    wave = directory / "pcm.wav"
    tones.generate_wave(wave)
    cases = [
        ("aac", "aac.m4a", ["-c:a", "aac", "-profile:a", "aac_low", "-b:a", "192k",
                            "-movflags", "+faststart"], "aac", 4, 1),
        ("flac", "native.flac", ["-c:a", "flac"], "flac", 3, 5),
        ("opus", "opus.ogg", ["-c:a", "libopus", "-b:a", "192k"], "opus", 3, 6),
        ("vorbis", "vorbis.ogg", ["-c:a", "libvorbis", "-q:a", "5"], "vorbis", 3, 7),
        ("mp4-flac", "flac.mp4", ["-c:a", "flac", "-strict", "-2", "-use_editlist", "0"], "flac", 3, 5),
        ("mov-pcm", "pcm.mov", ["-c:a", "pcm_s16le", "-use_editlist", "0"], "pcm_s16le", 3, 8),
        ("mkv-flac", "flac.mka", ["-c:a", "flac"], "flac", 3, 5),
        ("mkv-pcm", "pcm.mka", ["-c:a", "pcm_s16le"], "pcm_s16le", 3, 8),
        ("edited-pcm", "edited.mov", ["-c:a", "pcm_s16le", "-use_editlist", "1"], "pcm_s16le", 3, 8),
    ]
    records = [{"case": "wave", "file": wave.name, "codec": "pcm_s16le", "action": 3,
                "source_family": 8, "selected_id": 1, "bytes": wave.stat().st_size, "sha256": digest(wave)}]
    for label, name, options, codec, action, family in cases:
        output = directory / name
        encode(ffmpeg, wave, output, options)
        if label == "edited-pcm":
            # Reuse the checked original MP4 box author. Eight seconds of empty
            # edit plus source [8,64) keeps marker positions aligned after t=8.
            sys.path.insert(0, str(ROOT / "tools/audio-codec-spike"))
            from generate_audio_timeline_fixtures import with_edits
            output.write_bytes(with_edits(output.read_bytes(), 8 * 48000, 8 * 48000, 56 * 48000))
        probe = json.loads(subprocess.check_output([
            ffprobe, "-v", "error", "-show_streams", "-of", "json", str(output)], text=True))
        streams = probe["streams"]
        if (len(streams) != 1 or streams[0]["codec_type"] != "audio"
                or streams[0]["codec_name"] != codec or streams[0]["channels"] != 2
                or int(streams[0]["sample_rate"]) != 48000):
            raise RuntimeError("Generated audio stream metadata mismatch: " + label)
        records.append({"case": label, "file": name, "codec": codec, "action": action,
                        "source_family": family, "selected_id": 1,
                        "bytes": output.stat().st_size, "sha256": digest(output)})
    manifest = {"license": "Apache-2.0 original repository tone and edit authoring",
                "generator": "tools/audio-only-probe/generate_automatic_fixtures.py",
                "ffmpeg": subprocess.check_output([ffmpeg, "-version"], text=True).splitlines()[0],
                "ffprobe": subprocess.check_output([ffprobe, "-version"], text=True).splitlines()[0],
                "duration_seconds": 64, "sample_rate": 48000, "channels": 2, "video_streams": 0,
                "pulse": "0.75 s sound/0.25 s silence; 10 ms fades",
                "markers": [{"start": second, "left_hz": tones.channel_tones(second)[0],
                             "right_hz": tones.channel_tones(second)[1]} for second in range(0, 64, 8)],
                "edited_pcm": "8 s initial silence, then source [8,64); marker clock unchanged after 8 s",
                "files": records}
    (directory / "provenance.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"generated": len(records), "duration_seconds": 64, "video_streams": 0}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    try:
        generate(parser.parse_args().directory)
    except subprocess.CalledProcessError:
        raise SystemExit("External encoder/probe failed; retain the fresh directory for diagnosis")
