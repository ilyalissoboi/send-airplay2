# D91 Screenbox x64 automatic-audio integration

2026-10-16. The user approved implementing the D90 host proposal after publication.
Native PR #72 is merged as `910b48e`; the published `0.5.0-ci.282` bytes remain
unchanged. Screenbox fork [PR #13](https://github.com/ilyalissoboi/Screenbox/pull/13)
starts from current fork main `ce78235`. Its initial implementation head is
`f621f2675f9e0be50845a33f942d25db64b251c4`; runtime/test follow-up head is
`bbbfbc9db240a51547fa7d3b180cca369f29cb23`, documentation head `bff9e2fc`.
The current fork instructions were read. The [sanitized local artifact record](validation/screenbox-automatic-audio-host-windows-2026-10-16.json)
contains test counts, actual MSIX/executable hashes and the explicitly pending gates.

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

These are engineering choices under the approved plan, not additional user
requirements or a change to the published library's policy. Screenbox's adapter
is original host code following the existing callback contract; no third-party
implementation/dependency was imported. Existing source/license provenance and
dynamic LGPL profile are packaged unchanged.

## Current evidence and manual gate

Visual Studio 2026 Core builds pass for both profiles. Pure logic suites pass
111/111 ordinary and 125/125 opt-in. Coverage includes immutable reads,
cancelled seal, exclusive-file collisions, unknown-file preservation, retry of
locked-file cleanup, unused/pre-transfer release, sibling isolation, serial drain,
superseded admission, logger-failure containment and policy/error categories.
Cached-pin/corruption-preservation controls and downloader/MSBuild architecture,
RID and malformed-profile refusals pass. XAML formatting and whitespace checks
pass. These tests instantiate no XAML.

Both x64 MSIX profiles build. Inspection matches the two ordinary native files
and refuses added codec DLLs there; the opt-in MSIX matches all 40 pinned native,
source and notice files. Existing translation, legacy and third-party trimming
warnings remain. Development Screenbox 1.0.0.8 was registered in place from an
extracted real MSIX; app data/pairings were preserved and the source manifest was
restored. Its executable SHA-256 is
`e2949f4047b2f0a46d99da4c2a388a0502e1db828fd6ccb2897d83b2022c03cc`.
It includes the final guard suppressing stale failure notifications if Stop or
supersession wins a concurrent failure. Earlier 1.0.0.7 instructions were paused
and redirected to this final development build before any host observations.

The user reports both receivers available. Written setting/default and owned AAC
fixture instructions have been sent. Installed AppContainer/loaded module checks,
fresh Screenbox AAC-copy/DTS-conversion playback/controls, Mac pending-consent
cancellation/retry, copy/conversion natural queue changes, local paused handoff
and cache cleanup are still pending. Do not infer their outcome from D88's native
or UWP test host. PR #13 is a draft while that gate is pending. No PR checks were
created. Manual dispatch returns HTTP 422, "Actions has been disabled for this
repository," despite enabled read-only permissions metadata. The user has been
asked to inspect/enable workflows. CI is not passed; rerun it at the actual final
head after that external blocker is resolved. No Computer Use is used for visual
confirmation.

Only original D88 owned flash/beep fixtures may be cast. The private performance
film is not involved. Audio-only speakers and discrete converted surround remain
hardware-deferred; Chromecast hardware is unavailable. Sleeping Apple TV casting
has its separate earlier user observation.

The fork's [implementation and build/manual guide](https://github.com/ilyalissoboi/Screenbox/blob/codex/airplay-automatic-audio-x64/docs/AIRPLAY_AUTOMATIC_AUDIO.md)
records exact commands and host lifetime choices. The
[D90 proposal](screenbox-automatic-audio-plan.md) remains the pre-implementation
design record.
