# SendAirPlay2.StereoAac 0.5.0-ci.294 (x64 prerelease)

Prepared for review; publication requires explicit approval.

Source/tag target: `72e73ea914a7859f5cb900b5fb129d8e92d2d15b` (merged main).
Proposed tag: `nuget-stereo-aac-v0.5.0-ci.294`.

This corrects HEVC HDR HLS signaling. Bounded SPS VUI parsing identifies PQ/HLG
and adds `VIDEO-RANGE` to the master, including videos without subtitles and
automatic stereo AAC composition. The missing PQ declaration caused the recorded
Mac grey-screen startup failure; adding that declaration alone restored playback.
Video payload is copied, with no tone mapping or video transcoding.

This separate experimental API-5 package contains the x64 Windows UWP runtime
for video casting under the finite `AppleHlsV1` automatic-audio profile. Eligible
AAC-LC/AC-3/E-AC-3 is copied; the covered DTS-core, FLAC, Opus, Vorbis, PCM and
TrueHD subset can convert to 48-kHz stereo AAC. Audio policy, permission defaults,
cache limits, decoder coverage and API version are unchanged from ci.282.
This does not promise support for every stream with those codec names.

Reference `SendAirPlay2.StereoAac` instead of `SendAirPlay2`; direct and transitive
mixed profiles are rejected. This package supplies only `win-x64`, with clear
refusal for unsupported architectures. Published codec-free ci.207 is unchanged.
Screenbox's current opt-in pin stays ci.282 until a separate reviewed host update.

The host explicitly selects `CastDelivery.AutomaticAudioHls`, sets its conversion
permission and finite `CacheQuotaBytes`, and supplies a fresh `AacFragmentStore`
when conversion is allowed. UWP hosts implement brokered media/credential
callbacks. Delivery still defaults to progressive. `PreserveOriginal` refuses a
route requiring conversion; `AllowStereo` permits loss of surround/lossless audio.
No audio-only speaker transport or discrete converted 5.1/7.1 output is added.

All six runtime DLLs remain separate, with exact source/patches/configuration,
notices and rebuild inputs for the recorded FFmpeg 9.0.2 LGPL-2.1-or-later profile.
Preserve that payload when redistributing and allow runtime replacement/relinking.
Core/binding remain Apache-2.0. Private runtime validation exports are disabled.

Produced by [main CI run 38993860187](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38993860187), with all 17 checks passed.
Package: 21,314,548 bytes, SHA-256
`a06216e954e8ef1e2452abe8472e95fbbc04c036e5e1c017e4a92642b20c76b8`.
The unchanged package and matching SHA-256 sidecar are the proposed assets.
Clean source stamps, six runtime hashes, UWP distribution/source/profile controls
and fresh API-5/copy/RID/direct/transitive-conflict consumer checks pass.
See [candidate preparation](../hdr-corrected-audio-candidate.md).

D92 records Mac controls and bounded Apple TV retries on private development
builds from Windows x64, with macOS 26.7.1/tvOS 26.6 most recently reported.
An earlier Screenbox failure and packaged UWP premature `MediaEnd` remain
unexplained; successful visible-window retries do not establish a lifecycle cause
or a general reliability fix. These candidate bytes have no new receiver or
Screenbox queue observation. Whole-film playback, HLG receiver behavior,
perceptual HDR calibration, other receivers/hosts, speakers and discrete
converted surround remain outside the recorded evidence.
