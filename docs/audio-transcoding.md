# Video audio selection and conversion

Automatic delivery is a finite library policy for known source configurations.
It does not ask a receiver to play an arbitrary stream and transcode only after
failure. The API uses `SAP2_DELIVERY_AUTOMATIC_AUDIO_HLS` with
`SAP2_AUDIO_PROFILE_APPLE_HLS_V1`. Audio-only inputs use a
[different explicit profile](automatic-audio-only.md).

## Original track identity

Inventory retains MP4 `tkhd.track_ID` and Matroska `TrackNumber`, including
disabled, unknown and protected tracks. Resolve selection before codec filtering:
an explicit ID must name enabled audio; otherwise a sole enabled audio track
wins. Matroska additionally permits a unique enabled default. Ambiguity requires
caller selection. No enabled audio produces video-only output.

Do not substitute a filtered-array index, title or language for source identity.
The FFmpeg Matroska overlay exports the full original TrackNumber as reserved
metadata, protected from file-authored tag overrides. Native/backend identities,
codec configuration, layout and effective sample rate must agree.

Inventory is bounded to 128 tracks, 4096 outer/pre-cluster elements, 64-MiB MP4
moov or 16-MiB Matroska metadata, and a separate 128-MiB/1,048,576-callback host
probe budget. It does not scan/decode every payload. Zero/duplicate identities,
protection and unsupported description/configuration forms remain explicit errors.

## Selected copy table

| Family | Finite admitted configuration |
| --- | --- |
| AAC-LC | MPEG-4 OTI 0x40, object type 2, 44.1/48 kHz, channelConfiguration 1/2/6, 1024 samples; exact supported GASpecificConfig/SBR-absent forms |
| AC-3 | 44.1/48 kHz, bsid 8, bsmod 0, 1536 samples; mono/stereo without LFE or 3/2 with LFE; `dac3` agrees with complete frames |
| E-AC-3 | Same rates/layouts, bsid 16, one independent substream ID 0, six blocks/1536 samples; exact `dec3`; no dependent substreams or optional compression/mixing/informational/additional BSI |

This is narrower than everything an Apple TV might decode. HE/xHE-AAC, arbitrary
PCE/configuration extensions, dependent Dolby streams and JOC are not admitted
implicitly. Copy validates the supported headers/clocks, but does not decode
every AAC compressed payload. Version-zero sample entries and container/edit
requirements still apply. Selected Matroska copy refuses lacing/nonzero padding
it cannot preserve rather than silently dropping those semantics.

Copy/video-only works without the codec backend, fragment store or quota.
`SAP2_AUDIO_PRESERVE_ORIGINAL` permits it and refuses required conversion.

## Conversion input

Covered video-track forms include DTS-core, MP4/Matroska FLAC and Opus,
Matroska Vorbis/TrueHD, and explicitly packed PCM:

| Container | Supported decoder mappings |
| --- | --- |
| MP4/MOV | DTS-core forms, FLAC `fLaC`, Opus `Opus`; MOV `sowt`/`twos` signed 16-bit; version-one `in24`/`in32`/`fl32`/`fl64` with validated packing/endianness |
| Matroska | `A_DTS` core, `A_FLAC`, `A_OPUS`, `A_VORBIS`, `A_TRUEHD`; signed 16/24/32-bit integer and supported 32/64-bit IEEE PCM |

Configuration, parser, channel-mask, rate, trim, clock and resource validation
remain mandatory. Unspecified multichannel PCM layouts refuse; a channel count
does not define speaker positions. MOV version-two `lpcm`, unsigned/companded
PCM and DTS-HD are outside the validated matrix. Registered decoder names do
not establish support for other container/configuration combinations.

Conversion requires explicit stereo permission, `SAP2_STEREO_AAC=ON`, complete
per-cast storage callbacks and a positive quota. Failure never chooses another
track/representation or retries playback. `sap2_media_check_audio` can inspect
the preparation decision without a receiver; it is not a reusable prepared cast.

## Output and downmix

Output is 48-kHz stereo AAC-LC, nominal 192-kbit/s ABR, in a separate fMP4 HLS
audio rendition. Video and supported text tracks remain copied. FL/FR map
directly, center and corresponding side/back surround contribute at sqrt(0.5),
and LFE contributes 0.5 to each side. Both rows share a divisor equal to the
larger absolute row sum (at least one). A sole center/mono channel maps to both
outputs at unity. This is a fixed engineering matrix, not loudness normalization
or a surround-preserving policy. Invalid/nonfinite PCM or unsupported masks fail.

Absolute video boundaries are rounded once to the 48-kHz clock and snapped to
the AAC 1024-sample grid. Long gaps split on the absolute six-second grid; the
final endpoint retains its checked presentation duration. Encoder epochs have
bounded preroll/lookahead; independently rounded EXTINF values do not accumulate
into seek positions. Init edits account for priming once, and final sample/edit
timing trims padding. Consumers must honor presentation timing.

MP4 edits map presentation time to source media time; a leading empty edit is
silence. Matroska rebases video/text and decoded audio to one checked presentation
origin. CodecDelay, SeekPreRoll and DiscardPadding must not be applied twice.
Short audio is padded to the video end; audio beyond the video end is omitted.

## Bounded indexing and seeks

Source reads use host AVIO callbacks, not paths, network protocols or a spawned
encoder process. Seek windows include decoder/resampler history and align
resampling phase with presentation zero. Fixed/coarse Matroska clocks use native
packet/sample coordinates and verified continuity, including DTS lace members,
rather than trusting rounded per-packet timestamps alone.

Sparse restart anchors bound retained index memory: at most 65,536 anchors.
Codec-specific reconstruction preserves Vorbis overlap and TrueHD major-sync
restart requirements. Decoding opens near the requested window; a failed window
poisons the owner rather than silently decoding from file start.

Important current limits include a 1-TiB source, seven-day presentation, 256 MiB
of requested bytes and 1,048,576 callbacks per decoder operation, a ten-second
output window, twenty-second input span and 1-MiB compressed packet. Inventory
and selected-copy preparation have their own budgets. Short-read retries count
toward work limits. Startup can remain linear in selected packet count; these
bounds do not establish whole-film performance or arbitrary large-file admission.

## Storage and failure

Generated fragments are sealed atomically into host-owned storage, then served
immutably. A quota is a hard resource limit, not an eviction cache: exceeding it
fails rather than discarding already published VOD resources. Hosts choose an
appropriate finite budget and remove only the current cast's files.

Accepted conversion clears once after workers/readers drain, including startup
failure. Automatic copy/progressive routes leave offered stores unused; successful
cast create still transfers their contexts for release at destroy. Offline checks
borrow callbacks instead. See the [public ownership contract](public-api.md).

Source, cache, codec, cancellation, generation deadline and resource failures
remain explicit. After preparation, the supervisor also reports terminal lazy
generation failures independently from receiver network failure.

See [build options](building.md), [codec provenance](dependencies.md),
[compatibility](receiver-validation.md) and [surround research](roadmap.md).
