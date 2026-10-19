# D103: private MP3 inventory and direct-playback probe

2026-10-19 (Asia/Tokyo). The user authorizes this slice after native #87
merged as e6b0a4b; all 17 checks passed at final head 13602e0 in
[run 39182522426](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39182522426).
The user confirms MacBook Pro/macOS 26.7.1 and Apple TV/tvOS 26.6 available,
with versions unchanged. The private inventory, offline checks and native
CBR/VBR direct-playback gate are complete on both recorded receivers.

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

The private inventory does not change default/public probes, automatic audio
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

Windows Release static/shared CTests pass 43/43 and 44/44 (45.51/50.35 seconds);
the ordinary x64 UWP native library also compiles. After test-only naming changes,
focused MP3 checks pass in both builds (0.83 seconds each). Tests cover literal
headers/counts, malformed metadata/framing/reservoir/trim, locators, short reads,
cancellation/deadlines and literal source/frame/callback/locator limits.

## Native direct-delivery gate and decision

The codec-free Windows x64 static C API host from implementation 7f8fb30 uses
explicit audio/mpeg and the original complete MP3 bytes. Actual loaded executable
and two crypto dependency paths/hashes match the build in each run. The helper
does not participate in this existing progressive delivery path.

The user confirms Mac pending-consent cancellation and both receivers' CBR/VBR
listening checks: start near eight seconds, pause/resume, forward/backward marker
seeks, natural end and separate Stop with no delayed restart. All nine sender
records show one source release per run, no failed reads and completed media
cleanup. Cancellation has zero media reads; the pre-start controller retains
cleaned=no, while its media source is cleaned and released. Early Start/Status
position snapshots sometimes still report near zero; retain those raw snapshots
alongside the user's explicit start-position/marker confirmations. They do not
establish sample-exact initial seek timing.

The [sanitized evidence](validation/mp3-input-probe-windows-2026-10-19.json)
records source/binary/fixture hashes, commands, telemetry and separate written
observations. No cast failed or required a runtime change in this gate.

**Engineering decision:** retain direct progressive audio/mpeg as the proposed
delivery for this tested MP3 subset on these receivers. There is no demonstrated
need for an AAC fallback, so no MP3 decoder, codec-profile expansion or conversion
permission is added. This is not universal MP3 or receiver support. If a later
admitted input/receiver fails, investigate that concrete failure before deciding
whether to refuse it or add a separately validated AAC fallback.

**Next slice:** verify the same CBR/VBR files through the packaged UWP host's
brokered source, with actual AppContainer/module hashes, audible controls/natural
end/separate Stop, Mac consent cancellation and exactly-once source release.
Keep public/default admission and Screenbox routing separate until their own
design and lifecycle/queue gates. The existing private FLAC/WAVE/Ogg-to-AAC work
still needs public delivery integration and receiver validation.

No packaged MP3, Screenbox, low-rate mono receiver or sample-exact gapless playback
claim follows from these native stereo checks.

## PR #88 portability review follow-up

The original head 645ff5e failed Linux/macOS native and sanitizer compilation:
the integer-promoted reservoir distance compared as signed against uint64_t.
Clang also flags arithmetic directly on the test's encoder-marker string literal.
The distance is now explicitly narrowed to its unsigned 9-bit/8-bit byte range,
and the marker uses bounded string_view iterators with a direct include. Both
changes preserve parser behavior and fixture coverage.

Local Clang 22.1.3 compiles both changed files with C++17 and
-Wall -Wextra -Wpedantic -Werror; its separately built MP3 test executable passes
all existing scenarios. These are Windows-host Clang results, not Linux/macOS
runtime or new receiver observations. The original receiver source/binary hashes
remain historical; the progressive delivery path is unchanged.
Full Windows Release static/shared reruns pass 43/43 and 44/44; x64 UWP native
compilation also passes. The [review validation record](validation/mp3-input-review-windows-2026-10-19.json)
keeps these results separate from the original receiver gate and failing CI head.
