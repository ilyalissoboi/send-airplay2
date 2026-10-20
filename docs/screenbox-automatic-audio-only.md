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

## PR #17 review follow-up: opening-item metadata

[The current-item race](https://github.com/ilyalissoboi/Screenbox/pull/17#discussion_r4324921353)
is fixed in fork `23f708e8`. A current item can expose an empty or partial VLC
track array while Cast is already available. Current and queued opt-in items
now share bounded, cancellable metadata resolution using an independently
owned brokered descriptor as needed. Complete, nonempty inventories are reused;
incomplete/empty parser output refuses. Stop, a player swap or an item change
wins before the request pauses the local player or starts a native cast.
Current VLC display size remains preferred; parsed display metadata provides
an opening-video fallback. Descriptor/grant cleanup remains in `finally`.

Regression logic suites pass 142/176 ordinary/opt-in, including thirteen new
delayed-metadata/refusal/cancellation cases. The original manual gate below
still belongs to its 1.0.0.15 executable/module hashes. This review follow-up
does not install a new app or add receiver observations; packaged/CI checks are
recorded separately from the historical playback gate. Both actual Release
MSIX builds and pinned-payload inspections pass (2 ordinary DLLs, 40 opt-in
native/source/notice files). Read [the separate review record](validation/screenbox-api6-review-windows-2026-10-20.json).
The original receiver JSON remains unchanged. Inspect actual final PR-head CI
separately; no new receiver observation is inferred from the review builds.

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
only the extracted manifest's version. Four actual package identity, AppContainer,
executable and six loaded-DLL captures match the inspected layout and ci.330.
Read [the sanitized evidence](validation/screenbox-api6-audio-only-windows-2026-10-20.json)
for implementation, MSIX/executable/module and owned-fixture hashes. These local
gates do not prove receiver behavior; PR-head CI must also be inspected separately.

## Focused manual gate: passed

Written user observations cover development Screenbox 1.0.0.15 on Mac/macOS
26.7.1 and Apple TV/tvOS 26.6, with versions unchanged. No Computer Use visual
confirmation is used.

| Check | Observed result |
| --- | --- |
| MP3, conversion off, both receivers | Start near eight seconds, audible RIGHT/BOTH markers, pause/resume, seek32/seek8, Stop, local paused and app responsive: passed. |
| Native FLAC, conversion off, Mac | Format/conversion refusal, no receiver playback and local paused: passed. |
| Native FLAC, conversion on, both receivers | Start/markers, pause/resume, both seeks, Stop, local paused and app responsive: passed. |
| MP3 → converted PCM MOV audio → video → AAC, both receivers | Natural advance with correct sound/video and final local pause: passed. The user could seek near each end and let it finish naturally. |
| Rapid Next and transition Stop, both receivers | Next twice reached the third item's video/sound. Next then immediate Stop closed playback without delayed restart, local paused and app responsive: passed. |

Fixtures are copies of existing hash-verified repository-owned tones/video; no
private media is used. Four actual runtime captures before/during/after the gate
verify the same package identity, AppContainer, executable and all six pinned
ci.330 DLL hashes. The user restored conversion OFF and left the app open with
local playback paused. Final owned cache directories/files are both zero;
inspection performed no cleanup mutation. No further manual check is pending.

Earlier consent-cancellation and full codec-matrix coverage were reused; no
unchanged listening matrix was repeated. D107/D108 observations retain their
original runtime scopes. These new observations belong to the recorded local
1.0.0.15 executable/module hashes; they do not verify separately built CI artifacts,
all codec/container variants, gapless playback or long-input scaling.

Review [fork PR #17](https://github.com/ilyalissoboi/Screenbox/pull/17) and
[evidence PR #97](https://github.com/ilyalissoboi/send-airplay2/pull/97), inspecting
their actual final heads and CI separately. CI captured at the manual gate is
explicitly a pre-final-documentation snapshot in the record. Ask before merging.
No Screenbox release is created by this gate. Speaker/RAOP, discrete converted
surround, Chromecast hardware, user-deferred remote-Stop classification and
long-input/cache-quota evaluation remain deferred.
