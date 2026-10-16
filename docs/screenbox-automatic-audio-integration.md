# D91 Screenbox x64 automatic-audio integration

2026-10-16. The user approved implementing the D90 host proposal after publication.
Native PR #72 is merged as `910b48e`; the published `0.5.0-ci.282` bytes remain
unchanged. Screenbox fork [PR #13](https://github.com/ilyalissoboi/Screenbox/pull/13)
starts from current fork main `ce78235`. Its initial implementation head is
`f621f2675f9e0be50845a33f942d25db64b251c4`; runtime/test follow-up head is
`bbbfbc9db240a51547fa7d3b180cca369f29cb23`. The Cancel UI follow-up is
`a6f1fcc9f81b5c3361b3269197c4e05eb477f17a`; fork documentation head is `912cab6d`.
The current fork instructions were read. The [sanitized local artifact record](validation/screenbox-automatic-audio-host-windows-2026-10-16.json)
contains test counts, actual MSIX/executable hashes and the completed scoped host gate.

## Implemented host choices

- `AirPlayStereoAac=true` selects the published x64-only opt-in identity and a
  compile symbol; ordinary builds remain codec-free `SendAirPlay2 0.3.0-ci.207`.
  Cached/new package hashes are checked. Restore, tests and MSIX use one profile;
  generated and publish output paths are isolated. Invalid architectures/RIDs
  are refused. A separate PR/manual job does not change tag publication; manual
  dispatch defaults to ordinary validation and release creation requires tag push.
- The opt-in app ships all six exact DLLs and matching source archive, patches,
  configuration, SPDX, notices and licenses. It does not substitute VLC codecs.
- Settings exposes an explicit default-off permission for stereo conversion,
  explaining the loss of surround/lossless audio. The host explicitly chooses
  PreserveOriginal or AllowStereo under AutomaticAudioHls/AppleHlsV1; it does not
  select a different audio track or retry another delivery route after failure.
- Permitted casts reserve exclusive app-temporary storage before opening a
  brokered source, with a finite 512-MiB sealed-byte quota. Permission-off casts
  reserve no cache. Binding consumed-context ownership, pre-transfer abandon,
  unused release without Clear, immutable publication, cancellation polling and
  nonrecursive known-file cleanup are implemented. Cleanup failure is logged by
  category/type; broad scavenging and crash recovery remain separate designs.
- New admission waits for retired Stop/Dispose on a worker. Generation checks
  reject superseded requests before/after asynchronous source/cache creation and
  start. Queue changes keep the latest request; Stop invalidates pre-Create work.
  Audio selection/source/codec/cache/resource/deadline failures remain distinct,
  and terminal mapping respects the first end reason. Local paused handoff is
  retained.

The consent-gate preparation exposed a missing initial-start UI action. The
coordinator's cancellation path was unreachable from the flyout until playback
started. The follow-up adds a starting state with Cancel, disables selection and
hides pair/forget/cast while pending, and keeps the busy state until start returns.
Cancel calls the coordinator even before source/cache/native creation. Existing
Stop casting handles active casts and queue transitions. Command eligibility and
CanExecuteChanged coverage pass without XAML instantiation.

These are engineering choices under the approved plan, not additional user
requirements or a change to the published library's policy. Screenbox's adapter
is original host code following the existing callback contract; no third-party
implementation/dependency was imported. Existing source/license provenance and
dynamic LGPL profile are packaged unchanged.

## Evidence and completed recorded host gate

Visual Studio 2026 Core builds pass for both profiles. Pure logic suites pass
113/113 ordinary and 127/127 opt-in. Coverage includes immutable reads,
cancelled seal, exclusive-file collisions, unknown-file preservation, retry of
locked-file cleanup, unused/pre-transfer release, sibling isolation, serial drain,
superseded admission, logger-failure containment and policy/error categories.
Cached-pin/corruption-preservation controls and downloader/MSBuild architecture,
RID and malformed-profile refusals pass. XAML formatting and whitespace checks
pass. These tests instantiate no XAML.

Both x64 MSIX profiles build. Inspection matches the two ordinary native files
and refuses added codec DLLs there; the opt-in MSIX matches all 40 pinned native,
source and notice files. Existing translation, legacy and third-party trimming
warnings remain. Development Screenbox 1.0.0.9 was registered in place from an
extracted real MSIX; app data/pairings were preserved and the source manifest was
restored. Its executable SHA-256 is
`6253c86b73fc5e30e0fcadd30ce6f0493163486140feaa39f2b7d0d5c2530b6f`.
It includes the guard suppressing stale failure notifications if Stop or
supersession wins a concurrent failure and the new startup Cancel action.
Version 1.0.0.8 had passed AppContainer/six loaded-DLL hash checks before replacement;
1.0.0.9 independently passes those checks too. Both updates preceded receiver
observations. No app-owned AirPlay cache directory remained before the tests.

The user reports both receivers available and confirms the setting layout/default
off, then 1.0.0.9 AAC readiness with conversion kept off. Installed AppContainer
and six pinned loaded modules pass. The user confirms Mac pending-consent Cancel
closes the prompt and leaves Screenbox responsive/local paused with no error;
macOS 26.7.1/tvOS 26.6 are unchanged. Accepted AAC retry and AAC copy on both Mac
and Apple TV pass start near 45 seconds, synchronization/channel pattern,
pause/resume, both seeks, Stop closure and local paused handoff. With conversion
off, DTS refuses with an unsupported-format notification on the Mac, no receiver
playback and local paused. Enabling conversion lets the same DTS control pass
all those playback/control/Stop/handoff checks on both receivers.

The user also confirms on both receivers: natural queue advance through original
AAC, converted DTS and AAC again; synchronized channel-pattern output; local
paused at queue end; rapid Next twice reaches the third clip; Stop during a
transition closes receiver playback, keeps the app responsive/local paused and
prevents delayed restart. The queue procedure permitted seeking each 120-second
clip near 100 seconds before letting it finish naturally; this is short-control
queue evidence, not whole-film playback proof.

Read-only cache sampling at 250 ms covers the DTS controls (831 samples) and
queue checks (1,040 samples). Each observes at most one owned directory and peak
430,412 bytes, ending at zero directories/bytes with no unexpected entry observed.
These samples do not capture every instantaneous state. A final independent count
finds zero cache directories; the process's same executable/AppContainer/six
pinned modules are rechecked after testing. The user restores stereo conversion
off and leaves local playback paused. No manual check remains pending.

These are fresh Screenbox observations, distinct from D88's native/UWP host.
Initially no PR checks were created and manual dispatch returned HTTP 422, "Actions has been
disabled for this repository," despite enabled read-only permissions metadata.
The user enabled workflows. Manual run `38870523110` at `bff9e2fc` passes all four
jobs; PR run `38871102108` at `a6f1fcc9` also passes all four. Native checkpoint PR #73 passes
all 17 checks at `52c5a3b` in run `38851314214`. Inspect both actual PR heads and
their CI after the final evidence updates before merging; do not reuse an earlier
head's green checks. The user must approve merging. No Computer Use is used for
visual confirmation.

Only original D88 owned flash/beep fixtures may be cast. The private performance
film is not involved. Audio-only speakers and discrete converted surround remain
hardware-deferred; Chromecast hardware is unavailable. Sleeping Apple TV casting
has its separate earlier user observation.

The fork's [implementation and build/manual guide](https://github.com/ilyalissoboi/Screenbox/blob/codex/airplay-automatic-audio-x64/docs/AIRPLAY_AUTOMATIC_AUDIO.md)
records exact commands and host lifetime choices. The
[D90 proposal](screenbox-automatic-audio-plan.md) remains the pre-implementation
design record.
