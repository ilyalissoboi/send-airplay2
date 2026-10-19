# D106: explicit automatic audio-only delivery (API 6)

The user authorizes this implementation after native [PR #90](https://github.com/ilyalissoboi/send-airplay2/pull/90)
merged as `06e094c`. All 17 checks passed at final head `fec9448` in
[run 39209440573](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39209440573).
This connects the previously private audio-only builders to public preparation,
casting, decision reporting, the development CLI and C# binding. Receiver gates
for this new automatic route follow separately; no Screenbox or publication work
is part of this slice.

## Explicit profile and finite admission

Set `SAP2_DELIVERY_AUTOMATIC_AUDIO_HLS` and
`SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1`, or use the same profile with
`sap2_media_check_audio`. Despite the historical delivery constant's HLS suffix,
this profile chooses progressive or HLS resources. The default profile remains
Apple HLS v1 for video. Neither profile is negotiated from receiver model,
discovery, consent or a failed playback attempt.

| Source content | Delivery and bounds |
| --- | --- |
| Progressive MP4/M4A AAC-LC | Original `audio/mp4`, exactly one source track, enabled unprotected stereo, 44.1/48 kHz, the existing exact ASC/ISO sample-entry/packet-clock copy table. Native index and finite presentation timeline must validate. |
| Standalone MP3 | Original `audio/mpeg`, stereo 44.1/48 kHz MPEG layer III, within the existing complete CBR/VBR framing, metadata, stable-format, count and CRC/trim subset. ID 1. No MP3 AAC fallback or decoder is added. |
| FLAC/PCM in MP4/MOV | Stereo AAC HLS with explicit permission. Existing decoder packing/layout and finite packet timeline, including one positive unit-rate media edit optionally preceded by empty-edit silence. |
| FLAC/PCM in Matroska | Stereo AAC HLS with explicit permission. Canonical zero-based contiguous indexed audible packets, checked CodecDelay/DiscardPadding; no host/container-duration estimate. |
| Native FLAC and RIFF/RF64 PCM WAVE | Stereo AAC HLS with explicit permission. Existing header/layout/sample-count and decoded-end/coverage subset. ID 1. |
| Single-stream Ogg Opus/Vorbis | Stereo AAC HLS with explicit permission. Existing full-page CRC/granule/end validation, at most 64 MiB; chaining/grouping refuse. ID 1. |

All video tracks, including disabled ones, refuse in this profile. Direct original
AAC cannot filter additional audio/metadata tracks; multi-track containers refuse
that route. Conversion retains original identity selection before codec filtering,
including ambiguity/invalid-ID refusal. Covered containerized FLAC/PCM may select
one audio track; new Matroska codec/timeline families are not admitted implicitly.
AC-3/E-AC-3/DTS/TrueHD audio-only, containerized Opus/Vorbis, raw ADTS, unknown,
protected and unvalidated inputs refuse. Mono/low-rate MP3 and non-stereo AAC are
outside this first progressive table. This is a library policy, not a statement
that Apple TV cannot decode those formats.

Content signatures and parsed metadata determine the route; filenames/extensions
and caller MIME do not. Original-source preparation samples host size once and
shares its immutable value. Source size is at most 1 TiB. The signature/direct-MP4
reader has a separate 256-MiB requested-byte/1,048,576-callback budget, charging
short-read retries with callbacks at most 64 KiB. The existing inventory, MP3,
standalone/Ogg and decoder stages retain their documented independent budgets.
The shared caller deadline/cancellation spans every stage. Finite presentation is
bounded by seven days; complete-end conversion needs its existing one-sample
headroom. These bounds do not establish large-file performance.

## Decisions, ownership and failures

API 6 adds profile value 2, progressive action/reason value 4, and MP3 family value
10. Existing enum values, exports, structure sizes/prefixes and defaults are
unchanged. The C# development binding now requires API 6 and exposes
`AudioDeliveryProfile.AppleAudioV1`, `AudioDeliveryAction.Progressive`,
`AudioDeliveryReason.ProfileProgressive` and `AudioFamily.Mp3`.

Successful decisions report the actual profile, selected original ID, action,
reason, input/output families and channels. They remain after later Stop/end or
network failure as before. Failed offline checks clear stale output. A successful
preparation is not full payload decoding or receiver compatibility proof.

Progressive delivery works in codec-free builds with either conversion permission
and no store/quota. It creates one fixed neutral-named resource with the original
MIME and bytes, no encoder or HLS authoring estimates. The common controller uses
that entry resource and drains HTTP/resources at Stop, failure or natural session
end. Offered stores receive no seal/read/clear calls; accepted native cast create
still transfers source/store contexts for exactly-once release at destroy.

Conversion first checks permission, then backend availability, then complete
per-cast storage/positive quota. Preserve-original refuses conversion. The existing
audio-only owner validates source identity/timeline/decoded tail, authors one AAC
variant lazily and supervises terminal generation/cache failures. Accepted stores
clear once after workers/readers drain, including startup failure. No failure
chooses another track/representation or retries receiver playback. An offline
check borrows source/store and discards the prepared resources before returning.

## Development commands

```sh
airplay2-cli cast --check-audio --audio-only --file tones.m4a --preserve-audio
airplay2-cli cast --check-audio --audio-only --file tones.mp3
airplay2-cli cast --check-audio --audio-only --file tones.flac --cache-directory cache --cache-quota-bytes 16777216
airplay2-cli cast --automatic-audio --audio-only --address 192.0.2.10 --profile living-room --file tones.ogg --cache-directory cache --cache-quota-bytes 16777216
```

`--audio-only` requires automatic or offline-check mode. Discovery, pairing,
legacy and forced-stereo routes do not accept it. `sap2_media_probe` remains the
existing MP4/Matroska metadata interface; it does not gain standalone formats here.

## Validation and remaining gates

Owned tests cover exact progressive bytes/MIME with both permissions, immutable
size, literal public decisions, original IDs, preservation/backend/cache refusal,
ambiguity, protection, video refusal, malformed/truncated or out-of-subset input,
cancellation/deadline, stale-output clearing and terminal cache failure. Conversion
tests independently decode generated AAC and compare literal endpoint counts for
FLAC/PCM/Opus/Vorbis, edited MP4 and trimmed Matroska. Scripted controller checks
cover progressive entry selection and Stop/natural-end drain alongside existing
copied-video cases. Managed checks cover new-profile mapping and context ownership.
No new fixtures, third-party implementations, dependencies or codec settings are
added; existing Apache-2.0 fixtures/provenance and FFmpeg 9.0.2 port-6 profile apply.

Implementation `46321d0` passes Windows Release static **43/43** (47.66 s),
shared **44/44** (51.03 s), enabled shared **45/45** (110.25 s, including managed
and private-runtime checks), and codec **17/17** (83.48 s). Strict Clang 22.1.3
syntax checks pass for all eight touched C++ translation units with the backend
enabled. All touched C/C++ files pass formatting and whitespace checks. Ordinary
and enabled x64 UWP libraries compile; their two/six-DLL AppContainer, import and
app-runtime checks pass. No package is installed or run. The enabled desktop
configuration now uses the existing verified port-6 cache instead of its old
port-4 prefix; no dependency/profile change is made. The
[sanitized validation record](validation/automatic-audio-only-windows-2026-10-19.json)
contains source/fixture/binary hashes and exact scope. CI at the actual PR head
must be inspected separately. These are offline results, not new interoperability.

The initial PR head `061825f` failed x86 UWP CI with MSVC C4244 in the existing
FLAC channel-layout lookup, newly included in ordinary builds. Follow-up
`0245f49` explicitly converts the index after STREAMINFO's three-bit field
guarantees a 1..8 channel count. Behavior is preserved. Strict Clang for this
ninth touched C++ translation unit passes on x64 and i686 Windows targets;
full static/shared suites pass again **43/43** (46.67 s) and **44/44** (50.98 s).
The validation record retains the initial runtime/binary scope and separately
records this portability fix. Inspect final-head CI before merge.

The next gates are observed native and packaged audio-only generated-HLS playback,
controls, seeks/end/Stop/cancellation and cleanup with exact binaries; then
Screenbox per-item routing and mixed queues. Historical direct AAC/MP3 receiver
gates do not validate the new automatic profile or converted audio-only HLS.
Speaker/RAOP, discrete converted surround and Chromecast hardware deferrals,
user-deferred remote Stop classification and separate long-input/cache-quota
evaluation remain. No package, release, pin or quota changes are authorized here.
