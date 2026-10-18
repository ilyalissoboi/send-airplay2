#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original tones for the private unedited MP4/MOV audio-only foundation.

Uses repository-owned tone synthesis and the external development FFmpeg CLI.
No media from users or third parties; no new linked dependency or source import.
"""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

import generate_decoder_fixtures as base


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        temp = Path(temporary)
        cases = [
            ("flac-stereo.mp4", 2, 48000, 624144, "flac", False, False),
            ("flac-44100.mp4", 2, 44100, 573432, "flac", False, False),
            ("pcm-51.mov", 6, 48000, 144144, "pcm_s16le", False, False),
            ("aac.mp4", 2, 48000, 144144, "aac", False, False),
            ("edited-pcm.mov", 2, 48000, 144144, "pcm_s16le", True, False),
            ("multiple-flac.mp4", 2, 48000, 144144, "flac", False, True),
        ]
        for name, channels, rate, frames, codec, edited, multiple in cases:
            source = temp / "tones.wav"
            original = base.wave(source, channels, frames=frames, rate=rate)
            path = directory / name
            arguments = ["-i", str(source), "-map", "0:a"]
            if multiple:
                arguments += ["-map", "0:a"]
            base.run(*arguments, "-c:a", codec, "-strict", "-2",
                     "-use_editlist", str(int(edited)), str(path))
            if multiple:
                # MP4 muxing can disable nondefault tracks. Author both enabled
                # identities explicitly so this tests ambiguous selection.
                path.write_bytes(base.rewrite_movie(path.read_bytes(), [7, 42]))
            streams = json.loads(subprocess.check_output([
                "ffprobe", "-v", "error", "-show_streams", "-of", "json", str(path)
            ], text=True))["streams"]
            assert all(stream["codec_type"] == "audio" for stream in streams)
            records.append({"file": name, "original_tones": original,
                            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                            "bytes": path.stat().st_size, "streams": len(streams),
                            "edited": edited})
    (directory / "provenance.json").write_text(json.dumps({
        "license": "Apache-2.0 repository-owned synthetic tones",
        "generator": "generate_audio_only_fixtures.py",
        "ffmpeg": subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
        "files": records,
    }, indent=2) + "\n")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "audio-only")
