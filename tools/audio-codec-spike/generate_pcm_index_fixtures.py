# SPDX-License-Identifier: Apache-2.0
"""Original 30.003-second variable PCM movies; no external encoder needed."""
from fractions import Fraction
import hashlib
import json
from pathlib import Path
import tempfile

import generate_decoder_fixtures as base
import generate_expanded_decoder_fixtures as expanded
import generate_coarse_clock_fixtures as clocks


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    original = directory.parent / "expanded-decoder" / "pcm-s16le.mkv"
    segment = list(clocks.elements(original.read_bytes()))[1][1]
    tracks = next(body for tag, body, _ in clocks.elements(segment) if tag == "1654ae6b")
    video_entry = list(clocks.elements(tracks))[0][1]
    private = next(body for tag, body, _ in clocks.elements(video_entry) if tag == "63a2")
    first_cluster = next(body for tag, body, _ in clocks.elements(segment) if tag == "1f43b675")
    block = next(body for tag, body, _ in clocks.elements(first_cluster) if tag == "a3")
    assert block[:4] == b"\x8b\0\0\x80"
    # Original black all-IDR video, repeated at independently authored times.
    video = ([(index * 500, block[4:]) for index in range(61)], private)
    rate, frames = 44100, 1323132
    records = []
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
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
        fine = expanded.movie("A_PCM/INT/LIT", packets, b"", video, bit_depth=16,
                              coded_rate=rate, presentation_duration_ns=30500000000)
        for name, data in [("variable-44100-fine.mkv", fine),
                           ("variable-44100.mkv", clocks.coarse(fine))]:
            expanded.write_fixture(directory / name, data)
            records.append({"file": name, "bytes": len(data),
                            "sha256": hashlib.sha256(data).hexdigest()})
        delay = Fraction(132 * 48000, rate)
        primed = [(pts - delay, payload, discard, key)
                  for pts, payload, discard, key in packets]
        pts, payload, _, key = primed[-1]
        primed[-1] = (pts, payload, delay, key)
        fine = expanded.movie("A_PCM/INT/LIT", primed, b"", video, bit_depth=16,
                              coded_rate=rate, delay_samples=delay,
                              padding_override=-Fraction(192 * 48000, rate),
                              presentation_duration_ns=30500000000)
        for name, data in [("trimmed-44100-raw-fine.mkv", fine),
                           ("trimmed-44100.mkv", clocks.coarse(fine))]:
            expanded.write_fixture(directory / name, data)
            records.append({"file": name, "bytes": len(data),
                            "sha256": hashlib.sha256(data).hexdigest(),
                            "codec_delay_source_samples": 132, "block_skip_source_samples": 192,
                            "final_discard_source_samples": 132, "audible_frames_48khz": 1439857})
        # Independently prepared audible control: raw 192-frame first trim
        # includes CodecDelay 132, leaving 60 presentation frames of silence.
        # Physically remove the final 132 frames too. No trim/delay metadata is
        # needed, so this oracle cannot reproduce a demuxer skip-data overwrite.
        prepared = bytes(60 * 4) + pcm[192 * 4:-132 * 4]
        prepared_packets, start, index = [], 0, 0
        prepared_frames = len(prepared) // 4
        while start < prepared_frames:
            end = min(start + lengths[index % len(lengths)], prepared_frames)
            prepared_packets.append((Fraction(start * 48000, rate), prepared[start * 4:end * 4], 0, True))
            start, index = end, index + 1
        data = expanded.movie("A_PCM/INT/LIT", prepared_packets, b"", video, bit_depth=16,
                              coded_rate=rate, presentation_duration_ns=30500000000)
        name = "trimmed-44100-fine.mkv"
        expanded.write_fixture(directory / name, data)
        records.append({"file": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                        "prepared_audible_control": True, "leading_silence_source_samples": 60,
                        "physical_frames": prepared_frames, "audible_frames_48khz": 1439857})
    provenance = {"license": "Apache-2.0; original synthesized tones and D77 video payload",
                  "generator": Path(__file__).name, "input": input_record,
                  "video_source_sha256": hashlib.sha256(original.read_bytes()).hexdigest(),
                  "source_frames": frames, "source_rate": rate,
                  "audible_frames_48khz": 1440144, "video_frames": 61,
                  "video_frames_48khz": 1464000, "packet_frames": list(lengths),
                  "fixtures": records}
    (directory / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "pcm-index")
