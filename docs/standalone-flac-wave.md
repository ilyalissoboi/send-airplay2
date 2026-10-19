# D101: standalone FLAC and WAVE input foundation

2026-10-19 (Asia/Tokyo), private implementation and offline validation. The user authorizes the
next standalone FLAC/WAV slice after PR #85 merged as 23a030f; all 17 checks
passed at final head d0392a2 in run 39130131090. Public automatic routing,
packages, receiver gates and Screenbox remain separate.

## Source and license provenance recorded before profile expansion

Native parsing is original project C++ under Apache-2.0, designed from
[RFC 9639](https://www.rfc-editor.org/rfc/rfc9639.html), Microsoft's
[RIFF description](https://learn.microsoft.com/en-us/windows/win32/xaudio2/resource-interchange-file-format--riff-)
and [WAVEFORMATEXTENSIBLE fields](https://learn.microsoft.com/en-us/windows/win32/api/mmreg/ns-mmreg-waveformatextensible).
[EBU Tech 3306](https://tech.ebu.ch/docs/tech/tech3306v1_1.pdf) supplies the RF64/ds64
layout and sentinel rules. No source code from these documents or another
implementation is copied.

The existing dependency remains FFmpeg 9.0.2, pinned source n9.0.2 and SHA-512
21bf3fbcdfd2f41ea6edeab40433fecfe362b09ecaa177a652471b54f9357011b4f74dab18245ab1c26f1a473b94205490baecb42289afd2effd603cd0b22b59.
The overlay derives from vcpkg 434307da09bc05b2c86996dccc8b2351fc0d5d37 (MIT).
Its existing LGPL-2.1-or-later, four-library, network/protocol/GPL/nonfree-disabled
profile adds only the FLAC demuxer; WAV is already enabled. No decoder, encoder,
linked dependency, source patch or license changes are introduced. Increase the
overlay port version for the changed configuration. A future distribution must
carry its own rebuilt profile manifest/sources/notices; no release is made here.

## Implemented subset and lifetime

`probe_audio_only_tracks` is a private opt-in probe. The existing public/video
MP4/Matroska probe, default decoder admission and automatic delivery profile do
not gain standalone inputs. Standalone sources have one native identity, 1;
invalid explicit selections never fall back. Input is recognized by content,
not filename. Native metadata supplies the sample count and clock; no FFmpeg
format-duration estimate or caller-provided duration sets the HLS end.

WAVE supports little-endian RIFF and RF64, a single format before a single data
chunk, signed integer PCM16/24/32 and IEEE float32/64. Classic format supports
mono/stereo; surround requires WAVEFORMATEXTENSIBLE with a known speaker mask
and matching channel count. Valid bits must equal packed bits. Block alignment,
byte rate, root/chunk bounds, odd-byte padding and any fact count must agree.
RF64 requires the first ds64 chunk, known matching data/sample counts, its root
size matching the immutable source and no extra size-table entries. A sentinel
fact count uses ds64 sampleCount. Compressed
WAVE, PCM8, reduced-valid-bit packing, RIFX/W64, multiple data chunks, unknown
RF64 sizes and unsupported speaker positions refuse. Supported speaker bits are
FL/FR/FC/LFE/BL/BR/SL/SR; no surround layout is guessed.

Native FLAC requires leading fLaC, a first unique 34-byte STREAMINFO, known
positive total samples, 16/24-bit input and the supported native speaker map.
Metadata headers are bounded before payload processing. Seek points must have
ordered sample/byte coordinates, positive frame counts and in-file offsets;
placeholder entries must be last. A seek point is not trusted as a decoded frame
until the decoder supplies valid timestamp coverage. Padding/application/comment/
cuesheet metadata may be skipped; pictures, reserved metadata, ID3-prefixed FLAC
and unknown total samples are outside this initial subset. A comment that changes
the decoder layout away from the native expected map refuses. The standard
seven-channel FLAC map includes back center, outside the current downmix subset.
Whole-file FLAC MD5 verification is not performed by this bounded path.

Standalone probe bounds are 1 TiB source, 4096 headers/chunks, 16 MiB FLAC metadata,
65536 seek points, 128 MiB requested header bytes and 1048576 callbacks, each
at most 64 KiB. Source-frame counts are positive and convert to less than seven
days at 48 kHz, leaving room for one end-check sample. Skipped WAVE data is not
scanned or allocated by the native probe. The sparse RF64 test advertises five
GiB of stereo PCM16 and validates 1342177280 frames using only header reads.
This is parser scaling evidence, not long-file conversion/cache-throughput proof.

The decoder opt-in uses the same callback-only custom I/O and existing window
budgets. Codec/rate/depth/channel-mask metadata must match the native probe;
formats must remain stable across windows. A standalone seek must land early
enough to cover the required preroll, or refuse rather than inventing silence.
HLS startup reads a bounded tail window with one extra output sample and requires
EOF exactly at the declared end. Generated standalone windows must fully cover
their declared in-presentation span; truncated input becomes a terminal failure
instead of silent padding. Existing AAC priming, final trim, lazy sealing, quota,
cancellation, worker join and exact-once host cache cleanup are retained.

## Offline evidence and limits

Owned fixtures are original repository-generated tones, with source counts,
frequencies, hashes, metadata changes and development CLI version in
[fixture provenance](../tools/audio-codec-spike/fixtures/standalone-audio/provenance.json).
Positive cases cover FLAC 48/44.1 kHz and 5.1, a valid FLAC seek point,
PCM16/24/32, float32/64, mono 44.1 kHz, RF64 and an odd-size unknown WAVE chunk.
FLAC copies with total samples increased/decreased by 1024 refuse startup.
Independent MOV/AAC output decoding, literal final frame counts, cold final then
backward requests, cached bytes and cleanup extend the existing audio-only gate.
Native tests additionally check malformed sizes/packing/GUID/masks/seek points,
metadata bounds, sparse RF64, invalid identity, short reads and cancellation.
Default/public probe and video route refusal remain regression checks.

Windows Release static/shared CTests pass 41/41 and 42/42; the full optional
desktop codec suite passes 16/16. Final focused checks after RF64 sentinel and
protocol-naming changes pass 1/1, 1/1 and 2/2 respectively. The rebuilt private
UWP library and all five x64 AppContainer/app-CRT binary inspections pass.
Touched C++ clang-format dry-run/Werror and git diff --check pass. Results are
recorded separately in
[the evidence](validation/standalone-flac-wave-windows-2026-10-19.json).
No application is installed, no receiver is contacted and no Screenbox or audible
interoperability claim is added. Published ci.294 binaries remain unchanged.

## Next work

After this slice's review/merge, continue with Ogg Opus/Vorbis inventory, granule
trim, seek/restart and chaining rules, keeping MP3 input/direct-playback tests
and its evidence-based fallback decision explicitly planned. Edited MP4 and
Matroska audio-only timelines remain separate admission work. Connect only
validated subsets to automatic copy/conversion routing with existing explicit
stereo permission, then public reporting/CLI/binding and native/packaged receiver
gates. Screenbox integration follows those gates with written manual instructions.
No HomePod/RAOP, discrete converted surround, Chromecast, remote-Stop or separate
long-input/cache-quota deferral changes here. No release is published.
