# Automatic audio delivery and expanded decoder scope (D76)

Decision: 2026-10-14 (Asia/Tokyo). Baseline: merged PR #56, `02d8be6`.
Planning PR #57 merged as `faee858` at final head `d4f4a62`, with all 16 checks
passed and its block-padding review resolved. The subsequent authorized
[D77 decoder/profile foundation](audio-decoder-expansion.md) implements the recorded
private subset; coarse Matroska timing remains next. Automatic delivery and
versioned policy reporting are not implemented.

## User requirement and current evidence

The user requested automatic handling of unsupported audio streams and selected
**automatic policy plus FLAC, Opus, Vorbis, PCM and TrueHD** for the first
expansion. These are audio tracks in otherwise supported, finite video files.
Audio-only files/speakers, discrete converted 5.1/7.1 output and Atmos output
remain hardware-deferred under [D67](audio-features-assessment.md).

At the D76 planning baseline, API 4 offers explicit stereo AAC delivery. Its selected decoder accepts
AAC, AC-3, E-AC-3 and DTS-core in the recorded MP4/MKV matrix; it converts the
selected track even if that track could have been copied. Legacy HLS remux copies
eligible audio without decoding. Neither path implements this automatic policy.
Read [playback contracts](stereo-aac-playback.md),
[decoder evidence](audio-decoder-source.md) and [D75's offline AppContainer gate](stereo-aac-uwp-runtime.md).
Those passes do not validate the new input families or converted receiver playback.

Apple's [HLS authoring specification, audio sections 2.2-2.6 and 2.25](https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html)
includes FLAC, AAC and Dolby audio, subject to format/authoring requirements.
Thus FLAC is not universally unsupported by Apple TV. A codec's presence in that
specification does not establish this library's remux support or playback on a
particular AirPlay receiver. Discovery model/features are untrusted metadata;
the current API has no verified per-receiver codec capability negotiation.

## Proposed automatic policy

Add an **opt-in automatic audio delivery mode** for a named, versioned Apple HLS
video profile. Keep progressive, legacy remux and explicit force-stereo defaults
and contracts intact. Choosing automatic mode permits stereo conversion when
the selected audio cannot be copied under that profile. Retain a preservation
choice that refuses conversion rather than silently losing surround/lossless audio.
Names, enum values and ABI fields below are engineering proposals.

Resolve the original audio identity before any codec eligibility filtering:

1. An explicit original MP4 track_ID or MKV TrackNumber must identify enabled
   audio; missing, disabled or non-audio IDs are invalid and never fall back.
2. MP4 automatic selection requires a sole enabled audio track. MKV may also
   choose a unique enabled default among multiple audio tracks. Ambiguity reports
   selection-required, irrespective of which codecs are easier to handle.
3. No enabled audio gives video-only delivery. An invalid explicit ID cannot
   become video-only delivery.
4. Validate source protection, video eligibility, configuration and timeline.
   Copy the selected audio if eligible for this delivery profile; otherwise
   convert it only if a validated decoder/container/configuration combination
   exists. Report unsupported when neither route exists.

For the first profile, the copy candidates are AAC, AC-3 and E-AC-3 within the
native remuxer's supported configuration/timeline limits. Codec labels alone
are insufficient: record the exact accepted AAC object types/configurations,
rates/layouts and Dolby stream forms in an executable eligibility table during
implementation. Do not infer support for HE/xHE-AAC, PCE layouts, dependent
E-AC-3 substreams or JOC merely from a generic AAC/E-AC-3 decoder being present.
An otherwise valid configuration outside the copy table may convert if it has
its own validated decode case; malformed data must fail. Preserve eligible copied
surround payloads. Copy-only output is not a universal Apple HLS compatibility
claim: some authoring profiles require additional fallback renditions, which this
initial selected-track policy does not supply.

The conversion candidates are the existing validated inputs plus the new matrix
below. Conversion reuses 48 kHz, two-channel AAC-LC, the recorded downmix matrix,
copied video/text associations and bounded lazy host cache. This first profile
converts FLAC because FLAC remux is outside its copy table; report this as a
delivery-profile limitation, not receiver rejection. FLAC passthrough is separate
future work. TrueHD conversion loses lossless quality and object metadata and
does not implement Atmos or discrete surround output.

Report selected identity, source codec, resolved action (copy/convert/video-only),
output codec/channels and a non-secret reason before playback. Do not relabel
source/cache failures, malformed/encrypted input, unsupported video/timelines,
timeouts or cancellation as a reason to try another track or codec. Do not retry
with conversion after generic receiver/network/consent startup failure: those
failures are not reliable codec evidence.

## Proposed new input matrix

This finite matrix is the initial implementation target, not current support.
Every included mapping needs owned fixtures and exact native/backend identity
and configuration agreement. Other containers/variants return unsupported.

| Family | Initial video-container target | Additional work before acceptance |
|---|---|---|
| FLAC | MKV `A_FLAC`; MP4 `fLaC` | Validate CodecPrivate/STREAMINFO and MP4 sample entry; variable block lengths, source depth/rate/layout, seeks and final trim |
| Opus | MKV `A_OPUS`; MP4 `Opus` | Validate identification header or `dOps`, channel mapping, pre-skip/CodecDelay/preroll exactly once, variable packet durations and end discard |
| Vorbis | MKV `A_VORBIS` | Bound and validate Xiph-laced private headers, channel order and variable block/overlap timing; no MP4 Vorbis claim |
| PCM | MKV integer little/big endian and IEEE float; explicit MP4/MOV PCM entries | Carry bit depth, endian, packing, rate and layout metadata; map only fixture-backed sample-entry forms to exact decoder IDs |
| TrueHD | MKV `A_TRUEHD` | Verify native decoder dependencies, access-unit/parser behavior, channel layout, seek restart/preroll and final drain; no MP4 TrueHD claim |

Proposed PCM subset: signed 16/24/32-bit little/big endian and IEEE 32/64-bit
float (Matroska float is little endian). MP4/MOV `sowt`, `twos`, `in24`, `in32`,
`fl32`, `fl64` and `lpcm` require separate exact version/flag/packing validation;
an entry name is not sufficient. Exclude unsigned 8-bit, companded audio and
DVD/Blu-ray packed PCM from this initial subset. Unsupported PCM variants fail
explicitly. These subset boundaries are proposals within the user's PCM scope.
Retain the existing channel-layout and resource limits until measured evidence
justifies changing them; reject unknown layouts and unsupported format changes.
DTS-HD profiles and E-AC-3 dependent substreams remain unvalidated.

Use the [Matroska codec mappings](https://www.matroska.org/technical/codec_specs.html)
and the pinned [MOV configuration reader](https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.2/libavformat/mov.c)
as references for the proposed container mappings. Source labels must still match the actual decoder
stream, preserving D70's protection against metadata impersonation.

## Necessary library and packaging changes

- **Backend profile and provenance:** the pinned FFmpeg 9.0.2
  [codec registry](https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.2/libavcodec/allcodecs.c)
  contains native decoders for these families. Expand the narrow overlay's
  decoder/parser list only after checking their configuration dependencies and
  license notices. Prefer native decoders within the adopted libraries; do not
  enable all codecs, external codec libraries, network, GPL or nonfree code.
  Keep default builds codec-free. Already enabled PCM spike decoders do not
  establish production PCM source support.
- **Inventory/decoder:** extend `SourceTrack` metadata and
  `audio_decoder_source.cpp`'s current four-family allowlist. Preserve original
  IDs, bounded probing, exact native/backend agreement and callback ownership.
  Validate new sample-entry/private headers and distinguish malformed from
  unsupported configurations. Add only necessary native parser metadata.
- **Timeline:** the HLS builder currently opts into a fixed-frame Matroska clock
  that rejects changing decoded frame sizes. It cannot simply be applied to
  variable Opus/Vorbis/FLAC/PCM frames. Develop sample-accurate seek-window
  reconstruction for those streams with phase-stable resampling, codec delay,
  leading discard and final padding applied once. Preserve current fixed-frame
  behavior and literal joins/trim expectations for existing codecs.
  Matroska `DiscardPadding` is a signed nanosecond value on each `BlockGroup`,
  not track metadata: positive values trim the block's end, negative values its
  beginning ([RFC 9559 section 5.1.3.5.7](https://www.rfc-editor.org/rfc/rfc9559.html#section-5.1.3.5.7)).
  Keep delay/preroll in `SourceTrack`; its bounded pre-cluster inventory probe
  cannot supply block trimming. For the decoder route, preserve
  the pinned [Matroska demuxer's packet skip/discard side data](https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.2/libavformat/matroskadec.c)
  (`AV_PKT_DATA_SKIP_SAMPLES`) when sending each selected packet to FFmpeg.
  Verify [decoder consumption](https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.2/libavcodec/decode.c)
  before resampling; never reapply trimming already performed by the decoder.
  If native packet indexing is needed instead, retain bounded signed padding
  with its block/packet identity and convert nanoseconds to source samples with
  checked arithmetic. Neither route may replace per-block values with one
  track-wide tail trim or reuse consumed trim state across seek-window rebuilds.
  Apply each packet's trim once in every independently decoded window.
- **Selected copy route:** `remux_to_hls` currently accepts no original audio ID.
  Add a selection-aware composition path; reusing legacy filtered selection
  could copy AAC commentary instead of selected main DTS. Preserve legacy
  behavior through its existing entry point and share payload/index helpers.
- **Preparation/controller:** resolve policy before resource registration.
  Return one prepared presentation with a recorded resolved action and optional
  conversion owner. Only conversion needs an encoder/cache/worker monitor;
  copy must work without a codec backend or host cache when eligible. Arm and
  clear conversion storage under existing failure/Stop rules, without touching
  unrelated host storage. Preserve cancellation, deadlines and terminal results.
- **API/binding/CLI:** propose a versioned extension (likely API 5) carrying
  automatic delivery/profile and resolved action/reason, with original track
  selection retained. Preserve v1-v4 struct prefixes, enum values, ownership and
  defaults; older consumers must remain valid. Validate cache requirements after
  resolving action: eligible copy works in a codec-free build; conversion in
  such a build reports backend-unavailable, and missing conversion storage fails
  explicitly. Make reporting possible without receiver access or credentials.
- **Distribution/hosts:** update codec-profile records/notices and import checks
  for desktop/UWP enabled builds. Expand the separate offline AppContainer
  harness with fixtures for every new family, including brokered input and cache
  failure/cancellation. Package/Screenbox integration follows validation and
  separate publication approval; the current pin remains `0.3.0-ci.207`.

## Ordered implementation and acceptance gates

1. **Decoder/profile foundation:** pin the expanded minimal configuration and
   record provenance before adoption; generate owned video fixtures for all five
   families and the proposed PCM variants. Implement configuration/identity and
   variable-frame timeline support privately. Gate each mapping on original-ID
   selection, independent sample/channel expectations, cold/middle/end and
   backward/forward seeks, contiguous joins, delay/trim and EOF. Record unsupported
   variants explicitly rather than accepting arbitrary FFmpeg inputs.
   Include MKV Opus with positive final-block DiscardPadding and a distinct
   negative leading-padding case. Assert independent audible sample counts and
   tail samples for sequential decode, a cold tail window and repeated
   forward/backward window rebuilds, then check generated AAC final trim after
   decoder/resampler/encoder drain. Cover absent padding and malformed/over-limit
   values; neither missing nor double trimming may pass on duration alone.
2. **Policy and selected remux:** add deterministic copy/convert resolution and
   selection-aware remux. Cover main DTS plus earlier AAC commentary, compatible
   selected AAC/AC-3/E-AC-3 (payload equality/no decoder-cache calls), each new
   conversion family, no audio, ambiguity, invalid IDs, missing backend/storage
   and non-fallback failures. Preserve video/text payloads and associations.
3. **Public integration:** expose versioned contracts, offline decision reporting,
   binding and CLI; test ABI compatibility and both prepared-media lifecycles.
   Run README's static/shared CMake/CTest checks and the enabled Windows matrix;
   inspect actual PR-head CI. Preserve OFF-by-default/private-hook boundaries.
4. **Packaged runtime and performance:** repeat D75's loaded-module/hash,
   brokered-source, TemporaryFolder ownership and failure gates across new
   families. Measure real-file startup, requested/actual input bytes, cold seek,
   memory/CPU, cache quota/cleanup and cancellation. D75's tiny-read budget
   refusal and D73's bandwidth-estimate limitations remain open calibration items.
5. **Receiver and host release gates:** when appropriate hardware and user
   observation are available, validate copied and converted video/audio, sync,
   start position, pause/resume, seeks, Stop/recovery and natural EOF on explicitly
   recorded Apple TV/Mac firmware/hosts. Local resource exhaustion is not receiver
   EOF. Then seek package publication approval and update/test Screenbox. Visual
   confirmation uses written manual steps, per the user's preference.

Steps 1-4 can progress offline without audio-only speakers or surround-output
hardware. Their success must not be reported as receiver compatibility. The next
implementation PR should start at step 1; split family/timeline work into small
reviewable increments rather than adding a decoder list and declaring completion.
D75's real-file/performance and converted-receiver gates remain required for
existing DTS delivery as well as this expansion; they are not waived or reset.

Provisional additional effort from D74/D75: **10-20 engineering days**, excluding
hardware scheduling, review/CI waits and Screenbox release work. Rough allocation:
4-8 days decoder/configuration/timeline fixtures, 3-5 policy/selected remux/public
integration, 3-7 packaged/performance validation and corrections. This is an
engineering estimate, not a measured commitment. Variable-frame seek accuracy,
PCM packing/layout variants and TrueHD fixtures are the largest uncertainties;
revise the estimate at the first decoder gate.

## Evidence boundary

D76 changes documentation only. No decoder profile, source matrix, automatic
mode, ABI, package or receiver result is added. The linked primary specifications
and pinned source registry establish research references, not implementation
provenance or interoperability. Record exact source/configuration, fixture
generators/hashes and notices when implementing. The core retains Apache-2.0.
