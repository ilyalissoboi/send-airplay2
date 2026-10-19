# D102: private standalone Ogg Opus/Vorbis input

2026-10-19 (Asia/Tokyo). The user authorizes this next slice after native
PR #86 merged as `7d4ac4c`; its 17 checks passed at final head `9ee6cc8`
in [run 39178182511](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39178182511).

## Source and license provenance before profile expansion

The native parser is original Apache-2.0 project code designed from
[Ogg framing, RFC 3533](https://www.rfc-editor.org/rfc/rfc3533.html),
[Ogg Opus, RFC 7845](https://www.rfc-editor.org/rfc/rfc7845.html),
[Opus packet framing, RFC 6716](https://www.rfc-editor.org/rfc/rfc6716.html)
and the [Vorbis I specification](https://www.xiph.org/vorbis/doc/Vorbis_I_spec.html).
No implementation source is copied. These describe page checksums/lacing,
granule clocks, pre-skip/end trimming and identification/comment/setup headers.

The existing FFmpeg source remains pinned n9.0.2, SHA-512
21bf3fbcdfd2f41ea6edeab40433fecfe362b09ecaa177a652471b54f9357011b4f74dab18245ab1c26f1a473b94205490baecb42289afd2effd603cd0b22b59.
The overlay derives from MIT-licensed vcpkg
434307da09bc05b2c86996dccc8b2351fc0d5d37. The LGPL-2.1-or-later four-library
profile adds only Ogg demuxing (port version 6); Opus/Vorbis decoders and
parsers already exist. No new codec, encoder, linked dependency, source patch,
license or network/protocol capability is added. Future distributions must
rebuild and carry their own profile/source/notices. No release is made here.

## Initial admission and boundaries

`probe_audio_only_tracks` recognizes Ogg by content and invokes a private native
page/packet inventory. Identity is 1, independent of the Ogg serial or decoder
ordinal. The default/public MP4/Matroska inventory and automatic delivery policy
remain unchanged; invalid explicit selections never fall back.

Engineering choice: the initial inventory scans a complete immutable source of
at most **64 MiB**, validates every page checksum and retains at most one pending
packet. It allows 65536 pages, 1 MiB per packet and 1048576 host callbacks with
at most 64 KiB capacity each, under the caller's absolute deadline/cancellation.
The common signature probe adds four bytes before this Ogg budget. This is a
bounded small-file admission path, not a sparse large-file index or throughput
claim. The current HLS/decoder/calibration owners each probe independently; a
startup may therefore scan the same input multiple times. Cache quota is unchanged.

Pages require version 0, known flags, initial BOS/sequence zero, one serial,
consecutive sequence numbers, matching continuation/lacing and a complete audio
EOS at physical EOF. Chained streams, multiplexed streams/Skeleton, empty pages,
trailing bytes, missing EOS, truncated packets/pages and checksum failures refuse.
Headers finish before audio starts on a fresh page. Complete header pages use
granule zero; pages without completed packets use the unknown-granule sentinel.
Completed audio granules are nonnegative and monotonic.

Opus admits version 1 with mapping family 0 or bounded family 1. Its input-rate
hint does not change the 48-kHz clock. Native TOC duration counting requires a
zero origin, no granule gaps and EOS trimming confined to the final packet;
duration is final granule minus pre-skip. The decoder consumes packet skip and
discard metadata once. Vorbis admits version 0, checked identification, comment
framing and setup-header identity; the pinned decoder validates full setup
codebooks and encoded packets. Its final granule uses the declared source rate,
rounded once to 48 kHz. Duration must be positive and below seven days on both
clocks, including after rounding. A positive initial offset without coverage,
discontinuity, inconsistent end or incomplete seek window refuses during decoding.

Rates are 8..192 kHz (Opus output is always 48 kHz); speaker sets are the native
Vorbis/Opus family 0/1 layouts for 1..8 channels except seven-channel back-center,
which the existing stereo downmixer does not admit. Family-1 streams plus coupled
streams must fit eight decoded channels, with defined, non-silent map indices.
Reserved/ambisonic mappings refuse. Lossy audio does not declare a PCM bit depth;
codec, sample rate, channel count and native speaker mask must match the decoder.
Vorbis comments and OpusTags are bounded length-prefixed metadata and are not
used as a duration or source identity. No artwork resources are authored.

Near-start seeks reopen the callback-backed Ogg demuxer/decoder so consumed
initial skip state is restored. Other seeks retain the existing at-least-one-second
decoder/resampler preroll and require the first decoded frame to cover it. The
existing bounded tail-plus-one-sample/EOF check and complete-window requirement
prevent padding a malformed declared presentation. AAC priming, final trim,
audio-only master, lazy sealed fragments, cancellation, worker join and exact-once
host cache cleanup are reused.

## Offline validation and limits

Owned original tones and development encoder identity are recorded in
[fixture provenance](../tools/audio-codec-spike/fixtures/ogg-audio/provenance.json).
The six positive fixtures cover 48-kHz stereo/mono/5.1 Opus and 48/44.1-kHz
stereo plus 48-kHz 5.1 Vorbis. Expected output counts are literal 624144 frames
for 13.003-second cases and 144144 for 3.003-second cases. Five derived refusal
fixtures cover chaining, missing EOS, corruption, invalid Opus trim and an
inflated Vorbis end. No private media or receiver logs are used.

Native tests cover a literal CRC known-answer page, independent bitwise CRC
mutations, short reads, multi-page comment reassembly, sequencing/serial/BOS/EOS,
mapping and granule errors, source/page/packet bounds, seven-day rounding and
cancellation. Codec tests compare cold end/backward/repeated seek PCM against
linear history, check stereo tone origin/channels against authored sine waves,
independently decode authored MOV/AAC output for exact final trim, and verify
cache/refusal cleanup and default-route isolation. Mono and 5.1 are offline
decoder/downmix checks; this is not speaker or discrete-surround interoperability.

Windows Release static/shared CTests pass 42/42 and 43/43. The full optional
codec suite passes 17/17 in 74.43 seconds; final Ogg/HLS focused checks, including
the one-second seek boundary, pass 2/2 in 7.36 seconds. Final native duration-bound
checks pass 2/2 in both profiles. Exact-final-source UWP compilation and all five
binary checks pass. Eleven touched C++ files pass clang-format dry-run/Werror;
git diff --check passes. The initial full codec run had one stale assertion for
the Matroska-specific wording of the now-generic seek refusal; the refusal itself
still occurred. Updating that assertion restores the full suite.

The dated [validation evidence](validation/standalone-ogg-audio-windows-2026-10-19.json)
records final results and hashes. UWP compilation and five-DLL AppContainer/import
inspection are build/static evidence only; no application is installed and no
receiver is contacted. Public API, published ci.294/ci.207 packages and Screenbox
remain unchanged. No new audible casting, large-Ogg throughput or hardware claim
is made.

## Ordered follow-ups

After review/merge, MP3 bounded input/frame/timeline work and direct playback
validation on Mac/Apple TV remain next. Decide AAC fallback from receiver evidence,
retaining explicit stereo permission for any fallback; do not assume MP3 needs
conversion. Edited MP4 and Matroska audio-only admission follow separately, then
automatic copy/conversion and public reporting/CLI/binding integration, native and
packaged receiver gates, and Screenbox routing/queue checks with written manual
instructions. Larger Ogg input indexing and chained/grouped timeline support need
separate design/evidence. Preserve speaker/discrete-surround/Chromecast hardware,
remote-Stop user and separate long-input/cache-quota deferrals. No release is made.
