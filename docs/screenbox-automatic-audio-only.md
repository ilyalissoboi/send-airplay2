# D112: Screenbox automatic audio-only policy

2026-10-20. Native publication evidence [PR #96](https://github.com/ilyalissoboi/send-airplay2/pull/96)
merged as `b57981b8e034f9bc67d3515fca675b65bebe59d2`; all 17 checks passed at
final head `98b4dc8`. The user authorized Screenbox integration and confirmed
both receivers available with macOS 26.7.1/tvOS 26.6 unchanged.

[Screenbox PR #17](https://github.com/ilyalissoboi/Screenbox/pull/17), implementation
`59d83910`, updates the opt-in x64 profile to public API-6
`SendAirPlay2.StereoAac 0.6.0-ci.330`, SHA-256
`9567655c5c4f0183fddeaea9f497d869d28b79036d599cbf5d87f241e74eb3ad`.
The [approved publication/public downloads](audio-only-package-publication.md)
are prerequisites already completed; no asset is republished or replaced here.
Ordinary codec-free ci.207 remains unchanged.

## Implemented boundary

Opt-in local files use parsed track presence to choose AppleAudioV1 for audio
without video, or AppleHlsV1 for any video track. Native preparation validates
the content and finite timeline; extensions do not promise codec support and
failed playback does not retry another profile. Permission cannot overwrite
the selected profile. The [finite audio-only contract](automatic-audio-only.md)
covers original progressive AAC/M4A/MP3 and permitted stereo-AAC HLS conversion
of covered FLAC/PCM/Ogg inputs. The ordinary build retains its earlier M4A hint
and video-remux behavior.

Each queued opt-in item requires complete metadata before classifying absence
of video. An independently owned brokered descriptor is parsed when necessary,
disposed in `finally` and never opened in the paused local player. Stop and
supersession cancel parsing; request tickets prevent a stale native start.
Audio dimensions stay zero; current video preserves VLC display size and queued
video resolves its own sample-aspect/orientation dimensions. Policy and dimensions
are determined independently of the preceding item.

The existing conversion preference defaults off, and the quota stays 512 MiB.
Permission off offers no store. Permission on offers one dedicated empty cache
reservation per cast: progressive delivery leaves it unused and release removes
it; conversion uses the existing fragment/drain/cleanup owner. No new dependency,
codec, native runtime/API behavior, setting layout or transport is introduced.

## Completed local gates

VS 2026 MSBuild builds both logic profiles; tests pass 142/142 ordinary and
163/163 opt-in. The pinned public downloader and cached corruption/architecture/
profile refusal checks pass. Actual Release MSIX builds and distribution checks
pass: two ordinary native files with no codec leakage, and all 40 opt-in native,
source and notice files matching the exact pinned package. Existing translation,
legacy migration and third-party trim warnings remain.

Development 1.0.0.15 is registered in place with identity/data preserved, changing
only the extracted manifest's version. Its actual package identity, AppContainer,
executable and all six loaded DLL hashes match the inspected layout and ci.330.
Read [the sanitized evidence](validation/screenbox-api6-audio-only-windows-2026-10-20.json)
for implementation, MSIX/executable/module and owned-fixture hashes. These local
gates do not prove receiver behavior; PR-head CI must also be inspected separately.

## Focused manual gate: pending

Written instructions target the changed app paths on Mac/macOS 26.7.1 and Apple
TV/tvOS 26.6. No Computer Use visual confirmation is used.

1. MP3 with conversion off: nonzero start, audible markers, pause/resume,
   forward/backward seeks, Stop and paused local handoff on both receivers.
2. Native FLAC with conversion off: a format/conversion refusal, no receiver
   playback and paused local handoff on the Mac.
3. Native FLAC with conversion on: audible markers, controls/seeks, Stop and
   paused local handoff on both receivers.
4. MP3 → converted audio-only PCM MOV → video → AAC queue on both receivers:
   natural advance with correct audio/video, final local pause, rapid Next and
   Stop during transition without delayed restart.

Fixtures are copies of existing hash-verified repository-owned tones/video;
no private media is needed. No new receiver success is claimed until written
observations arrive. Reuse earlier consent-cancellation and full codec matrix
coverage rather than repeating unchanged paths. D107/D108 observations remain
scoped to their original binaries.

Next, finish the focused gate, inspect actual PR-head CI and review/merge the
fork and evidence PRs. No Screenbox release is created by this development gate.
Speaker/RAOP, discrete converted surround, Chromecast hardware, user-deferred
remote-Stop classification and long-input/cache-quota evaluation remain deferred.
