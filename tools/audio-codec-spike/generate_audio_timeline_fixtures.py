#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original FLAC/PCM tones for private edited MOV/Matroska duration checks."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import generate_decoder_fixtures as base
import generate_expanded_decoder_fixtures as expanded
import generate_coarse_clock_fixtures as clocks


def audio_only_pcm(packets, **options):
    data = expanded.movie('A_PCM/INT/LIT', packets, b'', ([], b''), bit_depth=16, **options)
    header, segment = list(clocks.elements(data))
    children = []
    for tag, body, _ in clocks.elements(segment[1]):
        if tag == '1c53bb6b':
            continue  # Removing the video entry invalidates unused cue byte offsets.
        if tag == '1654ae6b':
            body = b''.join(base.element(entry_tag, entry) for entry_tag, entry, _ in clocks.elements(body)
                            if any(field == '83' and int.from_bytes(value, 'big') == 2
                                   for field, value, _ in clocks.elements(entry)))
        children.append(base.element(tag, body))
    return base.element(header[0], header[1]) + base.element(segment[0], b''.join(children))


def with_edits(data, leading, start, duration):
    movie_rate = 48000
    entries = []
    if leading:
        entries.append(struct.pack('>IiHH', leading, -1, 1, 0))
    entries.append(struct.pack('>IiHH', duration, start, 1, 0))

    def rewrite(kind, body):
        if kind == b'elst':
            assert body[0] == 0
            body = bytes(4) + struct.pack('>I', len(entries)) + b''.join(entries)
        elif kind == b'mvhd':
            assert body[0] == 0
            body = bytearray(body)
            body[12:20] = struct.pack('>II', movie_rate, leading + duration)
        elif kind == b'tkhd':
            assert body[0] == 0
            body = bytearray(body)
            body[20:24] = struct.pack('>I', leading + duration)
        elif kind in (b'moov', b'trak', b'mdia', b'minf', b'stbl', b'edts'):
            body = b''.join(rewrite(child, payload) for child, payload in base.boxes(body))
        return base.box(kind, body)

    # The muxer writes moov after mdat, so edit-size changes preserve sample offsets.
    assert next(kind for kind, _ in base.boxes(data) if kind in (b'moov', b'mdat')) == b'mdat'
    return b''.join(rewrite(kind, body) for kind, body in base.boxes(data))


def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    records = []
    cases = [
        ('pcm-edit.mov', 48000, 144144, 'pcm_s16le', (12000, 24000, 96000)),
        ('pcm-cut.mov', 48000, 144144, 'pcm_s16le', (0, 24000, 48000)),
        ('flac-edit441.mp4', 44100, 132432, 'flac', (12000, 22050, 96000)),
        ('flac48.mka', 48000, 624144, 'flac', None),
        ('flac441.mka', 44100, 573432, 'flac', None),
        ('pcm48.mka', 48000, 144144, 'pcm_s16le', None),
    ]
    with tempfile.TemporaryDirectory(dir=directory) as temporary:
        source = Path(temporary) / 'tones.wav'
        for name, rate, frames, codec, edits in cases:
            original = base.wave(source, 2, frames=frames, rate=rate)
            target = directory / name
            options = ['-use_editlist', '1'] if edits else []
            base.run('-i', str(source), '-map', '0:a', '-c:a', codec, '-strict', '-2', *options, str(target))
            if edits:
                target.write_bytes(with_edits(target.read_bytes(), *edits))
            expected = edits[0] + edits[2] if edits else (frames * 48000 + rate // 2) // rate
            records.append({'file': name, 'original_tones': original,
                            'source_rate': rate, 'source_frames': frames,
                            'leading_48khz_frames': edits[0] if edits else 0,
                            'source_start_ticks': edits[1] if edits else 0,
                            'media_duration_movie_ticks': edits[2] if edits else None,
                            'movie_timescale': 48000 if edits else None,
                            'expected_48khz_frames': expected,
                            'bytes': target.stat().st_size,
                            'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
        original = base.wave(source, 2, frames=144144, rate=48000)
        wave = source.read_bytes()
        pcm = wave[wave.index(b'data') + 8:]
        packets = [(first - 96, pcm[first * 4:min(first + 4096, 144144) * 4],
                    144 if first + 4096 >= 144144 else 0, True)
                   for first in range(0, 144144, 4096)]
        variants = [('pcm-trim.mka', packets, {}, 143904),
                    ('pcm-leading-gap.mka', [(pts + 48, payload, trim, key)
                      for pts, payload, trim, key in packets], {}, None),
                    ('pcm-internal-trim.mka', [(pts, payload, 96 if index == 1 else trim, key)
                      for index, (pts, payload, trim, key) in enumerate(packets)], {}, None),
                    ('pcm-excess-skip.mka', packets, {'padding_override': -192}, None)]
        for name, packet_list, options, expected in variants:
            data = audio_only_pcm(packet_list, delay_samples=96, **options)
            (directory / name).write_bytes(data)
            records.append({'file': name, 'original_tones': original,
                            'codec_delay_source_frames': 96, 'final_discard_source_frames': 144,
                            'expected_48khz_frames': expected,
                            'purpose': 'positive trim' if expected else 'zero-origin/contiguous coverage refusal',
                            'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    (directory / 'provenance.json').write_text(json.dumps({
        'license': 'Apache-2.0 original repository-generated tones and edit metadata',
        'generator': 'generate_audio_timeline_fixtures.py',
        'development_encoder': subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0],
        'files': records}, indent=2) + '\n')


if __name__ == '__main__':
    generate(Path(__file__).parent / 'fixtures/audio-timelines')
