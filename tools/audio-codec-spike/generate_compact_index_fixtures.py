# SPDX-License-Identifier: Apache-2.0
"""Owned silent Opus cycle for high-packet-count sparse-source controls."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import wave

import generate_decoder_fixtures as base
import generate_expanded_decoder_fixtures as expanded
import generate_coarse_clock_fixtures as clocks


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    original = directory.parent / "compressed-index" / "truehd-fine.mkv"
    segment = list(clocks.elements(original.read_bytes()))[1][1]
    tracks = next(body for tag, body, _ in clocks.elements(segment) if tag == "1654ae6b")
    entry = list(clocks.elements(tracks))[0][1]
    private = next(body for tag, body, _ in clocks.elements(entry) if tag == "63a2")
    cluster = next(body for tag, body, _ in clocks.elements(segment) if tag == "1f43b675")
    picture = next(body[4:] for tag, body, _ in clocks.elements(cluster)
                   if tag == "a3" and body[0] == 0x8b)
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        temporary = Path(temporary)
        path = temporary / "silence.wav"
        with wave.open(str(path), "wb") as output:
            output.setnchannels(2)
            output.setsampwidth(2)
            output.setframerate(48000)
            output.writeframes(bytes(48000 * 4))
        input_hash = hashlib.sha256(path.read_bytes()).hexdigest()
        encoded = temporary / "silence.opus"
        base.run("-i", str(path), "-c:a", "opus", "-strict", "-2", "-opus_delay", "20",
                 "-f", "opus", str(encoded))
        packets, configuration = expanded.encoded(encoded)
        delay = -packets[0][0]
        # Keep the complete coded cycle, without Ogg's final audible discard.
        packets = [(pts, data, 0, key) for pts, data, _, key in packets[:50]]

        assert len(packets) == 50 and all(pts + delay == index * 960
                                        for index, (pts, _, _, _) in enumerate(packets))
        data = expanded.movie("A_OPUS", packets, configuration,
                              ([(0, picture), (500, picture)], private), delay,
                              presentation_duration_ns=1000000000)
        name = "opus-silence.mkv"
        expanded.write_fixture(directory / name, data)
    provenance = dict(license="Apache-2.0; synthesized silence and owned D80 video",
                      generator=Path(__file__).name, input_wav_sha256=input_hash,
                      video_source_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
                      development_tool=subprocess.check_output(["ffmpeg", "-version"],
                                                               text=True).splitlines()[0],
                      encoder_commands=base.ENCODER_COMMANDS,
                      fixtures=[dict(file=name, bytes=len(data),
                                     sha256=hashlib.sha256(data).hexdigest(),
                                     coded_frames=48000, codec_delay_samples=delay,
                                     packets=50)])
    (directory / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "compact-index")
