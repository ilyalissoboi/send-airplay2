# D94 Screenbox HDR-corrected package update

2026-10-17 (Asia/Tokyo). The user authorizes updating Screenbox's opt-in pin after
native publication-record PR #77 merged as `31b4474`; all 17 checks passed at its
final head `d0c4028`. Published ci.294 is already approved and publicly verified;
this slice does not republish it.

The fork starts `codex/airplay-hdr-ci294` from merged main `b52c58e` (PR #13).
`AirPlay.Package.props` now selects `SendAirPlay2.StereoAac 0.5.0-ci.294`, tag
`nuget-stereo-aac-v0.5.0-ci.294`, SHA-256
`a06216e954e8ef1e2452abe8472e95fbbc04c036e5e1c017e4a92642b20c76b8`.
The cache-integrity fixture uses the matching filename. Implementation head
`66660bf7` produces the local opt-in MSIX; the package-evidence-only follow-up
is `e2a0c19c` in [fork PR #14](https://github.com/ilyalissoboi/Screenbox/pull/14).
Inspect actual-head CI separately from these local checks. The x64-only guard,
ordinary codec-free ci.207 pin, host code/UI, permission default, 512-MiB quota
and audio policy are unchanged. No third-party source or dependency is added.
The package's six dynamic runtime DLLs and complete FFmpeg source/notice/rebuild
payload remain subject to the existing redistribution checks.

## Automated validation and receiver gate

Required VS 2026 MSBuild restores/builds both Core profiles. Existing pure logic
suites pass 113/113 ordinary and 127/127 opt-in, without XAML instantiation. New
public download and cached ci.294 verification pass, as do the existing cached
hash/corruption-preservation, downloader architecture and three build-profile
refusal controls. Both actual MSIX profiles build: the ordinary layout matches
two pinned native files without codecs, and the opt-in layout matches all 40
native/source/notice files. Development 1.0.0.11 is registered in place from the
inspected opt-in MSIX, with only its layout manifest version adjusted; the source
manifest and app identity/data are preserved. Existing translation, legacy
migration and third-party AOT warnings
remain separate from failures.

The bounded manual follow-up will identify the installed AppContainer and six
loaded published DLL hashes before observing HDR/DTS startup, audio/video,
pause/resume, seeks, 90-second continued playback and Stop/local paused handoff
on available receivers. It may use the previously user-authorized input without
committing its identity, path or contents. This is separate from D91's completed
original short controls and D92's private local-DLL correction checks; neither
is presented as a new published-ci.294 observation. No Computer Use is used for
visual confirmation. Written manual steps are provided in chat.

The user reports both receivers available and the installed app ready. Its
AppContainer, inspected MSIX executable and all six loaded published ci.294 DLL
hashes are verified, with zero owned cache directories before playback. Written
90-second HDR/DTS startup, audio/video, pause/resume, forward/backward seeks and
Stop/local-paused observations remain pending for both receivers. The user's
later Apple TV testing suggests incorrect HDR signaling caused the prior symptom,
with no recurrence after the correction; earlier attempts remain in D92's history.
No whole-film, HLG receiver, speaker or discrete converted-surround claim is added.

## CI cleanup-test follow-up

The initial opt-in CI job at `e2a0c19c` builds successfully but fails both boolean
cases of the cleanup-admission test near its five-second release deadline.
The gate's own timed wait can expire while a busy runner delays the assertion,
allowing admission to complete before it is checked. The correction holds the
gate until explicit test release, drains in `finally`, and retains a separate
30-second completion watchdog. Existing ordering, supersession and logger-failure
coverage remains. Both 113/127 logic suites pass again locally. No production
host/native code or installed package changes; the receiver check continues on
the inspected `66660bf7` MSIX. On corrected head `fbb01d20`, opt-in logic, MSIX
build and distribution-verification steps pass in CI; the overall run is still
finishing at this checkpoint. Inspect actual final-head CI before review.
