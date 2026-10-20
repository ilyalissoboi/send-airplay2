# SendAirPlay2.StereoAac 0.6.0-ci.330 (x64 prerelease)

Source/tag target: `746c520e17d812102a14ba4edae47a81239c4c81` (merged main).
Tag: `nuget-stereo-aac-v0.6.0-ci.330`.
Build: [main workflow 39399818931](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39399818931).

This experimental API 6 package adds the explicit finite AppleAudioV1 policy to
the existing video-casting runtime. Validated stereo AAC/M4A and CBR/VBR MP3 play
as their original progressive bytes. Covered native FLAC/PCM WAVE, FLAC/PCM in
MP4/MOV or canonical indexed Matroska, and single-stream Ogg Opus/Vorbis can
convert to stereo AAC HLS with explicit permission. Checked presentation edits,
packet clocks, source identity and finite bounds determine admission; a codec
name or filename alone does not promise support.

Set `CastDelivery.AutomaticAudioHls` with
`AudioDeliveryProfile.AppleAudioV1` for audio-only media. The historical delivery
name includes progressive results. The separate AppleHlsV1 profile retains
video copy/remux and covered video-audio conversion behavior. Defaults remain
progressive delivery and the video automatic profile; no receiver-discovery or
failed-playback heuristic changes profiles/tracks. The current binding requires
an API 6 native runtime from the same package. Older published packages are not
replaced in place.

Conversion requires `AllowStereo`, a dedicated `AacFragmentStore` and positive
finite `CacheQuotaBytes`; `PreserveOriginal` refuses required conversion.
Progressive AAC/MP3 requires no encoder, cache or quota and works with either
permission. UWP hosts supply brokered media and credential callbacks. This
initial audio-only profile refuses video tracks, raw ADTS, protected/unvalidated
inputs and audio-only DTS/AC-3/E-AC-3/TrueHD; the video policy stays separate.
Read [the exact profile contract](https://github.com/ilyalissoboi/send-airplay2/blob/746c520e17d812102a14ba4edae47a81239c4c81/docs/automatic-audio-only.md).

Reference `SendAirPlay2.StereoAac` instead of `SendAirPlay2`; direct and transitive
mixed profiles are rejected. This opt-in package supplies only `win-x64` and
refuses unsupported architectures. New API 6 packages use the `0.6.0` base;
published codec-free ci.207 and opt-in ci.294 assets remain unchanged. No
Screenbox pin or installation changes with this preparation.

Six runtime DLLs remain separate, with sources, overlay/patches, configuration,
notices and rebuild inputs for the already adopted FFmpeg 9.0.2 port-6
LGPL-2.1-or-later profile. Preserve that payload when redistributing and permit
runtime replacement/relinking. Core/binding remain Apache-2.0. Private runtime
validation exports are disabled in the distribution. This is not nuget.org or
Store certification.

Package: 21,355,553 bytes, SHA-256
`9567655c5c4f0183fddeaea9f497d869d28b79036d599cbf5d87f241e74eb3ad`.
Independent clean-source, six-DLL, pinned-source/license and fresh API-6 consumer
checks pass, including exact managed/native copy hashes and RID/direct/transitive
profile refusals. All 17 main checks pass,
including the independent CI consumer and UWP package-profile controls. Release
assets are the unchanged package and matching SHA-256 sidecar.

Development-runtime [native audio-only checks](https://github.com/ilyalissoboi/send-airplay2/blob/746c520e17d812102a14ba4edae47a81239c4c81/docs/automatic-audio-only-native-gate.md)
and [focused packaged checks](https://github.com/ilyalissoboi/send-airplay2/blob/746c520e17d812102a14ba4edae47a81239c4c81/docs/automatic-audio-only-uwp-gate.md)
passed on the recorded Mac/macOS 26.7.1 and Apple TV/tvOS 26.6 from Windows x64.
Those observations belong to their recorded runtime hashes; this CI archive has
no new receiver or Screenbox queue observation. Speaker/RAOP transport, discrete
converted surround, additional hardware, gapless/sample-exact playback and
sustained long-input/cache scaling remain outside that evidence. No unchanged
cancellation or full codec listening matrix is repeated for preparation.
