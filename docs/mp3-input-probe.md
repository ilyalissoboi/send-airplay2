# D103: private MP3 inventory and direct-playback probe

2026-10-19 (Asia/Tokyo). The user authorizes this slice after native #87
merged as e6b0a4b; all 17 checks passed at final head 13602e0 in
[run 39182522426](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39182522426).
The user confirms MacBook Pro/macOS 26.7.1 and Apple TV/tvOS 26.6 available,
with versions unchanged. Offline implementation and receiver checks are pending.

## Provenance and engineering boundary

The MP3 framing/timeline inventory and tests are original Apache-2.0 code.
References are [RFC 3119's MP3 structure](https://www.rfc-editor.org/rfc/rfc3119.html),
[RFC 3003's audio/mpeg MIME](https://www.rfc-editor.org/rfc/rfc3003.html),
[ID3v2.3](https://id3.org/id3v2.3.0) and
[ID3v2.4](https://id3.org/id3v2.4.0-structure).
The existing [FFmpeg 9.0.2 demuxer](https://github.com/FFmpeg/FFmpeg/blob/n9.0.2/libavformat/mp3dec.c)
and [LAME's tag authoring](https://github.com/rbrito/lame/blob/master/libmp3lame/VbrTag.c)
are primary references for factual Xing/Info/LAME field layout and counting
conventions; no implementation source is copied or linked into the parser.
FFmpeg is LGPL-2.1-or-later; LAME's referenced implementation is LGPL-2.0-or-later.
Only externally installed development encoders generate original owned tones.
No new library dependency, codec profile expansion or distributed tool is added.

The private inventory will not change default/public probes, automatic audio
routing, the AAC factory, packages or Screenbox. Direct receiver checks use the
existing progressive C API and explicit audio/mpeg, without conversion permission
or an AAC decoder/encoder. Any fallback remains an evidence-based later decision.

## Private implementation and initial admission

A separate probe_mp3_audio helper and airplay2-mp3-probe offline tool inspect
content through host callbacks. This helper is not wired to public/default
inventory, the AAC decoder factory or automatic policy. It admits unprotected
MPEG-1/2/2.5 layer III, indexed bitrate, 8..48 kHz and mono/stereo, with stable
version/rate/channel count and emphasis zero. It checks frame boundaries and
reservoir distances, not Huffman payload integrity. Free-format/CRC-protected
frames, VBRI, changing parameters, junk/resynchronization and truncation refuse.
ID3v2.3/2.4 revision-zero/flag-zero bodies (at most 1 MiB) are skipped opaquely;
trailing ID3v1 is excluded. Flagged/repeated/other metadata is outside this slice.
No labels or artwork are exposed. Same-format bare concatenation without tags
cannot be distinguished from one raw stream; no exact gapless end is inferred.

Xing/Info metadata must have empty side information, defined flags and a known
frame count. Counts and optional byte extent must agree with the full native
frame-header scan. Approximate TOC entries are checked for ordering but do not
supply native locators. Known revision-zero LAME/Lavc/Lavf encoder trim requires
its Info-tag CRC. The metadata frame is excluded from audio counts and cannot
supply the first audio frame's reservoir. Encoder delay/padding are subtracted
once for a metadata-derived audible count, without applying decoder synthesis
latency. Missing/unknown trim remains unknown; this is not decoded-content proof.

Limits are 1 TiB source, 1,000,000 physical frames, 65536 sparse locators,
64 MiB requested header bytes and 2,100,000 callbacks of at most 64 KiB under
one caller deadline. Locators are at least one source second apart and are not
independently decodable restart points. A future decoder must reconstruct its
reservoir/history. Positive coded duration is below seven days. The sparse test
advertises 216000000 bytes / 150000 frames and validates 172800000 coded samples
using under 6 MB of header reads; this is parser scaling, not sustained playback.

## Owned fixtures and development-tool finding

The generator reuses the original pulse/channel-marker authoring from the D97
tool. CBR 48-kHz and VBR 44.1-kHz stereo clips are 64 seconds; low-rate mono
fixtures cover MPEG-2 (24 kHz, 13 seconds) and MPEG-2.5 (12 kHz, 3 seconds).
Raw CBR and ID3-tagged raw copies test absent gapless metadata. The external
encoder/decoder identity and all bytes/hashes are recorded in
[provenance](../tools/mp3-probe/fixtures/provenance.json).

The installed development FFmpeg is a 2023 build, distinct from the pinned
9.0.2 library backend. Its low-rate mono Info-tag checksums do not match the
actual tag extent. Historical [FFmpeg muxer code](https://github.com/FFmpeg/FFmpeg/blob/n5.1/libavformat/mp3enc.c)
uses a fixed 190-byte checksum span; the native parser computes the actual span
ending at the tag's CRC field. The generator records original hashes/checksums
and repairs only the two checksum bytes in positive mono fixtures, retaining an
original invalid mono file as a refusal fixture. Encoded audio, delay/padding and
independent decoded sample counts are unchanged. Stereo receiver fixtures need
no repair. Native CRC checks remain strict.

Focused offline checks pass; full static/shared results and native receiver
observations will be recorded before completing this slice. Tests cover literal
headers/counts, malformed metadata/framing/reservoir/trim, locators, short reads,
cancellation/deadlines and literal source/frame/callback/locator limits.
