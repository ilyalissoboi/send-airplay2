# SPDX-License-Identifier: Apache-2.0
"""Owned long compressed movies and independently prepared short audible controls."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

import generate_decoder_fixtures as base
import generate_expanded_decoder_fixtures as expanded
import generate_coarse_clock_fixtures as clocks


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    original = directory.parent / "expanded-decoder" / "pcm-s16le.mkv"
    segment = list(clocks.elements(original.read_bytes()))[1][1]
    tracks = next(body for tag, body, _ in clocks.elements(segment) if tag == "1654ae6b")
    entry = list(clocks.elements(tracks))[0][1]
    private_video = next(body for tag, body, _ in clocks.elements(entry) if tag == "63a2")
    cluster = next(body for tag, body, _ in clocks.elements(segment) if tag == "1f43b675")
    block = next(body for tag, body, _ in clocks.elements(cluster) if tag == "a3")
    picture = block[4:]
    records, inputs = [], []

    def save(name, data, **metadata):
        expanded.write_fixture(directory / name, data)
        records.append(dict(file=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                            **metadata))

    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        temporary = Path(temporary)
        for name, codec, container, identifier in [
                ("flac", "flac", "flac", "A_FLAC"),
                ("opus", "opus", "opus", "A_OPUS"),
                ("vorbis", "vorbis", "ogg", "A_VORBIS"),
                ("truehd", "truehd", "truehd", "A_TRUEHD")]:
            for frames, suffix in [(1440144, "long"), (96000, "trim")]:
                rate = 48000
                frame_options = []
                if name == "flac" and suffix == "long":
                    rate, frames = 44100, 1323132
                    frame_options = ["-frame_size", "1537"]
                wave = temporary / (name + suffix + ".wav")
                inputs.append(base.wave(wave, 2, frames=frames, rate=rate))
                encoded = temporary / (name + suffix + ".encoded")
                base.run("-i", str(wave), "-c:a", codec, *frame_options, "-strict", "-2", "-f", container,
                         str(encoded))
                packets, private = expanded.encoded(encoded)
                delay = -packets[0][0]
                if name == "flac":
                    private = b"fLaC\x80\0\0\x22" + private
                if name == "vorbis":
                    pts, payload, discard, key = packets[-1]
                    extra = pts + 1024 - discard - frames
                    assert extra >= 0
                    packets[-1] = (pts, payload, discard + extra, key)
                if suffix == "long":
                    video = ([(second * 500, picture) for second in range(61)], private_video)
                    fine = expanded.movie(identifier, packets, private, video, delay,
                                          coded_rate=rate,
                                          presentation_duration_ns=30500000000)
                    save(name + "-fine.mkv", fine, audible_frames_48khz=1440144)
                    save(name + ".mkv", clocks.coarse(fine), audible_frames_48khz=1440144)
                    continue

                raw = temporary / (name + ".f32le")
                # Decode the original elementary/Ogg source with no authored
                # CodecDelay/negative block padding, then physically construct
                # the expected audible samples. No same-Matroska-demux trim oracle.
                base.run("-c:a", codec, "-i", str(encoded), "-f", "f32le", "-c:a", "pcm_f32le",
                         str(raw))
                pcm = raw.read_bytes()
                assert len(pcm) >= frames * 8
                pcm = pcm[:frames * 8]
                input_pcm_sha256 = hashlib.sha256(pcm).hexdigest()
                authored_delay, skip, tail = {
                    "flac": (132, 192, 132), "opus": (delay, delay + 96, 40),
                    "vorbis": (delay, delay, 16), "truehd": (10, 20, 10)}[name]
                assert name != "vorbis" or delay == 1024
                leading = skip - authored_delay
                source_cut = skip - delay
                prepared = bytes(leading * 8) + pcm[source_cut * 8:-tail * 8]
                expected_end = frames - authored_delay + delay - tail
                assert len(prepared) == expected_end * 8
                shifted = [(pts + delay - authored_delay, payload, discard, key)
                           for pts, payload, discard, key in packets]
                pts, payload, discard, key = shifted[-1]
                shifted[-1] = (pts, payload, discard + tail, key)
                video = ([(index * 500, picture) for index in range(5)], private_video)
                if name == "vorbis":
                    no_delay = [(pts + delay, payload, discard, key)
                                for pts, payload, discard, key in packets]
                    fine_no_delay = expanded.movie(identifier, no_delay, private, video,
                                                   presentation_duration_ns=2500000000)
                    save("vorbis-no-delay-fine.mkv", fine_no_delay, audible_frames=97024,
                         initial_silence_frames=1024, codec_delay_absent=True)
                    save("vorbis-no-delay.mkv", clocks.coarse(fine_no_delay), audible_frames=97024,
                         initial_silence_frames=1024, codec_delay_absent=True)
                if name == "flac":
                    middle = list(packets)
                    pts, payload, discard, key = middle[1]
                    middle[1] = (pts, payload, discard + 16, key)
                    save("flac-middle-discard.mkv", clocks.coarse(expanded.movie(
                        identifier, middle, private, video, delay, presentation_duration_ns=2500000000)),
                         internal_discard_source_samples=16, intentionally_discontinuous=True)
                fine = expanded.movie(identifier, shifted, private, video, authored_delay,
                                      padding_override=-skip, presentation_duration_ns=2500000000)
                save(name + "-trim.mkv", clocks.coarse(fine), audible_frames=expected_end,
                     codec_delay=authored_delay, first_skip=skip, extra_final_discard=tail)
                pcm_packets = [(start, prepared[start * 8:min(start + 1537, expected_end) * 8],
                                0, True) for start in range(0, expected_end, 1537)]
                control = expanded.movie("A_PCM/FLOAT/IEEE", pcm_packets, b"", video,
                                         bit_depth=32, presentation_duration_ns=2500000000)
                save(name + "-audible.mkv", control, audible_frames=expected_end,
                     prepared_audible_control=True, decoded_source_sha256=input_pcm_sha256)
        wave = temporary / "flac-cycle.wav"
        inputs.append(base.wave(wave, 2, frames=48000))
        encoded = temporary / "flac-cycle.encoded"
        base.run("-i", str(wave), "-c:a", "flac", "-frame_size", "48000", "-f", "flac", str(encoded))
        packets, private = expanded.encoded(encoded)
        assert len(packets) == 1 and packets[0][0] == 0
        video = ([(0, picture), (500, picture)], private_video)
        save("flac-cycle.mkv", expanded.movie("A_FLAC", packets, b"fLaC\x80\0\0\x22" + private,
                                              video, presentation_duration_ns=1000000000),
             source_frames=48000, independent_one_second_cycle=True)
    provenance = dict(license="Apache-2.0; synthesized tones and owned D77 video",
                      generator=Path(__file__).name, inputs=inputs, fixtures=records,
                      development_tool=subprocess.check_output(["ffmpeg", "-version"],
                                                               text=True).splitlines()[0],
                      encoder_commands=base.ENCODER_COMMANDS,
                      video_source_sha256=hashlib.sha256(original.read_bytes()).hexdigest())
    (directory / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")


if __name__ == "__main__":
    generate(Path(__file__).parent / "fixtures" / "compressed-index")
