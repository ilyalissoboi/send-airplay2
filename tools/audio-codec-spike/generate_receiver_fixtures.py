#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Original visible flash / audible left-right-center receiver controls.

The FFmpeg executable is a development-only encoder, never redistributed or
linked into the library. All input pixels/PCM are authored here; no private or
downloaded media is used. Output belongs in an ignored local validation folder.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

RATE = 48000
DURATION = 120


def wave(path, channels):
    frequencies = [310, 470, 630, 80, 790, 950] if channels == 6 else [440, 660]
    chunks = []
    for second in range(3):
        data = bytearray()
        for frame in range(RATE):
            envelope = max(0, min(1, frame / 96, (4800 - frame) / 96))
            for channel, frequency in enumerate(frequencies):
                group = 0 if channel in (0, 4) else 1 if channel in (1, 5) else 2
                active = group == second if channels == 6 else second == channel or second == 2
                sample = round(5000 * envelope * math.sin(2 * math.pi * frequency * frame / RATE)) if active else 0
                data.extend(struct.pack('<h', sample))
        chunks.append(data)
    if channels == 6:
        fmt = struct.pack('<HHIIHHHHI', 0xfffe, channels, RATE, RATE * channels * 2,
                          channels * 2, 16, 22, 16, 0x3f)
        fmt += bytes.fromhex('0100000000001000800000aa00389b71')
    else:
        fmt = struct.pack('<HHIIHH', 1, channels, RATE, RATE * channels * 2, channels * 2, 16)
    payload_bytes = DURATION * RATE * channels * 2
    with path.open('wb') as output:
        output.write(b'RIFF' + struct.pack('<I', 20 + len(fmt) + payload_bytes) + b'WAVEfmt ')
        output.write(struct.pack('<I', len(fmt)) + fmt + b'data' + struct.pack('<I', payload_bytes))
        for second in range(DURATION):
            output.write(chunks[second % 3])


def generate(output, all_codecs, font_file):
    output.mkdir(parents=True, exist_ok=True)
    commands = []
    def run(arguments):
        commands.append(arguments)
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-n', *arguments], cwd=output, check=True)
    video = 'receiver-video.mp4'
    font = '' if font_file is None else "fontfile='" + font_file.as_posix().replace(':', '\\:') + "':"
    filters = ["drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:enable='lt(mod(t,1),0.1)'",
               "drawtext=" + font + "text='%{pts\\:hms}':fontcolor=black:fontsize=36:x=20:y=20:box=1:boxcolor=white"]
    for label, group in [('LEFT', 0), ('RIGHT', 1), ('BOTH', 2)]:
        filters.append(f"drawtext={font}text='{label}':fontcolor=black:fontsize=36:x=20:y=80:box=1:boxcolor=white:enable='eq(floor(mod(t,3)),{group})'")
    run(['-f', 'lavfi', '-i', f'testsrc2=size=640x360:rate=24:duration={DURATION}',
         '-vf', ','.join(filters), '-an', '-c:v', 'libx264', '-preset', 'ultrafast', '-crf', '25',
         '-threads', '2', '-pix_fmt', 'yuv420p', '-g', '48', '-keyint_min', '48', '-sc_threshold', '0', '-bf', '0', video])
    for channels in (2, 6):
        wave(output / f'original-{channels}.wav', channels)
    cases = [('aac', 'aac', 2, ['-b:a', '192k']), ('dts', 'dca', 6, ['-b:a', '1411200', '-strict', '-2'])]
    if all_codecs:
        cases += [('ac3', 'ac3', 2, ['-b:a', '192k']), ('eac3', 'eac3', 2, ['-b:a', '192k']),
                  ('flac', 'flac', 6, []), ('opus', 'libopus', 6, ['-b:a', '384k']),
                  ('vorbis', 'libvorbis', 6, []), ('pcm', 'pcm_s16le', 2, []),
                  ('truehd', 'truehd', 6, ['-strict', '-2'])]
    fixtures = []
    for name, codec, channels, extra in cases:
        # MP4 retains the AAC encoder's supported priming edit. Ordinary copy
        # does not rebase the positive video origin of FFmpeg's shifted MKV.
        filename = f'receiver-{name}.' + ('mp4' if name == 'aac' else 'mkv')
        run(['-i', video, '-i', f'original-{channels}.wav', '-map', '0:v:0', '-map', '1:a:0',
             '-c:v', 'copy', '-c:a', codec, *extra, '-threads', '2', '-t', str(DURATION), filename])
        path = output / filename
        fixtures.append({'file': filename, 'bytes': path.stat().st_size,
                         'sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'source_channels': channels,
                         'expected_action': 'copy' if name in ('aac', 'ac3', 'eac3') else 'stereo_aac',
                         'expected_output_channels': channels if name in ('aac', 'ac3', 'eac3') else 2})
    record = {'license': 'Apache-2.0; original synthesized video, PCM and resulting media',
              'generator': 'generate_receiver_fixtures.py', 'duration_seconds': DURATION, 'sample_rate': RATE,
              'visual_signal': '100-ms white flash at each integer second; time and LEFT/RIGHT/BOTH labels',
              'audio_signal': '100-ms pulse at each integer second; 3-second left/right/center cycle, 2-ms ramps',
              'surround_layout': 'FL FR FC LFE BL BR (0x3f); left FL/BL, right FR/BR, center FC/LFE',
              'tool_version': subprocess.check_output(['ffmpeg', '-version'], text=True).splitlines()[0],
              'tool_distribution': 'Development-only encoder; no tool executable or implementation redistributed',
              'commands': commands, 'fixtures': fixtures}
    (output / 'provenance.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(f'Generated {len(fixtures)} original receiver controls.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--all-codecs', action='store_true')
    parser.add_argument('--font-file', type=Path)
    arguments = parser.parse_args()
    generate(arguments.output, arguments.all_codecs, arguments.font_file)
