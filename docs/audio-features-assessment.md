# Audio-file casting and unsupported video audio: effort assessment

Assessment: 2026-10-12, against native main `8a40ec3` and Screenbox main
`8bfd3dd9`. This is a proposal based on source inspection and primary sources,
not an implementation decision or a new receiver test.

## User scope and corrected validation queue

- The user requests an effort/change assessment for audio-file casting and
  video whose audio is unsupported by Apple TV, such as DTS.
- Audio-file casting must include audio-only AirPlay speakers such as HomePod,
  rather than only the existing Apple TV/Mac URL-video receivers.
- The user asks whether DTS can retain 5.1/7.1 surround. Both are evaluated
  below; a stereo-only first version has not been selected.
- Chromecast video regression is deferred: no suitable local receiver is
  available. The user says casting to a sleeping Apple TV was already verified
  earlier. Do not schedule that check again as an uncompleted gate. The exact
  earlier Screenbox build/date is not supplied by this clarification.
- Future visual UI changes require written instructions and manual user
  confirmation, without the Computer Use skill.

## Current implementation and reusable pieces

The in-tree remux is container conversion, not decoding/transcoding. MP4 and MKV
track selection require H.264/HEVC video; a file with audio but no remuxable
audio track is rejected. If an otherwise DTS-bearing file also has a remuxable
AAC/AC-3/E-AC-3 track, existing selection can use that alternative. That is not
DTS support and does not promise the user's desired language/track.

Relevant code:

- `src/mp4_demux.cpp` and `src/mkv_demux.cpp`: video-required selection and
  remuxable-audio filtering.
- `src/hls_remux.cpp`: segment boundaries/durations use track zero as video;
  media payload reads use one original input. Encoded audio needs a different
  sample source or output-generation path.
- `src/fmp4_writer.cpp` and `src/hls_variant.cpp`: reusable fMP4/playlist
  foundations; no independent audio-rendition model yet (subtitles have one).
- `include/send_airplay2/media_server.h`: fixed resource names with deferred
  `size_on_request`, cached after successful generation. This can help serve
  sealed, lazily encoded VOD segments, but is not itself a transcoder.
- `src/receiver_list.cpp`: low-level discovery already gathers `_raop`, but
  the public receiver list selects `_airplay` endpoints and drops RAOP-only
  speakers. A speaker advertising `_airplay` might be listed; that does not
  establish audio-stream support. Content capabilities and protocol-specific
  endpoints need explicit handling.
- `include/send_airplay2/playback.h` and `src/session_messages.*`: the public
  controls are Play/Pause/Stop/Seek, delivery is progressive or HLS remux, and
  current sessions set up type-130 URL/remote-control streams, not audio packets.
- Existing pairing/verification, secure control transport, credential stores,
  event handling and NTP timing are useful foundations for audio sessions.

The progressive path can already serve bytes with an audio MIME type. A short
M4A/MP3-to-Apple-TV experiment could therefore use existing machinery. It is
unverified and would not meet the speaker-inclusive requirement by itself.

## Effort ranges

These are rough engineering estimates for one full-time experienced developer,
including implementation, focused tests, CLI/C API/C# integration and a Windows
x64 packaged validation gate. They assume access to the relevant receivers and
unencrypted test media. They are not measured throughput or calendar promises.
Other-host hardware proof, broad receiver compatibility and Screenbox product
integration are additional work. Shared decoder work reduces the combined cost;
do not simply add every row as independent work.

| Slice | Effort | Completion scope |
| --- | --- | --- |
| Audio-only URL/HLS on Apple TV/Mac | About 5-10 engineering days, after a receiver feasibility probe | M4A/AAC first; useful smaller milestone, not speaker support |
| Audio files including one HomePod/audio-only speaker path | About 20-40 engineering days (4-8 working weeks) | New audio sender, common local audio formats, sender controls, safe teardown and x64 packaged proof; no multiroom/stereo-pair guarantee |
| H.264/HEVC video with DTS converted to stereo/5.1 compatible audio | About 15-25 engineering days (3-5 working weeks) | Optional audio-only transcoding, preserved video packets, seekable finite-file HLS, bounded cache and cancellation |
| Verified 7.1 preservation | About 5-10 additional engineering days if a codec/receiver spike succeeds | Correct channel layouts, independent audio HLS, seek/sync proof and actual eight-channel receiver/output test |

Protocol interoperability and a compliant, reproducible UWP codec build are
the largest uncertainties. A failed 7.1 codec spike or unavailable output
hardware prevents a reliable 7.1 completion estimate. Adding all legacy speaker
authentication modes, multiroom, low latency or live input would materially
increase the audio estimate.

## Feature 1: audio-file casting including speakers

An audio-only playlist is not enough for RAOP/AirPlay audio speakers. Add an
audio session alongside the existing URL-video session rather than forcing a
dummy video track or assuming a speaker fetches our video URL.

Necessary changes:

1. Expose audio/video capabilities and the appropriate AirPlay/RAOP endpoints
   through discovery and the binding, retaining advertisement-versus-tested
   compatibility distinctions. Do not make a video file selectable for an
   audio-only renderer merely because it was discovered.
2. Add audio-stream setup, media-packet encryption, sequence/timestamp handling,
   clock-based pacing, timing/synchronization, retransmission handling and bounded
   buffering. Validate the intended AirPlay 2 mode on an actual speaker; legacy
   RAOP and AirPlay 2 capability cannot be treated as interchangeable.
3. Decode local files to PCM and resample/convert channels to a negotiated
   speaker format. Start with a known WAV/PCM fixture for the transport spike,
   then common MP3/AAC/FLAC/WAV inputs through a reusable decoder backend.
4. Define pause/resume, seek/flush, volume, position, EOF/buffer drain, cancellation
   and cleanup contracts. Sender EOF is not proof the receiver has finished its
   buffered audio. Use synthetic-clock/packet-loss/counter-wrap tests plus manual
   audible receiver checks.
5. Extend the experimental C API/C# binding with an explicit audio mode and
   appropriate source/control contracts, preserving existing video behavior.
   A host-supplied PCM source can keep decoding optional; built-in file decoding
   is needed for a standalone file-casting experience.

The existing protocol reference already contains a separate encrypted PCM audio
sender path: [pyatv AirPlay v2 implementation](https://raw.githubusercontent.com/postlund/pyatv/master/pyatv/protocols/raop/protocols/airplayv2.py).
It is evidence of feasibility, not a drop-in portable implementation or a
compatibility guarantee. Its [MIT license](https://raw.githubusercontent.com/postlund/pyatv/master/LICENSE.md)
and the project's existing reference provenance are recorded separately from
any future implementation; no source is copied in this assessment.

## Feature 2: video with DTS or other unsupported audio

Keep supported H.264/HEVC video compressed and unchanged. Decode only the chosen
audio, optionally resample/map channels, then encode a receiver-compatible
audio track. Full video transcoding is outside this feature's proposed scope.

Recommended architecture:

- Add an optional codec backend, preferably a narrowly configured FFmpeg
  library build using `libavformat`, `libavcodec`, `libavutil` and
  `libswresample`. The first prototype may preconvert an entire file; production
  should not require duplicating a whole movie or waiting for full conversion.
- Adapt custom input I/O to existing `read_at`/size callbacks, preserving
  brokered UWP file access. A spawned `ffmpeg.exe` process is not a packaged
  UWP integration strategy.
- Select the input audio track explicitly enough to avoid silently choosing a
  different language. Pass through compatible audio; only invoke fallback when
  necessary and authorized by the selected delivery policy.
- Build a video VOD playlist plus an independent audio rendition. Handle codec
  configuration, channel layout, priming/padding, timestamp offsets and equal
  presentation ranges. Preserve existing subtitle renditions.
- Generate/cache encoded segments with bounded memory/disk and concurrency,
  stable bytes for HEAD/range/repeated requests, request deadlines and prompt
  Stop cleanup. Seek needs decoder preroll and encoder-delay accounting; avoid
  gaps/drift when encoded audio crosses segment boundaries.
- Extend C options/binding for an explicit fallback policy and errors. Keep
  existing HLS-remux-only semantics and rejection behavior when fallback is off.
- Add CMake/dependency packaging, DLL/source/hash/license provenance and UWP
  API/architecture checks. Screenbox needs to opt into the new delivery policy;
  its LibVLC capability does not automatically make the standalone native
  library a decoder.

For finite seekable files, a **growing EVENT playlist is not inherently required**.
An indexed VOD presentation can have a known resource/segment table while each
encoded segment becomes a sealed cached resource on demand. Existing deferred
size support helps, but scheduling, seeks and codec timing are new work. Live
or unknown-duration transcoder output would require the separate growing-HLS
feature and should not expand this first slice.

FFmpeg's [AAC transcoding example](https://ffmpeg.org/doxygen/9.0/transcode_aac_8c-example.html)
demonstrates the decoder/resampler/encoder building blocks. Prefer a reviewed
LGPL-compatible build profile without GPL/nonfree additions. Keep the core's
Apache-2.0 license and record exact dependency configuration, notices/source
and distribution obligations before adding it; see
[FFmpeg's licensing guidance](https://ffmpeg.org/legal.html). Dynamic library
packaging is its documented straightforward compliance approach, not the only
possible approach. No codec dependency is added by this assessment.

## DTS 5.1 and 7.1 feasibility

**Preserving surround channels is feasible; native DTS playback/passthrough is
not the proposed path.** Apple lists AC-3 5.1 and E-AC-3 7.1 support on
[Apple TV 4K](https://www.apple.com/apple-tv-4k/specs/).
Its [HLS authoring specification](https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html)
also describes multichannel AAC and requires separate audio streams for
multichannel content (9.6).

- **5.1:** decode DTS to six-channel PCM, encode AAC-LC or AC-3, and verify the
  six-channel mapping and the attached playback system. This is the practical
  first surround milestone.
- **7.1:** an eight-channel AAC-LC rendition is a candidate for an early receiver
  spike. Its exact layout/configuration and Apple TV HLS behavior need proof,
  including side-versus-back channels; do not infer it from a stereo pass or a
  codec label. The current AAC helpers need an audit of channel-configuration
  versus channel-count/PCE handling. FFmpeg's [AAC encoder source](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/aacenc.c)
  has PCE/layout handling and a 7.1-wide option, which is not proof of the
  intended standard 7.1 mapping on this receiver.
- **E-AC-3 7.1 is not a simple alternative with the proposed stock backend.**
  FFmpeg's [E-AC-3 encoder](https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0/libavcodec/eac3enc.c)
  uses the AC-3 layouts, whose
  [supported-layout table](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavcodec/ac3enc.c)
  stops at 5.1. Our MKV path also rejects E-AC-3 dependent substreams, used for
  some 7.1 streams. That limitation would need additional implementation and
  encoder evaluation; Apple TV's decoder support does not solve it.

Channel preservation does not mean bit-perfect DTS-HD Master Audio, and does
not preserve DTS:X object metadata through ordinary AAC/AC-3 encoding. These
are separate requirements, not promises of the proposed surround fallback.
The user's HDMI/receiver/speaker path must reproduce the target channels;
validation needs channel-identification fixtures and a suitable actual output
system, with the user reporting results. A lossless ALAC/FLAC route is a later
alternative needing its own multichannel HLS proof and writer work.

## Suggested order and acceptance gates

1. Run a small format/packaging spike: copied video plus DTS-to-5.1 output,
   then a distinct eight-channel AAC fixture using the existing development
   HLS-directory path. Check all channel positions, video sync, seeks and end
   behavior manually on the Apple TV. Separately prove WAV/PCM delivery to the
   chosen speaker. These are proposed experiments; none ran in this assessment.
2. Implement the shared decoder/source/cancellation backend and DTS 5.1 fallback
   as the first useful shipping slice. Use segment/seek/priming tests and
   receiver-observed channel mapping; do not silently downmix a requested
   surround policy.
3. Complete 7.1 only after the format spike passes; otherwise report the blocker
   instead of weakening the requirement without a user decision.
4. Implement the audio speaker session and reuse the common decoder. Validate
   one identified speaker/model/firmware first, then broaden receivers separately.

If speaker support is the higher product priority, steps 2/4 can be reversed
after the two feasibility probes. Screenbox music/queue/SMTC/volume/artwork
integration is a separate follow-up, roughly another 5-10 engineering days once
the native APIs work. Broad portable packaging or receiver matrices can add
further time. No feature implementation or prerequisite experiment is authorized
merely by this effort assessment.
