# SendAirPlay2.StereoAac 0.5.0-ci.282 (x64 prerelease)

Source/tag target:
`e6acba1f34b77ddd2a370a65209748f413bf2c8a` (merged main, PR #70).
Tag: `nuget-stereo-aac-v0.5.0-ci.282`.

This separate experimental package supplies the x64 Windows UWP runtime for
video casting with automatic audio handling under API 5's finite `AppleHlsV1`
profile. Eligible AAC-LC/AC-3/E-AC-3 is copied; the covered DTS-core, FLAC, Opus,
Vorbis, PCM and TrueHD subset can convert to 48-kHz stereo AAC. Video is copied.
This does not promise support for every stream with those codec names.

Reference `SendAirPlay2.StereoAac` instead of `SendAirPlay2`. Their managed and
native filenames overlap; mixed direct or transitive references are rejected.
The published codec-free package and Screenbox pin remain `0.3.0-ci.207`.
This prerelease contains only `win-x64`; x86/ARM64 consumers are rejected.

The host explicitly selects `CastDelivery.AutomaticAudioHls` and a conversion
permission, supplies a finite `CacheQuotaBytes` and a fresh `AacFragmentStore`
when conversion is allowed, and implements brokered media/credential callbacks
in UWP. Delivery still defaults to progressive. `PreserveOriginal` refuses a
route requiring conversion; `AllowStereo` permits loss of surround/lossless audio.
No unrestricted native-capability detection, video transcoding, audio-only
speaker transport or discrete converted 5.1/7.1 output is added.

The package includes all six native DLLs, matching configuration/hash records,
third-party notices, the pinned FFmpeg 9.0.2 source archive, exact overlay/patches,
manifest/triplet and rebuild instructions. Core/binding licensing remains
Apache-2.0; the dynamic FFmpeg runtime uses the recorded LGPL-2.1-or-later profile.
Preserve the source/notices/rebuild payload when redistributing. Runtime DLLs
remain separate for replacement/relinking. Private validation exports are disabled.

The candidate is produced by [main CI run 38836333166](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38836333166).
Candidate: 21,306,805 bytes, SHA-256
`5280f0868721476b2cae9018bd8197c3de3c9f356408781209ec57fd4b9ba4ff`.
The adjacent package SHA-256 sidecar identifies the same asset. Source attribution,
DLL hashes, UWP static properties, source completeness and local consumer restore/
copy/profile-refusal checks pass. All 17 main CI checks passed at the source above;
see
[the candidate record](../opt-in-audio-release-candidate.md).

Earlier short original controls passed native and packaged-test-host playback on
one MacBook Pro/macOS 26.7.1 and Apple TV/tvOS 26.6 from Windows x64. Those are
the [D88 observations](../automatic-audio-receiver-gate.md), with failures/retries
recorded separately. These candidate bytes have no new receiver or Screenbox
queue observation. Whole-film reliability, other hosts/receivers, speakers,
converted surround and Store certification remain outside that evidence.
