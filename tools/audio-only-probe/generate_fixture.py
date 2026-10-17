#!/usr/bin/env python3
"""Generate an original stereo tone fixture and an AAC-only M4A for receiver probes.

Requires FFmpeg and ffprobe as external development tools. No private media or
third-party fixture content is used. The manifest records tool versions and
hashes; successful generation does not establish receiver interoperability.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import wave

SAMPLE_RATE = 48_000
DURATION_SECONDS = 64
CHANNELS = 2
PEAK = 0.15


def channel_tones(second):
    """Return left/right tone frequencies for each named eight-second interval."""
    interval = int(second) // 8
    return [(440, 0), (0, 660), (880, 880), (440, 0),
            (1100, 1100), (0, 660), (440, 0), (880, 880)][interval]


def generate_wave(path):
    """Write deterministic 16-bit little-endian PCM with a brief gap each second."""
    with wave.open(str(path), "wb") as output:
        output.setparams((CHANNELS, 2, SAMPLE_RATE, 0, "NONE", "not compressed"))
        for second in range(DURATION_SECONDS):
            left, right = channel_tones(second)
            frames = bytearray()
            for sample in range(SAMPLE_RATE):
                phase = sample / SAMPLE_RATE
                # A 10-ms fade at either edge keeps the 0.75-second pulses smooth.
                envelope = max(0.0, min(1.0, phase / 0.01, (0.75 - phase) / 0.01))
                values = [round(32767 * PEAK * envelope * math.sin(2 * math.pi * frequency * phase))
                          if frequency else 0 for frequency in (left, right)]
                frames.extend(struct.pack("<hh", *values))
            output.writeframes(frames)


def digest(path):
    """Hash only the generated fixture bytes."""
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_directory", type=Path)
    args = parser.parse_args()
    ffmpeg = shutil.which("ffmpeg")
    ffprobe = shutil.which("ffprobe")
    if not ffmpeg or not ffprobe:
        parser.error("FFmpeg and ffprobe must be available on PATH")
    args.output_directory.mkdir(parents=True, exist_ok=True)
    wav = args.output_directory / "audio-only-tones.wav"
    media = args.output_directory / "audio-only-tones.m4a"
    if wav.exists() or media.exists():
        parser.error("Use a fresh output directory to preserve existing evidence")
    generate_wave(wav)
    subprocess.run([ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-i", str(wav),
                    "-map", "0:a:0", "-vn", "-c:a", "aac", "-profile:a", "aac_low",
                    "-b:a", "192k", "-movflags", "+faststart", "-map_metadata", "-1", str(media)], check=True)
    probe = json.loads(subprocess.check_output(
        [ffprobe, "-v", "error", "-show_streams", "-show_format", "-of", "json", str(media)], text=True))
    streams = probe["streams"]
    if len(streams) != 1 or streams[0]["codec_type"] != "audio" or streams[0]["codec_name"] != "aac":
        raise RuntimeError("Expected exactly one AAC audio stream and no video")
    stream = streams[0]
    if stream["profile"] != "LC" or int(stream["sample_rate"]) != SAMPLE_RATE or stream["channels"] != CHANNELS:
        raise RuntimeError("Unexpected AAC profile, rate or channel count")
    manifest = {
        "provenance": "Original generated mathematical tones; Apache-2.0 repository tool; no private media",
        "duration_seconds": DURATION_SECONDS,
        "sample_rate_hz": SAMPLE_RATE,
        "channels": CHANNELS,
        "video_streams": 0,
        "codec": "AAC-LC",
        "container": "M4A; faststart",
        "wave_sha256": digest(wav),
        "m4a_sha256": digest(media),
        "m4a_bytes": media.stat().st_size,
        "tone_intervals": [
            {"start_seconds": start, "end_seconds": start + 8,
             "left_hz": channel_tones(start)[0], "right_hz": channel_tones(start)[1]}
            for start in range(0, DURATION_SECONDS, 8)
        ],
        "pulse": "0.75 seconds sound, 0.25 seconds silence; 10-ms fades",
        "ffmpeg_version": subprocess.check_output([ffmpeg, "-version"], text=True).splitlines()[0],
        "ffprobe_version": subprocess.check_output([ffprobe, "-version"], text=True).splitlines()[0],
    }
    (args.output_directory / "fixture.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
