# SPDX-License-Identifier: Apache-2.0
"""Owned native-codec copy table controls; no downloaded media or executable."""
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile
import wave

import generate_decoder_fixtures as base


def without_audio(data):
    def rewrite(kind, body):
        if kind == b"moov":
            parts = []
            for tag, value in base.boxes(body):
                if tag == b"trak":
                    mdia = next(part for child, part in base.boxes(value) if child == b"mdia")
                    handler = next(part for child, part in base.boxes(mdia) if child == b"hdlr")
                    if handler[8:12] == b"soun":
                        continue
                parts.append(base.box(tag, value))
            body = b"".join(parts)
        return base.box(kind, body)
    return b"".join(rewrite(kind, body) for kind, body in base.boxes(data))


def rewrite_subtitle_ids(data):
    next_id = iter((11, 42, 63))
    def rewrite(kind, body, track_id=0):
        if kind == b"trak":
            track_id = next(next_id)
        if kind == b"tkhd":
            body = bytearray(body)
            assert body[0] in (0, 1)
            offset = 12 if body[0] == 0 else 20
            body[offset:offset + 4] = track_id.to_bytes(4, "big")
            body[1:4] = (int.from_bytes(body[1:4], "big") | 1).to_bytes(3, "big")
        if kind in (b"moov", b"trak", b"mdia", b"minf", b"stbl", b"edts"):
            body = b"".join(rewrite(child, payload, track_id) for child, payload in base.boxes(body))
        return base.box(kind, body)
    return b"".join(rewrite(kind, body) for kind, body in base.boxes(data))


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    video = directory.parent / "expanded-decoder" / "pcm-s16le.mkv"
    subtitle_source = directory.parent / "hls" / "dts-subtitles.mp4"
    records, inputs = [], []

    def save(name, data, **metadata):
        (directory / name).write_bytes(data)
        records.append(dict(file=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                            **metadata))

    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        temporary = Path(temporary)
        for rate in (44100, 48000):
            for channels in (1, 2, 6):
                wav = temporary / f"tone-{rate}-{channels}.wav"
                frequencies = [173] if channels == 1 else ([173, 227] if channels == 2
                                                          else [310, 470, 630, 80, 790, 950])
                pcm = b"".join(struct.pack("<h", round(4096 * math.sin(2 * math.pi * frequency * sample / rate)))
                               for sample in range(rate * 3) for frequency in frequencies)
                if channels == 6:
                    # Preserve the explicit original WAVEFORMATEXTENSIBLE 5.1 mask.
                    base.wave(wav, channels, frames=rate * 3, rate=rate)
                else:
                    with wave.open(str(wav), "wb") as output:
                        output.setnchannels(channels)
                        output.setsampwidth(2)
                        output.setframerate(rate)
                        output.writeframes(pcm)
                inputs.append(dict(rate=rate, channels=channels, frames=rate * 3,
                                   sha256=hashlib.sha256(wav.read_bytes()).hexdigest()))
                for codec in ("aac", "ac3", "eac3"):
                    encoded = temporary / "movie.mp4"
                    base.run("-i", str(video), "-i", str(wav), "-map", "0:v:0", "-map", "1:a:0",
                             "-map_metadata", "-1", "-c:v", "copy", "-c:a", codec, "-b:a", "384k",
                             "-strict", "-2", str(encoded))
                    data = base.rewrite_movie(encoded.read_bytes(), [11, 42])
                    name = f"{codec}-{rate}-{channels}.mp4"
                    save(name, data, codec=codec, rate=rate, channels=channels,
                         video_frames_48khz=312000)
                    if codec == "aac" and rate == 48000 and channels == 2:
                        save("video-only.mp4", without_audio(data), video_frames_48khz=312000)
        target = temporary / "subtitles.mp4"
        base.run("-i", str(subtitle_source), "-i", str(temporary / "tone-48000-6.wav"),
                 "-map", "0:v:0", "-map", "1:a:0", "-map", "0:s:0", "-map_metadata", "-1",
                 "-c:v", "copy", "-c:s", "copy", "-c:a", "aac", "-b:a", "384k", str(target))
        save("aac-subtitles.mp4", rewrite_subtitle_ids(target.read_bytes()),
             codec="aac", channels=6, rate=48000, copied_subtitles=True)
    provenance = dict(license="Apache-2.0; owned tones/video/subtitle sources",
                      generator=Path(__file__).name, inputs=inputs,
                      source_hashes={video.name: hashlib.sha256(video.read_bytes()).hexdigest(),
                                     subtitle_source.name: hashlib.sha256(subtitle_source.read_bytes()).hexdigest()},
                      development_tool=subprocess.check_output(["ffmpeg", "-version"], text=True).splitlines()[0],
                      tool_distribution="development use only; not incorporated or redistributed",
                      encoder_commands=base.ENCODER_COMMANDS, fixtures=records)
    (directory / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "audio-policy")
