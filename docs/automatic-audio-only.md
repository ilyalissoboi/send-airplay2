# Audio-only automatic delivery

API 6 exposes an explicit finite audio-only profile. Select
`SAP2_DELIVERY_AUTOMATIC_AUDIO_HLS` and `SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1`, or
the corresponding `CastDelivery.AutomaticAudioHls`/`AppleAudioV1` C# values.
The default automatic profile remains the video profile. Receiver discovery,
filenames and failed playback do not choose the profile.

## Admission table

| Input | Route and requirements |
| --- | --- |
| AAC-LC MP4/M4A | Original `audio/mp4`; exactly one source track, enabled/unprotected stereo 44.1/48 kHz with supported ASC, sample entry and finite clock |
| Standalone MP3 | Original `audio/mpeg`; stereo 44.1/48-kHz MPEG layer III within complete CBR/VBR framing, stable-format, count and metadata/CRC/trim bounds |
| Native FLAC, RIFF/RF64 PCM WAVE | Stereo AAC HLS; validated header/layout/sample count and decoded end/coverage |
| FLAC/PCM MP4/MOV | Stereo AAC HLS; supported decoder packing/layout and finite packet timeline, optionally one positive unit-rate media edit preceded by an empty edit |
| FLAC/PCM Matroska | Stereo AAC HLS; canonical zero-based contiguous indexed audible packets and checked CodecDelay/DiscardPadding |
| Single-stream Ogg Opus/Vorbis | Stereo AAC HLS; complete page CRC/granule/end validation within 64-MiB scan bound; no chaining/grouping |

All video tracks, including disabled tracks, refuse. Original AAC delivery
cannot filter additional tracks, so multi-track containers refuse that route.
Containerized conversion resolves original enabled identity before filtering
codecs, preserving selection-required/invalid-ID behavior. Standalone inputs
use ID 1.

Audio-only DTS/AC-3/E-AC-3/TrueHD, containerized Opus/Vorbis, raw ADTS, unknown,
protected or otherwise unvalidated inputs are outside this profile. Mono/low-rate
MP3 and non-stereo AAC are outside its progressive table. These are library
admission decisions, not claims that a receiver cannot decode those codecs.

## Permission and ownership

Progressive AAC/MP3 works in codec-free builds, with either permission and no
encoder/store/quota. Content signatures and parsed metadata determine the route;
caller MIME and extension do not override it. Original bytes are served under a
neutral resource name with their selected audio MIME type.

Conversion checks permission, then backend availability, then complete per-cast
storage and positive quota. Preserve-original refuses conversion. Accepted
conversion validates identity/timeline/decoded tail and lazily authors finite
stereo AAC HLS. A failure never falls back to another track or representation.
Source/store borrowing, release and cleanup follow the [C API](public-api.md).

`sap2_media_check_audio` reports profile, action, reason, original ID and
input/output families/channels without a receiver. It discards the presentation
before returning. Failed checks clear stale decision output. Success does not
prove every compressed payload, sustained throughput or receiver compatibility.

## Bounds and timing

Source size is sampled once and limited to 1 TiB. Signature/direct-MP4 preparation
has a 256-MiB requested-byte and 1,048,576-callback budget, with at most 64-KiB
callbacks. Inventory, MP3, standalone/Ogg and decoder stages have independent
bounds; caller deadline/cancellation spans all stages.

Finite presentation is at most seven days; complete-end conversion also needs
one output sample of headroom. MP4 edits require checked source coverage and
unit rate; complex or uncovered edits refuse. Matroska endpoints derive from
audible packet coverage, not container-duration estimates. Decoded windows must
cover the requested timeline. See [audio conversion internals](audio-transcoding.md).

## CLI examples

```sh
airplay2-cli cast --check-audio --audio-only --file tones.m4a --preserve-audio
airplay2-cli cast --check-audio --audio-only --file tones.mp3
airplay2-cli cast --check-audio --audio-only --file tones.flac --cache-directory cache --cache-quota-bytes 16777216
airplay2-cli cast --automatic-audio --audio-only --address 192.0.2.10 --profile living-room --file tones.ogg --cache-directory cache --cache-quota-bytes 16777216
```

`--audio-only` requires automatic/check mode. Legacy progressive audio remains
available with an explicit MIME; native `sap2_media_probe` remains an MP4/Matroska
inventory API and does not expand to standalone files.

This is URL/HLS audio for the recorded Mac/Apple TV receiver path. It is not
audio-only-speaker/RAOP transport, multiroom, gapless playback or discrete
converted surround. See [compatibility](receiver-validation.md) and
[unfinished work](roadmap.md).
