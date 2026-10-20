# D110: clean-main API 6 package candidate

2026-10-20 (Asia/Tokyo). The authorized package preparation continues after
[PR #94](https://github.com/ilyalissoboi/send-airplay2/pull/94) merged as
`746c520e17d812102a14ba4edae47a81239c4c81`. All 17 checks passed at final head
`275ceaa`. [D109](audio-only-package-readiness.md) corrects the shipping contract,
version bases and stale-binding consumer coverage. Its local validation archives
are not used as the publication candidate.

## Selected run and current state

Clean-main [workflow run 39399818931](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39399818931),
run number 330, is a `workflow_dispatch` on the exact merged commit above. It
will produce `SendAirPlay2.StereoAac.0.6.0-ci.330.nupkg` in the separate
`nuget-stereo-aac-x64` artifact. The same-run codec-free package is a comparison
input for the consumer profile-conflict check, not an additional release candidate.

**Candidate preparation passed.** All 17 checks in that actual main run pass,
including the independent CI consumer. The UWP package job's profile controls
also pass. Independent artifact and fresh-consumer audits below identify the
unchanged candidate bytes; its checksum and exact proposed release notes are
prepared. No release/tag/assets, nuget.org or Screenbox pin changes are made.
The [sanitized validation record](validation/audio-only-package-candidate-windows-2026-10-20.json)
keeps main CI, local artifact/consumer checks and receiver scope separate.

## Completed artifact checks

Opt-in archive: `SendAirPlay2.StereoAac.0.6.0-ci.330.nupkg`, 21,355,553 bytes.
SHA-256: `9567655c5c4f0183fddeaea9f497d869d28b79036d599cbf5d87f241e74eb3ad`.
Its matching `.nupkg.sha256` sidecar identifies those original bytes. Clean
binding/native source stamps name the exact selected main commit; all six
recorded runtime hashes and the four codec-profile hashes match. UWP distribution
properties/private-export refusal, pinned FFmpeg archive, source/configuration,
notices and rebuild inputs pass independent inspection. All 22 overlay files
match installed SPDX checksums exactly and repository text after line-ending
normalization (20 differ only in CRLF/LF checkout bytes).

A fresh isolated NuGet cache restores that exact archive. The consumer compiles
the actual API 6 profile/action/reason/MP3/offline-check members, copies the exact
managed binding and six native DLLs, and refuses unsupported RID plus direct/
transitive mixed profiles. No consumer executable or UWP DLL is run. The same-run
codec-free comparison has clean binding/three-architecture source stamps, six
matching DLL hashes and passing x64/x86/ARM64 distribution inspections; it has
no codec/source/profile assets and is not a publication candidate.

## Scope

API 6 exposes the explicit finite AppleAudioV1 profile: original progressive
stereo AAC/M4A or MP3, or permitted conversion of covered FLAC/PCM/Ogg inputs to
stereo AAC HLS. Video remains on AppleHlsV1; defaults do not infer a profile from
receiver discovery or failed playback. Conversion requires dedicated host storage,
positive finite quota and stereo permission. Sources/notices/rebuild inputs for
the adopted FFmpeg 9.0.2 port-6 profile accompany the six separate x64 UWP DLLs.

Development-runtime [native audio-only](automatic-audio-only-native-gate.md) and
[focused packaged](automatic-audio-only-uwp-gate.md) gates passed on the recorded
Mac/macOS 26.7.1 and Apple TV/tvOS 26.6. Those observations retain their original
binary scope; static package/consumer checks add no receiver, Screenbox queue,
gapless/sample-exact or long-input playback claim. No repeat cancellation/full
codec listening matrix is needed for this package-only slice.

## Publication boundary

After preparation is reviewable and complete, publication needs separate explicit
approval for the exact package/version/hash and target commit. Use the distinct
`nuget-stereo-aac-v0.6.0-ci.330` tag and unchanged package/checksum assets; do not
overwrite ci.207 or ci.294. Verify a fresh public download before changing a
Screenbox pin. The reviewed package/routing/queue integration follows separately,
with the existing speaker/RAOP, surround, Chromecast, remote-Stop and long-input/
cache-quota deferrals preserved.

The exact [proposed release notes](releases/sendairplay2-stereo-aac-0.6.0-ci.330.md)
are ready for review. The package is retained locally under ignored validation
storage; workflow artifacts expire after 14 days. Preserve the recorded archive
and checksum for publication rather than regenerating bytes under this version.
The preparation branch is `codex/api6-clean-main-candidate`; its PR-head checks
are separate from the selected main run above. Screenbox consumption/routing
follows publication and independent public-download verification.
