# D111: API 6 prerelease publication

2026-10-20 (Asia/Tokyo). After [PR #95](https://github.com/ilyalissoboi/send-airplay2/pull/95)
merged as `a7f82024bc54d72ac4fbc387687aed1bbb43e80e`, the user explicitly approved
publishing the exact reviewed candidate. All 17 checks passed at that PR's final
head `c2d1526ec20974dc88446fc8876e4ab39181f5d3`.

## Published assets and public verification

[SendAirPlay2.StereoAac 0.6.0-ci.330](https://github.com/ilyalissoboi/send-airplay2/releases/tag/nuget-stereo-aac-v0.6.0-ci.330)
is a GitHub prerelease with exactly two assets: the original x64 UWP package and
its unchanged SHA-256 sidecar. The release body matches the
[reviewed notes](releases/sendairplay2-stereo-aac-0.6.0-ci.330.md).
The tag resolves to the original clean-main build source
`746c520e17d812102a14ba4edae47a81239c4c81`, rather than the later documentation
merge. [Main run 39399818931](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39399818931)
passed all 17 checks. The [D110 preparation audits](audio-only-package-candidate.md)
remain the source, distribution, license and fresh-consumer evidence.

| Public asset | Bytes | SHA-256 |
| --- | ---: | --- |
| [Package](https://github.com/ilyalissoboi/send-airplay2/releases/download/nuget-stereo-aac-v0.6.0-ci.330/SendAirPlay2.StereoAac.0.6.0-ci.330.nupkg) | 21,355,553 | `9567655c5c4f0183fddeaea9f497d869d28b79036d599cbf5d87f241e74eb3ad` |
| [Checksum sidecar](https://github.com/ilyalissoboi/send-airplay2/releases/download/nuget-stereo-aac-v0.6.0-ci.330/SendAirPlay2.StereoAac.0.6.0-ci.330.nupkg.sha256) | 108 | `655a12480c59634e63a38420acdedbc493022c4f2c1079b518609b116e0ed49b` |

Both assets were downloaded afresh through public URLs with `Invoke-WebRequest`,
without credentials or Authorization headers. Their sizes and hashes match the
reviewed retained files and GitHub asset digests. This verifies public access
independently of authenticated release upload/metadata access. The sidecar names
the package and its exact hash. No package was regenerated or modified.
[The sanitized publication record](validation/audio-only-package-publication-windows-2026-10-20.json)
contains the release/asset identifiers, resolved target and public-download results.
The D110 JSON intentionally retains its preparation-time approval/publication state.

## Scope and next work

This publishes the experimental API-6 finite AppleAudioV1 contract. Original
validated stereo AAC/M4A and MP3 use progressive delivery; covered FLAC/PCM/Ogg
inputs can convert to stereo AAC HLS with permission, dedicated host storage and
a finite quota. Video retains its separate AppleHlsV1 policy. Read the
[exact admission contract](automatic-audio-only.md) before using the package.

Publication and its approval are complete. Do not repeat publication or replace
assets. The same-run codec-free comparison package was not published. Existing
ci.207 and ci.294 assets remain unchanged; this is neither nuget.org publication
nor a Store release. Screenbox's ordinary ci.207 and opt-in ci.294 pins and its
installed packages remain unchanged.

Next is a separate Screenbox slice: inspect its current repository instructions,
pin the publicly verified API-6 opt-in package/hash, select the explicit audio
or video profile per queue item, and verify the changed routing and mixed queues.
Preserve ordinary codec-free builds and the user's stereo-conversion preference.
Reuse unchanged lifecycle/codec coverage; no full cancellation or codec listening
matrix is required merely for this publication. Any new app/receiver observations
must identify the actual package and loaded binaries.

D107/D108 observations retain their original development-runtime scope. This
release audit adds no receiver interoperability, Screenbox queue, gapless,
sample-exact or sustained long-input result. Speaker/RAOP transport, discrete
converted surround, Chromecast hardware, user-deferred remote-Stop classification
and long-input/cache-quota evaluation remain deferred; quota is unchanged.
