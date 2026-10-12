# Next slice: video with surround audio downmixed to stereo AAC

Planning decision D67: 2026-10-12. Baseline: native merged main `aa0f25e`
(PR #43), Screenbox merged main `f596fd5e` (PR #11). This document specifies
the next implementation slice; it does not implement or validate it.

Planning complete: native PR #44 merged as `12c24a7`, with all 14 checks passed
at final head `bc4b381` and the source-track-selection review resolved.
Screenbox checkpoint PR #12 merged as `ce782356` (docs-only; no CI checks
reported). Those planning merges add no codec code, dependencies or receiver proof.

**Implementation authorization/status:** the user's subsequent Proceed
authorizes D68's [codec/packaging spike](stereo-aac-codec-spike.md). Its optional
development probe and offline AppContainer host are implemented and tested in
the recorded Windows x64 scope, merged in PR #46 as `cb9d641` after all 16 checks
passed at `579bb44`. This supersedes the initial planning-only
authorization below. D69's later authorized [source foundations](audio-source-inventory.md)
merged in PR #48 as `5d46479` after all 16 checks passed at `c816709`, adding
inventory/selection, independent video indexing and seek metadata.
Actual decoder stream matching/seeking, production HLS conversion and receiver
validation remain work; no new casting mode or package release is implemented.

## User decision and deferred work

The user selected video-file casting with surround audio **downmixed to stereo
AAC** as the next slice to detail. Audio-file casting, including HomePod and
audio-only AirPlay speakers, and DTS conversion retaining 5.1/7.1 output are
deferred because suitable local test hardware is unavailable. Keep their
[research, feasibility limits and effort estimates](audio-features-assessment.md)
for a later hardware-backed decision. Do not start those experiments or
implementations as prerequisites for stereo output.

This request authorizes documentation and planning. Feature implementation,
dependency adoption and package publication have not been authorized by it.
The architecture and output settings below are engineering proposals within
the selected scope; no new receiver test or performance measurement has run.

## Proposed first-version contract

- Accept finite, unencrypted, seekable MP4/MOV and MKV files with video already
  supported by the remux (H.264 or HEVC). Copy compressed video samples without
  re-encoding. Preserve existing text-subtitle support and presentation timing.
- Add an explicit stereo-AAC delivery mode. Convert the chosen audio track to
  two-channel AAC-LC, initially proposed at 48 kHz and 192 kbit/s. These settings
  need quality/performance checks; they are not user-mandated constants.
- Cover multichannel AAC, AC-3, E-AC-3 and DTS inputs. The codec spike must define
  the exact supported DTS core/DTS-HD profiles and E-AC-3 dependent-substream
  handling using fixtures. DTS-HD 7.1 input can be downmixed without a 7.1
  playback system, but must not be advertised until its decoder fixture passes.
  Unsupported profiles fail explicitly. TrueHD, encrypted inputs and arbitrary
  additional FFmpeg codecs are outside the initial support matrix.
- In this opt-in mode, also encode a selected mono/stereo track to the same
  stereo output format. This gives one clear contract and avoids a separate
  passthrough optimization in the first version. A video without audio can use
  the existing video-only remux; do not synthesize a silent track.
- Preserve today's progressive and HLS-remux modes and their defaults. Remux
  remains conversion without decoding, with its current unsupported-format
  behavior. Never change a surround-preserving request into stereo implicitly.

Stereo output does not retain discrete surround channels, lossless DTS-HD
audio or DTS:X object metadata. Audio-only media, speaker transport, volume,
multiroom, video transcoding, live/unknown-duration sources, new HDR support
and choosing tracks from the Screenbox UI are separate work.

## Source selection and decoding

Current MP4/MKV selection filters audio by whether it can be remuxed. In the
new mode, select the intended source **before** applying decoder support:

- **Explicit selection:** accept a source-container audio track identity, not
  an index in the filtered remux result. It takes precedence over automatic
  selection. A missing, disabled or non-audio identity is invalid selection;
  a valid selected track with an unsupported decoder/profile returns unsupported.
  Neither case falls back to another track.
- **MP4/MOV automatic selection:** use the sole enabled audio track. With two
  or more enabled audio tracks, require explicit selection. Do not infer the
  intended track from file order, codec preference or the enabled flag. The first
  version does not implement MP4 alternate-track/default-selection semantics.
- **MKV automatic selection:** use the sole enabled audio track, or the unique
  default-flagged enabled audio track among several. Multiple enabled tracks with
  zero or multiple default flags require explicit selection rather than file-order
  fallback.

For either container, automatic mode with no enabled audio retains video-only
behavior. An explicit identity still must pass validation; it cannot fall back
to video-only output if the requested audio is absent or disabled.

In particular, an enabled AAC commentary track preceding enabled main DTS in
an MP4 must report selection-required without an explicit identity. Selecting
the DTS track must decode that track, even if AAC is earlier or easier to remux.
Language/role metadata can help a caller choose, but must not be guessed from
codec or file order. Record this contract in the CLI, C API and binding docs.
Explicit native/binding selection is part of this slice; a Screenbox track
picker remains separate product work.

Use an optional in-process FFmpeg backend (`libavformat`, `libavcodec`,
`libavutil`, `libswresample`) for audio discovery, decoding, channel mapping,
resampling and AAC encoding. Adapt its seek/read I/O to the existing immutable
size/read-at callbacks, including short reads, concurrent ownership rules,
deadlines and cancellation. A spawned `ffmpeg.exe` is not the UWP backend.

Retain the native video's sample index and payload source. Separate video
indexing from the current remuxable-audio requirement so unsupported audio
does not prevent indexing an otherwise supported video. Match the decoder's
audio stream to the selected container track by stable identity, not by
assuming two demuxers assign the same array index. Probe malformed files with
bounded work and reject incompatible stream changes rather than changing
codec configuration halfway through a presentation.

Before dependency adoption, pin the source/version, build flags and exact
enabled codec profiles; record source/license provenance in `dependencies.md`
and distribution notices. Keep the core Apache-2.0. Review an LGPL-compatible
FFmpeg build without GPL/nonfree additions and its redistribution obligations
using [FFmpeg's licensing guidance](https://ffmpeg.org/legal.html). Verify the
actual binaries and build configuration, not just the project's general
license. This planning PR adds no third-party code or dependency.

## Downmix and timeline requirements

Decode to PCM with an explicit channel layout. Distinguish side/back variants
of 5.1/7.1; do not treat a channel count as a layout or AAC's channel-configuration
field as a literal channel count. Unknown/ambiguous layouts need a documented
rejection or verified container-specific rule, not guessed channel ordering.

Review and record the resampler's stereo matrix: center and surround
contributions to left/right, treatment of the LFE channel, and headroom to avoid
clipping. Test every channel independently and a correlated full-level signal.
Choose and document LFE inclusion/attenuation during the codec spike. Exclude
automatic loudness normalization and dynamic-range processing from the first
slice unless they are separately justified; the downmix must be predictable.

Keep audio on the video's presentation timeline, including original audio
offsets, MKV CodecDelay, resampling timebase conversion, decoder preroll, AAC
encoder priming and final padding. Account for timestamps in sample units
before converting to playlist seconds. Video decode/presentation reordering
must remain intact. The final audible duration must not acquire an extra AAC
frame of silence or truncate the last source samples through rounded timing.

Seek and lazy generation need reproducible decoder/encoder entry points.
Prototype both forward and backward segment requests, including a cold request
near the end, and prove the required preroll/discard and priming treatment.
Independently restarting an encoder at each segment is not acceptable without
proof that boundaries remain gap-free and correctly signaled. If bounded
random-access encoding cannot meet this gate, revise the segment strategy before
public API work; do not hide a whole-file conversion delay in Start.

## HLS presentation and resource generation

The proposed presentation is a copied-video VOD playlist plus a separate stereo
AAC fMP4 audio rendition. Separate audio is an engineering choice here: it
avoids duplicating video bytes in the encoded cache and keeps video remux work
independent. Apple's requirement for separate **multichannel** audio does not
make separate stereo audio mandatory. See the
[Apple HLS authoring specification](https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html).

Extend the master-playlist model with an audio group, `EXT-X-MEDIA:TYPE=AUDIO`,
the video variant's `AUDIO` association, two-channel declaration and accurate
codec/bandwidth metadata. Serve audio initialization/fragments as `audio/mp4`;
retain subtitle associations. Test receiver choice of the default audio
rendition; do not merely test that its URL is fetched.

Plan the finite VOD resource table up front from the video timeline. Audio and
video must cover matching presentation ranges with boundaries accounted for
in their own timebases. No growing EVENT playlist is required for this scope.
Refactor payload access to allow original video and generated audio sources;
the current remux's single original-input provider cannot serve both.

Generate audio segments lazily, with a bounded worker queue and small bounded
PCM/encoded buffers. `MediaResource::size_on_request` can wait for generation,
then publish a sealed fragment; HEAD also triggers this path. It does not
already provide encoding, scheduling or storage. Only publish a size after
successful generation; concurrent requests for one resource share that work.
Never expose partial fragments. Preserve absolute HTTP deadlines and ensure
one cancelled request cannot destroy bytes another reader is using.

For the first version, prefer a quota-limited, per-cast encoded-audio disk cache
plus a bounded memory working set. Retain sealed fragments for the cast's
lifetime, avoiding nondeterministic regeneration after eviction: repeated HEAD,
GET and range requests must return the same size and bytes. Disk usage may
grow with encoded audio duration, but never includes a full copied movie or
requires complete audio conversion before playback. Estimate admission needs
from duration/output rate and enforce an actual byte quota; exhaustion produces
a clear bounded failure rather than unlimited disk use.

The packaging spike must settle a host-authorized cache-directory or storage
callback contract that works inside AppContainer. Do not assume arbitrary
filesystem access from brokered `StorageFile` access. The host owns permission
to temporary storage; the cast owns its files. Stop must cancel work, join
workers/readers, release source ownership exactly once and remove only that
cast's cache. Startup failures and allocation/I/O failures need the same cleanup.

## Public interface and packaged integration

Propose a new named delivery value such as `HlsStereoAac`, separate from
`HlsRemux`. Bump the experimental API version when changing the interface;
append source-audio selection and any cache/output options using `struct_size`
and test tail-padding and layout on x86/x64/ARM64. Older structs retain existing
defaults. Old libraries must reject an unknown mode, and a backend-disabled build must report that
the requested mode is unavailable. Distinguish malformed input, unsupported
codec/profile, selection-required/invalid-selection, backend availability,
cache limits and conversion failure without exposing source paths or receiver
details. Exact names/result additions are to be settled in the implementation
review.

Expose the same opt-in mode and explicit source-audio selection in the CLI,
C++ interface and C# binding. Add a bounded source-track probe so callers can
obtain selectable container identities and available language/role metadata;
document cancellation and ownership without leaking source paths. Automatic
selection is the default for older/shorter option structs, subject to the new
mode's ambiguity rules. Test size initializers, exception containment,
release-once ownership and cancellation.
Keep the optional backend disabled in the baseline build and avoid loading
codec DLLs for existing remux-only callers. Follow CMake/CTest static/shared
checks in README and add the enabled-backend configurations to CI.

Before a consumable package, verify the configured FFmpeg APIs/dependencies
are AppContainer-compatible, build supported Windows payload architectures,
and record DLL/source/configuration hashes and notices. Begin packaged runtime
validation on x64; cross-compilation is not proof of x86/ARM64 playback.

Screenbox integration follows the native/binding gate: pin an approved package,
opt local video casting into the new policy, provide conversion/cache failure
feedback, and preserve pause/Stop/queue handoff. Until a host can supply an
explicit track identity, ambiguous files fail with selection-required feedback;
Screenbox must not guess an identity or silently retry another audio track.
LibVLC's decoder does not automatically supply this native backend. Keep pairing
data and package identity; verify loaded DLL hashes before attributing receiver
results to a build. Any
visual change requires written user checks, without Computer Use, per the user.
Publishing a new prerelease and merging remain separate approval steps.

## Ordered work and acceptance gates

1. **Codec and packaging spike.** Establish reproducible licensed library builds,
   UWP loading, brokered I/O and cache access. Generate owned 5.1/7.1 PCM
   channel-ID fixtures and compressed variants where an available encoder
   supports the profile. Obtain redistribution-permitted DTS/DTS-HD fixtures
   with recorded provenance; do not assume the backend can encode DTS-HD 7.1
   test files. Establish the supported decoder matrix, downmix policy, priming
   and random-access segment strategy. A full-file offline conversion can be a
   comparison oracle only.
   **D68 checkpoint:** the development codec/build/storage/window probe is merged
   in PR #46. Read its evidence and retain the remaining DTS-HD/dependent-substream
   fixture and compressed-input seek/priming/HLS limits; do not repeat completed
   probe checks or interpret the PCM window experiment as production movie seeking.
2. **Native conversion and presentation.** Implement independent audio payloads,
   stereo encoder, audio rendition, lazy generation and quota/cleanup contracts.
   Verify compressed video payload hashes remain identical; decode output and
   check two channels, expected per-channel contributions, clipping, sync,
   duration and segment joins. Preserve existing subtitle coverage.
   **D69 checkpoint:** source inventory/selection across MP4/MKV, independent video
   indexing and source seek metadata merged in PR #48 as `5d46479`. Actual decoder
   stream matching and compressed-input seek/timestamp validation remain next,
   before lazy stereo audio resource generation. Public probe/selection contracts
   remain part of the API/host gate.
3. **API and host gate.** Extend ABI/bindings/CLI and optional-backend CI. Cover
   MP4 AAC-commentary-before-DTS and reversed-order fixtures: both require
   selection when unspecified; explicit DTS selects DTS in either order.
   Also cover sole-enabled-track selection, disabled tracks, missing/non-audio
   identities, an unsupported explicitly selected track without fallback, and
   MKV unique/absent/duplicate default flags. Verify probe identities select the
   same source in both demuxers. Exercise malformed/truncated/unsupported inputs,
   long files, cold forward/backward
   seeks, out-of-order and duplicate requests, HEAD/ranges, deadline expiry,
   quota/I/O failure and Stop during probing/encoding/read. Run applicable static
   and shared CTest, pure-C ABI and C# checks; build the packaged x64 host using
   the project's documented tooling. No speaker session is added in this slice.
4. **Receiver and Screenbox gate, when hardware is available.** On an identified
   Apple TV/Mac receiver and exact loaded build, manually confirm audible stereo
   downmix, center/dialogue and surround contributions, video sync, start near
   45 seconds, pause/resume, seeks both ways, Stop, natural EOF and queue advance.
   Offline channel tests and CI do not prove AirPlay playback. Record receiver,
   firmware, source profile and output settings. Do not claim 5.1/7.1 output from
   this stereo gate or reopen the already user-verified sleeping-TV check.

Planning allowance: roughly **15-25 engineering days (3-5 working weeks)** for
one experienced developer through native/API/packaged readiness, consistent
with the earlier conversion estimate but now restricted to stereo output.
This is unmeasured; codec licensing/build and seek/priming/cache work dominate
the uncertainty, along with track probing/selection across two demuxers.
Screenbox product integration and waiting for suitable receiver hardware are
additional. Revisit the estimate after the first spike rather than
promising a completion date. The subsequent Proceed authorized the D68
codec/packaging gate recorded above; that initial planning-only authorization
limit must not cause a repeat permission request for the completed spike.
