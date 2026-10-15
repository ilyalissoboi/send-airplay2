#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original container controls around existing owned DTS-core/video packets.

RFC 9559 sections 10.3/10.3.5 specify lace framing and contiguous sample clocks.
No external implementation code or media is incorporated.
"""
import hashlib
import json
from pathlib import Path
import subprocess

import generate_origin_fixtures as origin


def generate():
    fixtures = Path(__file__).parent / 'fixtures'
    directory = fixtures / 'dts-lacing'
    directory.mkdir(exist_ok=True)
    source = fixtures / 'hls/dts.mkv'
    video, audio = origin.encoded(source, 'v:0'), origin.encoded(source, 'a:0')
    assert len(audio[0]) == 1547  # 792000 source samples + 64 DTS encoder padding.
    records = []
    for mode, gap, frames in [('fixed', 0, 8), ('xiph', 0, 8), ('ebml', 0, 8),
                              ('fixed-256', 0, 256), ('gap', 5, 8), ('gap-frame', 32, 8)]:
        packets = audio if not gap else (audio[0][:192], audio[1])
        data = origin.movie(video, packets, 'A_DTS', 83, 0, True, 0,
                            lacing='fixed' if gap or mode == 'fixed-256' else mode,
                            gap_ms=gap, lace_frames=frames)
        name = f'dts-{mode}.mkv'
        (directory / name).write_bytes(data)
        records.append({'file': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                        'lace': 'fixed' if gap or mode == 'fixed-256' else mode,
                        'maximum_frames_per_block': frames,
                        'audio_frames': 192 if gap else 1547, 'decoded_samples_per_frame': 512,
                        'sample_rate': 48000, 'gap_ms_after_one_second': gap})
    (directory / 'provenance.json').write_text(json.dumps({
        'license': 'Apache-2.0; existing original synthesized packets and new owned container metadata',
        'generator': 'generate_dts_lacing_fixtures.py', 'owned_packet_source': 'hls/dts.mkv',
        'owned_source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'specification': 'https://www.rfc-editor.org/rfc/rfc9559.html#section-10.3.5',
        'tool_version': subprocess.check_output(['ffprobe', '-version'], text=True).splitlines()[0],
        'tool_distribution': 'development extraction only; executable not redistributed',
        'video_origin_ms': 83, 'audio_default_duration_ns': 10666667, 'timestamp_scale_ns': 1000000,
        'fixtures': records}, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    generate()
