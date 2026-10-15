#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Recontainerize existing owned packets with independently authored origins.

No media is downloaded or encoded. ffprobe only extracts the repository-owned
compressed packets/configuration; native ticks, Cues and captions are authored here.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import generate_decoder_fixtures as base


def encoded(path, stream):
    record = json.loads(subprocess.check_output([
        'ffprobe', '-v', 'error', '-select_streams', stream, '-show_streams',
        '-show_packets', '-show_data', '-of', 'json', str(path)], text=True))
    packets = [(round(float(packet['pts_time']) * 1000), base.unhex_dump(packet['data']),
                round(float(packet.get('dts_time', packet['pts_time'])) * 1000), 'K' in packet['flags'])
               for packet in record['packets']]
    private = base.unhex_dump(record['streams'][0].get('extradata', ''))
    return packets, private


def movie(video, audio, codec, video_ms, audio_ms, indexed, delay_ms):
    element, uint = base.element, base.uint
    header = b''.join([uint('4286', 1), uint('42f7', 1), uint('42f2', 4), uint('42f3', 8),
                       element('4282', b'matroska'), uint('4287', 4), uint('4285', 2)])
    duration_ms = video_ms + max(packet[0] for packet in video[0]) + 500
    info = uint('2ad7b1', 1000000) + element('4489', struct.pack('>d', float(duration_ms)))
    entries, packets = [], []
    for number, kind, name, payloads, private in [
        (11, 1, 'V_MPEG4/ISO/AVC', video[0], video[1]),
        (42, 2, codec, audio[0], audio[1]),
        (51, 17, 'S_TEXT/UTF8', [], b'')]:
        entry = uint('d7', number) + uint('73c5', number + 1000) + uint('83', kind)
        entry += uint('b9', 1) + uint('88', 1) + element('86', name.encode())
        if private:
            # ffprobe exposes FLAC's 34-byte STREAMINFO payload; Matroska
            # CodecPrivate retains the stream marker and final metadata header.
            if codec == 'A_FLAC' and kind == 2 and len(private) == 34:
                private = b'fLaC\x80\x00\x00\x22' + private
            entry += element('63a2', private)
        if kind == 1:
            entry += uint('23e383', 500000000) + element('e0', uint('b0', 16) + uint('ba', 16))
        if kind == 2:
            channels = 6 if codec == 'A_DTS' else 2
            entry += element('e1', element('b5', struct.pack('>d', 48000.0)) + uint('9f', channels))
            if delay_ms:
                entry += uint('56aa', delay_ms * 1000000)
        entries.append(element('ae', entry))
        offset = video_ms if kind == 1 else audio_ms + delay_ms
        packets.extend((pts + offset, number, data, None, dts + offset, sync)
                       for pts, data, dts, sync in payloads)
    # Cues independently cover before-origin, crossing-origin and crossing-cut cases.
    packets += [(0, 51, b'Before video', 20, 0, True),
                (video_ms - 20, 51, b'Crosses origin', 60, video_ms - 20, True),
                (video_ms + 1000, 51, b'Original caption', 1000, video_ms + 1000, True),
                (video_ms + 5900, 51, b'Crosses segment', 300, video_ms + 5900, True)]
    # Keep compressed video in decode order, including reordered B-frames.
    packets.sort(key=lambda packet: (packet[4], packet[1]))
    prefix = element('1549a966', info) + element('1654ae6b', b''.join(entries))
    clusters = []
    for pts, number, data, duration, _, sync in packets:
        block = base.vint(number) + b'\0\0' + (b'\x80' if duration is None and sync else b'\0') + data
        payload = element('a3', block) if duration is None else element(
            'a0', element('a1', block) + uint('9b', duration))
        clusters.append(element('1f43b675', uint('e7', pts) + payload))
    def cues(start):
        position, points = start, []
        for (pts, number, _, _, _, sync), cluster in zip(packets, clusters):
            if number == 11 and sync:
                points.append(element('bb', uint('b3', pts, 8) + element(
                    'b7', uint('f7', 11) + uint('f1', position, 8))))
            position += len(cluster)
        return element('1c53bb6b', b''.join(points))
    index = b''
    if indexed:
        index = cues(len(prefix) + len(cues(0)))
    return element('1a45dfa3', header) + element('18538067', prefix + index + b''.join(clusters))


def generate():
    fixtures = Path(__file__).parent / 'fixtures'
    directory = fixtures / 'origin'
    directory.mkdir(exist_ok=True)
    records = []
    for name, source, codec, video_ms, audio_ms, indexed, delay_ms in [
        ('dts-early-indexed.mkv', 'hls/dts.mkv', 'A_DTS', 83, 0, True, 0),
        ('dts-late-full.mkv', 'hls/dts.mkv', 'A_DTS', 83, 203, False, 0),
        ('dts-reordered-indexed.mkv', 'hls/dts-bframes.mp4', 'A_DTS', 83, 0, True, 0),
        ('flac-common-indexed.mkv', 'expanded-decoder/flac.mkv', 'A_FLAC', 123, 123, True, 0)]:
        input_path = fixtures / source
        data = movie(encoded(input_path, 'v:0'), encoded(input_path, 'a:0'), codec,
                     video_ms, audio_ms, indexed, delay_ms)
        (directory / name).write_bytes(data)
        records.append({'file': name, 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                        'owned_packet_source': source, 'source_sha256': hashlib.sha256(input_path.read_bytes()).hexdigest(),
                        'video_origin_ms': video_ms, 'audible_audio_origin_ms': audio_ms,
                        'codec_delay_ms': delay_ms, 'indexed': indexed})
    record = {'license': 'Apache-2.0; existing original synthesized packets and new container metadata',
              'generator': 'generate_origin_fixtures.py',
              'tool_version': subprocess.check_output(['ffprobe', '-version'], text=True).splitlines()[0],
              'tool_distribution': 'development extraction only; no executable redistributed',
              'timestamp_scale_ns': 1000000, 'fixtures': records}
    (directory / 'provenance.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    generate()
