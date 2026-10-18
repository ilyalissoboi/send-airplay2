# Separate-session development handoff

Checkpoint: 2026-10-18 (Asia/Tokyo). PRs #12-#82 are merged. `main` contains
the experimental C API version 5, C# binding, packaged UWP host and native
builds, Windows/macOS/Linux credential stores, wake before play, MP4/MKV HLS
remux with text subtitles and indexed MKV startup, and D62's URL controls
fallback for receivers that reject remote-control SETUP. The Screenbox fork
has implemented its current design and Forget follow-up (PRs #1-#10); merged `main` now pins
`0.3.0-ci.207`, including D62/D63. Receiver evidence remains specific to the recorded
Apple TV and MacBook Pro from Windows. D62 used the CLI; completed D63 checked
desktop C API and packaged UWP playback on macOS 26.7.1, and the user reports
the earlier private Screenbox UI checks passed. D64 completed final Screenbox
checks of published `ci.207` by user report with running native/crypto DLL
hashes verified; fork PR #8 merged as `31013d5e`. Final evidence documentation
in native PR #41 and fork PR #9 is merged. D65's initial consent investigation
is complete: eleven clean casts, reuse across process/session/name changes, and
re-prompting during an equal-idle chain. Exact timer/key/renewal remain unproven;
no sender behavior change is proposed. D65 evidence PR #42 merged as `8a40ec3`
and D66 Screenbox PR #10 merged as `8bfd3dd9`. All 14 native checks and three
fork checks passed at final heads `8978188` and `47016474`. D67 and the following
historical increments are superseded by the current D99 checkpoint and ordered
queue below; do not resume completed slices.
Read `AGENTS.md`, then [HANDOFF.md](HANDOFF.md), [design.md](design.md) and
[receiver-validation.md](receiver-validation.md). This note is a focused restart
guide; the master handoff and dated artifacts preserve the longer history.

**Current D99 Screenbox AAC/M4A slice (2026-10-18), in progress.** The user's
Proceed after merged native #82 (`afb9ee8`; all 17 final-head checks passed at
`481ab01`) authorizes Screenbox AAC/M4A routing/controls/queue handling. Fork
[PR #16](https://github.com/ilyalissoboi/Screenbox/pull/16), implementation
`aff57b6f` from merged `7cea7023`, selects progressive `audio/mp4` for local M4A
per queue item in both existing profiles and bypasses conversion/cache even
when stereo permission is on. Video retains its existing HLS policy; pins and
native API are unchanged. Pure logic suites pass 122/137. Both actual Release
MSIX builds and two/40 pinned payloads pass. Development 1.0.0.13 is installed
in place with identity/data preserved; loaded-runtime and written receiver
gates are pending. The user confirms both
receivers available with macOS 26.7.1/tvOS 26.6 unchanged. See
[the active slice](screenbox-audio-files.md). Automatic conversion of other
standalone audio formats is a separate follow-up, not implemented here. No new
release is published; D97/D98 are complete and must not be repeated.

**Completed D98 packaged AAC/M4A gate passed (2026-10-18).** Test-host
implementation `153a840` is installed as codec-free x64 MSIX 0.1.30.0, preserving
identity/data. The user confirms the original video-free brokered AAC/M4A plays
through progressive `audio/mp4` on MacBook Pro/macOS 26.7.1 and Apple TV/tvOS
26.6, explicitly unchanged. Both pass audible pause/resume, forward/backward
seeks, natural end and separate Stop/no delayed restart. Mac pending-consent
cancellation passes through the existing Stop button, with the app responsive.
Before and after, actual AppContainer/executable/two loaded DLL hashes match the
inspected package. All eight source instances release exactly once. Preserve the
initial consent timeout and two pre-media Mac connection failures; a later Mac
retry passes using the same package, with cause not isolated.
Build/binary/parser checks pass. PR #81 is merged as `45f39be`, with all 17
checks passed at final head `0c16c4a` in
[run 39092030693](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39092030693).
No further D98 manual check is pending. See [the gate and bounded scope](audio-only-uwp-gate.md). Native D97 and
the published fork preview remain separate evidence. Merge/CI adds no new
receiver observation. The next proposed slice is Screenbox AAC/M4A routing
and lifecycle/queue controls, not started here. No new release is published.

**Completed D97 checkpoint (2026-10-17).**
[PR #80](https://github.com/ilyalissoboi/send-airplay2/pull/80) records the native
AAC/M4A audio-only URL gate on MacBook Pro/macOS 26.7.1 and Apple TV/tvOS 26.6,
with versions explicitly unchanged. Both receivers pass audible playback,
pause/resume, forward/backward seeks, natural end and separate Stop/no delayed
restart. Mac pending-consent cancellation also passes. The runtime is merged
source `449457a`, public C API 5/progressive delivery with `audio/mp4`, with no
production change or video track. The fixture, exact binary hashes, user
observations and limits are in [the D97 record](audio-only-url-probe.md).
All five existing progressive-path CTests pass. All 17 PR checks pass at the
original probe head `99c2ee3`; inspect review-follow-up CI at its actual head
separately. No further D97 manual check is pending.

Publication/UI checkpoint #79 is merged as `7403044`; its completed D94 pin,
D95 settings and D96 fork prerelease are historical below. Do not repeat them.
D98 above completes the packaged UWP brokered AAC/M4A progressive gate;
Screenbox audio-file routing is the next proposal. Follow the
[ordered work queue](#open-issues-and-ordered-next-work); audio-only HLS,
transcoding and speaker transport are separate scopes. This review request
updates documentation only; it does not start the proposed implementation.

**D96 fork preview published and verified (2026-10-17).** The user explicitly
authorizes a Release build and GitHub release after fork #15 merged as `7cea7023`;
all four checks pass at final head `7f80fa25`. The
[Screenbox AirPlay Preview 0.1.0-preview.1 prerelease](https://github.com/ilyalissoboi/Screenbox/releases/tag/airplay-preview-0.1.0-preview.1)
is published from that merged commit, with x64 Release/experimental ci.294,
package version 0.1.0.0 and a separate unsigned preview identity. Packaging-only
version/identity/display changes are included in the corresponding source ZIP.
127 release-source logic checks, the actual MSIX build, all 40 pinned payload
files, dependency metadata and complete installer/source checksum coverage pass.
The elevated installer passes after avoiding redundant shared-framework installs;
Windows activation and the running executable hash pass. This adds installation/
launch evidence, not a new visual or receiver observation. All five public assets
download without authentication and match sizes/hashes; the tag resolves to
`7cea7023`. Read [the release record](screenbox-airplay-preview-release.md).
No Store upload, Sentry DSN, winget submission, existing app data reset or new
library publication is performed. The preview coexists with Store/debug apps.
Publication is complete and authorized; do not ask again or replace assets.
Native checkpoint #79 is merged as `7403044`. D97 above supersedes this slice's
next-work suggestion. Long-input/cache-quota behavior remains a separate proposal;
the current 512-MiB budget and hardware/user deferrals are unchanged.

**D95 Casting settings presentation merged (2026-10-17).**
The user asks to move the stereo conversion preference into its own localized
**Casting** settings group and add an icon consistent with other settings.
Fork [PR #15](https://github.com/ilyalissoboi/Screenbox/pull/15), branch
`codex/casting-settings-group` from merged `8d2851c8`, implements the group
between General and Player with the existing audio glyph and settings-card
styles. Both heading/card use the existing opt-in visibility gate; ordinary
builds have no empty Casting group. Toggle binding, accessibility labels,
stored preference/default, audio policy and package pins are unchanged.
XAML Styler and diff checks pass. Both Release MSIX profiles build, and their
two/40 pinned payload files verify. Development 1.0.0.12 is registered in place
with identity/data preserved and source manifest unchanged; its running
executable matches the inspected implementation `6d3a30b4` MSIX. The written
manual layout/toggle persistence check passes by user confirmation: Casting
appears between General and Player, the icon aligns with neighboring settings,
and the toggle retains its value after leaving/reopening Settings. No Computer Use visual confirmation
is permitted by the user. Fork #15 is merged as `7cea7023`, with all four CI
checks passed at final head `7f80fa25`. D96 above records the separately
authorized fork release, without the upstream tag/Store/Sentry path. No new
receiver check is required for this presentation-only change.

**D94 merged checkpoint (2026-10-17).** Native
[PR #78](https://github.com/ilyalissoboi/send-airplay2/pull/78) merged as
`449457a`; all 17 checks pass at final head `722e84b` in
[run 39006282777](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39006282777).
Screenbox [PR #14](https://github.com/ilyalissoboi/Screenbox/pull/14) merged as
`8d2851c8`; all four validation jobs pass at final head `3a9de5b1` in
[run 39006286791](https://github.com/ilyalissoboi/Screenbox/actions/runs/39006286791)
(release intentionally skipped). The opt-in pin update and recorded bounded
Mac/Apple TV gate are complete; ordinary ci.207 remains unchanged. This merge
record adds no new runtime or receiver observation. Do not repeat publication,
the pin update or completed manual checks. No new implementation slice has been
authorized. A suggested next slice is to evaluate sustained long-input playback
and finite-cache/quota behavior, preserving the existing 512-MiB boundary until
a design is agreed. Speaker audio, discrete surround and Chromecast validation
remain hardware-deferred; remote-Stop classification remains user-deferred.

**D94 Screenbox ci.294 recorded host gate passed (2026-10-17).** Fork
[PR #14](https://github.com/ilyalissoboi/Screenbox/pull/14) updates the opt-in
x64 pin from ci.282 to published ci.294, retaining ordinary ci.207 and existing
host code/UI, permission, audio policy and quota. Both Core profiles pass
113/127 logic checks; public download/cache integrity and profile refusals pass.
Both actual MSIX layouts match their pins (two ordinary native files; all 40
opt-in native/source/notice files). The inspected implementation `66660bf7` MSIX
is registered in place as development 1.0.0.11, preserving data and source manifest.
Before and after playback, its AppContainer, executable and six loaded published
DLL hashes pass. The user confirms both Mac and Apple TV passed the written
90-second HDR/DTS playback, pause/resume, forward/backward seeks, Stop closure
and local-paused checks. Final owned cache is zero directories/bytes. The most
recently reported receiver versions are macOS 26.7.1/tvOS 26.6; this reply reports
no change but does not explicitly reconfirm them. The test-only cleanup gate fix
at `fbb01d20` passes all four fork CI jobs; production/app code is unchanged.
Both PRs are merged with final-head CI passed, as recorded above. Read
[the checkpoint](screenbox-hdr-package-update.md). No further D94 manual check is pending. Keep D91/D92
history separate; no whole-film, HLG, speaker or discrete converted-surround
claim is added. Use written manual steps, with no Computer Use for visual
confirmation. Do not republish ci.294.

**D93 HDR-corrected prerelease published; public download verified (2026-10-17).**
The user's Proceed starts `codex/hdr-corrected-audio-candidate` from clean merged
main `72e73ea`, after PR #75 merged (all 17 final-head checks passed at `98cda90`).
[Main dispatch run 38993860187](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38993860187)
passes all 17 checks and produces `SendAirPlay2.StereoAac 0.5.0-ci.294`, including
D92's HDR fix. The unchanged candidate is 21,314,548 bytes, SHA-256
`a06216e954e8ef1e2452abe8472e95fbbc04c036e5e1c017e4a92642b20c76b8`.
Clean binding/native attribution, six DLL hashes, UWP distribution/source/profile
checks and fresh local API-5/copy/RID/direct/transitive consumer checks pass;
the UWP job passes all 12 packaging controls. Five dependency DLLs and the
profile/notices match ci.282. Read [candidate preparation](hdr-corrected-audio-candidate.md)
and its release notes/evidence. After PR #76 merged as `b4e38a3` (all 17 final-head
checks passed at `3d63486`), the user explicitly approves publication. The
[GitHub prerelease](https://github.com/ilyalissoboi/send-airplay2/releases/tag/nuget-stereo-aac-v0.5.0-ci.294)
contains the unchanged package and checksum, with the tag on `72e73ea`. An
unauthenticated public download matches the original SHA-256/size and sidecar;
[publication evidence](validation/hdr-corrected-audio-publication-windows-2026-10-17.json)
records the result. This approval is complete; do not ask again or republish ci.294.
Publication documentation is on `codex/published-hdr-corrected-candidate` from
merged main; inspect its actual PR head when reviewing. Next is a separate
Screenbox opt-in pin update. Its current ci.282 pin/app is unchanged.
Published ci.282 remains unchanged. The user's later testing suggests the Apple
TV symptom was HDR-related, with no recurrence since the correction. Earlier
failed attempts remain in D92's history; candidate checks add no instrumented
receiver observation or verification of these exact candidate bytes.

**D92 HDR startup correction merged (historical implementation record).** PR #74 merged
as `f1598cf`; all 17 checks passed at final head `1e917aa` in
[run 38909207891](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38909207891).
D93 above records the new clean-main candidate, passing artifact/consumer
checks and approved publication/public-download verification. The Screenbox
pin update remains separate; do not overwrite ci.282 or republish ci.294.
No receiver observation is pending for the recorded D92 checks. Preserve the
earlier Apple TV attempts and the 2026-10-17 user follow-up: later testing suggests
the incorrect HDR flag caused the issue, with no recurrence since the correction.
Do not present it as a separately recurring defect or an independently isolated cause.

Fork #13 is merged as
`b52c58e` and native #73 as `d9cd2c2`. The user reports a grey-screen failure
for one private long HEVC/DTS input after enabling conversion. Native/offline
diagnostics isolate the missing PQ declaration; the same file plays after
adding only `VIDEO-RANGE=PQ`, confirmed manually. A bounded original SPS VUI
reader now selects PQ/HLG automatically, preserving SDR behavior and video
payload. Read [D92](hevc-hdr-range.md). The implementation branch
`codex/long-dts-cast-failure` started from merged main. Screenbox 1.0.0.10 is a
private DLL override, with verified AppContainer/loaded hashes; the user's Mac
controls pass, with final hashes/empty cache checked. The user then reports a
silent Apple TV Screenbox failure from zero. A native Apple TV 20-second cast
plays normally by user report. The packaged UWP comparison first receives early
`MediaEnd` near 16 seconds; visible-window 90-second UWP and Screenbox retries
then pass. The later user follow-up suggests HDR as the cause and reports no
recurrence after the correction; window visibility was not established as a cause.
Final Screenbox hashes/AppContainer pass again; confirmed
Stop leaves zero owned cache directories/bytes.
Both implementation and final evidence heads of #74 passed all 17 checks.
The published ci.282 package is unchanged; candidate preparation/publication
and the host pin remain separate follow-ups. Do not resume D91/D92 as open PRs.

**D91 recorded Screenbox host gate passed (2026-10-16).**
The user's Proceed starts fork [PR #13](https://github.com/ilyalissoboi/Screenbox/pull/13)
from `ce78235`, initial head `f621f267`. Experimental x64 package selection,
verified cached/new downloads, isolated outputs, app-owned finite cache with
serial cleanup/admission, Stop/supersession checks, explicit default-off stereo
permission and error categories are implemented. Ordinary builds retain ci.207.
Both Core/MSIX profiles build; 113/113 ordinary and 127/127 opt-in logic checks,
hash/corruption/profile refusals and actual MSIX payload checks pass. Development
1.0.0.9 is registered in place with data preserved; follow-up `bbbfbc9d` suppresses
stale failure results when Stop wins and `a6f1fcc9` exposes initial-start Cancel.
Both receivers are available. The user confirms the setting/default off and
unchanged macOS 26.7.1/tvOS 26.6. Version 1.0.0.9 passes AppContainer/six pinned
loaded hashes and real Mac consent cancellation via Cancel: prompt closure,
responsiveness, local paused/no error. Accepted AAC retry and AAC-copy controls
pass on both receivers with sync/channel pattern, start near 45 seconds,
pause/resume, both seeks, Stop closure and local paused handoff. Off-permission
DTS refusal passes on the Mac and converted DTS controls pass on both receivers.
Both pass natural AAC → converted DTS → AAC queue advance, rapid Next and Stop
during a transition, with paused/responsive handoff and no delayed restart. Both
250-ms cache samples end at zero and observe at most one directory/430,412 bytes;
the independent final count is zero. Final process hashes are rechecked; the user
restores conversion off and local paused. No manual gate remains.
Read [the checkpoint](screenbox-automatic-audio-integration.md), inspect fork #13
at review-fix head `6377bccf` and native #73, including actual-head CI,
then review/ask before merging. The user enabled Actions
after the initial HTTP 422 disabled response; manual run `38870523110` passes all
four jobs at `bff9e2fc`. Cancel-head PR run `38871102108` at `a6f1fcc9` also passes all four.
Native PR #73 passes 17 checks at `52c5a3b`. Recheck final evidence heads.
Review follow-up `6377bccf` restores previously playing local media after a current
failed start, including a Cancelled result, while caller Cancel/Stop returns
earlier via generation invalidation. Both logic suites pass again. This changes
only recovery; the hardware record remains 1.0.0.9/`a6f1fcc9`, with no new
receiver-dismissal observation.
No Computer Use for visual
confirmation; original owned fixtures only. Native checkpoint branch starts from
PR #72 merge `910b48e`. Do not repeat publication or infer host results from D88.

**D90 opt-in prerelease published / public hash verified (2026-10-16).**
PR #71 merged as `7f3aa6d`, with all 17 checks passed at `c16dc31` in run
`38840760789`. The user explicitly approved publishing the prepared candidate.
Main run `38836333166` / number 282 builds
`e6acba1`, with all 17 checks passed. The exact `0.5.0-ci.282` opt-in candidate
is 21,306,805 bytes, SHA-256 `5280f0868721476b2cae9018bd8197c3de3c9f356408781209ec57fd4b9ba4ff`.
Provenance/source/static and local/CI consumer checks pass. The
[GitHub prerelease](https://github.com/ilyalissoboi/send-airplay2/releases/tag/nuget-stereo-aac-v0.5.0-ci.282)
is published with the original package and sidecar; its tag targets `e6acba1`.
Unauthenticated public downloads match the exact package hash/size and sidecar.
Read [publication evidence](validation/opt-in-audio-publication-windows-2026-10-16.json),
[candidate preparation](opt-in-audio-release-candidate.md) and [Screenbox plan](screenbox-automatic-audio-plan.md).
Remote fork main `ce78235` is the planning baseline; the local checkout was stale.
No fork change or new receiver check was made. Next suggested slice is host
implementation under that plan, with current fork instructions and its own PR.
Keep codec-free `ci.207` and the fork pin unchanged until that integration slice.
Publication documentation is on `codex/published-stereo-aac-candidate`; do not
repeat publication or ask for approval of the already published candidate.

**D89 opt-in runtime packaging merged (2026-10-16, PR #70).**
PR #70 merged as `e6acba1`; all 17 checks passed at final head `4c4bfb8`
in [CI run 38830621263](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38830621263).
PR #69 merged as `64ed4db`; all 16 final-head checks passed at `e6a9970`.
The user's Proceed starts `codex/opt-in-audio-runtime-package` from origin/main;
implementation and clean local artifacts are in [PR #70](https://github.com/ilyalissoboi/send-airplay2/pull/70).
The separate `SendAirPlay2.StereoAac` package adds the validated x64 UWP codec
runtime plus exact source/patch/configuration/notices; the existing package stays
codec-free. Source/prefix/overlay/profile and private-export guards plus local
restore/build/hash/refusal checks pass. Distribution core builds with private
validation OFF. Clean source `50c2a66` passes thirteen profile controls plus the
API-5 consumer build/copy/RID/mixed-profile checks, with both package hashes in
[the sanitized record](validation/opt-in-audio-runtime-package-windows-2026-10-16.json).
No release, Screenbox pin/app, native audio policy or new receiver
claim is included. Read [the contract](opt-in-audio-runtime-package.md). Speakers/discrete surround
remain hardware-deferred.

D89 review follow-up: `dd7b037` adds resolved dependency/asset checks for transitive
mixed profiles plus a real wrapper-package restore regression. The old artifact
misses the conflict; clean `0.5.0-d89.review1` rejects it. Consumer build, six copied
hashes, RID/direct/transitive refusals and thirteen profile controls pass. Read
[review evidence](validation/opt-in-audio-package-conflict-review-windows-2026-10-16.json).
All 17 checks passed at reviewed head `949492b` in run `38761426436` and final
head `4c4bfb8` in run `38830621263`. No new runtime, receiver or publication claim.

Follow D90 above for the published package and Screenbox proposal;
host implementation/pins remain a later slice.
Completed D88 checks remain recorded; do not restart them as pending work.

**D88 recorded automatic audio receiver gate complete (2026-10-15, PR #69).**
Branch `codex/automatic-audio-receiver-gate` starts at merged PR #68, `f6c85ef`.
Native Mac/Apple TV AAC copy and DTS conversion pass manual synchronization,
channel-pattern, start, controls and natural-end checks. Native Mac Stop and
unanswered-consent cancellation pass. Packaged UWP 0.1.28.0 passes all nine
original format cases on each receiver, converted DTS Stop on both and manual
Mac pending-consent cancellation: 18 natural ends, two Stops and one cancelled
start, expected policy fields and complete cache cleanup. The user confirms all
observations; macOS 26.7.1 and tvOS 26.6 unchanged. Native seven DLL hashes and
UWP AppContainer/six loaded hashes are verified. Read
[the gate and limitations](automatic-audio-receiver-gate.md).

Retain one native Apple TV AAC seek disconnect separately from its successful
retry; cause remains unestablished. Two packaged Mac consent expiries/rejections
are also separate from the successful cancelled retry. The first native Mac AAC
diagnostic omitted final/module fields and has a corrected repeat. No private
film was cast. Original media and local scripts stay ignored under `build-review`;
generator, provenance, host changes and sanitized evidence are in
[PR #69](https://github.com/ilyalissoboi/send-airplay2/pull/69). Inspect actual-head
CI before merging. No cast or receiver question is pending. Review/merge this
slice, then separately approve opt-in packaging/Screenbox integration. Keep
backend defaults, publication/pin boundaries and deferred speaker/discrete
surround scope. No Computer Use for visual confirmation.

PR #69 review: validate all four FFmpeg runtime DLLs before `PrepareForBuild`
and package that same explicit list. Eight missing/complete/default-off guard
checks and an enabled publish pass; DLL/notice hashes match. Historical receiver
evidence/package remain unchanged. See
[offline packaging evidence](validation/automatic-audio-runtime-packaging-review-windows-2026-10-15.json).

**D87 bounded DTS seeks (2026-10-15, PR #68).**
PR #67 merged as `8a284ad`; all 16 final-head checks passed at `9064f78` in
run `38677650564`. The user's Resume work continues on fresh
`codex/matroska-dts-bounded-seek`. Read [the seek contract and evidence](matroska-dts-bounded-seek.md).
The private middle refusal occurred during demuxer audio-index extension.
Canonical DTS can use a nearer already parsed video coordinate, backed off by
the maximum 256-frame lace span, retaining selected audio and all bounds.
Owned >1-GiB virtual controls reproduce the original guard; fixed-offset PCM/EOF,
missing/late cues, source failure and retained maximum-lace PCM/AAC checks pass.
Static/shared/enabled/codec suites pass 39/39, 40/40, 41/41 and 14/14.
Private package 0.1.0.8 at `731db15` passes bundled and hash-verified folder-granted
137-case/61-fixture matrices, with actual token/six loaded hashes verified.
The authorized brokered video passes three independent first/middle/last owners:
138/314/64 ms cold, 7/13/<1 ms byte-identical cached repeats, complete cleanup.
The middle request uses 76,775,424 requested source bytes under the unchanged
256-MiB ceiling. These are bounded samples, not whole-film or receiver proof.
Next: manual copied/converted receiver checks, keeping receiver/firmware and host
results explicit. Preserve private identity/content, OFF-by-default backend and
publication/Screenbox boundaries. Inspect [PR #68](https://github.com/ilyalissoboi/send-airplay2/pull/68)
and actual-head CI before writing or merging. Speakers/discrete converted surround
stay hardware-deferred.

**D86 Matroska DTS lace clock (2026-10-15, PR #67).**
The user's Proceed starts `codex/matroska-dts-frame-clock` from merged
[PR #66](https://github.com/ilyalissoboi/send-airplay2/pull/66), `1fe9f61`.
All 16 final-head checks passed at `cbe7440` in run `38669659266`.
Read [the owned reproduction and clock contract](matroska-dts-frame-clock.md).
An owned eight-frame DTS lace reproduces D85's refusal. Canonical continuation
frames now count decoded samples within their known native block; new blocks
keep existing grid/continuity tolerances. Fixed/Xiph/EBML and 256-frame controls
pass with exact PCM/AAC equivalence; 5-ms and frame-aligned gaps still refuse.
A bounded private-video preparation diagnostic accepts. Local static/shared/
enabled/codec suites pass 39/39, 40/40, 41/41 and 13/13. Private package 0.1.0.7
at `db35a6f` passes bundled and hash-verified folder-granted 137-case/61-fixture
matrices and both gap refusals, with token/six loaded hashes verified. The brokered
video converts its first fragment in 207 ms with a 10-ms identical cached repeat.
The middle request refuses at the resource limit after exactly 256 MiB requested
source bytes; the last window is not attempted. Cleanup passes. Overall result:
**MEASURED_REFUSAL**, not first/middle/last throughput. Next proposed slice:
reproduce middle-seek read amplification with owned long-duration laced DTS
controls and investigate bounded seek/restart I/O before repeating performance
and receiver gates. Do not widen budgets or substitute routes/tracks. Preserve
private identity/content, receiver boundaries and OFF-by-default/publication
limits. Inspect [PR #67](https://github.com/ilyalissoboi/send-airplay2/pull/67)
and actual-head CI before writing or merging.

**D85 Matroska presentation origins (2026-10-15).**
PR #66 merged as `1fe9f61`; all 16 final-head checks passed at `cbe7440`.
D86 above supersedes this slice's historical next queue.
The user's Proceed starts `codex/matroska-presentation-origin` from merged
[PR #65](https://github.com/ilyalissoboi/send-airplay2/pull/65), `76abc3c`.
All 16 final-head checks passed at `9290fb3` in run `38663140087`.
Read [the shared-origin contract](matroska-presentation-origin.md). The private
composition rebases Matroska video/text to its first video timestamp and reads
audio at that source offset, retaining relative timing and existing refusal
bounds. Owned indexed/full-scan, reordered-video, earlier/later audio, subtitle,
seek/EOF and common-offset byte-equivalence checks pass. Private package 0.1.0.6
at `b946bb7` passes bundled and hash-verified folder-granted 126-case/57-fixture
matrices, with actual token/six loaded hashes and cleanup verified. Local static,
shared/enabled and codec suites pass 39/39, 40/40, 41/41 and 12/12. Inspect PR-head CI.

The authorized brokered video passes its origin requirement but still refuses
before fragment generation: media-unsupported in 29 ms, 332 reads / 5,553,062
returned bytes, complete cleanup. A bounded diagnostic accepts the initial
offset window, then hits the existing canonical-frame-grid guard. Do not infer
source corruption or widen tolerances. Next proposed slice: reproduce the DTS
timestamp/decoder-phase mismatch with owned controls, distinguish valid timing
from discontinuities, then repeat brokered conversion performance before receivers.
Keep private input identity/content out of committed evidence; no receiver,
publication or Screenbox change. Speakers/discrete surround stay hardware-deferred.

**D84 expanded automatic AppContainer gate (2026-10-15).**
PR #65 merged as `76abc3c`; all 16 final-head checks passed at `9290fb3`.
D85 above supersedes this slice's historical next queue.
The user's Proceed starts `codex/automatic-audio-uwp-runtime` from merged
[PR #64](https://github.com/ilyalissoboi/send-airplay2/pull/64), `2bdd397`.
All 16 checks passed at final head `4126ca9` in run `38593843522`.
Read [the private package/catalog/performance contract](automatic-audio-uwp-runtime.md).
Private package 0.1.0.5 at `b0cd5ec` passes bundled and hash-verified folder-granted
115-case/53-fixture automatic matrices, retained forced-stereo/public lifetimes,
unused TemporaryFolder cleanup and expected refusals. Actual AppContainer tokens
and six loaded hashes match each registered layout/native build. Initial 0.1.0.4
exposed old codec DLLs staged from the separate core prefix; staging now follows
selected import libraries, with regression/profile checks. Final local static,
shared and enabled suites pass 39/39, 40/40 and 41/41. Inspect actual PR-head CI.

The authorized brokered private-video check records **MEASURED_REFUSAL**:
media-unsupported in 74 ms, 300 reads / 4,721,852 returned bytes, complete cleanup,
no fragment generated. A bounded desktop diagnostic identifies the existing
zero-origin-video guard: this Matroska video starts after zero. Keep the private
input's path/name/hash/content out of committed evidence. No first/middle/tail
conversion throughput is established. Next proposed slice: preserve common
nonzero Matroska presentation origins with independent audio/video/subtitle,
seek/trim/EOF controls, then repeat brokered performance before receiver gates.
No receiver, network, publication or Screenbox change; speakers and discrete
converted surround stay hardware-deferred.

**D83 public automatic audio integration (2026-10-15).**
PR #64 merged as `2bdd397`; all 16 final-head checks passed at `4126ca9`.
D84 above supersedes this slice's historical next queue.
The user's Proceed starts `codex/automatic-audio-public` from merged
[PR #63](https://github.com/ilyalissoboi/send-airplay2/pull/63), `6788583`.
All 16 checks passed at final head `4f2494b` in run `38584885677`.
Read [the API 5 / offline-check / binding / CLI contract](automatic-audio-public.md).
Automatic delivery is opt-in under D82's finite profile. Successful preparation
reports original ID, source/output families, action, channels and fixed reason;
preservation refuses required conversion. Copy/video-only need no backend/cache.
Only accepted conversion clears storage; unused automatic stores release their
contexts without Clear. Offline checking borrows source/cache without credentials
or receiver access, then discards the presentation; casting prepares again.
Old v1-v4 prefixes/defaults remain valid. Package/Screenbox pin and dependency
profile are unchanged. Next: expanded packaged-runtime/module/storage checks,
real-file/brokered performance, then copied/converted receiver gates. Speaker
audio and discrete converted surround stay hardware-deferred. Inspect actual-head CI.

D83 offline validation: static **38/38** (45.84 s), shared **39/39** (49.35 s),
enabled shared **40/40** (66.05 s). Final output-canary, managed unused-storage
and preparation-cancellation checks pass in their affected default/enabled
builds. x64 UWP core compilation, six-DLL import/AppContainer checks and the
offline UWP harness/binding build pass; the harness was not run or installed.
Eleven touched native C/C++ files pass formatting; whitespace, 135 owned fixture
hashes and 530 relative documentation targets pass. Inspect new PR-head CI.

**D82 private automatic audio policy (2026-10-14).**
[PR #63](https://github.com/ilyalissoboi/send-airplay2/pull/63) review follow-up:
selected MP4 audio copy now refuses finite media edits ending before the remaining
sample span, rather than exposing source-trimmed audio when init duration becomes
zero. Exact rational boundaries, optional empty edits and both elst versions have
offline regressions. Full local static/shared/enabled shared suites pass
37/37, 38/38 and 39/39; standalone codec policy and final fractional-boundary
checks pass. PR #63 merged as `6788583`; all 16 final-head CI checks passed.
No receiver evidence is added. D83 above supersedes this slice's historical next queue.
The user's Proceed starts `codex/automatic-audio-policy` from merged
[PR #62](https://github.com/ilyalissoboi/send-airplay2/pull/62), `c69543a`.
All 16 checks passed at final head `cc3bb2c` in run `38560520412`.
Read [the selected-copy table, conversion routing and ownership contract](automatic-audio-policy.md).
A private named-profile plan selects the original audio identity before filtering.
Prepared automatic media copies eligible AAC-LC/AC-3/E-AC-3 without backend/cache,
converts selected DTS-core/FLAC/Opus/Vorbis/PCM/TrueHD through the existing bounded
stereo owner, or copies video-only when no audio is enabled. Invalid/ambiguous
selection and source/configuration/cache/timeout/cancellation failures never
choose another track or retry conversion. Preservation can refuse stereo loss.
The first copy table is finite; unvalidated AAC/Dolby configurations are refused,
not guessed compatible or converted merely because a generic decoder exists.
Selected copy currently uses a bounded full native scan, without Cues acceleration;
per-block MKV padding/lacing is refused. This is private offline implementation.
API 4, bindings/CLI, delivery defaults, package and Screenbox pin are unchanged.
Next: versioned public integration/reporting, then expanded packaged runtime,
real-file/brokered performance and converted-receiver gates. Speakers and discrete
converted surround remain hardware-deferred. Inspect actual slice head/CI.

**D81 compact restart anchors (2026-10-14).**
[PR #62](https://github.com/ilyalissoboi/send-airplay2/pull/62) merged as `c69543a`;
all 16 checks passed at `cc3bb2c` in run `38560520412`. Do not resume its closed branch.
D82 above supersedes its historical next-policy queue.
The user's Proceed starts `codex/matroska-compact-index` from merged
[PR #61](https://github.com/ilyalissoboi/send-airplay2/pull/61), `00c845d`.
Its final head `27cbb1e` passed all 16 checks in run `38555473407`.
Read [the sparse scan/index contract](matroska-compact-index.md).
Canonical PCM/FLAC/Opus/Vorbis/TrueHD now scan selected native packets without
retaining a complete packet table. At most 65536 source-second restart anchors
support binary lookup and nearby clock/trim reconstruction. Vorbis restores its
previous overlap mode; TrueHD anchors begin at major sync. The legacy vector
helper retains its 262144-packet bound; the canonical decoder no longer uses it.
Synthetic one-hour TrueHD (4.32 million packets) and two-hour Opus (360000)
check cold/reused >4-GiB seeks, literal EOF, compact memory and cancellation.
All existing timing/trim/AAC controls remain. I/O/callback/deadline/window/input
bounds are unchanged: a large source scan or restart gap can still be refused.
This is bounded offline scaling, not unrestricted full-film or receiver proof.
Next: selected-track remux/copy eligibility and automatic delivery policy under
an explicit profile, followed by versioned public reporting. Real-file/brokered
performance, expanded packaged runtime and converted-receiver gates remain.
No package or Screenbox pin change; speakers/discrete surround stay deferred.

**D80 compressed packet indexing (2026-10-14).**
[PR #61](https://github.com/ilyalissoboi/send-airplay2/pull/61) merged as `00c845d`;
all 16 checks passed at `27cbb1e` in run `38555473407`. Do not resume its closed branch.
D81 above supersedes the historical packet-table scaling queue below.
The user's Proceed starts `codex/matroska-compressed-index` from merged PR #60
(`531ff58`, final head `a2b3953`, all 16 checks passed in run `38532987731`).
Read [the compressed clock/restart contract and limits](matroska-compressed-index.md).
Canonical FLAC/Opus/Vorbis/TrueHD now share native selected packet coordinates,
parser-derived sample counts and signed per-block trim. TrueHD seeks restart at
major sync units; Vorbis first delay/skip cannot exceed intrinsic overlap priming.
Smaller/absent CodecDelay preserves initial presentation silence.
Thirty-second phase/AAC and independent short audible trim controls pass offline;
one-hour synthetic FLAC seeks above 4 GiB have bounded nearby reads.
The original absolute precision is reported separately from exact decoded-frame
continuity; an interior-discard gap must fail, not hide within container tolerance.
The 262144-packet/I/O/window/deadline bounds remain, so many full TrueHD/Opus/Vorbis
movies still exceed this subset. Next: compact streaming anchors/high-packet-count
scaling, before general selected copy/automatic policy and public reporting.
Inspect the actual open slice head/CI. Real-file/brokered-host performance,
expanded packaged runtime and converted-receiver gates remain. No package or
Screenbox pin change; speaker audio and discrete surround remain hardware-deferred.

**D79 selected PCM indexing (2026-10-14).**
The user's Proceed starts `codex/matroska-audio-index` from merged PR #59
(`9763189`, final head `2bc7a5c`, all 16 checks passed in run `38510002555`).
Read [the packet/index contract and evidence](matroska-pcm-index.md).
Canonical Matroska PCM now uses bounded native original-ID packet coordinates,
exact packed-sample counts, source-sample PTS and per-block trim. Cold seeks use
binary lookup and nearby preroll; PCM no longer has D78's ten-second movie horizon.
Thirty-second variable 44.1-kHz controls, independent delay/leading/final-trim
controls and one-hour synthetic seeks above 4 GiB pass offline tests. The combined
trim fixture exposed generic demux skip overwrite; indexed PCM avoids it, while
compressed-family/noncanonical trim proof remains a separate gate.
The index is limited to 262144 selected unlaced packets, with the existing I/O,
window, input, deadline and cancellation bounds. [PR #60](https://github.com/ilyalissoboi/send-airplay2/pull/60)
also preserves the one-second original clock admission bound before narrowing
precision; owned boundary/refusal controls cover the review correction.
[PR #60](https://github.com/ilyalissoboi/send-airplay2/pull/60) merged as `531ff58`;
all 16 checks passed at `a2b3953` in run `38532987731`. Do not resume its closed branch.
Do not generalize synthetic results to real files, UWP runtime or receivers.
Next: compressed exact durations/anchors and large-seek/trim gates for FLAC, Opus,
Vorbis and TrueHD. Their coarse clocks remain under the ten-second movie horizon.
Then selected remux/policy and versioned public reporting, expanded packaged
runtime/performance and converted-receiver gates. No package or Screenbox change.

**D78 bounded coarse-clock timing (2026-10-14).**
The user's Proceed starts `codex/matroska-audio-clock` from merged PR #58
(`820e89c`, final head `129ae6e`, all 16 checks passed in run `38504706633`;
output-rate review resolved). Read [the bounded timing gate](coarse-matroska-audio.md).
[PR #59](https://github.com/ilyalissoboi/send-airplay2/pull/59) merged as `9763189`;
all 16 checks passed at `2bc7a5c` in run `38510002555`. Do not resume its closed branch.
Expanded canonical Matroska windows rebuild from fresh start owners and count
actual decoded samples, including trim, under a ten-second presentation horizon.
Fourteen owned cases compare cold/reused windows and identical generated AAC
bytes with fine-clock controls. Long coarse HLS fails before initial decode,
resource registration or sealing; owned-store cleanup remains required.
Local gates: codec 7/7, static/shared 36/36 and 37/37, enabled shared 38/38,
standalone/integrated x64 UWP compilation and codec import inspection.
Inspect actual-head PR CI. This is the short-file timing foundation only.
Next: bounded exact long-file packet/sample indexing, cold-seek phase and scaling
tests before selected remux/policy and public reporting. Retain resource/horizon
limits; automatic policy, expanded packaged runtime/performance and converted
receiver checks remain pending. No package or Screenbox pin change.

**D77 private decoder foundation (2026-10-14).**
The user's Proceed starts `codex/audio-decoder-expansion` from merged PR #57
(`faee858`, final head `d4f4a62`, all 16 checks passed; padding review resolved).
Read [the expanded input matrix and offline gates](audio-decoder-expansion.md).
[PR #58](https://github.com/ilyalissoboi/send-airplay2/pull/58) merged as `820e89c`;
all 16 checks passed at `129ae6e` in run `38504706633`. Do not resume its closed branch.
The minimal native FFmpeg profile and original-ID decoder now cover FLAC,
Opus, Vorbis, PCM and TrueHD in the documented container/configuration subset.
Owned stereo/5.1 fixtures cover seeks, literal EOF trimming and generated AAC;
no new ABI, automatic policy, package or receiver result is added.
At this merge, expanded Matroska HLS required sample-accurate timestamps.
D78 above adds short-file coarse reconstruction; long-file indexing is next.
D75 performance/converted-receiver and expanded packaged-runtime gates remain.
PR #58 review correction retains Matroska OutputSamplingFrequency and compares
the effective output rate, preserving distinct AAC/SBR coded/output rates.
Owned metadata/decode and invalid-rate regression checks accompany the fix.

**D76 scope expansion (merged PR #57):** the user chose automatic policy plus FLAC, Opus,
Vorbis, PCM and TrueHD. Read [the expanded implementation plan](automatic-audio-transcoding-plan.md)
before writing. D77 above implements the private foundation subset; complete its
coarse-clock timing gate before selected remux/policy and public integration.
The D76 planning slice added no runtime behavior. D75's outstanding performance/converted-receiver
gates remain required; speakers and discrete converted surround stay deferred.

PR #57 review follow-up corrects DiscardPadding ownership: retain per-block
signed trimming via packet skip/discard data (or a native packet index), not
track inventory. Require cold/repeated tail-window and final AAC trim checks;
the documentation correction adds no runtime evidence.

**D71 merged:** the private
[stereo AAC HLS builder/cache](stereo-aac-hls.md) merged in PR #51 as `e09c57f`,
with all 16 checks passed at final head `475ae61` in run `38330651001`; both
review threads are resolved. Private audio resources/cache, compressed joins,
copied video/subtitles, edits/short-source silence and selected lifecycle checks
pass offline tests. Baseline static/shared passes 35/35 and 36/36; optional
codec/decoder/HLS passes 3/3; x64 UWP compilation passes.

**D71 declaration follow-up merged:** [PR #52](https://github.com/ilyalissoboi/send-airplay2/pull/52)
merged as `c092c9b` at final head `6716f1a`; all 16 checks passed in run
`38331920914`. It contains the master/audio independence correction missed by
PR #51's merge; copied video retains its declaration. The merge gate is complete.

**D72 readiness merged (2026-10-13).**
[PR #53](https://github.com/ilyalissoboi/send-airplay2/pull/53) merged as `25b84a1`.
Its tested final head `6bf1bd2` is included in main; all 16 checks passed in
run `38336511817`. No review threads were present. Private long-GOP splitting,
terminal cache/source failure reporting and synthetic one-hour / >4-GiB seeking
are implemented in the recorded offline scope. No new receiver or package result
is added. Do not resume its closed branch.

**D73 authoring merged (2026-10-13).**
[PR #54](https://github.com/ilyalissoboi/send-airplay2/pull/54) merged as `6923332`.
Final head `1d34596` passed all 16 checks in run `38339986770`; no review threads
were present. Its implementation head `3cd620e` passed codec 4/4, static/shared
35/35 and 36/36, runner 10/10 and x64 UWP compilation/import checks. Read
[policy and audit evidence](stereo-aac-authoring.md): six MP4 audits meet the
recorded tolerance; three estimate limitations remain explicit failed audits.
No whole-VOD accuracy guarantee, hardware result or publication is added.

**D74 merged (2026-10-13).**
[PR #55](https://github.com/ilyalissoboi/send-airplay2/pull/55) merged as `774198d`.
Final head `74e9ea9` passed all 16 checks in run `38407164325`, attempt 2;
the late-start cleanup review thread is resolved. API 4 explicit stereo delivery,
original track probe/selection, host cache, binding/CLI and terminal supervision
are implemented in the recorded offline scope. Default builds remain codec-free.
No package is published; Screenbox stays on `0.3.0-ci.207`. Read
[the contract](stereo-aac-playback.md); do not resume the closed D74 branch.

**D75 merged: integrated AppContainer runtime/storage validation (2026-10-14).**
[PR #56](https://github.com/ilyalissoboi/send-airplay2/pull/56) merged as `02d8be6`.
Final head `8f03874` passed all 16 checks in run `38490056580`; the runtime-gate
documentation review thread is resolved. Do not resume its closed branch.
[The offline runtime gate](stereo-aac-uwp-runtime.md) uses a
separate Native AOT/MSIX identity, the enabled production core and a private
OFF-by-default validation export, brokered StorageFile callbacks and app-owned
TemporaryFolder storage. It does not access credentials or a receiver. Packaged
results and attribution are in [the artifact](validation/stereo-aac-uwp-runtime-windows-2026-10-13.json).
The PASS bundled 0.1.0.2 run at `9371502` has matching loaded hashes for all six DLLs.
The corrected 0.1.0.3 run at `5a4e5e3` also passes: three borrowed probes,
17 resource/preparation cases (including the hash-verified outside-package
picker grant), and nine public lifecycle cases. The user confirmed fixture
selection; all six loaded hashes match this run's registered layout. No receiver
was contacted. This completes the recorded x64 AppContainer storage gate.
Baseline static/shared 36/36 and 37/37, enabled shared 38/38, six-DLL UWP imports
and existing-host compilation pass. Implementation head `5a4e5e3` passed all
16 CI checks in run `38415880324`; code/evidence head `8fb2bcc` also passed
all 16 checks in run `38421602489`. Its final-head merge gate is complete;
real-file performance and receiver checks remain required alongside D76.
Speaker audio and discrete surround remain
hardware-deferred. Ask before merging or publishing; visual checks use written
manual instructions, without Computer Use.

**Completed D70:** private [selected decoder/seek windows](audio-decoder-source.md)
merged in PR #50 as `65bb8a7`, with all 16 checks passed at final head `8b1adf2`
in run `38280966375`. The optional include review is fixed/resolved. Its branch
started at PR #49 checkpoint `a7905db`. Desktop actual compressed decoding and
x64 UWP compilation/import inspection passed; packaged adapter runtime and
receiver evidence were not added. Preserve those limits. D71-D75 are merged;
do not repeat the completed decoder merge gate.

**Completed D69:** the subsequent Proceed authorized the native source increment.
Its [inventory/selection/index/seek-metadata foundation](audio-source-inventory.md)
merged in PR #48 as `5d46479` after all 16 checks passed at final head `c816709`
in run `38268265899`; both metadata review findings are fixed and resolved.
It originally branched from main `cb9d641` and integrated PR #47's merged
checkpoint `031acae`, resolving documentation conflicts without native changes
from tested review head `fc63a327`. Start any next implementation slice on a fresh
branch from current `origin/main` with its own PR. D70 above completed selected
decoder streams and compressed seeking/timestamps; D71 now supplies private stereo HLS resources;
no new ABI/delivery value, package publication or receiver test is included.

**Completed D68:** the user's Proceed authorized the stereo-AAC codec/UWP
packaging spike. PR #46 merged as `cb9d641` after all 16 checks passed at
final head `579bb44` in run `38246014059` (attempt 2; transient GitHub HTTP 504
bootstrap downloads on x86/ARM64 passed the failed-job retry).
The optional development DLL and separate offline packaged
host are implemented; desktop, AppContainer import and packaged codec/cache
checks pass in the recorded Windows x64 scope, with loaded-module hashes
verified. Baseline CTest passes 35 static/36 shared. No receiver test or casting
delivery mode is added. Read [the spike](stereo-aac-codec-spike.md) and its
[artifact](validation/stereo-aac-codec-spike-windows-2026-10-12.json) before
resuming production HLS conversion/track selection. Do not ask for the same
implementation authorization again; speaker/surround output remain deferred.
Planning PR #44 is merged as `12c24a7`, with all 14 checks passed at `bc4b381`
and the selection review resolved. Merge-checkpoint PR #45 merged as `e92e29e`
(14 checks passed). Inspect actual open heads before editing; ask before merging/publishing.

Spike PR #46's initial `6a5777d` CI head failed after 66m24s at UWP configure
(`10.0` lost its decimal in an unquoted PowerShell argument); the desktop codec
test and all 14 baseline jobs passed. The CI follow-up isolates FFmpeg dependencies,
builds Release only, parallelizes desktop/UWP and saves completed binary-cache
entries even after failure. At implementation head `22f14a0`, hosted run
`38243945234` passes both codec jobs in 5m10s desktop/3m45s UWP, in parallel;
the codec gate is about 92% shorter. Run `38244731622` at `e3680a3` passes all 16
jobs with cached codec jobs at 1m06s desktop/1m16s UWP. The merged PR #45
documentation is integrated to resolve conflicts blocking automatic PR CI.
Final automatic PR CI passed at `579bb44`, with cold codec jobs at 5m01s
desktop/3m59s UWP. Read the
[CI follow-up](stereo-aac-codec-spike.md#ci-failure-and-runtime-follow-up-2026-10-12)
and use its standalone reproduction paths rather than assuming the root layout.

The subsequent research checkpoint merged in native PR #43 as `aa0f25e`
(14 checks passed at final head `789f029`) and fork PR #11 as `f596fd5e`
(docs-only; no CI checks reported). All prior PRs are merged.

Latest user decision D67 defers audio-file casting, including audio-only
speakers/HomePod, and DTS conversion retaining 5.1/7.1 output because suitable
local test hardware is unavailable. Preserve the [assessment](audio-features-assessment.md).
The completed next-slice plan is [video with surround audio downmixed to stereo AAC](stereo-aac-casting-plan.md).
That initial request authorized documentation/planning; the subsequent Proceed
authorizes the codec/packaging spike recorded above.
Do not start speaker or surround-output probes as prerequisites for this plan.
Native PR #44 merged as `12c24a7` after all 14 checks passed at final head
`bc4b381`; its selection review is resolved. In the plan, ambiguous MP4/MKV
audio requires explicit source-track selection, with native/binding probe and
selection support in scope. Fork PR #12 merged as `ce782356` at head `0cd9ff56`,
with no review findings or CI checks reported (docs-only). Its pinned link is
the initial draft, so follow the current native
plan for the revised contract. That planning review added no conversion code.
Chromecast video validation is deferred (no suitable local receiver); sleeping
Apple TV casting was already verified earlier by user report, with no new
build/date attribution supplied. Do not repeat it as an uncompleted gate.

## Repository, branch and user authorization

- Repository: [ilyalissoboi/send-airplay2](https://github.com/ilyalissoboi/send-airplay2).
- **D66 (2026-10-12):** user selected "Forget this AirPlay device" in Screenbox.
  Fork branch `codex/airplay-forget-device` starts from merged main `2cee2ea7`;
  [fork PR #10](https://github.com/ilyalissoboi/Screenbox/pull/10) merged as `8bfd3dd9`;
  final head `47016474` passed test, XAML lint and package build (release skipped).
  implementation `a3d64831`, validation docs `35285224`.
  Review fix `f05c10e6` applies the Forget guard before Chromecast and AirPlay
  eligibility; 99/99 tests pass and the reviewed bypass fails two regression
  cases. The review thread is resolved; its x64 package passed as 1.0.0.4.
  User-requested `4c346af8` shortens the label to Forget and uses the same footer
  row as Cast. Its x64 package passed, was registered in place as 1.0.0.5 and
  preserves data/pairings. Computer Use verified running modules and the compact
  row without overlap; no new credential action or receiver playback. Inspect
  Final-head CI passed. The Mac playback evidence below predates these
  follow-ups, with no new receiver claim.
  UI/local-only removal is implemented with target-only deletion, idempotence,
  failure feedback, selection-change protection and pairing/start/cast guards.
  Logic tests pass 96/96 and the VS 2026 MSBuild x64 package passed. Private
  development 1.0.0.3 was registered in place; source manifest restored. Running
  executable/native module hashes match. Computer Use verified Forget → Pair,
  other receiver's paired actions retained, Chromecast/active-cast visibility,
  restored Cast after user re-pairing and local paused handoff. The user confirmed
  normal Mac video/audio, pause/resume and Stop closure. All three CI checks passed
  at `a3d64831`; final-head checks and the user-approved merge are also complete.
  [The artifact](validation/screenbox-forget-macos-windows-2026-10-12.json) records
  exact hashes and limits. Preserve the existing package pin and all other pairings.
  Latest requested refinement `1ba38e55`: group Forget immediately before Cast
  at the bottom right, with 8-DIP button spacing and 12-DIP separation from the
  list. Its x64 package passed and is registered in place as 1.0.0.6, source
  manifest restored. The user answered Yes to the written grouping/list-spacing
  check; longer names were conditional if available, with no exact length recorded.
  This is manual layout confirmation only. **User preference:** for future
  visual UI confirmation, print steps in chat and ask the user to confirm;
  do not use the Computer Use skill. Retain prior observations as historical.
- **D65 (2026-10-12):** user chose consent investigation after reviewing public
  documentation. Completed branch `codex/macos-consent-investigation` began at main
  `671e714`. All eleven desktop CLI casts start/Stop cleanly with saved pairing;
  receiver logs and observer reports are separately attributed in
  [the study](macos-receiver-consent.md) and
  [artifact](validation/native-macos-consent-windows-2026-10-12.json).
  Fresh random per-session identities and a synthetic name comparison retained
  short-term reuse. H/J are log-derived prompt candidates, I/K had no delegate
  sequence, and only the aggregate multiple-prompt report and corrected K=no
  prompt were explicitly observed. Equal 45.078-second idle gaps and the log
  pattern motivate a grant-age hypothesis; J's UI prompt is not individually confirmed.
  Exact duration/key/renewal are unresolved; no shipping C++ change remains.
  PR #42's attribution review is resolved and all 14 checks passed at final
  head `8978188`; it merged as `8a40ec3`. D66 is also complete and merged.
- **Completed slice (2026-10-11):** the user chose documentation reconciliation,
  then bringing D62 to Screenbox. PR #39 merged as `2829242`, final head
  `811c022` passed all 14 checks and its review thread is resolved. The
  historical final validation was on `codex/mac-screenbox-final-validation`
  from `a29212b` (merged PR #40). C API,
  packaged UWP and private Screenbox checks are recorded below. Approved main
  package `ci.207` is now published and publicly hash-verified; fork PR #8
  merged its pins. Running DLL hashes match; final Screenbox receiver checks
  passed by user report. Native PR #41 merged as `671e714` with all 14 checks
  passed at `997b0be`; fork PR #9 merged as `2cee2ea7`. D65/D66 have since completed and merged.
  Follow [HANDOFF.md](HANDOFF.md) section 0 for the current gates.
- **D63:** HLS natural end initially returned `receiver_stop` in both the C API
  and UWP host despite user-observed natural EOF. Native commit `0ad1e36`
  recognizes the Mac's exact stopped-event root `reason: ended`; corrected
  shared C API and UWP checks report `media_end` and pass visually. Static
  35/35 and shared 36/36 CTest passed. Native consent-time cancellation passed.
  The user reports private Screenbox cancellation/recovery, playback controls,
  Stop/local handoff and natural queue advance passed. Development app 1.0.0.1
  is registered with the corrected installed DLL hash; a loaded-DLL hash was
  not captured before the app closed. PR #39 is merged. Main workflow run
  `38107556167` built published `0.3.0-ci.207` at `2829242`; provenance and
  public-download verification passed; `ci.203` lacks D63. D64 below records
  the separate final published-package validation; the earlier private x64
  build is not that release.
  D64 publication: `ci.207` clean provenance, architecture payload hashes,
  licenses and UWP binary checks passed; all 14 main CI jobs passed.
  SHA-256 `e52affdf9f79e20dcd00b59380c9949d1f9b11d1339307f8a2b86ad14e1ffa4d`.
  Screenbox worktree `build-review/screenbox` has prepared pins, 86/86 logic
  tests and a matching x64 MSIX (private test version 1.0.0.2; source manifest
  restored). The matching development layout is registered as 1.0.0.2;
  running native/crypto DLL hashes match the package. The DLLs loaded after
  opening the Cast/device flyout. The user then reported **"All checks passed"**
  for consent-time cancellation/recovery, video/audio near 45 seconds,
  pause/resume, forward/backward seeks, Stop/local paused handoff and natural
  queue advance. This is manual UI evidence for the recorded Mac/Windows x64
  pair without an automated cast log or exact timings.
  The user approved publication; the public and fresh fork-script downloads
  match that hash. [Fork PR #8](https://github.com/ilyalissoboi/Screenbox/pull/8)
  merged as `31013d5e`; test, lint and package-build checks passed at `59936fe`.
  Native PR #40 merged as `a29212b`; all 14 checks passed at `772b850` and both
  review threads are resolved. Final package receiver validation is complete
  for this recorded receiver/host; native PR #41 and fork PR #9 record it.
  PR #39's review follow-up commits explicit EOF atomically before another
  event receive and exits the reader, preserving earlier terminal results.
  Its regression covers prompt channel closure and prior Stop/failure;
  that original follow-up added no new hardware observation. D64's final
  published-package UI observations are recorded separately above.
- **Casting to a Mac (D62)**, merged as PR #37: URL controls when a receiver
  rejects remote control. Start, pause/play, seek, start position, status,
  natural end and stop passed on the user's MacBook Pro from the CLI (seek
  once it named the queue item). D63 adds the C API/UWP and user-observed
  private Screenbox checks; D64 completed the published-package Screenbox gate.
  Open: the per-cast consent explanation. The Mac profile is `macbook-pro`
  in Windows Credential Manager.
- **Screenbox integration: done for its design (2026-10-10).**
  - Library side: HLS delivery (D60, PRs #26-#32), the UWP test host and
    start-position seek (D61, PR #34), and packaging (PR #35, prerelease
    `nuget-v0.3.0-ci.196`).
  - Fork side: PRs #1-#6 in ilyalissoboi/Screenbox, and the cosmetic
    follow-ups in fork PR #7 (blurred overlay, 4-digit PIN dialog, device
    icons and model families).
- The macOS Keychain (D58) and Linux Secret Service (D59) stores are merged
  (PRs #23-#25); remote-Stop detection stays deferred by the user.
- [PR #22](https://github.com/ilyalissoboi/send-airplay2/pull/22), UWP x86/ARM64
  and a certification kit run (D57), was merged by the user as `8ecb8b0`.
- [PR #21](https://github.com/ilyalissoboi/send-airplay2/pull/21), wake before
  play (D56), was merged by the user as `99942d1`.
- [PR #20](https://github.com/ilyalissoboi/send-airplay2/pull/20), the UWP native
  build (D55), was merged by the user as `796a595`.
- [PR #19](https://github.com/ilyalissoboi/send-airplay2/pull/19), the C
  discovery interface (D54), was merged by the user as `ac7717f`.
- [PR #18](https://github.com/ilyalissoboi/send-airplay2/pull/18), the packaged
  UWP test host (D53), was merged by the user as `271d1e0`.
- [PR #17](https://github.com/ilyalissoboi/send-airplay2/pull/17), the C# binding
  (D52), was merged by the user as `623d411`.
- [PR #16](https://github.com/ilyalissoboi/send-airplay2/pull/16), host stores and
  pairing (D50-D51), was merged by the user as `0b65912`. Its real pairing used
  the new profile `living-room-api`.
- [PR #15](https://github.com/ilyalissoboi/send-airplay2/pull/15), the D49 design
  record, was merged by the user as `4c03c0a`.
- [PR #14](https://github.com/ilyalissoboi/send-airplay2/pull/14), the public
  playback interface (D46-D48), was merged by the user as `9c8869e` after all
  ten checks passed at its head `d84a365`.
- [PR #13](https://github.com/ilyalissoboi/send-airplay2/pull/13), the D45 handoff
  reconciliation, was merged at the user's request as `b0b0f86` after all ten
  checks passed at its head `5e0a98e`.
- [PR #12](https://github.com/ilyalissoboi/send-airplay2/pull/12),
  `feat: native URL playback, MRP controls and lifecycle cleanup`, was merged by
  the user on 2026-10-07 as `2bb25df4b4f58ef0a2c6936ed6f44161b815890c`. Its final
  head `923d8efbee066a5a7ce37dd6e83cb891ede58a06` (D44, documentation only) passed
  all ten [CI checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37587291158),
  and its single review thread is resolved. PRs #1-#12 are all merged.
- Latest merged implementation slice: D63, PR #39, final head `811c022`
  (all 14 CI jobs passed), merged as `2829242`. `main` was `a0fd5b9` when the
  validation slice began, after documentation PR #38. Inspect live refs and actual PR-head CI before writing;
  historical D43/D40 hashes below describe those dated results only.
- **Branch workflow (D45):** start each new slice on a fresh descriptive branch
  from current `origin/main` and publish it as its own PR. CI runs only for pull
  requests, so a branch push without a PR has no CI result. Do not reuse the
  merged `claude/url-playback-session` branch; it was deleted locally.
- Routine implementation, validation, commits and pushes to the active slice's
  own PR branch are authorized once the user has asked for that slice. **Ask
  before merging**, before force-pushing, and before closing or replacing a PR.
- The former Codex checkout `C:\Users\Ilya\.codex\worktrees\681a\send-airplay2`
  was detached at `923d8ef` with no changes so its branch could be deleted. The
  main checkout `E:\work\send-airplay2` now tracks `main`. Neither is assumed to be
  an active collaborator; use the new session's actual checkout.

## Review and merge record for PR #12

The automated review of `ab7ec01` produced one inline
[P2 comment](https://github.com/ilyalissoboi/send-airplay2/pull/12#discussion_r4204085497)
about contradictory network-recovery gate status. D44 corrected the README and
required-observations table to report **selected Ethernet interruption cleanup
and fresh explicit same-credential recovery PASS**, with automatic in-session
reconnect/resume unimplemented and broader network/lifecycle reliability pending.
The thread was resolved and all ten checks passed at `923d8ef` before the user
merged. D44 and D45 add no new receiver observation.
For later review comments, compare the claimed issue with source/evidence before
changing code.

## What is implemented

The public interface remains experimental, at API version 3. The C playback,
pairing, credential and discovery interfaces and the C# binding are implemented.
The packaged UWP host and Screenbox fork have receiver-tested Apple TV paths.
JNI and Linux/macOS/Android sender interoperability remain unvalidated.

- Bounded DNS-SD discovery, PIN/SRP provisioning, authenticated HAP peer
  verification, native TCP/control records and Windows Credential Manager storage.
- Boost HTTP GET/HEAD media serving from immutable byte-source callbacks, byte
  ranges and private file-backed CLI sources; receiver access is restricted.
- Independently authenticated URL and remote-control sessions with separate
  secrets, directional keys, record counters and UUIDs. Retain remote control
  through URL teardown; presentation failed without it on the recorded receiver.
- Event parsing/acknowledgments, UDP NTP timing, best-effort feedback, URL SETUP /
  RECORD / type-130 stream setup and four `/command` queue-start operations.
- Bounded in-tree protobuf/MRP framing, handshake, correlation, heartbeat, player
  tracking, status, pause/resume, absolute seek and Stop.
- One lifecycle supervisor: cancellation, automatic terminal/failure cleanup,
  first end reason and joined teardown. The host stops media serving after session
  cleanup. `stop()` is idempotent; command input cannot hold terminal cleanup open.
- Normal `cast` defaults to **16 bounded media connections**, override 1..16;
  generic server/`serve` defaults remain four. MRP is enabled by default.
- Startup requires **one continuous second** of eligible URL playing within the
  original deadline (normally 30 seconds), reset on loading/pause/zero/reverse-rate
  states. There is no automatic Play/seek retry; confirmation is telemetry only.
- Bounded opt-in startup, HTTP completion, buffering, fixed-label event and final
  received MRP diagnostics. Fixed URL state/type labels use `other` for unknown
  strings; peer targets/unknown keys/arbitrary descriptions do not enter output.
- **D47, receiver-tested in D48:** at teardown, an in-flight remote `/feedback`
  request is waited for (bounded by its deadline) instead of cancelled, so
  remote control outlives the URL session.
- **D46, receiver-tested in D48:** experimental C interface `playback.h` (API
  version 1). One `sap2_cast` handle owns the media server and session, loads
  credentials by profile, takes host `read_at` callbacks, and offers blocking
  start, cancelling stop, polled/waited status and MRP commands. D48 passed its
  seven manual checks and ten cycles on one Apple TV 4K / tvOS 26.6 / Windows 11.
- **D48 also passed, for one selected file:** a seek past 4 GiB (37 range
  requests above 4 GiB, user-observed picture) and E-AC-3 audio, via `cast`.
- **D50-D59:** host credential stores and pairing, C# and discovery bindings,
  UWP builds and Windows/macOS/Linux built-in credential stores are merged.
- **D60-D61:** API version 3 adds HLS remux delivery; MP4/MKV supported codecs,
  text subtitle renditions, indexed MKV startup and post-start position seeking
  passed their recorded Apple TV checks. See [hls.md](hls.md).
- **D62:** rejected remote-control SETUP can fall back to URL controls;
  start, status, pause/play, seek, start position, natural end and stop passed
  from the CLI on the recorded Mac. The active slice extends host validation.

D43 review fixes are in `d1011ab`, `880feb3` and `ab7ec01`: decoded MRP extension
payloads have move-only erasing owners; event bodies are erased if acknowledgment
fails before ownership reaches the caller; URL bodies are protected while
constructing diagnostics. The contract does not erase every metadata copy in
generic protobuf/plist trees. The silent-feedback test now arms its fault after
startup; its 80 ms request deadline and 500 ms cleanup assertion remain intact.
See [pr-review.md](pr-review.md) for reproduction and exact scope.

## Validation evidence and its limits

Current D62 receiver evidence and its limits are in
[receiver-validation.md](receiver-validation.md#macos-airplay-receiver-url-controls-d62-2026-10-10).
D62's final PR head `ed24ae4` passed all 14 CI jobs, including desktop
static/shared checks, sanitizers, UWP architecture builds and packaging.
Those checks do not establish receiver interoperability for additional hosts.

Historical PR #12 validation: at `ab7ec01`, Windows Release static/shared CTest passed **24/24 each**
(15.40/15.43 seconds), offline runner contracts **10/10**, and the session test
passed five repetitions under concurrent local load. All **52 PR-changed C++
files** passed clang-format dry-run; whitespace checks passed. All ten
[CI checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37584220175)
passed: Windows/Linux/macOS static/shared, ASan/UBSan and three runner jobs.
Earlier macOS shared CI exposed the feedback-fixture scheduling race; it passed
after the test-only correction. D44 adds documentation; inspect its own CI.
D46 local checks are in [public-api.md](public-api.md#validation); inspect the
slice PR's actual-head CI.

Hardware evidence is for **Apple TV 4K (`AppleTV14,1`) / tvOS 26.6 (`23L773`) /
Windows 11 x64**, with firmware assumed unchanged where a later run did not query
it. Selected G1/G2/G3 results do not establish other receivers, firmware or hosts.
The user observed normal video/audio, controls in both seek directions, sender
Stop/Home, full-clip natural EOF/Home, and selected sleep/wake and network recovery.

Historical D40 receiver-tested code head:
`4a18b2662150768a00154ca17e222f2e701fd192`. D42 reused that runtime; D43/D44 have
**no new receiver observation**. D42 CLI SHA-256:
`da3f85a3f4b8f0595834b0462250859ee56c3bab89c38acdd632434d889f6e83`.

| Evidence | Result and important limit |
| --- | --- |
| [D39 manual batch](manual-validation.md) / [artifact](validation/native-manual-batch-windows-static-2026-10-07.json) | Selected startup, controls, full EOF/Home and sleep/wake recovery passed; remote Stop/Home cleaned up but retained connection_lost/exit 1. |
| [D40 Stop comparison](validation/native-stop-diagnostics-windows-static-2026-10-07.json) | Repeated observed Stop/Home ended on URL event EOF without a terminal state. Sleep had paused URL/final owned MRP. The first attempt was unobserved. |
| [D41 pyatv audit](pyatv-stop-reference.md) / [artifact](validation/pyatv-stop-source-audit-2026-10-07.json) | Upstream polling swallows connection failure; the tvOS fork's event waiter does not finish on event EOF alone. Neither gives a validated Stop discriminator. Reference source only, no copied implementation. |
| [D42 network artifact](validation/native-network-recovery-windows-static-2026-10-07.json) | User-confirmed Ethernet removal caused URL feedback timeout and automatic joined connection_lost/exit 1 cleanup with stdin open. After reconnection/Home, a fresh same-credential cast reached owned EOF/media_end/exit 0 with observed video/audio/Home. Pre-interruption presentation was asked about but not reported; action times were not synchronized. Recovery used a planned near-end seek, adding no full-clip proof. |

The test media is the user's `E:\work\gas.mp4`: 53,953,926 bytes, about 131.6 s,
H.264/AAC MP4; SHA-256
`a91fb5c781f4a6ecc90b780dc77793403b6aac7220af7c899ba3b217129b5e65`.
Do not commit the media. PC playback past 18 seconds was user-confirmed normal.

## Open issues and ordered next work

The current queue follows [D99's active slice](screenbox-audio-files.md):

1. **D97 complete, PR #80 merged.** Merge `377b7ce` includes all 17 passing
   final-head checks at `b105333`. No further native listening check is pending.
   D94-D96 are complete in merged #79; do not repeat pin/publication work.
2. **D98 packaged gate complete; PR #81 merged as `45f39be`.** Implementation
   `153a840`, codec-free x64 MSIX 0.1.30.0, passes brokered progressive AAC/M4A
   audible controls/natural end/separate Stop on both recorded receivers and
   Mac pending-consent cancellation. Actual AppContainer/executable/two loaded
   native hashes match before/after; all eight source instances release once.
   Retain the initial timeout and two Mac connection failures alongside the
   successful retry; no further D98 manual check is pending. All 17 checks pass
   at final head `0c16c4a`; merge/CI adds no new receiver observation.
3. **Active authorized D99 slice: Screenbox AAC/M4A routing/controls.** Fork
   #16 implementation `aff57b6f` passes 122/137 logic tests and both actual MSIX/
   pinned-payload checks. Development 1.0.0.13 is installed in place. Verify
   loaded runtime after the user opens M4A/Cast, then coordinate written
   Mac/Apple TV audio/controls/cancellation/queue checks.
   Use the current slice record and preserve exact bytes and failed attempts.
4. **Later proposal: automatic audio-only conversion.** Reuse decoder/downmix/
   AAC components with an audio-only timeline/output and validated standalone
   container support. This is separate from D99 and is not implemented here.

No new publication starts here. Preserve speaker/discrete-surround/Chromecast
hardware deferrals and user-deferred remote-Stop classification. Long-input/
cache-quota evaluation is separate; the existing quota is unchanged.

The implementation history below is reference, not the active queue. C API/UWP
and private Screenbox checks are recorded in completed D63; D64 published the
approved main prerelease and merged its fork pins. Running DLL hashes match,
and final-package receiver checks passed by user report; final evidence PRs
are merged. D65's initial Mac consent investigation is complete and its evidence
is merged in PR #42. D66's Forget-device feature is implemented and build/UI
validated in the fork, merged in PR #10. Research PR #43 and fork PR #11 are
also merged. D67's stereo AAC plan and review are complete in native PR #44,
with the companion fork checkpoint merged in PR #12. The subsequently authorized
D68 codec/packaging spike is implemented and merged in native PR #46 as
`cb9d641`, with all 16 final-head checks passing. D69 merged the first native
source increment described above. D70's selected-stream/seek-window adapter is
merged in PR #50. D71 implements separate stereo AAC fMP4/HLS resources and
bounded lazy generation/cache in its recorded private offline scope.
Public API/binding/CLI and receiver/Screenbox gates follow the plan. The spike's
single-stream/short-input probe is not a production movie source adapter.
Speaker audio and surround-preserving output remain deferred for unavailable
test hardware. D72 now checks private long-GOP splitting, terminal failure
reporting and synthetic cold-seek scaling. D73 authoring is merged with explicit
estimate limitations. D74 playback/public/storage and D75's x64 AppContainer
runtime/storage gate are merged. D77 implements the expanded private decoder
subset; D79 supplies bounded long-file PCM indexing and D80 adds compressed
duration/restart coordinates. D81 replaces full packet tables with sparse restart
anchors and validates the recorded high-count synthetic subset. D82 adds private
selected-copy eligibility and automatic routing; D83 implements API 5 public
reporting, offline checking and opt-in binding/CLI integration. D84 passes expanded
packaged runtime/storage checks. D85 supports bounded nonzero Matroska origins.
D86 corrects canonical DTS lace clocks with owned PCM/AAC and discontinuity
controls plus bundled/folder-granted 137-case matrices in package 0.1.0.7.
D87 corrects the subsequent demuxer index-extension scan using bounded existing
video cue coordinates and maximum-lace backoff. Owned PCM/EOF/refusal controls,
both 137-case matrices and three independent brokered first/middle/last fragment
samples pass in package 0.1.0.8 at `731db15`, with token/loaded hashes verified.
Cold timings are 138/314/64 ms with byte-identical cached repeats and cleanup;
this is bounded one-input evidence, not sustained whole-film throughput. D88
now records native AAC/DTS and packaged nine-format playback on the recorded
Mac/Apple TV, plus applicable Stop/cancellation and host-slot reuse observations.
See its explicit firmware, failed-attempt and short-media limits above; this is
not Screenbox queue or unrestricted reliability proof. D89 packaging is now merged
in PR #70 (`e6acba1`), with all 17 final-head checks passed. D90 published the
approved clean-main `0.5.0-ci.282` opt-in prerelease: all 17 main checks and local
artifact/consumer gates pass, and the unauthenticated public download matches.
The user approved that slice: D91 implements it in merged fork PR #13; native
checkpoint #73 is merged too. D92's HEVC HDR startup correction is merged in
PR #74 as `f1598cf`, with all 17 final-head checks passed at `1e917aa`.
Passing Mac controls and Apple TV 90-second retries remain separate from the
earlier Apple TV end. Later user testing suggests incorrect HDR signaling as the
cause, with no recurrence since the fix. Final Stop/cache cleanup is confirmed.
D93 completes corrected candidate preparation and approved publication, including
independent public-download verification. D94 completes the Screenbox opt-in
ci.294 update in merged fork #14 and native #78, with final-head CI and bounded
Mac/Apple TV controls passed. Do not repeat publication or this host gate.
D95's Casting group/icon is merged in fork #15 and manually confirmed. D96
publishes the authorized separate-identity x64 preview, with public-download,
installer and launch checks passed; native checkpoint #79 is merged. D97 now
records native audio-only AAC/M4A success on both receivers. The next proposed
gate is packaged UWP brokered AAC/M4A, then Screenbox audio-file routing, in the
order above. Sustained long-input/cache-quota behavior remains a separate
proposal; do not widen the existing budget as part of this probe.
The 1.0.0.9 setting/default, loaded hashes/AppContainer, Mac consent cancellation,
copied/converted controls, refusal, natural/rapid queue and cleanup checks pass;
the user restores conversion off/local paused. GitHub Actions is enabled. Do not
restart implementation, publication or completed manual checks.
Preserve the
OFF-by-default/publication boundaries. Preserve the earlier Apple TV attempts
as history alongside the user's later HDR-related attribution and no recurrence;
do not treat the symptom as a separately recurring defect.
Grant-timer/key research, other fork follow-ups, MRP track/queue research, HDR and growing
HLS, nuget.org publishing, and additional host/receiver support remain choices.
Remote-Stop classification stays deferred by the user. The following PR #12
follow-up record is historical; completed items are not a new work queue.

1. **Done (merged in PR #14): versioned public playback interface (D46).** The user chose
   credentials by profile name (secrets stay inside the library) and a
   playback-only first slice. `include/send_airplay2/playback.h` and its
   controller are implemented; see [public-api.md](public-api.md). The
   development host `airplay2-api-host` drives only that interface. Its manual
   validation plan **passed on 2026-10-08 (D48)**, on the D47 runtime, static and
   shared, with the user observing; see receiver-validation.md. Follow-up work
   is item 6. The fake receiver is now shared
   (`tests/fake_receiver.h`) and has an optional MRP data stream, so the
   controller's commands, ends and ownership loss are unit-tested. That work
   found a teardown ordering bug, now fixed (D47): an in-flight remote
   `/feedback` request is waited for at stop instead of cancelled. D47 changes
   the session runtime shared with `cast`; D48 is the first hardware batch on
   it (including one `cast` CLI run). Related hardware-free work: a
   synthetic beyond-4-GiB byte-source/range test through the media server, and
   aggregate connection/request counts in `serve`'s summary (HANDOFF section 7).
2. **Startup reliability.** D56 reproduced first-frame-then-Home when casting to
   a sleeping receiver and fixed that case by waking it over MRP before play
   (three sleep cycles: CLI, C interface static and shared). The user reported that the earlier intermittent pause was observed
   while the Apple TV was waking from sleep, so D56 is its likely cause and fix;
   watch for any recurrence from an awake receiver. Historical notes, which follow:
   spontaneous pause at zero/first-frame-only occurred
   without remote input, including a 16-slot run. One-second confirmation improves
   readiness reporting but does not explain or cure the receiver transition.
   If it recurs, retain failed-start traces/cleanup and compare normal MRP versus
   `--minimal-remote` from matched observed Home/idle conditions, varying order and
   time since prior teardown. Earlier alternating trials confounded those factors.
   Do not add an arbitrary Play retry or extend deadlines without an explicit
   engineering decision and new tests/evidence.
3. **Remote Stop semantics and longer lifecycle reliability.** Stop/Home may
   close URL events while last state is playing and report connection_lost/exit 1.
   Sleep and network interruption also cause connection failure. Keep conservative
   classification; pause, ownership loss and EOF from an independent channel do
   not by themselves establish receiver intent. Investigate a discriminating
   protocol signal or propose a separate API termination policy, labeling it as
   a policy choice rather than discovered intent; the policy itself needs a user
   decision before it changes public end reasons. Broaden interruption/idle/long
   playback checks with explicit observer checkpoints and current fingerprints.
4. **Frozen-video priority (user decision D37).** The historical buffering pause
   near 18 s followed by frozen video/normal audio remains a recorded FAIL.
   Insufficient media connections is the likely cause, with later 16-slot passes,
   not proven causality. Keep low priority **unless it recurs**, then reopen active
   investigation. Do not relabel the original run PASS.
5. **Other standalone gates.** Authentication after real host/receiver restart,
   wrong PIN/revocation, opted-in disposable profile deletion, departure/interface
   changes and neutral sender identity (D30). A selected actual-file seek
   beyond 4 GiB passed in D48; broader files/hosts remain untested.
   The selected D42 network check is already PASS; broaden it rather than claiming
   it has never run. Cooperative duration-based ownership can misidentify a
   concurrent same-duration takeover and does not guarantee exclusive ownership.
6. **Public library/host work, next implementation (D49 order).** Follow
   [credential-interface.md](credential-interface.md): (a) **done in D50:**
   host-store callbacks and pairing in the C interface, with test stores on all
   CI platforms, and one real pairing into the built-in store (D51); (b) the C#
   binding is **done in D52**, and the packaged UWP test app with a C#
   `PasswordVault` store is **done in D53** (native loading, PasswordVault
   pairing, StorageFile serving, casts; Screenbox's capability works on Private
   networks only), and receiver discovery through C and C# is **done in D54**
   (found inside the AppContainer on a Private network); (c) the UWP library
   build without the Credential Manager adapter, against the app C runtime, is
   **done in D55** (Botan static through an overlay port; D57 added x86, ARM64 in CI
   and a certification kit run; formerly WACK and x86/ARM64 not
   run); (d) **done in D58-D59:** macOS Keychain and Linux Secret Service
   adapters. Linux/macOS/Android sender device support remains. Botan UWP packaging is
   resolved for x64 by the D55 overlay port; desktop/CI success is not
   packaged-host proof.
7. **Screenbox integration:** the historical PR #12 queue has since completed
   through fork PR #10, including the Mac package and Forget follow-up.
   Re-read its current instructions before changes. Chromecast video regression
   is deferred for unavailable hardware; sleeping Apple TV casting is already
   verified by user report. Additional architecture/receiver checks remain.

Settled choices: original Apache-2.0 code; in-tree plist/protobuf codecs;
synchronous session threads (D28); MRP controls (D29); configurable reference
identity first (D30); bounded normal cast admission (D36). Standalone audio,
DRM, mirroring, multiroom and transcoding remain outside the first MP4 slice.

## Local tools, checks and execution constraints

Build directories `build-static` and `build-shared` are per checkout, using
Visual Studio 18 2026/MSVC and the pinned vcpkg toolchain
`E:\work\send-airplay2\build-tools\vcpkg\scripts\buildsystems\vcpkg.cmake`.
Check `CMAKE_HOME_DIRECTORY` in `CMakeCache.txt` before reusing a build
directory in another checkout; use README's normal
CMake configure instructions on another machine. Preserve dependency/license
provenance before adding third-party code or dependencies.

Current tool paths:

```powershell
$cmakeTool = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctestTool = 'C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
$formatTool = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin/clang-format.exe'
# Use a real interpreter file. The Microsoft Store `python` alias under WindowsApps
# cannot be opened for hashing, so three runner contracts fail with exit 2 there.
$pythonTool = 'C:/Users/Ilya/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$githubTool = 'C:/Program Files/GitHub CLI/gh.exe'

git fetch --prune origin
git status --short
git branch --show-current
git rev-parse HEAD origin/main
& $githubTool pr list --repo ilyalissoboi/send-airplay2 --state open
# After opening the slice's PR:
# & $githubTool pr view <number> --repo ilyalissoboi/send-airplay2 --json headRefOid,isDraft,state,statusCheckRollup

& $cmakeTool --build build-static --config Release
& $ctestTool --test-dir build-static -C Release --output-on-failure
& $cmakeTool --build build-shared --config Release
& $ctestTool --test-dir build-shared -C Release --output-on-failure
& $pythonTool tests/e2e_runner_tests.py
git diff --check
# Format touched C++ files, then verify them with:
# & $formatTool --dry-run --Werror <explicit touched files>
```

Use `.clang-format` without sorting platform-sensitive includes. Apply C++17,
focused responsibilities, descriptive names, direct includes, RAII/deleted-copy
ownership guards, callable invariants and useful non-secret failure context as
required by AGENTS.md. Retain independent known-answer bytes and literal bounds.
After an implementation change, run applicable static/shared checks and inspect
CI at the actual head. A docs-only correction needs whitespace/link/status checks
and actual-head CI; repeating receiver tests or rebuilding unchanged source adds
no new receiver evidence.

Local sandbox restrictions can block loopback UDP and runner temporary files;
the same tests passed when run outside the sandbox. Git mutations/shared worktree
metadata and GitHub network calls have also needed escalation. A sandbox access
error is not proof of bad credentials, firewall configuration or source failure.
Do not change host network/firewall settings to make these local tests pass.

Ignored `build-review/` contains local build/test/CI logs, historical hardware
drivers, source-audit files and PR body files. They are optional local aids and
may not exist in a fresh checkout. The committed sanitized validation artifacts
are the portable evidence. Review a historical driver's staging paths and output
filters before reusing it with a new binary; do not blindly run it or publish its
raw output. Previously staged allowed executables were restored with hash checks.

No hardware cast or implementation automation was left running when this slice
began. Inspect live PR/CI status in a new session. The user confirmed availability
to observe the Mac during this slice; later sessions must check availability again.
Batch manual checkpoints if they are unavailable; unattended telemetry never
substitutes for visual video/audio/Home observations.

Never read raw credential files or print/store PINs, private media URLs, receiver
addresses/MACs/identifiers or Windows Credential Manager contents. Reuse stored
credentials through the native APIs. Do not delete/re-pair credentials or alter
receiver/host settings as an incidental diagnostic step. All hardware results need
exact code/binary, host/firmware and observer scope recorded separately from CI.

## Suggested opening prompt for the next session

> Continue from AGENTS.md, HANDOFF.md, design.md, receiver-validation.md and
> CONTINUATION.md. D99 Screenbox AAC/M4A is authorized and in progress; read
> screenbox-audio-files.md and actual fork PR #16 head/review/CI before editing.
> Fork implementation aff57b6f from merged 7cea7023 routes local M4A per item to
> progressive audio/mp4 in both profiles, bypassing conversion/cache regardless
> of stereo permission. Video policy/pins/native API remain unchanged. Both
> logic suites pass 122/137; both actual MSIX/pinned payload checks pass.
> Development 1.0.0.13 is installed in place with identity/data preserved.
> Loaded-runtime and written receiver/queue checks are pending. The user confirms both receivers available
> with macOS 26.7.1/tvOS 26.6 unchanged. Inspect current fork instructions before
> editing; use VS 2026 MSBuild, never dotnet build. Preserve identity/data/grants.
> Use written user observations, not Computer Use, for visual UI confirmation.
> D97/D98 and merged native #82 (afb9ee8; 17 checks passed at head 481ab01) are
> complete; do not repeat those checks or the published preview release.
> Automatic audio-only conversion is a distinct later slice, not part of D99.
> Keep speaker/surround/Chromecast and remote-Stop deferrals, existing quota and
> privacy boundaries. Ask before merging or publishing; no release is part of D99.
