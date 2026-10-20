# Receiver validation record

**D112 opening-item review follow-up verified locally (2026-10-20).**
The [Screenbox #17 review](https://github.com/ilyalissoboi/Screenbox/pull/17#discussion_r4324921353)
found that Cast can be requested before VLC inserts the current item's tracks.
Fix `23f708e8` shares bounded, cancellable metadata resolution between current and
queued items, validates completion before the track snapshot and rechecks the
request/player/item before local pause or native start. Independent descriptors
are disposed; current display size is preserved with an opening-video fallback.
Logic suites pass 142/176 ordinary/opt-in, including thirteen race/refusal cases.
Both actual Release MSIX/payload checks pass (2/40 files). Read the
[separate review evidence](validation/screenbox-api6-review-windows-2026-10-20.json).
The 1.0.0.15 receiver gate below retains its original hashes; no new app is
installed and no receiver/cancellation/codec matrix is repeated or newly claimed.
Inspect actual final heads/CI of fork #17 and native #97; ask before merging.

**D112 Screenbox API-6 focused gate passed (2026-10-20).**
Native #96 merged as `b57981b`; all 17 checks passed at `98b4dc8`. The authorized
[Screenbox PR #17](https://github.com/ilyalissoboi/Screenbox/pull/17), implementation
`59d83910`, pins public opt-in `0.6.0-ci.330` and independently selects the audio
or video profile from each current/queued item's parsed tracks. Logic suites
pass 142/163 ordinary/opt-in; both actual Release MSIX/payload checks pass (2/40
files). Development 1.0.0.15 preserves identity/data; four actual package,
AppContainer, executable and six-DLL captures match the inspected ci.330 layout.
The user confirms MP3 controls/Stop with conversion off on both receivers, the
Mac FLAC refusal with conversion off, converted-FLAC controls/Stop on both, and
natural MP3 → converted PCM MOV audio → video → AAC queues on both. Rapid Next
reaches the third video; transition Stop closes playback with no delayed restart,
local paused and app responsive. macOS 26.7.1/tvOS 26.6 remain unchanged.
Conversion is restored OFF; the app stays open/paused and final owned cache
directories/files are zero without cleanup mutation. No further manual check is
pending. [Native evidence PR #97](https://github.com/ilyalissoboi/send-airplay2/pull/97)
and [the exact scope/record](screenbox-automatic-audio-only.md) separate local
binary observations from actual PR-head CI; inspect final heads before merge.
Ordinary ci.207, native/codec behavior, quota, publication and all deferrals
remain unchanged. No unchanged consent/full-codec matrix was repeated. Review
both PRs and ask before merging; no new release is created by this gate.

**D111 API 6 prerelease published and public downloads verified (2026-10-20).**
After #95 merged as `a7f8202` (all 17 checks at `c2d1526`), the user explicitly
approved publishing the exact D110 candidate. [SendAirPlay2.StereoAac 0.6.0-ci.330](https://github.com/ilyalissoboi/send-airplay2/releases/tag/nuget-stereo-aac-v0.6.0-ci.330)
is now a GitHub prerelease. Its tag resolves to the original build source
`746c520e17d812102a14ba4edae47a81239c4c81`, not the later documentation merge.
Fresh unauthenticated public downloads of the unchanged 21,355,553-byte package
and checksum match the reviewed hashes; the package SHA-256 is
`9567655c5c4f0183fddeaea9f497d869d28b79036d599cbf5d87f241e74eb3ad`.
Read [publication evidence and next work](audio-only-package-publication.md).
Publication is complete and authorized; do not repeat it or ask again. No
Screenbox pin/installation or new receiver observation is included. Next is a
separate Screenbox API-6 pin, explicit per-item profile routing and focused queue
checks, after reading that repository's instructions. Reuse unchanged lifecycle
and codec evidence. All hardware/user deferrals and quota remain unchanged.

**D110 preparation history (2026-10-20).**
Clean-main [run 39399818931](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39399818931)
from `746c520` passed all 17 checks. Independent source/six-DLL/UWP distribution/
pinned-source/22-overlay/license and fresh API-6 consumer/copy/refusal audits
passed for the exact ci.330 bytes. Same-run ordinary assets remain comparison
only. [Preparation evidence](audio-only-package-candidate.md) remains historical;
D111 above records the subsequently approved publication and public verification.

**D109 API 6 package readiness passed locally (2026-10-20).**
The user authorizes package preparation after #93 merged as `ee968e9`; all 17
checks passed at final head `f31227b`. [The readiness record](audio-only-package-readiness.md)
covers implementation `04a3559`: both new package bases `0.6.0`, finite audio-only
shipping descriptions and actual restored API-6 consumer coverage. Local clean-
branch packages pass thirteen profile controls, API-6 compile/six-DLL copy,
RID/direct/transitive refusals, binding CTest and static source/license audit.
The unchanged API-5 package fails the strengthened compile check as expected.
These local x64 archives are validation inputs; no clean-main candidate or
release is prepared/published yet. After review/merge, dispatch clean main,
audit its exact artifact and prepare notes/checksum before publication approval.
Screenbox pin/routing/queues follow separately. Existing published assets/pins,
native/codec behavior, quota and all hardware/user deferrals remain unchanged.

**D108 focused packaged automatic audio-only gate passed (2026-10-20).**
[PR #93](https://github.com/ilyalissoboi/send-airplay2/pull/93), implementation
`c868572`, records thirteen public preparation/cleanup checks over twelve owned,
hash-verified brokered inputs plus three pre-file script refusals. Final enabled
Native AOT UWP host 0.1.36.0 updates in place, preserving data/pairings/grants.
Three actual AppContainer/executable/six-DLL captures match the inspected package.
The user confirms converted-FLAC controls/seeks/natural end on Mac/macOS 26.7.1
and Apple TV/tvOS 26.6, plus one Apple TV Stop without delayed restart; versions
are unchanged. All sixteen offline/cast sources and stores release once with
expected clear/removal/sibling behavior. Final cache children/sentinels are zero.
Enabled UWP build/binary checks, zero-warning MSIX build and C# binding CTest pass.
Inspect PR #93 final-head CI separately. Read [the evidence and limits](automatic-audio-only-uwp-gate.md).
No further D108 manual check is pending; earlier lifecycle/format evidence was
reused, with no cancellation or full listening-matrix repeat. After review/merge,
Screenbox routing/queues and package/pin prerequisites are separate next work.
Native/API/codec, Screenbox, quota, publication and all deferrals stay unchanged.

**D107 native automatic audio-only receiver gate passed (2026-10-20).**
[PR #92](https://github.com/ilyalissoboi/send-airplay2/pull/92) records 24 natural-end
checks and two representative converted-FLAC Stops, confirmed by written user
observation on MacBook Pro/macOS 26.7.1 and Apple TV/tvOS 26.6. The user resumed
the paused run; only the 19 missing format/timeline checks were completed. Native
source remains merged `d378249`; test checkpoints `4cd545a` and `a3ff5bd` used
identical verified host/seven-DLL hashes. All 30 attempts, including four retained
failed/nonqualifying attempts, released their source once with zero failed reads
and empty post-run cache. The final test-host process/cache counts are zero.
The original WAVE rejection and three nonqualifying consent attempts remain
explicit; they are not passes. No fresh pending-consent cancellation is claimed.
The user questioned redundant cancellation checks: reuse unchanged lifecycle
evidence, repeating manual cancellation only for relevant code changes or a
concrete defect. Read [the complete scope and evidence](automatic-audio-only-native-gate.md).
The prepared head `4cd545a` passed all 17 CI checks; inspect the final PR head
separately. Next, after review/merge, validate packaged automatic audio-only HLS
and brokered sources, then Screenbox routing/queues. No runtime/API, codec,
package, Screenbox, quota or publication change is included. Speaker/RAOP,
discrete-surround, Chromecast, remote-Stop and long-input deferrals remain.

**D106 explicit automatic audio-only offline gate complete (2026-10-19).**
Native #90 merged as `06e094c`; all 17 checks passed at final head `fec9448` in
[run 39209440573](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39209440573).
The user authorizes the next slice. API 6 adds the explicit Apple Audio v1 profile,
progressive action/reason and MP3 family, preserving structure sizes, older
prefixes/defaults and the video profile. Content-based finite admission serves
original stereo AAC/M4A/MP3 progressively or converts covered FLAC/PCM/Ogg
Opus/Vorbis through the existing audio-only AAC owner with stereo permission,
backend and bounded host storage. Public check/cast reporting, CLI and C# binding
are connected. Read [the exact subset and remaining gates](automatic-audio-only.md).
Implementation 46321d0 passes static/shared/enabled suites 43/44/45 and codec 17;
strict Clang, formatting and both x64 UWP compile/binary gates pass. Initial
x86 UWP CI exposed a narrowing warning in the now-common FLAC probe; 0245f49
uses its proven 1..8 channel bound for an explicit index conversion. Strict x64/
i686 Clang and repeated static/shared suites 43/44 pass. Inspect PR #91 final-head
CI separately. There is no new receiver/app observation, codec-profile,
package, Screenbox, quota or publication change. Native/packaged generated audio-only
HLS receiver gates follow review/merge before Screenbox routing/queues. All
hardware/user deferrals remain. Continue `codex/automatic-audio-only-delivery`
while its PR is open and inspect its actual head before writing.

**D105 private edited/container audio-only offline gate complete (2026-10-19).**
Native #89 merged as e4d1a28; all 17 checks passed at final head 3e59f9c in
[run 39204589746](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39204589746).
The user authorizes the next slice. Private FLAC/PCM admission now uses checked
finite MP4 edits or zero-based contiguous Matroska packet clocks/trim. Edited
MP4 windows clip at the intentional presentation boundary; video/default decoder
behavior stays separate. Owned offline tests cover source phase, endpoint,
seeks and refusal/cleanup, including a fractional 44.1-kHz edit offset. Static/
shared CTests pass 43/44 and codec CTests 17. Both UWP builds/binary checks,
strict Clang and formatting pass. Implementation 6b81756 adds no public policy/
API, codec-profile, package, Screenbox or receiver changes. Read
[the scope and remaining gates](audio-only-container-timelines.md).

**D104 packaged MP3 gate passed (2026-10-19).** Native #88 merged as
be5ae92, with all 17 checks passed at final head 552ebca in
[run 39201242905](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39201242905).
The user authorizes the next gate and confirms both receivers available with
macOS 26.7.1/tvOS 26.6 unchanged. Test-host implementation b098646 adds
progressive audio/mpeg options and MP3 picking. Codec-free x64 MSIX 0.1.32.0
updates in place with identity/data/grants preserved. Build/binary checks and
negative MIME/HLS scripts pass. Actual AppContainer/executable/two native hashes
match the inspected package in five before/after captures. The user confirms
CBR/VBR nonzero start, controls, natural end and separate Stop on both receivers,
plus Mac consent cancellation. All nine brokered sources release exactly once.
No further D104 manual check is pending; no AAC fallback is demonstrated.
Native/public policy, codec profile, Screenbox and publication stay unchanged.
Read [the scoped evidence and next work](mp3-uwp-gate.md). Preserve all deferrals.

**D103 private MP3 inventory/native direct-playback gate complete (2026-10-19).**
Native #87 merged as `e6b0a4b`; all 17 checks passed at final head `13602e0` in
[run 39182522426](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39182522426).
The user authorizes MP3 work and confirms both receivers available with macOS
26.7.1/tvOS 26.6 unchanged. A separate private MP3 framing/timeline inventory and
offline tool cover bounded MPEG-1/2/2.5 CBR/VBR, metadata, sparse locators and
checked gapless fields. Default/public probes, AAC/automatic routing, codec
profile, packages and Screenbox are unchanged. Static/shared CTests pass 43/44;
UWP native compilation passes. The user confirms native CBR/VBR controls, natural
end and separate Stop on both receivers, plus Mac consent cancellation. All nine
source instances release once; actual loaded executable/crypto hashes match.
Direct progressive audio/mpeg works in this tested subset; no AAC fallback is
justified by this gate. Packaged brokered-source MP3 verification is next.
Read [the scope, evidence and decision](mp3-input-probe.md). Preserve all deferrals.

**D102 standalone Ogg Opus/Vorbis private slice (2026-10-19).** Native #86
is merged as `7d4ac4c`, with all 17 checks passed at final head `9ee6cc8` in
[run 39178182511](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39178182511).
The user authorizes the next input slice. A bounded native Ogg inventory checks
page CRC, lacing, one stream, sequence/continuation and EOS; granules derive the
audible clock, with Opus pre-skip and final trim. The private audio-only AAC owner
reuses decoded tail/coverage checks and restores initial skip state on near-start
seeks. The 64-MiB full scan is an initial subset, not large-file scaling proof;
chained/grouped inputs refuse. FFmpeg 9.0.2 port version 6 adds only Ogg demuxing,
with unchanged source/license and existing Opus/Vorbis decoders. Owned offline
checks cover PCM seek/origin, exact AAC trim, malformed inputs and cleanup.
Read [the subset and validation record](standalone-ogg-audio.md). Public/default
inventory and automatic routing, packages, Screenbox and receiver evidence stay
separate. MP3 input/direct playback/fallback is next; all hardware/user deferrals
and publication boundaries remain unchanged.

**D101 standalone FLAC/WAVE private input slice (2026-10-19).** Native #85
is merged as `23a030f`, with all 17 final-head checks passed at `d0392a2` in
[run 39130131090](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39130131090).
The user authorizes the next input slice. A private content probe admits bounded
native FLAC and RIFF/RF64 WAVE, supplies source identity/sample count/layout,
and drives the existing audio-only AAC owner. Tail/EOF and seek-coverage checks
refuse inconsistent duration or truncation. The pinned FFmpeg 9.0.2 overlay adds
only FLAC demuxing (port version 5); WAV was already enabled, and source/license
provenance is recorded. Existing public/default video probes, automatic policy,
packages and Screenbox do not gain standalone admission. Owned offline tests
cover decoded output, native malformed-input bounds and sparse five-GiB RF64;
no new receiver gate is claimed. Static/shared CTests pass 41/42 and codec
CTests 16; final focused checks and UWP compilation/binary inspection pass.
Read [the subset and next gates](standalone-flac-wave.md).
Ogg Opus/Vorbis and the user-requested MP3 input/direct-playback/fallback work
remain planned. No release or hardware/user deferral changes.

**D100 private audio-only AAC foundation (2026-10-18).** After native #84
merged as `b9af011` (all 17 checks passed at `64c3595` in
[run 39124033437](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39124033437)),
the user authorizes the next automatic audio-only conversion slice. A private
`StereoAacHls::open_audio_only` entry now derives a 48-kHz timeline from the
selected unedited progressive MP4/MOV packet index, admits the covered FLAC/PCM
subset without video tracks, and authors one audio-only AAC HLS variant. It
reuses bounded decoding, stereo downmix/encoding, lazy host storage and joined
Stop cleanup. Public API/automatic routing, packages and Screenbox are unchanged.
Standalone FLAC/Ogg/WAV, edited timelines and Matroska audio-only duration are
not admitted by this slice. Offline tests cover owned 48/44.1-kHz FLAC and 5.1
PCM, final trim, cold/backward reads, selection, cancellation, cache failure and
cleanup. Static/shared suites pass 40/41; the codec suite passes 15 and UWP
private-library compilation passes. This is offline evidence, not a new receiver
result. See [the scope and ordered follow-ups](audio-only-aac-foundation.md).
The user adds MP3 input support, direct-playback validation on Mac/Apple TV and
a conditional AAC fallback decision to the work plan. These are planned only;
MP3 is not admitted or receiver-verified by D100.

**D99 dimension-review gate passed on Mac (2026-10-18).** Fork #16 is merged
as `c10dc464`, with all four checks passed at final head `a0888786` in
[run 39101341534](https://github.com/ilyalissoboi/Screenbox/actions/runs/39101341534).
Implementation `c8cc5584`, development 1.0.0.14, passes the user's written checks:
current anamorphic-video compact proportions, fresh-session M4A to an unopened
video with correct compact proportions and enabled Aspect ratio, Stop during
transition with no delayed restart, and rapid Next to the third audio item.
Receiver output, responsiveness and paused local handoff pass as requested.
AppContainer, the inspected MSIX executable and six pinned ci.294 DLL hashes
match in before/after-current/after-queued/after-final snapshots. These are
point-in-time captures across the requested app restart, not continuous process
monitoring. Final owned cache is zero directories/files/bytes. Mac OS version
was not reconfirmed in this focused exchange; no new Apple TV result is claimed.
Both logic suites pass 142/157 and both actual MSIX/payload profiles pass.
Native #83 is merged as `e551388`, with 17 checks passed at `9d594d8` and no
findings; both fork review threads are resolved. The original 1.0.0.13/`aff57b6f`
full playback gate remains historical for its exact bytes. No further focused
manual check is pending. Read [the completed follow-up](screenbox-audio-video-dimensions.md).
No native runtime/API/pin, audio delivery/policy or publication changes occur.

**D99 Screenbox AAC/M4A recorded host gate passed (2026-10-18):** fork #16
implementation `aff57b6f`, opt-in x64 development 1.0.0.13, passes user-confirmed
Mac/Apple TV audio controls/Stop with conversion OFF/ON, natural mixed queue,
rapid Next and transition Stop, plus Mac consent cancellation. Versions are
explicitly unchanged. Both logic suites pass 122/137 and actual MSIX/payload
profiles pass. Exact pre-gate process/post-test layout/reopened-process hashes
match; the original app was intentionally closed, so this is not same-process
before/after evidence. Final owned cache is empty. Read [the record and limits](screenbox-audio-files.md).
No further D99 receiver check is pending. All four/17 CI checks pass at
`9c5202bf`/`5502e8e`; inspect final documentation-head CI separately. Automatic
audio-only conversion is the next proposal, not implemented here. No native
API, pin or release change is included; ordinary receiver checks remain separate.

**D98 packaged audio-only gate passed (2026-10-18):** codec-free x64 UWP
test host 0.1.30.0 from implementation `153a840` plays the original brokered
AAC/M4A through progressive `audio/mp4`. The user confirms both Mac/macOS 26.7.1
and Apple TV/tvOS 26.6 unchanged and passing audible pause/resume, both seeks,
natural end and separate Stop/no restart; Mac pending-consent cancellation also
passes. Actual AppContainer/executable/two loaded native hashes match before/
after; all eight source instances release once. Preserve the initial timeout
and two pre-media Mac connection failures alongside the later successful retry;
their cause is not isolated. See [the bounded gate and evidence](audio-only-uwp-gate.md).
No further D98 manual check is pending. PR #81 merges as `45f39be`, with all 17
checks passed at final head `0c16c4a` in
[run 39092030693](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39092030693).
This adds no receiver observation. Screenbox AAC/M4A routing is the next proposal;
no native core/API, decoder, speaker transport or release change occurs.

**D95/D96 settings and publication (2026-10-17):** the user confirms the new
Casting group, audio-icon alignment and toggle persistence in development
1.0.0.12 (`6d3a30b4`). Fork #15 merges as `7cea7023`, with all four final-head
checks passed. The [separate preview release](screenbox-airplay-preview-release.md)
adds package/public-download, elevated installation and process/executable-hash
launch evidence. It adds no visual or receiver observation for its new identity;
D94's bounded receiver checks below retain their original binary/host scope.

**D94 Screenbox published-ci.294 gate passed (2026-10-17):**
[the checkpoint](screenbox-hdr-package-update.md) records passing 113/127 logic
checks, package/profile controls and both actual MSIX payload checks (two/40
files). Development 1.0.0.11 is registered in place from implementation
`66660bf7`, with data preserved. AppContainer, executable and six loaded
published DLL hashes match before and after playback. The user confirms BOTH
Mac and Apple TV passed the written 90-second HDR/DTS startup/playback,
pause/resume, forward/backward seeks, Stop closure and local-paused checks.
Final owned cache: zero directories/bytes. macOS 26.7.1/tvOS 26.6 are the most
recently reported versions; this response reports no change without explicitly
reconfirming them. These are bounded observations for these installed bytes,
separate from historical D91/D92 results, without whole-film, HLG, speaker or
discrete converted-surround verification. No D94 manual check remains pending.
Fork #14/native #78 are merged (`8d2851c8`/`449457a`), with all four/17 checks
passed at final heads `3a9de5b1`/`722e84b`. Merge/CI evidence adds no new receiver
observation.

**D93 publication (2026-10-17):** ci.294 is published after explicit user approval;
[unauthenticated package/checksum verification](validation/hdr-corrected-audio-publication-windows-2026-10-17.json)
matches the unchanged candidate. This adds no receiver or installed-app result.
Screenbox's opt-in pin remains ci.282 until the separate host update.

**D93 candidate preparation (2026-10-17):** [ci.294](hdr-corrected-audio-candidate.md)
passes clean-main CI and local artifact/consumer validation, without a new
receiver or installed-app observation. D92 private-build results below do not
establish interoperability for these exact candidate bytes. Published ci.282
and Screenbox pins are unchanged. The user's later 2026-10-17 testing suggests
the Apple TV symptom was caused by incorrect HDR signaling and has not recurred
since the correction. This is user-reported follow-up, with no build identity or
new coordinated receiver run recorded for these exact candidate bytes.

D92 status update (2026-10-17): PR #74 is merged as `f1598cf`; all 17 CI
checks passed at final head `1e917aa` in
[run 38909207891](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38909207891).
This adds merge/CI evidence only, with no new receiver observation. Recorded
D92 manual checks are complete. The later user follow-up above supersedes the
then-unexplained Apple TV status while preserving the failed-attempt record.
Published ci.282 still predates the fix.

**D92 long HEVC/DTS startup investigation (2026-10-16):** after both D91 PRs
merged, the user reports a grey-screen Mac failure from the beginning of one
private input. Read-only preparation/initial fragments succeed; a native cast
reproduces `start_timeout` without a source/conversion error. The Mac fetches
playlists/init only. Adding only the missing HDR10 `VIDEO-RANGE=PQ` declaration
makes that file play, confirmed by the user. The automatic SPS-based correction
also reaches native playing; the private Screenbox 1.0.0.10 loaded-DLL override
is verified and its Mac controls pass by user report, with final hashes/empty
cache checked. The user reports a subsequent silent Apple TV Screenbox failure
from zero. Native Apple TV playback passes a bounded 20-second check by
telemetry/user observation. The packaged UWP host first receives a premature
`MediaEnd` near 16 seconds, then passes a visible-window 90-second retry with
Stop/cleanup. A matching 90-second Screenbox retry passes by user report; final
AppContainer/six hashes pass and confirmed Stop leaves zero owned cache
directories/bytes. The later user follow-up above associates the symptom with
incorrect HDR signaling and reports no recurrence since the correction; window
visibility was not established as a cause. The earlier runs did not independently
isolate the Apple TV cause. See
[D92 and its scope](hevc-hdr-range.md). This adds startup/short-play evidence,
not full-film, HLG or discrete-surround validation. D91's original
H.264 fixture observations remain separate. Published ci.282 is unchanged.

**D91 recorded Screenbox host gate passed (2026-10-16):** the
[host implementation checkpoint](screenbox-automatic-audio-integration.md) records
passing local logic/MSIX checks and development version 1.0.0.9 installed in place.
It passes AppContainer/six pinned loaded hashes. The user confirms the setting
layout/default off and Mac consent cancellation through the new startup Cancel:
prompt appears/closes, app remains responsive, local playback paused/no error.
macOS 26.7.1/tvOS 26.6 remain unchanged. AAC copy with conversion off passes on
both receivers: start near 45 seconds, sync/channel pattern, pause/resume, both
seeks, Stop closure and local paused handoff. DTS refuses with conversion off on
the Mac; enabling conversion passes those same controls on both receivers. Both
pass natural AAC → converted DTS → AAC queue advance, rapid Next twice to the
third clip and Stop during a transition, with local paused/responsive handoff and
no delayed restart. Each 250-ms cache sampling window observes at most one
directory/430,412 bytes and ends at zero, with independent final count zero;
sampling does not prove every instantaneous state. Final process hashes pass.
The user restores conversion off/local paused. Original 120-second controls,
including permitted late seeks before natural EOF, establish the recorded host
gate without whole-film, speaker or discrete converted-surround proof. Actions
is enabled now; inspect final-head CI separately from manual observations.

**D88 recorded gate complete (2026-10-15):** [automatic audio receiver gate](automatic-audio-receiver-gate.md).
On the recorded Windows x64 host and unchanged macOS 26.7.1 / tvOS 26.6 receivers,
the user confirms native AAC-copy/DTS-conversion checks on both, native Mac
Stop/cancellation, all nine packaged format cases on both, packaged Stop on both
and Mac pending-consent cancellation. Checks cover synchronized A/V, stereo
pattern, start near 45 seconds, pause/resume, forward/backward seek, natural end
and applicable prompt/video closure. Native seven loaded hashes and packaged
AppContainer/six hashes are verified, with cleanup. The
[sanitized record](validation/automatic-audio-receiver-windows-2026-10-15.json)
retains one initial Apple TV AAC disconnect, two packaged consent rejections
and successful retries separately. These original short controls establish no
whole-film, speaker, discrete converted-surround or Screenbox queue result.

**D87 offline gate (2026-10-15):** [bounded DTS seeks](matroska-dts-bounded-seek.md)
pass owned PCM/EOF, maximum-lace and refusal controls. Private package 0.1.0.8 at
`731db15` passes bundled/folder-granted 137-case/61-fixture matrices with actual
AppContainer tokens/six loaded hashes verified. Three independent brokered
first/middle/last fragment samples pass in 138/314/64 ms cold, with identical
cached repeats, unchanged limits and complete cleanup. This adds bounded offline
conversion evidence for one input/host. No receiver was contacted; audible output,
A/V sync, controls and receiver EOF remain manual gates.

**D86 offline gate (2026-10-15):** [DTS lace sample clocks](matroska-dts-frame-clock.md)
pass exact owned PCM/AAC and gap-refusal controls plus bundled/folder-granted
137-case/61-fixture matrices in private package 0.1.0.7 at `db35a6f`. AppContainer
tokens/six loaded hashes and cleanup are verified. The brokered video's first
fragment completes in 207 ms with a 10-ms identical cached repeat; the middle
request refuses after 256 MiB requested source bytes, so the last is not attempted.
This is an overall MEASURED_REFUSAL, not full conversion-throughput validation.
No receiver was contacted; playback, A/V sync and receiver EOF remain gates.

**D85 offline gate (2026-10-15):** [Matroska common-origin composition](matroska-presentation-origin.md)
passes native video/audio/subtitle, seek and EOF controls plus bundled/folder-granted
126-case/57-fixture matrices in private package 0.1.0.6 at `b946bb7`, with actual
AppContainer token/six loaded hashes and cleanup verified. The private video now
passes its origin requirement but refuses at the existing DTS frame-grid guard.
This adds no conversion-throughput or receiver playback/A-V sync/EOF observation.

**D84 offline gate (2026-10-15):** [expanded automatic AppContainer checks](automatic-audio-uwp-runtime.md)
pass bundled and hash-verified folder-granted 115-case/53-fixture matrices in
private package 0.1.0.5 at `b0cd5ec`, with actual token and six loaded hashes
verified. A private-video brokered benchmark refuses before fragment generation
at the existing zero-origin-video guard. These are offline runtime/refusal and
cleanup results; real-file conversion throughput and copied/converted receiver
playback, A/V sync and EOF remain gates. No receiver was contacted.

**D83 boundary (2026-10-15):** [API 5 automatic integration and offline checks](automatic-audio-public.md)
add public/binding/CLI compatibility and owned/scripted lifecycle controls.
No receiver or expanded AppContainer runtime observation is added; compilation/
import and unit/CI results do not establish playback support for the profile.

**D82 boundary (2026-10-14):** [private copy/conversion policy controls](automatic-audio-policy.md)
are offline and include owned payload, tone, lifecycle and refusal checks. No
receiver or expanded packaged-runtime observation is added; the named copy table
is not negotiated receiver support or universal Apple HLS compatibility.

**D81 boundary (2026-10-14):** [compact indexing controls](matroska-compact-index.md)
cover synthetic high-count TrueHD/Opus and retained offline timing/trim/AAC cases.
They add no receiver, real-film or expanded packaged-runtime observation.

**D80 boundary (2026-10-14):** [compressed index/duration/restart checks](matroska-compressed-index.md)
cover owned 30-second movies, short independently prepared audible controls and
synthetic one-hour FLAC. They add no receiver or expanded packaged-runtime result;
high-packet-count/full-film and real-file performance remain separate gates.

**D79 boundary (2026-10-14):** [selected PCM index/seek checks](matroska-pcm-index.md)
include synthetic one-hour / >4-GiB offsets and independent trim/AAC controls.
They add no receiver or expanded packaged-runtime observation.

**D78 boundary (2026-10-14):** [bounded coarse Matroska timing checks](coarse-matroska-audio.md)
are offline and limited to short presentations. No receiver or packaged runtime
result is added; long-file indexing and existing converted-receiver gates remain.

**D77 boundary (2026-10-14):** [expanded private decoder/rendition checks](audio-decoder-expansion.md)
cover original stereo/5.1 fixtures offline. They add no receiver or packaged
runtime observation. Preserve historical build/firmware attribution; speakers
and discrete converted surround output remain hardware-deferred.

**D76 planning boundary (2026-10-14):** [automatic audio policy and expanded inputs](automatic-audio-transcoding-plan.md)
are proposed for supported video files. No new decoder or receiver observation
is added. FLAC's inclusion in Apple's HLS specification is not this library's
passthrough/receiver proof. D75 is merged in PR #56; its offline passes remain
separate from real-file performance and converted-audio receiver checks.

**D75 offline gate (2026-10-13):** the separate integrated x64 AppContainer
harness passed its bundled fixture/cache/lifecycle checks at `9371502`, private
package 0.1.0.2; all six loaded native module hashes match the registered layout.
The complete 0.1.0.3 run at `5a4e5e3` passes on 2026-10-14 JST, including the
hash-verified outside-package picker-granted owned DTS MP4 and cleanup. The user
confirmed selection; all six loaded hashes match this run's registered layout.
See [scope/evidence](stereo-aac-uwp-runtime.md). No receiver was contacted;
local resource exhaustion/Stop does not establish receiver natural EOF. Historical
Apple TV/Mac results remain attributed to their recorded delivery/builds.

**D74 boundary (2026-10-13):** explicit stereo AAC delivery/API 4 has only
offline/scripted loopback validation in this slice. Historical Apple TV/Mac
results below cover their recorded delivery/builds and do not validate converted
audio. Integrated AppContainer runtime, real-file performance and stereo-output
receiver checks follow [the current gate](stereo-aac-playback.md).

Status: DISCOVERY AND PAIRING OBSERVED; built-in fresh-socket and separate-process
verification passed; pyatv 0.18.0 reference playback FAILED (known upstream
tvOS 26 incompatibility); reference playback PASSED with an unmerged pyatv fix
fetching from `airplay2-cli serve`. Native `cast` initially failed G1 alone;
the minimal native remote-control SETUP/event session then PASSED G1 without
pyatv: video/audio and home-screen return after sender shutdown were user-observed.
Native MRP command/telemetry checks passed; G2 visual confirmation passed by user report on 2026-10-07.
Native automatic EOF cleanup and ten short cycles passed, with user-confirmed
EOF video/audio/home and sleep/wake recovery. Receiver-remote stop returned home
and automatically cleaned up but classified connection_lost/exit 1. Normal stop
classification remains G3 work. D42's selected Ethernet interruption and fresh
same-credential recovery passed as recorded below. A historical full 132-second
native run reached EOF cleanup but **failed sustained video presentation**:
buffering near 18 s was followed by frozen video while audio continued to clip end.
D35 follow-up: the minimum remote session played the full clip normally; default
MRP/four slots buffered but recovered; MRP/16 slots played normal video/audio and
returned Home, with no recorded loading transition. Admission capacity is a
supported candidate, not proof that the original persistent freeze is fixed.
User decision D37 lowers that frozen-video issue to low priority for now, with
insufficient media connections the likely cause; raise priority if it recurs in
later testing. Preserve its original FAIL evidence. The intermittent startup
pause was observed while the Apple TV was waking from sleep (user report,
2026-10-08); D56 reproduced it from sleep and wake-before-play fixed it in three
sleep cycles, so sleep-wake is its likely cause. The original runs were not repeated.
See the dated observations below; record each receiver/firmware/platform separately.

D43's [PR review](pr-review.md) changes diagnostic redaction and decoded MRP/event
plaintext ownership. It passed offline checks only; no hardware pass is attributed
to the D43 build on its own.

**D48 (2026-10-08):** the experimental C playback interface passed its seven-check
[manual plan](public-api.md#manual-validation-plan) through `airplay2-api-host`
(static and shared), on the D47 teardown runtime at `8a7050d`. A `cast` CLI run
on the same runtime played a user-provided remuxed film with E-AC-3 audio and
served user-observed seeks past 4 GiB. See
[the D48 record](#public-playback-interface-manual-batch-d48-2026-10-08).

The Boost HTTP media server is implemented with loopback tests on Windows
static/shared builds; see [media-server.md](media-server.md). Apple TV HTTP
fetch and firewall reachability passed in later reference and native runs.
Real-file >4-GiB seek passed later for one selected file (D48). The original media-server slice itself
performed no receiver or credential operation. The development
`airplay2-cli serve` command now exposes that server for the pyatv reference
playback step. With pyatv 0.18.0 the receiver never connected to `serve`. With
an unmerged pyatv fix it fetched the whole file from `serve` while video and
audio played. See
[pyatv reference playback](#pyatv-reference-playback-2026-10-06) and
[the fork result](#reference-playback-with-unmerged-pyatv-fix-2026-10-06).

The environment below was supplied by the user on 2026-10-06 (Asia/Tokyo).
It identifies the intended test setup. A Windows discovery run subsequently
resolved this receiver. The user subsequently confirmed authenticated PIN pairing,
credential save and built-in fresh-socket verification after the M6 metadata fix.
Reference playback, the initial native failure and the subsequent native-only G1 pass are recorded below.

## Environment

- Record dates and library commits: dated observations below identify each tested build.
- Receiver manufacturer/model/generation: Apple TV 4K (user-provided), advertised model `AppleTV14,1`; pyatv 0.18.0 reports "Apple TV 4K (gen 3)". That label is pyatv's model-table mapping, not an independent hardware check.
- Firmware version and build: tvOS 26.6 (23L773), user-reported from Settings > General > About on 2026-10-06; consistent with advertised `osvers`/`ov=26.6`. Earlier results in this record predate the build report and assume the firmware was unchanged.
- AirPlay access policy and PIN/password settings (no secrets): access limited to people on the same network (user-reported); pyatv scan reports no password required and mandatory pairing for AirPlay/RAOP/Companion.
- Sender OS/version/architecture: Windows 11 x64; automated runner observes Windows build `10.0.26200`, AMD64.
- Host: native Windows desktop CLI; packaged Windows C# and Android NOT RUN.
- Network: host on Wi-Fi, same subnet as the receiver. Windows network category **Public** (the runbook expected Private; the user chose to proceed). Existing inbound Allow rules (Public profile) for the `airplay2-cli.exe` builds; pyatv timing needed a temporary user-created inbound UDP rule for the Python interpreter, LocalSubnet, Public profile. See the playback observation.
- Media SHA-256, container, codecs, duration, dimensions and bitrate: user-owned clip, 53,953,926 bytes, SHA-256 `a91fb5c781f4a6ecc90b780dc77793403b6aac7220af7c899ba3b217129b5e65`; MP4 (`isom`, `moov` before `mdat`), H.264 Main profile level 4.2 1280x720, AAC (`mp4a`) stereo 44.1 kHz, 131.6 s. Read with a standard-library box parser; ffprobe was unavailable. Bitrate not measured.
- Negotiated protocol/authentication path (reference sender): HAP pair-verify with stored credentials, encrypted RTSP control, NTP timing (sender UDP), event channel, type-130 control stream, `POST /command` queue commands; receiver fetches the URL over plain HTTP from the sender.
- Reference sender/version and baseline result: pyatv 0.18.0 AirPlay pairing PASS (user-reported); reference URL playback FAIL: `/play` 200, then `/playback-info` 500 and no media fetch, also with a public Apple HLS URL. Matches open upstream issues. Unmerged pyatv fix `robkochman/pyatv@8144c77c` (`/command` queue flow): playback PASS, video and audio (user-observed), served by `airplay2-cli serve`. See [reference-baseline.md](reference-baseline.md) and the observation below.
- Native sender path: two independently HAP-verified encrypted control/event
  sessions. Minimum remote-control-only SETUP/event connection retained alongside
  URL SETUP/NTP/RECORD/type-130 `/command` start and HTTP media fetch. Native-only
  G1 passed for one 45-second run; no MRP or pyatv was used.

## Required observations

| Test | Expected | Result/evidence |
|---|---|---|
| Discovery and departure | Correct identity; no duplicate/stale entries | Discovery observed; departure/interface changes NOT RUN |
| First pairing | PIN UI and credential save succeed | PASS, user-reported after M6 metadata fix; see observation below. Through the C interface: PASS (D51), `sap2_pair()` into the built-in store with hidden console PIN entry, then a cast with the new profile |
| Wrong PIN / revoked pairing | Explicit failure, no playback | NOT RUN |
| Reconnect after restart | Stored pairing works when receiver policy permits | Built-in fresh-socket and separate-process verification PASS; host/receiver restart NOT RUN |
| Repeated verification | Independent processes reuse stored pairing | PASS, baseline plus three reconnects and three fault-recovery verifications per static/shared run |
| Profile/input guards | Existing/missing profiles and redirected PIN input are refused | PASS, automated native CLI static/shared runs |
| Local forget | Local deletion and idempotence | Absent-profile no-op PASS; real disposable deletion SKIPPED (no opt-in) |
| Start MP4 | Both audio and video play | Reference: pyatv 0.18.0 FAIL (no fetch); unmerged pyatv fix PASS, video and audio from `serve` (user-observed). Native: initially headless (FAIL); minimum native remote SETUP/event session then PASS without pyatv, video/audio user-observed |
| Pause/resume | Receiver and host state agree | Reference (unmerged pyatv fix): PASS, `Paused` then `Playing`, user-observed. Native PASS: command/telemetry and user-observed pause/resume/audio (dated G2 record) |
| Seek forward/back | Playback moves to requested position | Reference: forward seek to 30 s PASS (position 36 s about 7 s later), user-observed; backward NOT RUN. Native PASS: forward/backward command/telemetry and user-observed movement (dated G2 record) |
| Position/duration | Values follow receiver playback | Reference: PASS, positions 17/36/37/46 s against duration 131 s, consistent with timing. Native PASS: duration 131.6 s and positions follow pause/forward/backward seek |
| End-of-file | Correct ended state and resource cleanup | Native near-end seek/play: automatic media_end and joined cleanup, exit 0 with stdin held open; user confirmed video/audio and home-screen return (dated lifecycle record) |
| Sustained full-clip video | Moving video and normal audio through the full clip | Original FAIL: video froze after buffering near 18 s; audio/EOF cleanup passed. D35 follow-up: minimal remote/four slots and MRP/16 slots PASS video/audio/Home; MRP/four slots buffered but recovered. Persistent-freeze resolution remains unproven. |
| Stop from sender/receiver | Correct state and resource cleanup | Reference sender `stop`: exit 0 and the TV returned to the home screen (user-observed), but `device_state` then reported `Paused` and the sender's URL session stayed open; see observation. Native sender shutdown: user observed home-screen return after minimum SETUP/event run; native MRP Stop accepted followed by teardown and user-confirmed home return; receiver-remote stop returned home and automatically cleaned up, but classified connection_lost/exit 1; protocol idle unresolved |
| Repeated casting | Ten start/stop cycles without stale sessions | Native PASS: ten short independent casts, alternating five MRP Stop and five direct teardown; owned playing status, exit 0, no session/failed-read errors in each. Long sessions and visible home-screen observation remain separate |
| Receiver sleep/wake | Terminal cleanup; fresh cast works after waking | PASS: user-confirmed sleep, automatic connection_lost/exit 1 cleanup, fresh cast after wake without pairing, normal video/audio and EOF return home |
| Cast to a sleeping receiver | The cast wakes the receiver and plays | PASS for three runs, CLI and C interface static/shared (D56): with MRP `WAKE_DEVICE` before play, normal video/audio and Home after stop. Without it the receiver showed one frame and returned Home (start timeout). See [D56](#casting-to-a-sleeping-receiver-d56-2026-10-08) |
| Network interruption | Bounded failure; next cast can recover | PASS for the selected D42 Ethernet interruption: URL feedback timeout triggered automatic joined cleanup; a fresh explicit cast after reconnection/Home reused credentials and passed video/audio/near-end EOF/Home. See [D42](#ethernet-interruption-and-fresh-recovery-d42-2026-10-07). Broader network/reliability cases remain pending; automatic in-session reconnect/resume is not implemented. |
| Large file | Seek beyond 4 GiB without integer truncation | PASS for one selected file (D48): 6.32 GB remuxed film (64-bit `co64` offsets); after `seek 4800`, 37 range requests at offsets of 4,490,723,328 bytes and above were all answered 206; user saw the picture jump to about 1:20:00 with sound, then back to about 0:10:00. Other files, receivers and hosts untested |
| Non-AAC audio | E-AC-3 audio plays | PASS for one selected file (D48): E-AC-3 5.1 (Atmos-flagged) in MP4 played normally by user report; no Atmos indication was shown, and whether the TV/AV chain can show one is unknown |
| Public C interface | Start, controls, end, stop, recovery through `playback.h` | PASS (D48): all seven manual checks through `airplay2-api-host`, static and shared, plus ten cycles in one process; see the D48 record |
| Packaged Windows host | Discovery, native loading, file access, serving work | PARTIAL (D53-D55): passed for one sideloaded AOT UWP app: the UWP-built library (app C runtime, AppContainer, no Credential Manager; D55) casting with controls and repeated Stops, discovery through the C interface (Private network, Screenbox's capabilities; Public not measured), native loading, PasswordVault pairing, StorageFile serving, casts and controls. Casting with Screenbox's `privateNetworkClientServer` fails on a Public network and passes on a Private one; `internetClientServer` passes on Public. The built-in Credential Manager store fails inside the AppContainer. D57: an x86 package with the x86 library passed casting and controls; the certification kit passed 26 of 27 tests, failing Supported APIs only for the .NET AOT runtime in the host executable. ARM64 built in CI only |

Use a personally owned or redistributable unprotected test clip. Capture sanitized
diagnostics: timestamps, state transitions, status codes and range requests.
Do not commit pairing secrets, PINs, private media URLs or raw credential logs.
Unit tests and mock receivers cannot substitute for this record.

The dated slice observations below preserve their original evidence. Statements
such as NOT RUN in those historical sections apply at that date/slice; the status
and required-observations table above summarize the latest result for each gate.

## Discovery observation: 2026-10-06 (Asia/Tokyo)

Windows 11 x64 / MSVC 19.51 Release CLI, discovery implementation on
`codex/receiver-discovery` (working-tree scan before publishing implementation
commit `48189897ba167b44c3da7c6e4a7857bf28120498` in
[PR #2](https://github.com/ilyalissoboi/send-airplay2/pull/2)). A 15-second scan outside the execution
sandbox received four DNS responses with zero rejected packets and no warnings.
It discovered "Living Room", model `AppleTV14,1`, on interface 24. Its AirPlay
and RAOP services were merged by matching advertised identity and both resolved
port 7000 with one IPv4 and four IPv6 addresses, including a scoped link-local
address. The feature mask was `0x3c177fde4a7fdfd5`; password and pairing
requirements remain unknown. A second device, a `Mac14,2`, was also resolved.

Discovery/identity resolution is observed. Real receiver departure, expiry,
interface changes and the complete "Discovery and departure" test above remain
pending; lifecycle behavior has only synthetic test coverage. Reference playback
and all media controls are NOT RUN. Later pairing evidence is recorded below.
This is not playback certification.
Raw LAN identifiers, addresses and advertised public keys are omitted here.

## Pairing transport foundation: 2026-10-06

The next slice adds private TLV8 and authenticated control-record codecs with
synthetic test coverage; see [pairing-transport.md](pairing-transport.md). It does
not perform a handshake or receiver I/O. No PIN, credential or playback operation
was attempted; all corresponding hardware results above remain NOT RUN.

## Peer-verification slice: 2026-10-06

Private verification of existing, pinned identity credentials is implemented with
synthetic RFC/transcript/failure tests; see [peer-verification.md](peer-verification.md).
No receiver HTTP/socket connection, PIN enrollment, credential change or playback
operation was attempted. Synthetic peer-rejection tests are not evidence of actual
Apple TV revocation behavior; all hardware authentication results remain NOT RUN.

## PIN-pairing slice: 2026-10-06

Private PIN/SRP provisioning message processing is implemented and tested with
public synthetic transcripts; see [PIN pairing](pin-pairing.md). No receiver
connection, PIN-display request, actual PIN entry, credential save or playback
operation was attempted. All hardware pairing/playback results remain NOT RUN.

## Receiver-transport slice: 2026-10-06

Private bounded HTTP/RTSP framing, native IPv4/IPv6 TCP, deadlines/cancellation and
pairing/verification-to-record integration are implemented; see
[receiver transport](receiver-transport.md). Synthetic peers and real loopback
sockets exercise these flows. No connection to "Living Room", PIN display/entry,
credential change/save or playback was attempted. The hardware table remains
NOT RUN for authentication and playback; loopback success does not validate tvOS.

## Credential storage / CLI slice: 2026-10-06

Windows desktop pairing, stored-credential verification and local forget commands
are implemented; see [credential storage](credential-storage.md). Tests use only
public synthetic credentials in a separate random test namespace. Actual Windows
Credential Manager save/load, cross-process reload, malformed-entry rejection,
concurrent create refusal and redirected-input refusal passed locally in static
and shared Release builds. Synthetic enrollment credentials survive serialization
and satisfy the independent peer-verification transcript/control-key oracle.
At that slice's merge, no Apple TV PIN-display request, actual PIN entry,
application credential save, receiver revocation or playback operation had been
attempted. Subsequent live enrollment and separate-process verification are
recorded below. Remaining tests include wrong PIN,
cancel/timeout and receiver-revoked credentials. Record the exact firmware build
and access settings without recording secrets.

## Live PIN pairing and M6 compatibility: 2026-10-06

Receiver: Living Room / Apple TV 4K, advertised `AppleTV14,1`, user-reported
tvOS 26.6. Host: Windows 11 x64, MSVC Release static CLI. Exact firmware/OS builds
and AirPlay access policy remain unspecified.

The user observed a PIN on the TV and entered it through the CLI's hidden prompt.
Initial enrollment failed with `Invalid pair-setup message`. Instrumented retries
isolated M6: HTTP 200, outer body 322 bytes, wire fields `5:255,5:60,6:1(state=6)`.
A later decrypted schema diagnostic reported 299 bytes and
`source=identity TLV=[1:36,3:32,10:64,17:159]`. Those are headers/lengths only;
no actual identity, keys, PIN, ciphertext or metadata contents were retained.
The failure occurred after mandatory M4 server-proof verification and successful
M6 AEAD decryption, but before accessory-signature validation/credential release.
The strict schema rejected the additional type-17 receiver metadata.

An agent-operated no-PIN/no-save LAN probe independently observed
`/pair-pin-start` HTTP 200 with an empty body, and M2 HTTP 200 (409 bytes): state
2, 16-byte salt and 384-byte SRP public value. This probe did not complete pairing.

The fix in [PR #8](https://github.com/ilyalissoboi/send-airplay2/pull/8), source
commit `f61698fa930893c139efcb5f5d2b0a40a93d0cae` on `codex/pairing-diagnostics`,
based on PR #7 merge
`6e83badfc5146371ee0c886e3f75fba492f9ab61`, accepts one optional opaque M6
type-17 value up to 256 bytes and discards it. Mandatory ID/key/signature checks,
server proof, duplicate rejection and rejection of other unknown tags remain.
Only public synthetic bytes were added as regression fixtures. Failed earlier
M6 processing did not save a local profile; receiver-side pairing may nevertheless
have been provisioned by M5. No receiver revocation was performed or asserted.

The user reran the rebuilt
`build-pairing-static/Release/airplay2-cli.exe` and reported:

```text
Pairing authenticated; credentials saved. Verifying a fresh connection.
Peer verification succeeded; encrypted control transport established. Playback is not implemented.
```

This establishes authenticated enrollment, Windows application credential save,
reload and built-in verification on a fresh socket for this receiver/host
combination. The user then launched `verify` separately with the saved profile
and reported the same peer-verification success message and `$LASTEXITCODE` of 0.
No PIN was required. The `pair` exit code itself was not supplied.
These results do not establish encrypted application request/response exchange,
restart/revocation behavior or playback. Physical-console echo/mode restoration,
wrong PIN, cancellation/timeout and host/receiver restart checks remain pending.

## pyatv reference pairing: 2026-10-06

The user ran pyatv 0.18.0 (MIT, external tool) on the Windows host, following
[reference-baseline.md](reference-baseline.md), and reported the following.

- Unicast scan: "Living Room", Apple TV 4K (gen 3) per pyatv's model table,
  tvOS 26.6, deep sleep false. Services: AirPlay port 7000, Companion port 49153
  and RAOP port 7000. Each reported `Requires Password: False` and
  `Pairing: Mandatory`, with no stored credentials before pairing.
- `atvremote --protocol airplay pair`: succeeded, exit code 0 (user-reported).
- `device_state` on a fresh pyatv invocation using stored credentials:
  `DeviceState.Idle`.

This shows that a second, independent controller can complete AirPlay HAP
pairing on this receiver/firmware alongside the existing `airplay2-cli` profile,
and that pyatv can reuse its credentials. It is not a playback result, and it
says nothing about this library's session code. The address, MAC address and
device identifiers printed by the scan are omitted here; credentials were not
shared. The user subsequently reported tvOS build 23L773 and AirPlay access
limited to the same network (see Environment). The Companion protocol was not
paired.

## pyatv reference playback: 2026-10-06

Step 4 of [reference-baseline.md](reference-baseline.md), run by the agent in a
local session on the user's Windows 11 x64 host (build `10.0.26200`), with the
user watching the TV. Receiver: Living Room / Apple TV 4K / `AppleTV14,1` /
tvOS 26.6 (23L773). Reference sender: pyatv 0.18.0 (MIT, external tool) on
Python 3.11.9, using its existing stored AirPlay credentials. Media server:
`airplay2-cli serve`, static Release build of PR #11 code head
`19a08d256fdd2092aecfc63438af2b6a07c95b8c`; media as listed under Environment.

The receiver address came from a fresh `discover` run, and the private URL came
from `serve` output. Both stayed in driver-script memory. Child output was
filtered before display: addresses, URLs, MAC addresses, identifiers, headers
and plist values are omitted, except allowlisted status fields. No raw debug
output was stored.

**Run 1: no Python firewall rule, commands per the runbook.**

- `play_url` exited 1 after `no response to SETUP` for the base RTSP SETUP.
  This SETUP advertises an NTP timing port on the sender.
- A concurrent `device_state` at about 15 s exited 1 after `no response to POST
  /pair-verify`. Later `set_position=30`, `pause`, `play` and `stop` exited 0,
  and every status read returned `DeviceState.Idle`, so those commands had
  nothing to act on.
- `play_url` logged power state On, Off, On. The user saw the TV wake from
  sleep with no playback UI.
- `serve`: `Stopped. reads=0 bytes=0 failed_reads=0`.

Windows Firewall had no rule for the Python interpreter, and the network
category is Public. The working hypothesis is that the receiver's inbound UDP
timing requests were dropped, which stalled SETUP. The user added a temporary
inbound UDP Allow rule for the interpreter (LocalSubnet, Public profile).

**Run 2: same, with the rule.** SETUP completed; `play_url` exited 1 about 2 s
later with `GET /playback-info` HTTP 500. Control commands were skipped.
`serve`: `reads=0`.

**Run 3: diagnostic, `--debug` with filtering; TCP connections to `serve`
polled every 200 ms.** Sanitized sequence, all within about 1.4 s:

| Request | Response |
|---|---|
| `POST /pair-verify` x2 | 200, 200 |
| RTSP `SETUP` (base, no timing) | 200, `eventPort` |
| `RECORD` | 200 |
| `SETUP` stream type 130 | 200, `streamID`, `dataPort` |
| `POST /pair-verify` x2 | 200, 200 |
| RTSP `SETUP` (base, `timingProtocol=NTP`) | 200, `eventPort` |
| `RECORD`, `POST /feedback` | 200, 200 |
| `POST /play` (bplist with `Content-Location`) | 200, empty body |
| `PUT /setProperty?isInterestedInDateRange`, `?actionAtItemEnd` | 200, `errorCode=0` |
| `POST /rate?value=1.000000` | 200 |
| `PUT /setProperty?forwardEndTime`, `?reverseEndTime` | 200, `errorCode=0` |
| `GET /playback-info` | **500**, empty body; connection then lost |

Inbound TCP connections to `serve`: **0**. `serve`: `reads=0`.

**Run 4: control.** The same filtered run with Apple's public HLS example URL
in place of `serve` produced the identical sequence and the same 500. The
failure therefore does not depend on this project's server, the URL or the
local firewall for TCP.

Interpretation:

- The receiver accepts pyatv 0.18.0's legacy `/play` request on tvOS 26.6, but
  it does not start media or fetch the URL. This matches open upstream issues
  [pyatv#2906](https://github.com/postlund/pyatv/issues/2906) (Apple TV 4K,
  tvOS 26.6, identical symptom) and
  [pyatv#2821](https://github.com/postlund/pyatv/issues/2821) (tvOS 26.2,
  regression of [pyatv#2512](https://github.com/postlund/pyatv/issues/2512)).
- An unmerged proposal, [pyatv#2846](https://github.com/postlund/pyatv/pull/2846)
  (head `8848ad3fd9ae46b8eb733bfc667b536a28f04c5a`, read on 2026-10-06), replaces
  `/play` with a type-130 stream SETUP (`controlType` 1), then
  `POST /command` with a binary plist wrapping queue commands such as
  `insertPlayQueueItem` (`mediaType` `file`, `Content-Location`), plus
  `setProperty`. It reads playback state from the event channel instead of
  polling `/playback-info`. Neither the proposal nor a reworked fork linked from
  it was run here; neither is maintainer-reviewed or verified on this receiver.
- Inbound UDP timing to the sender was needed before SETUP completed. A native
  session will need the same on Windows. Run 1 does not prove the cause; it was
  not confirmed by a packet capture.
- `serve` was started, announced and stopped cleanly in every run. Its fetch
  path and firewall reachability from the Apple TV remain NOT RUN, because no
  sender caused a fetch. `serve` counts reads only. A HEAD-only or aborted
  connection would also show `reads=0`, which is why run 3 polled connections.

Seek, pause/resume, position and stop results remain NOT RUN for any working
playback. Remove the temporary Python firewall rule after testing.

## Reference playback with unmerged pyatv fix: 2026-10-06

Same host, receiver, firmware, network, media and `serve` build as the previous
section, and the same temporary Python UDP rule. After that failure, the user
chose to try an unmerged upstream fix as the reference sender.

- **Sender:** `robkochman/pyatv` commit
  `8144c77c6cecbed4f9ba2adb5a350ad86a8f6604` ("Fix AirPlay URL playback on
  modern tvOS"). It is one commit on top of the maintainer's `Release 0.18.0`
  commit `b277a4c82`, linked from
  [pyatv#2846](https://github.com/postlund/pyatv/pull/2846). It is MIT like
  pyatv. It is not merged or maintainer-reviewed, and it reports its version
  as 0.18.0.
- **Review:** the agent read the four changed source files before the user
  installed it. Changes are confined to the AirPlay URL stream and
  event-channel code: no new hosts, processes, file access or dependencies.
  The user installed it from that commit's archive into a separate venv and
  ran the filtered diagnostic driver. The agent did not execute the fork.
  It is an external test tool only: not a dependency, and no code was copied
  into this repository.

Sanitized sequence, filtered as in the previous section:

| Phase | Requests and results |
|---|---|
| Remote-control session | pair-verify 200 x2; base SETUP (no timing) 200; RECORD 200; type-130 SETUP 200 |
| Stream session | pair-verify 200 x2; base SETUP with `timingProtocol=NTP` and `sessionCorrelationUUID` 200; `GET /info` 200; `RECORD` 200; type-130 SETUP (`controlType` 1) 200 with `streamID` |
| Start | four `POST /command` (`insertPlayQueueItem`, two `setProperty`, `setRate`), each 200 with an empty body |
| Events | `playbackState` `loading` x3, then `playing`; many `notification` and `updateInfo` events; `/feedback` 200 about every 2 s |

`serve` results:

- **TCP connections from the receiver:** 17. The first arrived about 3.7 s after
  `play_url` started.
- **Stop summary:** `reads=1466 bytes=95888702 failed_reads=0
  span=[0,53953926)`. The receiver requested the whole file range, and about
  1.8 times the file size in total within 60 s, so ranges overlap or repeat.
  Individual range requests were not logged.
- **Firewall:** fetch worked on the Public network with the existing inbound
  Allow rules for `airplay2-cli.exe`; no new rule was needed for `serve`.

The user reported that video and audio played without issues. `play_url`
blocks while media plays, so the driver ended it after 60 s. End-of-file,
sender stop, seek, pause/resume and position were not exercised in this run.

This result establishes:

- the receiver plays this H.264/AAC MP4 when it is fetched from this project's
  `MediaServer` through `serve`;
- the receiver can reach that server through the Windows firewall on this host;
- the `/command` flow works on tvOS 26.6 with this sender.

It is reference-sender evidence, attributed to unmerged third-party code. It
did not exercise native session code. Later native G1 observations below
supersede that implementation status, including the later native-only G1 pass.

### Control pass with the unmerged fix

The user ran the sanitized control driver with the same fork, `serve` build,
media and network. Each control is a separate `atvremote` process, which uses
pyatv's remote-control channel; the fork did not change that code.

| Time (s) | Command | Exit | Reported state / position / duration |
|---|---|---|---|
| 10.2 | `play_url` started (background) | | |
| 35.4 | `device_state position total_time` | 0 | `Playing` / 17 / 131 |
| 36.3 | `set_position=30` | 0 | |
| 43.2 | status | 0 | `Playing` / 36 / 131 |
| 44.1 | `pause` | 0 | |
| 51.0 | status | 0 | `Paused` / 37 / 131 |
| 51.9 | `play` | 0 | |
| 60.8 | status | 0 | `Playing` / 46 / 131 |
| 61.7 | `stop` | 0 | |
| 65.6 | `device_state` | 0 | `Paused` |
| 96.5 | `play_url` still running 30 s after `stop`; killed by the driver | | |

`serve`: `reads=1436 bytes=93970360 failed_reads=0 span=[0,53953926)`.

The user reported normal video and audio, and that seek, pause and play worked
as expected. Positions match wall-clock progress, the seek target and the
pause. The concurrent status connection that failed in the pyatv 0.18.0 run
worked here once playback was established.

After `stop`, the user saw the TV return to the home screen. However,
`device_state` reported `Paused` rather than idle, and the fork's `play_url`,
which waits for an `idle` or `stopped` event, did not return. The sender's
URL session stayed open until the driver closed its connections. For native
code, "stop" must therefore be defined explicitly: for example, a stop or
queue-removal command followed by session teardown, verified by observed
receiver state, not assumed from a 200 response. End-of-file, backward seek,
repeated casts and receiver-side stop remain NOT RUN.

## Native cast, hardware gate G1: 2026-10-07

The first casts by this library's own session code. Receiver: Living Room /
Apple TV 4K / `AppleTV14,1` / tvOS 26.6 (23L773). Host: Windows 11 x64, Public
Wi-Fi profile, existing inbound Allow rules for this `airplay2-cli.exe`.

- **Build:** static Release from `claude/url-playback-session`; step 6 commit
  `1630b61`, plus the diagnostic event log added after run 1.
- **Command:** `airplay2-cli cast` with the stored `airplay2-cli` profile and
  the same media as the reference runs.
- **Driver:** the agent ran it; the receiver address stayed in driver memory.
- **Observation:** the user watched the TV.

| Run | Setup | `cast` output (sanitized) | TV (user-observed) |
|---|---|---|---|
| 1 | `cast` alone, 45 s | `playing` within 1 s, then `paused`; `events=42 feedback=22 timing=21 failed=no reads=762 bytes=49859866 failed_reads=0 span=[0,47054848)`; exit 0 | Nothing appeared; home screen throughout |
| 2 | `cast --event-log` alone, 25 s | `loading` x3, `playing` with full playback info, then `loading`/`playing`/`loading` around 26-32 s; `events=47 reads=1103 bytes=72159904 span=[0,53953926)`; exit 0 | Nothing appeared |
| 3 | `cast` while pyatv 0.18.0 `atvremote push_updates` held its remote-control session open, 30 s | `playing`; `events=40 feedback=15 timing=14 reads=1219 bytes=79762080 span=[0,53953926)`; exit 0. pyatv's session saw device states including `Playing` | **Video and audio played correctly**, with one short buffering pause around 15 s |

Interpretation:

- The native sequence works at the protocol level on tvOS 26.6. Every request
  was accepted, and the receiver fetched the whole MP4 from this project's
  `MediaServer` through the Windows firewall, without errors. Event channel,
  feedback and timing all worked: 12-22 feedback requests and 13-21 timing
  answers per run. Stop was clean each time.
- **H5 is supported:** the receiver presents URL playback only when the sender
  also has a remote-control session. On its own, our session's playback
  happens headlessly. Run 3 used official pyatv 0.18.0 for that session (the
  `isRemoteControlOnly` SETUP plus its MRP data stream). Whether the SETUP and
  event channel alone are enough, or the MRP handshake is needed, is not yet
  known.
- **Event structure** (run 2, key names only):
  - `playbackState` events carry `params.playbackState`, `rate`, `position`,
    `duration`, `readyToPlay`, `stallCount`, buffer flags, and loaded and
    seekable time ranges.
  - Notifications carry `name`, `item.uuid` and sometimes `position` or
    `value`.
  - Some events (`updateInfo`) are bare dictionaries without the `params.data`
    envelope. The parser now accepts both shapes; earlier they were counted as
    unreadable and answered.
- The receiver read more than the file size in every run (up to 1.5x), so ranges
  are re-requested. The state also returned to `loading` mid-play; the buffering
  pause in run 3 may be related. Not yet investigated.
- **G1 status: FAIL** for `cast` alone. The remote-control session must be part
  of native start. H4 (stop behavior) could not be assessed, because nothing
  was on screen in runs 1 and 2.


### Native minimum remote-control session: G1 PASS, 2026-10-07

Same receiver, firmware, host and MP4 as the previous native runs. The new native
session opens its own stored-credential pair-verify and `isRemoteControlOnly`
SETUP (`timingProtocol=None`), connects the separately keyed event channel, then
starts the URL session. It retains remote control until URL teardown. No remote
RECORD, feedback, type-130 data stream, MRP or pyatv process was used.

- Windows MSVC static Release, based on `66b8a94` with this native minimum change.
  Implementation subsequently committed as `cd2c983`; source blobs in the record
  identify the tested code independently of later documentation changes.
  Tested CLI SHA-256 and sanitized output are in the
  [machine-readable record](validation/native-minimum-session-windows-static-2026-10-07.json).
- The agent ran native `cast` using the existing paired profile. Receiver address
  and discovery output remained in driver memory; no credential file was read.
- The user reported: video and audio played normally, and the device returned
  to the home screen after playback finished.
- The driver sent Enter after 45 seconds. Exit 0; summary:
  `state=playing events=49 remote_events=0 feedback=22 timing=21 failed=no
  reads=1317 bytes=86184608 failed_reads=0 span=[0,53953926)`.
- Existing firewall rules were reused by temporarily staging the tested executable
  in the stopped session's allowed build location. Its original executable was
  restored; no firewall rule was added or changed.

**G1 PASS for this run:** native-only visible video/audio, full-file fetch and
user-observed home-screen return after sender shutdown. The minimum SETUP/event
connection was sufficient on this receiver; an idle remote event channel is not
an error. MRP remains the selected controls path (D29/D31). The printed final
state is the last URL event, not proof of protocol idle. This was not EOF or a
receiver-side stop test. Native controls, repeated casting, extended session
lifetime and other firmware/hosts remain unvalidated.

## Native MRP controls: G2 PASS, 2026-10-07

Same Apple TV 4K / tvOS 26.6 (23L773), Windows 11 x64 and MP4 as G1. The
normal native CLI now extends the independently verified remote session with
RECORD, type-130 controlType-2 data SETUP, keyed MRP framing and handshake.
No pyatv controller was running. Existing credentials and firewall rules were
reused; the temporarily staged executable was restored.

The [sanitized record](validation/native-mrp-controls-windows-static-2026-10-07.json)
contains final CLI SHA-256, tested source blobs, command times and allowlisted
scalar output. Commands were scoped to the full selected player path using D32
startup correlation; this firmware omitted our URL and queue UUID in MRP.

| Native command/check | Receiver evidence |
|---|---|
| Initial status | owned=yes, playing, position 10.2 s, duration 131.6 s |
| Pause | Correlated result accepted; URL and MRP paused, position 10.0 s after 5 s |
| Resume | Correlated result accepted; URL loading then playing |
| Seek forward to 45 s | Accepted; playing position 51.5 s about 5 s later |
| Seek backward to 15 s | Accepted; playing position 19.8 s about 5 s later |
| Heartbeat | One correlated acknowledgment after 30 s |
| MRP Stop then teardown | Accepted; URL paused; exit 0 and failed=no |

Final summary: `state=paused events=71 remote_events=9 feedback=19 timing=18
failed=no reads=1773 bytes=116021286 failed_reads=0 span=[0,53953926)`.
Repeated range reads explain total bytes exceeding file size. Command results and
telemetry pass for this run; the user subsequently confirmed visible pause/resume, both seek directions,
normal resumed audio and home-screen return after stop. **G2 PASS** for this
recorded combination; the final paused URL event still does not prove protocol idle.
This control run did not test EOF.

Compatibility issues discovered and covered by independent synthetic fixtures:
receiver replies can contain a plist body; an idle readiness poll must preserve
the socket; the reference subscription yields a 2363-byte authenticated record;
configuration/heartbeat use correlated type-0 acknowledgments; URL duration is
a CMTime rational dictionary. MRP receive opts into a bounded 16-KiB record
budget while other channels retain 1024 bytes. Full command result types and
request IDs remain checked. D32 is an engineering fallback for cooperative
startup, not proof against simultaneous same-duration AirPlay senders.

Windows static/shared Release each passed 22/22 CTest targets (11.88/11.71 s),
including independent protobuf/frame/large-record oracles, split/coalesced
frames, malformed input, correlation/authentication/timeout/cancellation,
heartbeat, ownership/replacement guards and native idle-readiness regression.
Formatting and diff checks passed. All ten [CI checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37502438877)
passed at code/test head `8dca4a55c831563be952a5b3d6c8a353ad8b7807`: six native
platform/static/shared jobs, ASan/UBSan and three offline runner jobs. The test
const-reference follow-up corrected GCC/Clang warnings-as-errors; native runtime
code and its tested executable remained unchanged. Inspect the actual PR head
after documentation-only follow-ups. Unit tests and CI are separate from G2/G3.
EOF, receiver-side stop, automatic failure cleanup, ten cycles, sleep/wake,
network loss/recovery and other hosts/firmware remain step 3 or later gates.

## Native lifecycle: selected G3 cases PASS, manual gates pending, 2026-10-07

Same Apple TV 4K / advertised AppleTV14,1 / user-reported tvOS 26.6 (23L773),
Windows 11 x64 and 131.6-second unprotected MP4 as G1/G2. Existing credentials
and firewall rules were reused. No pyatv controller was running; the temporarily
staged native executable was restored. The [sanitized record](validation/native-lifecycle-windows-static-2026-10-07.json)
contains CLI SHA-256, tested runtime source blobs, allowlisted scalar output and
per-cycle results. This evidence covers this combination only.

- **EOF cleanup:** after owned playing telemetry, native seek to 124 seconds and
  Play were accepted. Stdin remained open. URL stopped, then the supervisor
  recognized owned receiver-reported terminal position and completed ordered
  cleanup. Summary: media_end, cleaned=yes, failed=no, failed_reads=0, exit 0;
  the receiver fetched the entire file span. This tests the end boundary after
  a near-end seek, not uninterrupted full-duration playback. The user confirmed
  normal video/audio and return home both for the earlier native EOF checkpoint
  and for repeated EOF after wake using the fingerprinted final executable.
- **Ten short cycles:** each used a new process/session and existing credentials,
  reported owned playing status, then alternated five MRP Stops with five direct
  Enter teardowns. All exited 0, cleaned=yes, no session/failed-read errors and
  full-file read spans. Subsequent starts established fresh playback without
  observed stale ownership. These are protocol/process results; no long-session,
  simultaneous-sender or visual home-screen result is inferred.
- **Receiver-remote stop:** the user exited playback and confirmed normal return
  home. With stdin held open, the CLI automatically joined/closed its session
  and media server. Last URL state paused; end=connection_lost, cleaned=yes,
  failed=yes, exit 1. Cleanup passed, but a normal receiver_stop reason is
  unresolved. Do not classify every paused-plus-disconnected session as normal
  stop: that would conceal a real outage during pause.
- **Sleep:** the user put the receiver to sleep and confirmed its screen was off.
  The CLI automatically cleaned up with stdin open, connection_lost/exit 1,
  cleaned=yes and no failed file reads. Before sleep, this run emitted repeated
  loading/playing states; this is additional buffering evidence, not resolution.
- **Wake recovery:** after the user woke the receiver to Home, a fresh native
  cast reused the existing credentials and reported owned/playing at 4.1 s.
  Near-end seek and Play were accepted; it automatically ended with media_end,
  exit 0, cleaned=yes, failed=no and no failed file reads. The user confirmed
  normal video/audio and EOF return home. Sleep cleanup plus fresh wake recovery
  pass for this combination; this does not prove actual network-outage recovery.
- **Automated fault tests:** scripted URL and remote-event EOF now trigger joined
  automatic cleanup without operator input. A silent established feedback peer
  exercises its 80 ms test deadline; stopping during blocked feedback cancels
  rather than waiting the normal two-second test deadline. Ten synthetic
  concurrent-stop cycles verify one cleanup owner and URL-before-remote closure.
  Independent protobuf fixtures reject mid-item pause and playing/extrapolated
  EOF, and accept only owned paused-at-duration telemetry. Pending MRP command
  cancellation and real native-pipe partial/idle/EOF input are covered.
  These injected peers are separate from actual receiver/network interruption.

D33 preserves first terminal reason, cancellation and fixed cleanup order.
The CLI stops its media server after session cleanup and exits without blocking
on command input. Failure returns exit 1; a recovered network requires a fresh
explicit cast and retains credentials. A silent MRP-only peer may require the
next 30-second heartbeat plus its five-second default request deadline.

Final Windows static/shared Release each passed 23/23 CTest targets
(13.33/13.07 s), offline runner contracts 10/10, clang-format dry-run/Werror
and git diff --check. Actual-head CI is recorded in the handoff after publishing.
Unit/CI results do not close G3 interoperability gates.

**Still pending:** normal receiver-stop reason classification, actual established
receiver network loss then fresh-cast recovery, longer playback and other
receiver firmware/hosts. The user is available for remote/sleep checks but cannot
perform network disconnect/reconnect in this session.
A preliminary EOF probe paused near startup; seek preserved that pause and the
probe needed explicit Play. Later cycles reported playing. That intermittent
buffering/startup behavior remains unresolved; it is not evidence of EOF failure.

Completed remote-stop and sleep/wake checks are recorded above. The remaining
network continuation uses the same paired profile and supplied local MP4:

1. Start a fresh native cast, confirm video/audio and owned status, then
   disconnect the receiver's network during playback. Keep stdin open and wait
   for terminal failure/cleanup (a silent data-only peer may take about 35 seconds).
2. Restore the network, start a fresh cast with the retained profile, and confirm
   playback. Record actual interruption/recovery separately from scripted
   deadlines, receiver sleep/wake and short-cycle results.
3. Investigate receiver-stop classification with sanitized channel/event facts;
   do not infer normal stop solely from pause followed by disconnect. Preserve
   ordinary pause and genuine outage detection. Investigate repeated loading/
   playing and startup pauses during longer playback.

## Native receiver-stop diagnosis: EOF cleanup PASS, full-clip video FAIL, 2026-10-07

Same receiver/firmware, Windows static Release and supplied 131.6-second MP4.
The [sanitized artifact](validation/native-stop-buffering-windows-static-2026-10-07.json)
fingerprints the CLI and tested source blobs; its media hash was rechecked. No
pyatv controller, new credentials or firewall/network changes. The allowed
executable was restored after the checks. D34 diagnostics use fixed channel/error
categories; per-event state outlines prevent a transient terminal state being
hidden by the CLI's latest-state display.

**Repeated remote Stop:** the user exited playback and confirmed normal Home
return. URL events remained playing until the event socket reported disconnected
at 13.9 s after cast start. All received state events were loading/playing; none
were stopped, idle or ended. Ordered cleanup completed at 14.0 s: connection_lost,
failed=yes, cleaned=yes, exit 1, failure_channel=url_events,
failure_reason=disconnected, failed_reads=0 and full-file span. This identifies
the socket failure; it does not distinguish a user stop from an unexpected event
connection closure using the parsed protocol facts. Unmapped notification names
and reason values were omitted; shapes alone cannot establish terminal intent.
Keep connection_lost until explicit receiver intent is validated. A new scripted
pause-then-EOF regression preserves genuine outage detection during pause.
The earlier timed-out operator window ended locally and provides no remote-stop evidence.

**Full-clip natural EOF, protocol/process PASS:** a fresh cast received only
`status` input every five seconds, with stdin held open and no seek or transport
command. Owned playing snapshots progressed from 4.1 to 129.5 s; these snapshots
can extrapolate receiver timestamps and are not independent continuous clock
measurements. Four heartbeats were acknowledged. Two loading/playing pairs
occurred about 17.3 and 18.3 s after playing began: the first spanned about 0.4 s,
the second shared a tenth-second trace timestamp. There were no later loading or
paused state events in this run. URL stopped at 133.1 s after cast start, and
cleanup finished at 133.9 s: media_end, cleaned=yes, failed=no, exit 0,
reads=1652, bytes=108091430, failed_reads=0, full span [0,53953926).
That is a complete clip, rather than the earlier near-end seek experiment.
**Observer follow-up: sustained video presentation FAIL.** Around the 18-second
mark the user observed a short buffering stop. Afterwards the video remained
frozen, while audio continued playing normally until clip end. This agrees in
timing with the loading/playing pairs, but does not establish their cause. Returned
playing state, advancing/extrapolated position, full-file fetch and successful
heartbeats did not reveal the frozen picture. Natural EOF/cleanup remains a
protocol/process PASS; this run is not successful full-clip video playback.
Home return for this particular run remains unconfirmed. Earlier separate
near-end EOF and short-session observations remain unchanged. This D34 gate
initially prioritized video-freeze investigation; D37 subsequently lowers its
priority unless it recurs. No automatic retry/resume workaround has been validated.
The user confirmed moving video past 18 s during normal local PC playback of
the same original file. A receiver-specific media/decoder issue is still possible;
this comparison does not prove a particular casting fault.

**Diagnostic limits and next experiment:** file counters are updated before
asynchronous TCP writes complete. Full-file read span and failed_reads=0 therefore
do not establish completed HTTP bodies or successful receiver decoding. The
600000 ms media-request deadline used by `cast` exceeds this full run, so the
default 30 s server deadline is not an explanation. The recorded outlines omit
buffer/stall values and per-request completions/write failures. Next add bounded
numeric range/status/write/completion and concurrency facts, and reviewed buffer/
stall scalars, then correlate a controlled reproduction with visible video.
No root cause or recovery behavior is established by the current trace.

Final Windows static/shared Release each passed 23/23 CTest targets
(13.13/13.05 s), offline runner contracts 10/10, touched C++ formatting and
git diff --check. All ten checks passed at diagnostic code/test head
`27356a8279c25da30821af5c708e5d4bbed7baf4` in
[CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37556596080).
Inspect actual-head CI after this documentation-only observer follow-up. G3 remains
open for sustained video recovery, explicit normal-stop classification, actual network interruption and
fresh-cast recovery; the user cannot perform the network checkpoint this session.

## Automated CLI E2E observation: 2026-10-06

The agent ran the [noninteractive runner](e2e-runner.md) from
[PR #9](https://github.com/ilyalissoboi/send-airplay2/pull/9), initially introduced
at `2bf31dd8f61d17e4475ec36c78f81bcac21f0009` and then hardened for closed-port
refusal portability at `650a0440a72d5e263bcca3fa8e6aa623cd628a7f`.
The report fingerprints identify the tested runner bytes.
The runs used Windows 11 x64,
observed Windows build `10.0.26200` / AMD64, using the existing user-paired profile
and the known Living Room Apple TV 4K / advertised `AppleTV14,1` / user-reported
tvOS 26.6. Static/shared Release CLIs contain the unchanged C++ code merged in
PR #8 (`f24ac4825a47b2b641caaac1d0f1c00dd1058c33`). No PIN entry, enrollment,
receiver revocation/settings change, reboot, network-interface change or primary
credential deletion occurred during these runs.

Each default run passed 16 checks with one skipped opt-in deletion case:

- **Live discovery:** two 5-second scans each found one matching target device
  without duplicate device/service records or warnings; advertised target identity
  was consistent. This does not test actual departure/interface changes.
- **Live authentication:** baseline verification, three independent reconnects
  and verification after each of three injected faults all passed with exit 0.
  These prove stored-credential reuse/fresh verification, not playback or encrypted
  application request/response exchange.
- **Local native CLI:** existing-profile refusal, random missing-profile refusal,
  redirected PIN refusal and absent-profile forget all passed with expected exact
  checkpoints/exit codes. No profile was created or deleted.
- **Loopback faults:** a selected/released closed port produced a network failure;
  an accepting silent peer produced the native deadline error; a peer closing
  after request bytes produced disconnect/network failure. These peers never
  forwarded to Apple TV and retained no payloads. Actual Apple TV network outage,
  reconnect after host/receiver reboot and established-session interruption remain
  NOT RUN; this evidence does not change those hardware gates.
- **Disposable deletion:** SKIPPED, not opted in. Real-profile deletion/idempotence
  remains a live gate; its orchestration has offline synthetic coverage.

Sanitized machine-readable artifacts contain runner/CLI SHA-256 fingerprints,
OS/build/architecture, endpoint address family/port/scope usage and constructed
case facts. Addresses, identities, profiles, paths, TXT/public keys, PINs, raw
child output and exception text are omitted:

- [Static CLI E2E report](validation/e2e-windows-static-2026-10-06.json).
- [Shared CLI E2E report](validation/e2e-windows-shared-2026-10-06.json).

These artifacts are selected-case evidence for this receiver/firmware/host only.
Offline runner tests and native/sanitizer CI remain distinct from device evidence.


### Instrumented HTTP/buffering investigation, 2026-10-07

D35 adds bounded per-request socket-write/completion facts and allowlisted finite
receiver scalars; neither constitutes decoder or visual playback proof. The same
53,953,926-byte MP4 and SHA-256 were retained, with no pyatv controller. Each run
fingerprints its own executable and source blobs; the explicit comparison flag
was added after the first run, so the executable fingerprints differ.

The first control-enabled reproduction did not reach sustained playback. It
reported playing briefly, then paused at zero about 1.6 s after cast start. The
user saw the first frame and confirmed no remote buttons were pressed. MRP
remained owned/paused at zero with five successful heartbeats. At the 175-second
observation deadline the driver sent Enter; ordered cleanup reported sender_stop,
cleaned=yes and no session failure. This is a separate startup failure, not a
reproduction of the earlier moving-video freeze near 18 s. Audio and Home return
were not confirmed. The driver timed out rather than reaching natural EOF; its
exit-code field was not captured on that path, so no exit code is inferred.

Eight GET range requests were recorded: six completed and two ended with I/O
errors after partial writes. Request 2 selected the full file but wrote 4,849,664
bytes; request 4 selected offset 3,158,760/length 23,842,072 and wrote 13,107,200.
Their last reported body progress was at 720/1,576 ms after server construction;
I/O completion arrived at 19,726/20,534 ms. No media request reached its 600,000 ms
deadline. Source reads totalled 58,908,600 bytes, while socket writes totalled
58,777,528 bytes: two read chunks were never reported written. Overlapping written
ranges covered the representation, which still does not prove receiver receipt,
per-request completion or decoding. Last-write timestamps describe completion
callbacks, not packet-level progress within an outstanding write.

A second full-clip comparison explicitly uses the existing minimum native remote
SETUP/event-only session, omitting remote RECORD/data/MRP/remote feedback. Logging,
file source, HTTP range policy and four-connection budget are unchanged. Its
user confirmed normal video for the whole clip without buffering/freezing, normal
audio and Home return at EOF. Its URL stopped event produced receiver_stop/exit 0
without MRP EOF evidence. No fallback or automatic Play/seek retry is introduced. Aborted read-ahead requests alone do not identify
which side intentionally cancelled a transfer or establish the freeze's cause.


The same-executable MRP/four-slot repeat instead buffered and recovered. URL
loading intervals were 56.0-62.4, 64.6-69.1 and 105.9-106.0 seconds on the driver
clock (cast began at 40.1). The user observed buffering near clip time 18 s, then
normal video/audio through EOF and Home return. All four slots were occupied at
server times 13,933-22,116 and 22,202-28,747 ms; a new small range was accepted at
22,116/28,747 ms exactly when an older stalled response ended. Natural media_end,
exit 0 and joined cleanup passed. `stallCount=0`/readyToPlay=yes on playing events
did not exclude these observed pauses. MRP position extrapolation ran ahead during
the loading interval and later corrected; it is not a video-progress measurement.

For the next comparison only the media limit increased to 16 (MRP stayed enabled;
the server API already supported 1..16). The trace admitted low-offset ranges
with five/six active requests and had no recorded loading transition. The user
confirmed normal video/audio for the entire duration and Home at EOF. Natural
media_end, exit 0 and cleanup passed. This supports admission-capacity starvation
as a cause of the buffering pauses: long-lived read-ahead transfers consume the
four-slot budget while new requests wait for acceptance. Backlog arrival times
are not measured, so the timing remains inference, not a packet-level proof.
At this D35 gate the default remained four; `--media-connections 16` was an
explicit comparison. D36 below records the subsequent policy decision.

| Run | HTTP requests | Complete / I/O / cancelled | Local body bytes written | Presentation / end |
|---|---:|---|---:|---|
| MRP/four, startup pause | 8 | 6 / 2 / 0 | 58,777,528 | First frame/paused at zero; sender deadline/Enter cleanup |
| Minimal remote/four | 30 | 12 / 17 / 1 | 100,423,718 | Normal full video/audio/Home; URL stopped cleanup, exit 0 |
| MRP/four repeat | 27 | 13 / 14 / 0 | 99,581,886 | Buffered then normal full video/audio/Home; media_end, exit 0 |
| MRP/16 | 29 | 14 / 15 / 0 | 125,410,732 | Normal full video/audio/Home; no loading transition; media_end, exit 0 |

No request hit its server deadline and no source read failed. Successful playback
also had I/O errors on abandoned read-ahead requests; those alone do not establish
failure. The original frozen-video/audio-continuing run remains a separate FAIL,
and the spontaneous startup pause remains unresolved. The next action at this
D35 gate was the controls/lifecycle capacity comparison recorded under D36 below;
do not reduce MRP functionality or add automatic Play/seek to conceal failures.
Static/shared Release passed 23/23 CTest targets (13.74/13.39 s), offline runner
contracts 10/10, touched C++ clang-format and diff checks passed. CI is separate
and must be inspected at PR #12's actual head. The four exact runtime/source and
observer records are in the [sanitized artifact](validation/native-http-buffering-windows-static-2026-10-07.json).
Each staged allowed executable was restored; no firewall or receiver settings changed.


### Bounded cast admission policy and controls/lifecycle checks, 2026-10-07

D36 repeats the explicit `--media-connections 16` configuration with MRP enabled
on the same receiver/firmware/host and 53,953,926-byte MP4. The native static CLI
SHA-256 was `0d77c4719c05a8d90ad36e1609c0be5ad6da3fac82276ecd8132bfd36e009641`.
The [sanitized artifact](validation/native-capacity-controls-lifecycle-windows-static-2026-10-07.json)
fingerprints its pre-policy source blobs and records scalar events, commands and
HTTP completion facts. These checks use an explicit override before changing
the default; rebuilt default-policy CTest/CI results are separate.

| Case | Automated result | Human observation |
|---|---|---|
| First 16-slot attempt | Playing line then paused at zero, before any transport command; status owned/paused; Enter cleanup, exit 0 | FAIL: first frame only; remote untouched |
| Fresh 16-slot controls | Six accepted commands; pause, Play, seek 65, seek 15, Play, Stop; owned positions 25.2, 26.0 paused, 34.1, 72.7, 22.7, 31.7; sender_stop/exit 0, joined cleanup | PASS: all requested operations performed as expected, including video/audio and Home after Stop |
| Ten fresh short processes | Five MRP Stop/five Enter; each owned/playing around 4.2-4.3 s, sender_stop/exit 0, cleaned=yes, failed=no, no failed file reads | Individual visual results not asserted for these ten cycles |
| Full 131.6-second repeat | No transport command; stdin open; natural media_end/exit 0, cleaned=yes, failed=no, four heartbeats, full-file source span, no loading transition | PASS: normal video/audio through the full clip and Home at EOF (user confirmed) |

The startup failure used only two active media requests; it appeared about 0.1 s
after the playing line, and receiver scalar rate was zero. The test did not
send Play to conceal it. Higher admission capacity does not eliminate this
intermittent failure. The original 18-second buffering/frozen-video failure also
remains in its earlier record. Successful selected runs do not establish longer
reliability or decoder progress from status alone.

The successful control run recorded 26 media requests (13 complete, 11 I/O ends,
two cancelled), with up to six active slots and 147,806,246 locally written body
bytes. The full-clip repeat recorded 38 (18 complete, 20 I/O ends), up to five
active slots and 125,148,588 locally written bytes. No media deadline or source
read failed. Abandoned read-ahead I/O ends also occur in successful runs. Socket
completion remains distinct from receiver receipt/decoding; request arrival in
the backlog is not measured.

Normal `cast` now defaults to 16 bounded media slots; an explicit 1..16 override
remains available. Generic `MediaServerOptions` and `serve` retain their four-slot
default. This allows at most 16 source workers and 1 MiB of body buffers, with
the existing 600,000-ms request budget and range/cleanup behavior. The choice
uses the earlier D35 full video/audio/Home pass, confirmed D36 controls and
selected lifecycle coverage; it is not a claim that capacity fixes the original
persistent freeze or startup pause. No automatic Play/seek, retry or new dependency.

The allowed executable was restored after every run; no receiver/firewall setting
changed. Receiver remote Stop and sleep/wake are the earlier four-slot evidence,
not repeated 16-slot observations. Normal receiver-stop intent remains open;
actual network interruption remains NOT RUN because the user cannot perform it
this session. Investigate startup ordering/state and sustained moving-video
reliability next. Local tests and actual PR-head CI remain separate from hardware.

Windows static/shared Release passed 23/23 CTest targets each (13.48/13.44 s),
offline runner contracts 10/10, touched C++ clang-format dry-run/Werror and
git diff --check passed. Inspect CI at the actual PR head.


### Frozen-video triage, user decision D37, 2026-10-07

The user considers insufficient media connections the most likely cause of the
original frozen-video failure and requests low priority for now, unless it recurs
in later testing. D35 admission timing and the successful 16-slot D35/D36 runs
support that working explanation. Causality is not proven and the original FAIL
record is retained. Monitor later playback tests and raise priority if video
freezes again. The separate paused-at-zero startup failure remains active and
is the next investigation. This changes task priority only; no runtime or
receiver behavior changed and no new hardware result is inferred.


### Startup confirmation and deferred manual batch, D38, 2026-10-07

During these D38 samples the user was unavailable for manual observation.
These new native runs therefore record **telemetry only**; moving video, audio
and Home return are NOT RUN. The existing Apple TV 4K/tvOS/Windows combination
and 53,953,926-byte MP4 are unchanged to our knowledge. All runs used the normal
16-slot media default and an explicit 10,000-ms startup deadline. Only native
status and Enter were sent; there was no Play/seek retry or pyatv control.

The first instrumented candidate rejected explicitly stationary/reverse-rate
playing, but still returned immediately on positive-rate playing. Twelve short
trials alternated MRP and minimal remote. In the first MRP trial, all four queue
commands had been acknowledged by 450 ms; playing/rate 1 arrived at 767 ms and
was immediately accepted. Paused followed about 0.4 s later, and owned status
remained paused at position zero. Enter cleaned up normally. The other five MRP
and six minimal-remote samples remained playing during short observations.
Rate-only readiness is therefore insufficient. Order/idle/transient-state
confounding prevents a conclusion that MRP causes the pause. All four command
acknowledgements preceded first playing in these and the final samples; this
does not exclude other receiver races.

D38 now requires 1,000 ms of continuous eligible URL playing before `start()`
returns. Every observed loading/pause/zero/reverse-rate state resets the interval;
missing rate retains state-only compatibility. Confirmation consumes the original
startup deadline and respects cancellation, with ordered cleanup on failure.
It neither resumes the receiver nor guarantees playback after startup returns.
`--event-log` retains a fixed 64-record startup trace, including failed starts:
relative steady times, local phases, successful queue-command HTTP statuses,
allowlisted states, finite numeric rates, truncation and cleanup status. No peer
metadata, URLs or authentication material are added to those records.

| Candidate / sample | Automated result | New human observation |
|---|---|---|
| Exploratory rate-only, six MRP/six minimal | First MRP returned then paused; other short samples remained playing; all Enter cleanups exited 0 | NOT RUN |
| Final 1,000-ms confirmation, three MRP/three minimal | All six returned after 1,002-1,025 ms of eligible playing and cleaned up with exit 0 | NOT RUN |
| Same final candidate, one MRP after 300 s without a native session | First playing/rate 1 at 1,138 ms, ready at 2,159 ms; short status/Enter cleanup passed | NOT RUN |

Every process ended by sender Enter with cleaned=yes, failed=no and no failed
source reads. These are short samples, not new full-clip EOF or lifecycle passes.
The final CLI SHA-256 was
`0e67c6e03dd09c4cdacf37678949b0fcd4c4f4e02305d4a228fac5a2903e22ae`;
the exploratory CLI had a distinct fingerprint. The
[sanitized artifact](validation/native-startup-confirmation-windows-static-2026-10-07.json)
preserves exact runtime/source fingerprints, per-trial scalar traces and the
variant distinction. After sampling, seven direct standard includes were added
to the final runtime source and regression timing/names were finalized; the
include-only runtime difference was verified against the recorded blob hashes.
The rebuilt submitted code is tested separately below. Every staged allowed
executable was restored; no firewall, credentials or receiver settings changed.

Windows static/shared Release passed 23/23 CTest targets each (15.41/15.22 s),
offline runner contracts 10/10, and touched C++ clang-format dry-run/Werror plus
git diff --check passed. New scripted cases cover stationary playing, positive
playing interrupted during confirmation, original deadline, cancellation,
invalid interval, positive recovery after zero-rate states and fixed-log overflow.
CI must be checked at the actual PR head; none of these checks establishes visible
receiver presentation.

The physical startup pause remains unresolved. Complete the
[deferred manual batch](manual-validation.md) when the user announces availability:
fresh startup, controls/full EOF, remote Stop and sleep/wake. Results were PENDING
at this gate; D39 below records the subsequent observed batch. Network interruption remains NOT RUN because the user
cannot perform it this session. D37's frozen-video issue stays low priority
unless it recurs; healthy scalar telemetry cannot establish that it did not recur.


### Observed normal-default manual batch, D39, 2026-10-07

The user returned and performed the [queued checks](manual-validation.md).
Six native casts used the submitted D38 runtime on the same recorded Apple TV
4K/tvOS 26.6/Windows 11 combination and 53,953,926-byte MP4. Receiver firmware
is assumed unchanged from the earlier user report. No pyatv, explicit media
capacity/start-timeout override, credential deletion, re-pairing or startup
recovery command. Normal MRP/16-slot/30,000-ms deadline/1,000-ms confirmation
defaults were used. Every start confirmed after 1,011-1,027 ms of eligible URL
playing, and every process joined cleanup.

The static CLI SHA-256 was
`f5f7fcd3ef15307c7e64294875f405e115d4452c6c71760cda84c6ee56970309`.
The [sanitized six-case artifact](validation/native-manual-batch-windows-static-2026-10-07.json)
records exact runtime/source fingerprints, scalar traces, commands and separate
observer quotes. All sampled runtime blobs match tested head
`fa9b9189f599b0b6405b8c1b81327761900470c2` (D38 code head `6bed9fe`), including
the final direct-standard-include adjustment. This is a newly rebuilt binary,
distinct from the earlier unattended D38 sampling fingerprint.

| Case | Native result | User observation |
|---|---|---|
| First startup, 45 s observation then Enter | Confirmed in 1,011 ms; owned/playing; sender_stop/exit 0, cleaned=yes | Normal video/audio until Stop, then Home |
| Fresh after sender Stop/Home, controls | Confirmed in 1,019 ms; six accepted controls; pause, Play, seek 65, seek 15, Play, Stop; sender_stop/exit 0 | Everything requested performed as expected: startup, visible pause/resume/both seeks, resumed presentation and Home |
| Separate full clip, status only, stdin open | Confirmed in 1,011 ms; natural media_end/exit 0, four heartbeats, no post-start loading transition, cleaned=yes | Normal video/audio for the clip, automatic Home at EOF |
| Receiver remote Stop, stdin open | Confirmed in 1,017 ms; URL event disconnect while last state playing, no terminal state; connection_lost/exit 1, cleaned=yes | Home appeared normally after the remote-Stop cue |
| Sleep, stdin open | Confirmed in 1,027 ms; paused then URL event disconnect; connection_lost/exit 1, cleaned=yes | Screen off after the sleep cue |
| Fresh after wake/Home, same credentials | Confirmed in 1,023 ms; owned/playing, planned seek 124/Play, media_end/exit 0, cleaned=yes | Home visible before cast; normal video/audio, Home after near-end EOF |

The first run's initial Home and untouched remote were not explicitly answered;
retain that condition limit. The next cast followed user-confirmed sender
Stop/Home. The full run has direct normal-video/audio/EOF/Home evidence; healthy
status is not used as a substitute. No new freeze was reported in this batch.
Selected successful starts do not establish the cause of the historical
intermittent startup pause or prove it fixed. No matched minimal-remote comparison
was triggered because these starts succeeded.

| Case | HTTP records | Peak active | Complete / I/O / cancelled | Local body bytes written |
|---|---:|---:|---|---:|
| First startup | 23 | 4 | 10 / 11 / 2 | 89,199,264 |
| Controls | 30 | 7 | 16 / 13 / 1 | 147,161,022 |
| Full clip | 30 | 6 | 15 / 15 / 0 | 124,624,300 |
| Remote Stop | 16 | 5 | 8 / 4 / 4 | 77,019,704 |
| Sleep | 27 | 7 | 14 / 10 / 3 | 89,264,800 |
| After wake | 23 | 5 | 12 / 8 / 3 | 83,764,542 |

No source read or HTTP request deadline failed. Abandoned read-ahead I/O ends
also occurred in the observed passing runs; they do not establish receiver
failure. Local writes and source spans remain distinct from receiver receipt
and decoder progress. Every staged allowed executable was restored; no firewall,
network or receiver settings changed.

Remote Stop/Home and automatic cleanup are observed, but explicit normal-stop
protocol intent remains OPEN: socket EOF alone conservatively stays a connection
failure. Sleep is separate from an actual network disconnect/reconnect, which
remains NOT RUN under the prior user constraint. D37's frozen-video issue remains
low priority unless it recurs, with the original FAIL retained. Longer reliability,
other receiver/firmware/host combinations, public ABI and packaged-host gates
remain open. This gate changes evidence and documentation only.

The unchanged D38 code already passed Windows static/shared Release CTest 23/23
each, offline runner contracts 10/10 and touched C++ format/diff checks. All ten
CI checks passed at both the [code head](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37564851802)
and [tested documentation head](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37565052646).
Inspect the actual PR head for this documentation/evidence follow-up. Local/CI
results remain separate from the receiver observations above.

## Remote-Stop diagnostic comparison (D40, 2026-10-07)

This slice adds bounded remote notification observations and retained final MRP
state under `--event-log`; termination priority and exit behavior are unchanged.
The selected host/receiver/media/profile and normal MRP/16-slot/30-second startup
budget/one-second confirmation are the same as D39. Receiver firmware is assumed
unchanged from the prior user report, not newly queried. Exact source blobs,
executable/media fingerprints, scalar traces and observer scope are in the
[D40 artifact](validation/native-stop-diagnostics-windows-static-2026-10-07.json).

The first attempt was unobserved: the user requested repetition. Its pause/URL
disconnect and owned/paused final MRP snapshot cannot be attributed to remote
Stop or counted as a Home/presentation result. It is retained separately. That
executable preceded a code-equivalent direct-standard-header adjustment; the
final sources were rebuilt and tested in both configurations before the repeat.

The repeated Stop, sleep and fresh post-wake cases used static CLI SHA-256
`da3f85a3f4b8f0595834b0462250859ee56c3bab89c38acdd632434d889f6e83`.

| Case | Native result after joined cleanup | Observer scope |
|---|---|---|
| Repeated remote Stop, stdin open | URL event disconnect while URL playing; final MRP unowned/unknown, at_end=false, 21 messages; connection_lost/exit 1, cleaned=yes | Home appeared normally in response to Stop cue; presentation before Stop was not separately answered |
| Sleep, stdin open | URL paused; URL feedback disconnected first; final MRP owned/paused, received position 0 s, duration 131.567 s, rate 1, at_end=false, 24 messages; connection_lost/exit 1, cleaned=yes | Screen off confirmed in response to sleep cue |
| Fresh cast after wake/Home, same credentials | Owned/playing; planned seek 124 s and Play accepted; media_end/exit 0, cleaned=yes; final MRP owned/paused at received position/duration 131.567 s, rate 0, at_end=true, 26 messages | Home before cast confirmed; normal video/audio and Home after EOF confirmed |

Remote observations were fixed-label updateInfo outlines (five in the Stop case,
six for sleep), without an allowlisted terminal state or reason/error/status code.
Retained MRP is the last processed snapshot, not a fresh receiver query or proof
that every final message arrived before cancellation. Loss of ownership and
pause alone are not normal-stop signals. Independent socket/feedback ordering
also changes which failure is observed first. This selected comparison therefore
does not justify converting connection failure to normal Stop. Classification
remains OPEN and conservative; actual network disconnect/reconnect remains
NOT RUN. No new dependency, credential replacement or receiver/network/firewall
setting change was introduced. Staged executables are restored after each run.
Fresh playback after wake reused the existing credentials and passed observed
video/audio/near-end EOF/Home. No new buffering or freeze was reported; D37's
historical frozen-video issue remains low priority unless it recurs. This is a
selected recovery observation, not an established-session network-loss proof.

Final Windows Release static/shared CTest passed 23/23 each (15.67/15.53 s), offline
runner contracts 10/10, touched C++ format checks and `git diff --check` passed.
Inspect CI at the actual published D40 PR head. Those checks are separate from
the selected receiver observations above.

## Ethernet interruption and fresh recovery (D42, 2026-10-07)

The user became available for the actual network check, previously NOT RUN.
Two native casts used the unchanged D40 runtime on Windows 11 x64 and the same
Living Room Apple TV 4K (AppleTV14,1), media file and stored profile. tvOS 26.6
(23L773) is assumed unchanged from the prior user report, not newly queried.
Normal MRP/16-slot/30-second-startup/one-second-confirmation defaults applied.
Static CLI SHA-256 was
`da3f85a3f4b8f0595834b0462250859ee56c3bab89c38acdd632434d889f6e83`.
Tested source blobs match D40 implementation head
`4a18b2662150768a00154ca17e222f2e701fd192` and the actual tested PR head
`72b3059c1a1fa11cd641c24f20764b5b99e6e535`. Exact hashes, traces and observer
quotes are in the [artifact](validation/native-network-recovery-windows-static-2026-10-07.json).

| Case | Native result | Observer scope |
| --- | --- | --- |
| Established cast, user removes Ethernet cable, stdin open | Owned/playing before cue; later URL paused and URL feedback timeout; automatic connection_lost/exit 1, cleaned=yes; final MRP owned/paused, received position 0 s, duration 131.567 s, rate 1, at_end=false, 24 messages and one heartbeat | Disconnection confirmed; cable explicitly confirmed as Ethernet/network. Video/audio before action was asked about but not reported. |
| Fresh process after reconnection/Home, same credentials | Owned/playing; planned seek 124/Play accepted; URL stopped then owned received EOF; media_end/exit 0, cleaned=yes; final MRP owned/paused, received position/duration 131.567 s, rate 0, at_end=true, 21 messages and one heartbeat | Home after reconnection confirmed. Normal video/audio and Home after EOF confirmed; separate buffering/freeze detail was not supplied. |

The interrupted process required no sender Stop/Enter; only `status` was sent
and stdin remained open. The user was cued to leave the network disconnected
until cleanup completed, then to reconnect. User action times were not synchronized
with process logs; the known cue-to-cleanup interval is not disconnect-detection
latency. This tests automatic failure cleanup and a fresh explicit cast after
reconnection, not automatic in-session reconnect/resume. The recovery cast ended
after a planned near-end seek; it adds no full-clip proof.

Both cases joined cleanup and restored the staged allowed executable, verified
by SHA-256 equality. No sender firewall/network setting, credential or runtime
source changed. The user temporarily removed and restored the receiver Ethernet
connection. The selected network cleanup/fresh recovery gate is PASS. D40 Stop
ended on URL event EOF while this network sample ended on feedback timeout; that
difference and retained ownership do not establish a reliable cause classifier.
The existing conservative policy remains: ambiguous connection failure stays
connection_lost/exit 1. Remote-Stop intent, intermittent startup pause, broader
network cases and longer reliability remain open. D37's frozen-video priority
and original FAIL evidence remain unchanged.

No rebuild or local CTest rerun was needed for this evidence-only slice. The
unchanged D40 runtime retains its Windows static/shared Release 23/23 results
and offline runner 10/10. All 51 PR-changed C++ files passed clang-format checks,
and `git diff --check` passed. All ten CI checks passed at tested head `72b3059`
in [this run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37579365861).
Inspect the actual PR head after publishing this evidence follow-up; CI remains
separate from the selected receiver observations above.

## Public playback interface manual batch (D48, 2026-10-08)

First receiver runs through the experimental C interface
([public-api.md](public-api.md)) and the first on the D47 teardown runtime.
Source head `8a7050d8e17a7ec7684ef0dbcb3561c78731660c`; Windows 11 x64; the
Living Room Apple TV 4K (AppleTV14,1), tvOS 26.6 (23L773) assumed unchanged from
the prior report and not queried; stored profile, no pairing. A local driver
discovered the receiver (address never printed), staged each tested executable
at a path with an existing inbound firewall rule, and restored the original with
SHA-256 equality. No firewall, network, credential or receiver setting changed.
Normal MRP/16-slot/30-second-start/one-second-confirmation defaults applied.
Executable hashes, tested source blobs, fixed-field output and observer answers
are in the [artifact](validation/native-api-host-manual-batch-windows-2026-10-08.json).

| Check | Native result | Observer |
| --- | --- | --- |
| 1. Start, 30 s, local stop (static host) | `Start: ok` in 1.8 s, owned/playing; sender_stop, cleaned, failed_reads=0, releases=1, exit 0 | Normal video/audio; Home after stop |
| 2. Pause, play, seek 60, seek 10, MRP stop | All `Control: ok`; paused at 11.1 s rate 0; positions 61.7 s and 11.5 s after seeks; sender_stop, exit 0 | All visible as expected; Home after stop |
| 3. Full clip, status only, stdin open | Positions 61.3/121.3 s on time; receiver stopped at end, media_end at 131.6/131.6 s; host exited by itself, exit 0 | Moving video throughout, including near 0:18; normal audio; Home at end |
| 4. Stop 1 s into a blocking start | `start=cancelled` at 1,078 ms (93 reads already made); releases=1; follow-up cast started in 1.8 s and stopped cleanly | A few frames showed, then Home; follow-up played normally and returned Home |
| 5. TV/Home button on the remote | URL events closed about 11.6 s into playback; connection_lost (url_events/disconnected), cleaned in 0.3 s, exit 1 (known classification) | Button pressed about 10 s in; Home |
| 6. Receiver sleep, then fresh cast after wake | Paused, then URL events closed about 5.5 s later; connection_lost, exit 1; final MRP position 0.0. Fresh cast after wake: same credentials, started, sender_stop, exit 0 | Sleep (not a cable pull); woke to Home; fresh cast played video/audio, then Home |
| 7. Check 1 with the shared build | `linkage=shared`; same results as check 1 | Normal video/audio; Home |
| Ten cycles in one process | 10/10 PASS alternating MRP+local and local stop; 10,000 reads, failed_reads=0, releases=10, exit 0 | Telemetry only |
| Film via `cast --media-log` | Seeks 4800/600 accepted, positions 4807.4/607.4 s of 6824.5 s; 94 requests all 206, 37 at offsets >= 4 GiB (about 82 MB served from there); 45 read-ahead requests closed by the receiver after seeks (`io_error`); failed_reads=0; MRP stop, sender_stop, exit 0 | E-AC-3 audio played normally; no Atmos indication; picture at about 1:20:00 with sound, then about 0:10:00; Home after stop |

The film was remuxed by the user from MKV with ffmpeg stream copy: H.264 High
level 4.0 1920x802 23.976 fps, E-AC-3 5.1 (Atmos extension flag set in `dec3`),
`mov_text` subtitles, index before media data and 64-bit `co64` chunk offsets.
It is personally held and not committed; the record identifies it by SHA-256,
size and codecs only. Check 7's first attempt found no receiver in discovery and
ran nothing. Not run: an unreachable-address check, Task Manager handle counts
during the cycles, and a network-cable interruption on this runtime (check 6
used sleep).

These results cover one receiver, firmware and host. Remote Stop/Home still
reports connection_lost/exit 1; no classifier change is implied. The original
frozen-video FAIL keeps its D37 priority; no freeze recurred in this batch,
including the full clip. Startup pause did not recur in this batch; that does not
establish its cause.

## Pairing through the public interface (D51, 2026-10-08)

First receiver pairing through `sap2_pair()` ([pairing.h](../include/send_airplay2/pairing.h)),
at source head `b87e1ae8366e569c757d12b6d6ef7548822821a9` on Windows 11 x64 with
the Living Room Apple TV 4K (AppleTV14,1), tvOS 26.6 (23L773) assumed unchanged
and not queried. `airplay2-api-host --pair` (static, SHA-256
`eb20a5c68aab9fee2809596fe7cb8c79209c7d4d115901b95246708656e8ecc7`) paired a new
profile, `living-room-api`, into Windows Credential Manager. The user typed the
PIN in a separate console window with echo off; it never reached any output, log
or record. The existing `living-room-test` profile was not touched. Details are
in the [artifact](validation/native-api-pairing-windows-2026-10-08.json).

| Step | Native result | Observer |
| --- | --- | --- |
| `sap2_pair()` | `Pair: ok`: authenticated enrollment, `save_new`, reload and verification on a fresh connection | PIN shown on the TV; console accepted it hidden and closed |
| Cast with `living-room-api` | `Start: ok` in 1.8 s, owned/playing; sender_stop after 20 s, cleaned, failed_reads=0, releases=1, exit 0 | Normal video/audio; Home after stop |

This adds one pairing entry on the Apple TV, which only its settings can remove.
Not run on hardware: pairing into a host-provided store, a wrong or cancelled
PIN, and `sap2_forget_profile()`; those remain unit-tested only. One receiver,
firmware and host.

## Packaged UWP host (D53, 2026-10-08)

The [C# binding](csharp-binding.md) ran inside a sideloaded, packaged UWP app
built like Screenbox (modern .NET UWP, Native AOT, `DisableRuntimeMarshalling`,
MSIX), at source head `fe4162c3692536a02eae19cd09374304b2d1a4a6`, on Windows 11
x64 with the Living Room Apple TV 4K (AppleTV14,1), tvOS 26.6 (23L773) assumed
unchanged and not queried. See [uwp-host.md](uwp-host.md) for the app and the
[artifact](validation/native-uwp-host-windows-2026-10-08.json) for hashes, tested
source blobs and the app's fixed-field log. The receiver address reached the app
through the clipboard and never appeared in any output.

| Run | Network | Capabilities | Native result | Observer |
| --- | --- | --- | --- | --- |
| Native load | Public | `internetClient`, `privateNetworkClientServer` | API version 2 inside the AppContainer | |
| Built-in store | Public, Private | either | `credential_store` for removing an absent profile and for casting with the existing `living-room-test` | |
| Pair into PasswordVault | Public | Screenbox's set | `Pair: ok`; one credential, kept across two in-place updates | PIN entered in the app's dialog |
| Cast, PasswordVault | Public | Screenbox's set | `connection` after about 5.3 s (about the request timeout) | TV played nothing |
| Cast, PasswordVault | Public | adds `internetClientServer` | `Start: ok` in 1.8 s; sender_stop after 42 s, cleaned | Normal video and audio |
| Cast and controls, PasswordVault | Private | Screenbox's set | `Start: ok` in 1.8 s; pause, play, seek 60, seek 10 ok; sender_stop, cleaned, source released | Video and audio, pause and seeks visible, Home after Stop |

The user switched the network to Private in Windows Settings for the last run.
One new Apple TV pairing entry exists for the `living-room-uwp` identity. Not run:
PasswordVault roaming, desktop provisioning of the app's locker, Store
certification, x86/ARM64 and discovery inside the app (since run, D54 below). The
native DLLs were built for the desktop C runtime and loaded because it is
installed system-wide.

## C discovery interface (D54, 2026-10-08)

First scans through [`receivers.h`](../include/send_airplay2/receivers.h), on top
of `271d1e0` with the D54 working tree (tested blobs, binary hashes and outputs in
the [artifact](validation/native-discovery-c-api-windows-2026-10-08.json)).
Windows 11 x64 on a Private network (read, not changed); the Living Room Apple TV
4K (AppleTV14,1), tvOS 26.6 (23L773) assumed unchanged and not queried.
Discovery only: no pairing, cast or credential access. Names and addresses were
compared in memory; hosts printed fixed fields only.

| Run | Result | Observer |
| --- | --- | --- |
| `airplay2-api-host --discover --discover-ms 3000 --expect-name "Living Room"`, static | `Discover: ok receivers=1`; IPv4 address, port, features; expected found; exit 0 | |
| Same, shared build | Same | |
| Packaged UWP host 0.1.4.0, Screenbox's capabilities, **0. Discover** (5 s) | `Discover: ok receivers=1`; IPv4, port, features; expected found; address box filled | The address box filled in |

**PASS for these runs:** multicast discovery works from native hosts and inside
the AppContainer with `privateNetworkClientServer` on a Private network. Not run:
Public networks or `internetClientServer` for discovery, a cast to the discovered
address inside the app, departure and interface changes, several receivers,
IPv6-only networks and other platforms. *Corrected in D55:* the reason first
given for not casting, that the app's PasswordVault was empty after the fresh
registration, was wrong; the D53 credential was still there.

## UWP native build (D55, 2026-10-08)

The native library built for app packages ([uwp-native-build.md](uwp-native-build.md))
ran in the packaged UWP test host on top of `ac7717f` with the D55 working tree
(tested blobs, hashes, logs and the resolved crash stack in the
[artifact](validation/native-uwp-native-build-windows-2026-10-08.json)).
Windows 11 x64 on a Private network (read, not changed); the Living Room Apple
TV 4K (AppleTV14,1), tvOS 26.6 (23L773) assumed unchanged and not queried.
Screenbox's capabilities. Casts used the D53 `living-room-uwp` PasswordVault
credential; pairing it again was refused with `profile_exists`, so no new pairing
was made.

Static checks (`scripts/check_uwp_binaries.ps1`): `send_airplay2.dll` and
`libcrypto-3-x64.dll` carry the AppContainer flag and link `VCRUNTIME140_APP`;
the library has no `ADVAPI32` import and no Botan DLL. In the running app the
`_APP` runtime DLLs loaded from the `Microsoft.VCLibs.140.00` framework package.

| Host build | Native result | Observer |
| --- | --- | --- |
| 0.1.6.0, first UWP-built library | Loads; built-in store probe `unsupported`; discover ok; cast starts in about 1.8 s; pause, play, seek 60 and seek 10 ok; **every Stop ends the process** (`0xc0000409` in `ucrtbase!abort`, Application Error 1000) | Initial attempt failed while the receiver was asleep; after waking it, video played, controls worked and the TV returned Home after Stop; the app closed after Stop |
| 0.1.8.0, RelWithDebInfo plus the host's abort trace | Stack resolved with the linker map: `sap2_cast_stop` → `CastController::stop` → `MediaServer::Impl::stop` → `asio::thread_pool::join` → `std::terminate` (Asio `winapp_thread`) | The app closed after Stop |
| 0.1.10.0, Release with the media server's own reader pool | Cast, pause, play, seek 60, seek 10; Stop in about 80 ms with `cleaned=yes`, source released; a second cast and Stop in the same process; no Application Error | Video and audio normal; app stayed open after both Stops; Home after Stop |
| 0.1.12.0, ended casts free the host's slot | Remote Home ended the cast (`connection_lost`, UrlEvents/Disconnected); slot released 24 ms later; next Cast started without Stop; Stop cleaned up | Second cast started; app stayed open; video and audio normal; Home after Stop |

**PASS for these runs** on one receiver and host. The Stop crash was specific to
app builds and is fixed in the library (D55). Remote Stop/Home still ends as
`connection_lost` (known). Not run: Windows App Certification Kit (needs
elevation), x86/ARM64, a device without VCLibs preinstalled, Public networks
with this build, pairing through the UWP-built library, and the slot release at
a natural end of media.

## Casting to a sleeping receiver (D56, 2026-10-08)

The user asked whether a cast can wake the receiver remotely and then chose the
order: reproduce, check pyatv's wake, then fix. All runs used `airplay2-cli
cast --event-log` (Windows 11 x64, MSVC Release static, on top of `ac7717f`)
with the existing built-in-store profile, `gas.mp4` and this worktree's existing
firewall Allow rule; the Living Room Apple TV 4K (AppleTV14,1), tvOS 26.6
(23L773) assumed unchanged and not queried. The user put the receiver to sleep
from the remote and waited about two minutes before each asleep run. Logs,
hashes and tested blobs are in the [artifact](validation/native-receiver-wake-windows-2026-10-08.json).

| Run | Build | Native result | Observer |
| --- | --- | --- | --- |
| 1, asleep | `main` | Session set up; `playing` at 885 ms, `paused` at 1272 ms; start timeout at 30.5 s, cleaned, exit 1 | First frame, then Home |
| 2, awake | power diagnostic | Handshake `logical_devices=1` at 138 ms, update 1 at 300 ms; `ready` at 1.8 s; local stop, exit 0 | Not observed |
| 3, asleep | power diagnostic | Handshake 0 at 169 ms, 0 at 312 ms; `insert_item` at 369 ms; `playing` at 684 ms; 1 at 712 ms; `stopped` at 1625 ms; 0 at 1787 ms; 1 at 2619 ms; start timeout, exit 1 | First frame, then Home; afterwards awake on Home |
| 4, awake | wake before play | No `waking` phase; `insert_item` at 354 ms; `ready` at 1.7 s; local stop, exit 0 | Not observed |
| 5, asleep | wake before play | Handshake 0 at 142 ms; `waking` at 198 ms; 1 at 757 ms, 0 at 1192 ms, 1 at 2639 ms; `insert_item` at 5338 ms; `playing` at 5693 ms; `ready` at 6.7 s; played 45 s; local stop, cleaned, exit 0 | Video played normally; Home after the stop |
| 6, asleep | wake before play, C interface (`airplay2-api-host`, static) | `Start: ok` after 6.7 s, owned and playing; 45 s; `sender_stop`, cleaned, `failed_reads=0`, `releases=1`, exit 0 | Everything worked as expected |
| 7, asleep | wake before play, C interface, shared library | `linkage=shared`; `Start: ok` after 6.7 s; 45 s; `sender_stop`, cleaned, `failed_reads=0`, `releases=1`, exit 0 | Everything worked as expected |

**PASS for runs 5-7 on this receiver and host** (CLI, and the C interface static
and shared). The receiver stays reachable while asleep and accepts the whole
session; the failure was starting playback while tvOS was still waking. Not run:
more than three sleep cycles with the fix, the UWP host on a sleeping receiver
(same session code), deeper sleep or an unreachable receiver, a TV without
HDMI-CEC power control, other receivers and firmware.
The intermittent startup pause recorded since D38 also showed a first frame and
a pause at zero. After this run the user reported that it was observed when the
Apple TV was waking from sleep. The same condition and symptom make D56's cause
its likely explanation and wake-before-play its likely fix; the earlier runs
themselves were not repeated, and their traces predate the power reports.

## UWP architectures and certification kit (D57, 2026-10-08)

The user asked to finish the standalone items except remote-Stop detection.
On top of `99942d1`, the library built for x86 UWP and passed the binary check
(x86, AppContainer, `VCRUNTIME140_APP`, no `ADVAPI32` or Botan DLL); ARM64 is
built and checked in CI only, since this Visual Studio lacks the ARM64 tools.
Details, hashes and logs: [artifact](validation/native-uwp-architectures-wack-windows-2026-10-08.json).

| Run | Result | Observer |
| --- | --- | --- |
| x86 test host 0.1.14.0 (x86 library, existing PasswordVault credential, Private network, Screenbox's capabilities) | x86 `_APP` runtime from VCLibs; discover ok; cast in 1.8 s; pause, play, seek 60, seek 10 ok; Stop `cleaned=yes`, source released; no Application Error | Everything worked as expected |
| Certification kit 10.0.28000.2526 on x64 test host 0.1.12.0 (run by the user, elevated) | 26 of 27 tests pass; Supported APIs fails for six kernel32 calls in `SendAirPlay2.UwpHost.exe` (the .NET Native AOT runtime); nothing reported for `send_airplay2.dll` or `libcrypto` | |

Not run: ARM64 locally or on a device, the kit on the x86 package or on a
signed Store-style package.

## HLS phase 1 (D60, 2026-10-09)

The user asked to add HLS support to the base library with scope "serve plus
built-in remux". Phase 1 casts a pre-made presentation: ffmpeg 9.0.2 cut
`gas.mp4` with `-c copy` into fMP4 VOD HLS (H.264 Main 720p30 + AAC-LC stereo,
20 segments, target 8 s, 22 files; not committed). `airplay2-cli cast --hls`
served it through a media server resource set (Windows 11 x64, MSVC Release
static on top of `2aad3c7`, existing built-in-store profile, this worktree's
existing firewall Allow rule) to the Living Room Apple TV 4K (AppleTV14,1),
tvOS 26.6 (23L773) assumed unchanged and not queried. Lines, request summary,
hashes and blobs: [artifact](validation/native-hls-directory-windows-2026-10-09.json).

| Run | Native result | Observer |
| --- | --- | --- |
| 1, before `near_end` | Playing at 860 ms, owned, duration 131.6; pause, play, seek 100 accepted; 21 GETs (playlist, init, 19 segments) each once, all 200 and complete; URL `stopped` at the end, final MRP paused at 131.483 of 131.566 s, `receiver_stop`, cleaned, exit 0 | Audio and video normal; all actions worked and were visible; Home after the stream finished |
| 2, with `near_end` | Playing at 951 ms, owned; seek 120 accepted; final MRP paused at 131.467 s; `media_end`, cleaned, `failed_reads=0`, exit 0 | Video and audio played after the seek; Home at the end |

**PASS for this receiver and host** with `mediaType` `file` and no other
session change. Not run: HEVC, AC-3/E-AC-3, separate audio, TS, gzip
playlists, `EVENT` playlists, the C interface and UWP host, a sleeping receiver,
other receivers and firmware. Library-built segments are phase 2.

## HLS phase 2 (D60, 2026-10-09)

Phase 2 casts `gas.mp4` through the library's own remux
(`airplay2-cli cast --file gas.mp4 --remux`): MP4 sample tables to fMP4 VOD
HLS, 20 segments, target 8 s, segments generated on demand from the file.
Windows 11 x64, MSVC Release static on top of `2642161`, existing
built-in-store profile and firewall Allow rule; Living Room Apple TV 4K
(AppleTV14,1), tvOS 26.6 (23L773) assumed unchanged and not queried. Offline,
ffmpeg found every packet identical to the source. Lines, requests, hashes
and blobs: [artifact](validation/native-hls-remux-windows-2026-10-09.json).

| Run | Native result | Observer |
| --- | --- | --- |
| Remux cast | Playing at 864 ms, owned, duration 131.6; pause, play, seek 100 accepted; 21 GETs (playlist, init, 19 segments) each once, all 200 and complete; URL `stopped` at the end with MRP `at_end` (131.576 of 131.566 s); `media_end`, cleaned, `failed_reads=0`, exit 0 | Video and audio normal; pause, play and seek visible; Home at the end |

**PASS for this receiver and host.** Not run: HEVC, AC-3/E-AC-3, MKV (phase
3), long files, the C interface and UWP host, a sleeping receiver, other
receivers and firmware.

## HLS phase 3a: MKV (D60, 2026-10-09)

Three MKVs made with ffmpeg 9.0.2 from `gas.mp4` (not committed), cast through
the library's remux with `airplay2-cli cast --file X.mkv --remux`. Windows 11
x64, MSVC Release static on top of `9fa8d6d`, existing built-in-store profile
and firewall Allow rule; Living Room Apple TV 4K (AppleTV14,1), tvOS 26.6
(23L773) assumed unchanged and not queried. Offline, every packet payload
matched the source, video presentation times matched exactly and audio times
within 0.33 ms. Lines, fixtures, hashes and blobs:
[artifact](validation/native-hls-mkv-windows-2026-10-09.json).

| Fixture | Native result | Observer |
| --- | --- | --- |
| H.264 + AAC-LC stereo 44.1 kHz | Playing at 816 ms, owned; pause, play, seek 110 accepted; 18 GETs all complete; `media_end` (MRP `at_end`), cleaned, `failed_reads=0`, exit 0 | Video and audio played; actions visible; Home at the end |
| HEVC Main + AC-3 5.1 384 kbit/s 48 kHz | Playing at 880 ms, owned; controls accepted; 24 GETs all complete; `media_end` (MRP `at_end`), exit 0 | Video and audio played; actions visible; Home at the end |
| H.264 + E-AC-3 stereo 256 kbit/s 48 kHz | Playing at 854 ms, owned; controls accepted; 18 GETs all complete; `media_end` (`near_end`, 131.5658 of 131.566 s), exit 0 | Video and audio played; actions visible; Home at the end |

| User-provided 1 h 54 min film (6.3 GB, H.264 High 1920x802 + E-AC-3 5.1 Atmos JOC; title withheld) | Remux built in 1.9 s (warm; 4.9 s cold); playing at 1033 ms, owned; pause, play, seek 3600 (to 3609.6 s after 10 s), seek 6780 accepted; 35 GETs all complete; `media_end` at 6824.5 s (MRP `at_end`), exit 0 | Video and audio played after each seek; seeks landed correctly; Home at the end; Atmos not checked (no Atmos hardware) |

**PASS for this receiver and host.** These are the first HEVC, AC-3 and E-AC-3
casts recorded for this project, and the first long file.

## HLS phase 3b: C interface (D60, 2026-10-09)

`SAP2_DELIVERY_HLS_REMUX` through `airplay2-api-host --remux` (C interface
only), MSVC Release on top of `5df2b80`, existing built-in-store profile;
Living Room Apple TV 4K (AppleTV14,1), tvOS 26.6 (23L773) assumed unchanged
and not queried. The network was read as Private; earlier records had Public.
Lines, hashes and blobs: [artifact](validation/native-hls-api-windows-2026-10-09.json).

| Run | Native result | Observer |
| --- | --- | --- |
| Static, user-provided 6.3 GB film MKV (H.264 + E-AC-3 5.1 Atmos) | `Start: ok` after about 4 s including the remux; pause, play, seek 3600 (3609.5 s), seek 6780 accepted; `media_end` at 6824.5 s, cleaned, `failed_reads=0`, `releases=1`, exit 0 | Video and audio normal; seeks worked; Home at the end |
| Static, HEVC + AC-3 5.1 MKV | `Start: ok`, owned; local stop after about 10 s, `sender_stop`, `releases=1`, exit 0 | Video and audio normal; Home after the stop (no seek in this run) |
| Shared, HEVC + AC-3 5.1 MKV, run in place | `Start: ok` in 1.9 s; pause, play, seek 115 accepted; `media_end` at 131.6 s, `releases=1`, exit 0 | Video and audio normal; seek worked; Home at the end |

**PASS for this receiver and host** (C interface, static and shared). The
user reported that video and audio played normally in every attempt that
started, that the runs seeked successfully and that every run returned to the
Home screen. Environment finding: three shared attempts staged at the earlier
shared path (`build-pairing-shared\release`), one of them a progressive
`gas.mp4`, failed with `connection` before playback. That executable has an
Inbound Block rule on the Private profile (Allow only on Public). The rule was
read, not changed; the shared host ran from its own path, which is allowed on
Private. Not run: the C# binding and packaged UWP host on the receiver.

## HLS phase 3c: text subtitles (D60, 2026-10-09)

The user's 6.3 GB film MKV (one English SubRip track) cast through
`airplay2-cli cast --remux` (MSVC Release static on top of `15acd95`, existing
profile; network read as Private) to the Living Room Apple TV 4K
(AppleTV14,1), tvOS 26.6 (23L773) assumed unchanged and not queried. The
remux served `main.m3u8` with one WebVTT rendition (English, DEFAULT=NO,
AUTOSELECT=YES). Offline, the WebVTT cues matched ffmpeg's conversion exactly.
Record: [artifact](validation/native-hls-subtitles-windows-2026-10-09.json).

| Run | Native result | Observer |
| --- | --- | --- |
| Film, subtitles enabled from the TV's menu | Playing at 1033 ms, owned; seek 3600 accepted (3639.4 s at the stop); 70 GETs all 200, including empty and single-cue WebVTT segments; local stop, `sender_stop`, cleaned, `failed_reads=0`, exit 0 | English offered in the subtitle menu; subtitles shown when enabled; in sync before and after the seek; video and audio normal |

**PASS for this receiver and host.** Not run: several or forced subtitle
tracks, ASS/WebVTT sources, sender-side subtitle selection, the C interface
with subtitles.

## Indexed MKV start (D60, 2026-10-09)

The user's film MKV through the indexed path (`cast --remux`, MSVC Release
static on top of `9b0e1d5`): segments planned from Cues and built on their
first request. Startup fell from 4.9 s (2.0 s warm) to under 0.1 s. Living
Room Apple TV 4K (AppleTV14,1), tvOS 26.6 (23L773) assumed unchanged and not
queried. Record: [artifact](validation/native-hls-indexed-mkv-windows-2026-10-09.json).

| Run | Native result | Observer |
| --- | --- | --- |
| Film, subtitles enabled | Remux ready in about 0.1 s; playing at 990 ms, owned; pause, play, seek 3600 (3609.5 s), seek 6780 (6785.4 s) accepted; 80 GETs all 200, one abandoned by the receiver at a seek; `media_end` at 6824.5 s, cleaned, `failed_reads=0`, exit 0; 395 MB read (2.8 GB before) | Everything worked as expected |

**PASS for this receiver and host.** Not run: real-world MKVs (other muxers,
long files), 7.1 E-AC-3, the C interface and UWP host, a sleeping receiver,
other receivers and firmware.

## MP4 text subtitles (D60, 2026-10-09)

A 10-minute MP4 made with ffmpeg from the user's film (H.264 + E-AC-3 + a tx3g
subtitle track with italics; not committed) cast through `airplay2-cli cast
--remux` (MSVC Release static on top of `ce8aaec`). Living Room Apple TV 4K
(AppleTV14,1), tvOS 26.6 (23L773) assumed unchanged and not queried.
Offline, the 190 cues matched ffmpeg's WebVTT conversion exactly. Record:
[artifact](validation/native-hls-mp4-subtitles-windows-2026-10-09.json).

| Run | Native result | Observer |
| --- | --- | --- |
| Controls and natural end | Playing at 1008 ms, owned; pause, play, seek 300, seek 560 accepted; 74 GETs all complete; `media_end` (`near_end`, 600.03 of 600.06 s), exit 0 | Everything worked as expected: subtitles without the menu, in sync after seeks; italic lines not reached |
| Italic section | Seek 95 s, played to 149 s; local stop, exit 0 | Italic lines rendered properly |

**PASS for this receiver and host.**

## UWP host: HLS remux and start position (D61, 2026-10-10)

Screenbox step 1, as the user ordered after choosing HLS remux for every file
(option B): the packaged x64 test host with Screenbox's capabilities and the
UWP-built library (`build-uwp`, on top of `6ae08fa`), driven by its new script
mode. Media: the user's 6.3 GB MKV film and the 10-minute tx3g MP4, both opened
as brokered StorageFiles. Living Room Apple TV 4K (AppleTV14,1), tvOS 26.6
(23L773) assumed unchanged and not queried. Record: [artifact](validation/native-uwp-host-remux-start-windows-2026-10-10.json).

First batch (0.1.20.0):

| Run | Native result | Observer |
| --- | --- | --- |
| 1: MKV, remux, start 0; pause, play, seek 1800, seek 600 | Start ok after 2520 ms; each control ok; positions 16.0, 1813.7, 613.9 s; local stop | Played normally; controls moved the TV; subtitles |
| 2: MKV, remux, start 3600 | Start ok after 2498 ms; position 21.0 s after 20 s | Started at 0:00; Home at the scripted stop |
| 3: MP4, remux, start 300 | Start ok after 2455 ms; position 21.0 s | Started at 0:00 |
| 4: MP4, remux, start 580; seek 580 | First attempt: a scan missed the receiver. Retry: position 4.0 s, seek ok, `MediaEnd` at 600 s, slot freed | Started at 0; played to the end; subtitles; Home |
| 5: MP4, progressive, start 120 | Start ok after 2421 ms; position 10.8 s | Started at 0:00 |

**Finding:** tvOS 26.6 accepts `Start-Position-Seconds` in `insertPlayQueueItem`
and plays from 0 anyway, for HLS and progressive media. A CLI diagnostic
(`cast --remux --start-position 300 --event-log`) agreed: URL and MRP
positions stayed at 0 and the video played from 0 (user). pyatv's AirPlay 2
`play_url` sends the same item, so it offers no other key.

**Fix (D61):** the session still sends the key and, for a start above zero,
waits up to 5 s for MRP ownership, then sends one seek before `start` returns.

Second batch (0.1.22.0, with the fix):

| Run | Native result | Observer |
| --- | --- | --- |
| 2: MKV, remux, start 3600 | Start ok after 2502 ms; seek's Loading then Playing; position 3629.1 s after 30 s | Everything worked as expected |
| 3: MP4, remux, start 300 | Start ok after 2474 ms; position 329.1 s after 30 s | Everything worked as expected |
| 4: MP4, remux, start 580 | Start ok after 2563 ms; `MediaEnd` at 600 s about 21 s later; slot freed | Everything worked as expected |
| 5: MP4, progressive, start 120 | Start ok after 2283 ms; position 139.0 s after 20 s | Everything worked as expected |

**PASS for this receiver and host.** The certification kit on 0.1.22.0 (run by
the user) matched D57: 26 of 27, Supported APIs failing only for the .NET AOT
runtime in the host executable. Not run: the x86 and ARM64 packages, a sleeping
receiver from the UWP host, MKVs without Cues through brokered access.

## Screenbox integration (2026-10-10)

The fork ilyalissoboi/Screenbox, using `SendAirPlay2 0.3.0-ci.196` (the
UWP-built library) in its packaged x64 app ("[Debug] Screenbox", Screenbox's
capabilities, Private network). Living Room Apple TV 4K (AppleTV14,1), tvOS
26.6 (23L773) assumed unchanged and not queried. All observations are the
user's; the fork's PRs hold the code.

| Fork phase | Checked on the TV | Result |
| --- | --- | --- |
| 2: discovery (#3) | Living Room listed in the cast flyout | As expected |
| 3: pairing (#4) | Pair shown, a wrong PIN rejected, Cancel silent, a real PIN paired, Cast shown afterwards; repeated after removing the pairing in Credential Manager | As expected |
| 4a: casting (#5) | Start at the current position (2:00); play/pause and seek from the app; stop from the app and from the TV remote, back to local playback paused at the last position; a natural end | As expected, after fixes found in the runs: the play/pause state after a cast, seek lag, the seek bar at start and on seek |
| 4b: queue (#6) | The next clip at the end of media; Next/Previous; repeat one; the end of the queue; the media keys and the Windows media flyout; stopping after moving on; the overlay | As expected, after a fix for queue items created while casting |

Receiver behaviour found:
- After a seek, and right after a cast starts mid-file, the receiver reports
  the previous position for up to about a second.
- It reports Loading after each seek.
- Its duration of the remuxed HLS differs from VLC's by a fraction of a
  second.
- Each queue item is its own session, so the Apple TV shows no playlist and
  its remote cannot skip between items.
- A DTS-audio clip could not be cast (the remux refuses DTS), and Screenbox
  paused it locally, as designed.

Not run: x86/ARM64 host builds, a sleeping receiver from Screenbox, a
receiver without a PIN prompt, other receivers and firmware.

## macOS AirPlay Receiver: URL controls (D62, 2026-10-10)

The user asked whether video can be cast to a Mac as to the Apple TV.
Receiver: the user's MacBook Pro, advertised model `Mac14,10`, AirPlay
`srcvers` 960.13.25, macOS version not queried; AirPlay Receiver on, "Allow
AirPlay for: Anyone on the same network", no password. Host: Windows 11 x64
(10.0.26200), `airplay2-cli`, MSVC Release static, built in this worktree on
top of `aa10119` (temporary diagnostics noted below were removed afterwards);
media `gas.mp4`; a firewall Allow rule for this worktree's binary existed.
Log: [artifact](validation/native-macos-url-controls-windows-2026-10-10.json).

**Discovery.** The Mac advertised AirPlay and RAOP on port 7000, `acl=0`, no
`pw`, features `0x38174fde4a7fcfd5`. Against the Apple TV's
`0x3c177fde4a7fdfd5` the Mac lacks bits 12, 44, 45 and 58; community feature
tables name bit 58 as remote control, which is consistent with what follows
but not established by it.

**Pairing.** `POST /pair-pin-start` is held while the Mac shows an "allow this
AirPlay request" notification; unanswered, it returns 400 after about 15 s
(the CLI's default 10 s operation timeout first reported a timeout). After the
user accepted, the Mac showed a PIN; `pair --timeout-ms 60000` saved the
profile and verified a fresh connection. Pairing through the Screenbox fork
also succeeded this way (user).

**The failing step.** With a paired profile, every cast failed in 66-141 ms
with status 500. A temporary trace showed the 500 answers the remote-control
SETUP, after that connection's pair-verify succeeded, consent given or not.
With remote control skipped (temporary switch), the URL session's base SETUP,
GET /info, RECORD, stream SETUP and the four start `/command`s were all 200,
and the Mac reported `playing` at rate 1 after 1.3 s with duration 131.567 s;
the user saw normal video and audio with the Mac's own on-screen controls, and
an on-screen seek produced a URL notification with the new position. The Mac
played to the end and reported `stopped` at 131.567 s.

**Consent per session.** The base SETUP waits for the same on-screen consent
when the Mac has not recently accepted this sender: a 5 s request timeout
expired while it waited, and an unanswered prompt gave 400 after about 15 s.
Casts a few minutes after an accepted one started without a prompt; one about
four minutes later prompted again. The Mac's log asks permission "for client
"send-airplay2"" (the sender name) and its receiver keeps a permission grant
period, so the memory is most likely per client and time-limited rather than
per device ID (D30); not tested further.

**Fix (D62):** a rejected remote-control SETUP now closes that session and
starts with URL controls; with them the base SETUP may wait up to 30 s for
consent. Runs with the fix (`cast --event-log`, commands piped on a schedule):

| Run | Native result | Observer |
| --- | --- | --- |
| A: status, pause, status, play, seek 100, status; to the end | Remote SETUP 500 traced; `playing` at 1.1 s; status 4.9 s at 6 s; pause: `paused` in 0.01 s, position held 8.9 s; play: `playing` again, Mac reported 8.892 s; seek 200 OK but no jump (16.7 s four seconds later); final position 131.567 s then `stopped` at 140.9 s: `media_end`, cleaned, exit 0 | Not reported |
| B: start 20, seek 100, pause, stop | First attempt: prompt not accepted in time, base SETUP 400 after 15.2 s, cleaned, exit 1. Second: accepted, `playing` at 3.5 s; start seek and seek 100 both 200 without a jump; pause `paused`; stop 200 (Mac then reported `paused`), `sender_stop`, cleaned, exit 0 | First attempt: prompt seen, not accepted in time |
| C: legacy `POST /scrub?position=100` (temporary switch) | No prompt; 200 without a jump; stop as in B | "The stop command resulted in video closing" (user, for the stop runs) |

At that point start, status, pause/resume, natural end and stop passed and
seek did not: the Mac answered `/command` `seek` (with or without
`kind`/`messageID`), legacy `/scrub` and the queue item's
`Start-Position-Seconds` with 200 or acceptance and ignored them.

**Seek diagnosis.** The user captured the Mac's unified log (`log stream`,
ControlCenter and the AirPlay/Core Media subsystems) during run D (seeks to 60
and 90 s, stop). Our seek arrived (a 216-byte request answered 200) with no
player activity, whereas `setRate` logged `playerairplay_setRateCommon` and the
Mac's own on-screen seek logged `itemfig_setCurrentTime…`. The log also showed
the remote-control rejection as "Setup session failed: kUnsupportedErr" for
the remote-only session, consent as "Asking delegate … for permission to
proceed for client "send-airplay2"", and our `stop` as a pause, with the item
removed only when our session closed. Strings the user extracted from the
Mac's shared library cache (`grep -F insertPlayQueueItem`, 4 KB around each
hit; used as facts only) show the receiver `APRKMediaPlayer`: it dispatches
`/command` dictionaries by `type` (`unhandledURL`, `playbackInfo`,
`setProperty`, `insertPlayQueueItem`, `removePlayQueueItem`, `setRate`,
`seek`, `streamingKey`, `stop`, …), and its seek handler logs "Sender seek to
time is %f for item %@" and "Cannot add pending seekID for item %@, UUID %@".
It also keeps a permission grant period and timer. Our seek named no item.

**Fix:** a seek now carries `item: {uuid}` of the queued item.

| Run | Native result | Observer |
| --- | --- | --- |
| E: seek 60, seek 90, stop | Each seek: a notification with the exact target (60.000, 90.000 s) and the Mac's `kind: response` within 0.3 s; positions 63.7 and 93.9 s about 4 s later; stop, `sender_stop`, cleaned, exit 0 | Everything worked as expected |
| F: `--start-position 45`, stop | `playing` at 1.0 s; the start seek's response at 2.3 s with 45.000 s; position 52.8 s at 10 s; stop, cleaned, exit 0 | Everything worked as expected |

**PASS for start, status, pause/resume, seek, start position, natural end and
stop on this receiver and host** (user: everything worked as expected in E and
F). The Mac's log during E and F (filtered to seeks) read "Sender seek to time
is 60.000000 / 90.000000 / 45.000000 for item <our UUID>", each followed by
`SeekDidComplete` with `seekErr 0`. The start position plays about one second
from 0 before the seek, as on tvOS (D61). Not run: the C interface, the UWP host or
Screenbox with the fix; HLS remux; other Macs, macOS versions or access
settings; a password-protected receiver.

## Mac C API, packaged UWP and HLS EOF (D63, 2026-10-11)

The user chose the Screenbox-on-Mac work queue after the documentation refresh.
Receiver: the recorded MacBook Pro, advertised model `Mac14,10`, with macOS
**26.7.1 supplied by the user in this session**. Host: Windows 11 x64 on a
Private network. Existing firewall rules and app capabilities were preserved;
desktop runs reused the saved profile and UWP used its own PasswordVault profile.
Media: the recorded 53,953,926-byte H.264/AAC MP4 (131.6 s). Detailed sanitized
telemetry and binary hashes:
[D63 artifact](validation/native-macos-api-uwp-windows-2026-10-11.json).

| Run | Telemetry | Observer |
| --- | --- | --- |
| Static progressive C API, main `a0fd5b9` native sources | Start active/playing/owned; pause held position; seek 60 then 10; sender stop cleaned, no failed reads, one source release, exit 0 | All checks passed: normal video/audio, pause/resume, both seeks and Stop closed the video |
| Shared HLS C API, same baseline | Start 45, pause/resume, seek 100; natural finish and cleanup, but end **`receiver_stop`** | All checks passed; natural end, without pressing Stop |
| Packaged UWP 0.1.24.0, same baseline | User paired into PasswordVault and picked the brokered file. First cast: receiver rejection after about 15 s at consent. Retry: start 45, controls and seek 100 succeeded; natural finish returned **`ReceiverStop`** and released the cast/source | All checks passed; natural end |
| Shared HLS C API, D63 native sources `0ad1e36` | Same start and controls; state `ended`, end `media_end`, cleaned, zero failed reads, one release, exit 0 | All checks passed; natural end |
| Packaged UWP 0.1.26.0, D63 native sources | In-place update retained credentials and file grant; script selected a unique `Mac14,10` and the saved profile. Start 45, pause held position, seek 100; state `Ended`, end `MediaEnd`, cast/source released | All checks passed; natural end |
| Static C API consent-time cancellation, D63 native sources | Stop scheduled after 5 s; start `cancelled` in 5,068 ms, zero source reads, one release, exit 0; staged binaries restored and hashes verified | Prompt appeared and closed; left unanswered |

**Classification failure and fix.** Baseline HLS played correctly but reported
`receiver_stop`, which would prevent Screenbox advancing the queue. A CLI HLS
diagnostic starting at 120 s reproduced it: the last reported position stayed
120 s, duration 131.566 s, followed by a stopped playback-state event with a
root `reason`. A temporary, bounded equality diagnostic proved that reason
was exactly `ended`; arbitrary receiver text was not logged. The diagnostic
was removed. Native commit `0ad1e365f46ec72b1b6d796ecd64fa806cfb9170`
normalizes only `playbackState` / stopped / exact root `reason: ended` to the
existing state `ended`, so the session reports `media_end`. It does not invent
final progress, use elapsed time as EOF, interpret unknown reasons, or change
the deferred receiver-remote Stop policy. API version remains 3.

Regression checks use an independent plistlib fixture, retain the final-position
EOF path, and cover unknown/malformed reasons, nonterminal states, joined
cleanup and source ownership. Windows Release static CTest **35/35**, shared
**36/36** including the C# binding, UWP x64 build/binary checks, touched C++
clang-format and `git diff --check` passed.

**Screenbox checkpoint:** fork main `2b8a53b0`, private local x64 package
`0.3.0-local.202610110209` from D63 native commit, SHA-256
`a5fdd72461efdb1a3e85d149932cbbfde3aa1385a04e36d62e51e7e2da9daa13`.
VS 2026 MSBuild produced the x64 MSIX, whose native DLL matched the UWP build.
The first registration reused development version 1.0.0.0 and did **not** replace
the existing layout: the launched app still loaded the older DLL (SHA-256
`63b1d8df4d581249ad04e0599e125bffd905d80479c34f67d2e4bea8775e386e`).
The private development version was raised to 1.0.0.1 and rebuilt; Windows now
registers the new layout under the same identity. Its DLL SHA-256 is
`6cccab9bdaa84c33ea49308048688d02c305f40b05724addab4de530fc1fa07a`.
Registration and the installed DLL hash were verified. The running DLL was
not independently captured: no Screenbox process was available when inspected
after the user reported it visible or after the completed checks. Keep this
provenance limitation separate from the user-observed outcomes below.
Screenbox's 86 logic tests passed. The private package records clean native
sources at `0ad1e36`, while its binding/package stamp notes uncommitted host/doc
changes; it is an x64 test candidate, not a reproducible published release.
Two identical copies of the authorized clip form the test queue. Computer Use
could load the queue, pause and seek locally in the older app, but its
input calls repeatedly rejected the UWP window's ownership as
`ApplicationFrameHost.exe`; refreshing the selection did not resolve that
failure. Computer Use was subsequently stopped by the user's physical Escape
key. The user performed the remaining checks manually and answered
**"Everything worked as expected"** to the explicit four-item checklist:

- Start casting with consent unanswered, cancel, and confirm the Mac prompt closes.
- Cast again, accept consent, and confirm normal video/audio near the current local position.
- Pause/resume, seek forward/backward, then Stop; confirm the Mac video closes
  and local playback remains paused near the last position.
- Let the first item finish naturally and confirm the second queue item starts
  on the Mac, then Stop.

These UI checks are **PASS by user report** for the development app following
the 1.0.0.1 update. No automated Screenbox event log, loaded-DLL hash, or exact
timing is attributed to that report. Final published-package validation remains
a separate gate.

**PR #39 review follow-up:** the review found that the event reader could publish
`ended`, read again, and record a disconnect before the supervisor's poll committed
`media_end`. The fix commits the terminal reason with the playback state under
the state mutex and returns from the reader. The supervisor retains cleanup
ownership, and an earlier recorded failure or terminal reason is preserved.
New regressions reproduce the race against `b8bf806` (10 assertions fail), cover
immediate closure after both forms of explicit EOF in URL-only and MRP sessions,
and preserve a prior sender Stop or remote-channel failure. The initial test run
with the fix had two incorrect fallback closure-order expectations; those were
corrected to reflect remote SETUP rejection closing remote control before URL
start. After restoring the fix, the first full static run reused the pre-fix test
binary because the restored source retained its older timestamp; explicitly
rebuilding it resolved that build-state issue. Final Windows Release static
CTest **35/35 (44.38 s)** and shared **36/36 (49.75 s)**, including the C# binding,
passed, as did touched C++ clang-format and `git diff --check`. This follow-up
has no new hardware run; the receiver/binary records above remain the earlier
D63 observations.

At the D63 checkpoint no new prerelease was published. The all-architecture main candidate
`0.3.0-ci.203` passed all 14 CI jobs but lacks D63, so it is excluded from the
planned update. D64 below records subsequent merge/package publication;
final published-package receiver validation remains a separate gate. These results cover only this receiver,
firmware and Windows hosts; they do not establish other-host interoperability.

### Published main package checkpoint (D64, 2026-10-11)

PR #39 merged as `2829242`. Main workflow run `38107556167` passed all 14 jobs
and produced `0.3.0-ci.207`, including the review race fix. With explicit user
approval, the exact artifact was published as
[nuget-v0.3.0-ci.207](https://github.com/ilyalissoboi/send-airplay2/releases/tag/nuget-v0.3.0-ci.207).
Its public download and a fresh Screenbox script download match SHA-256
`e52affdf9f79e20dcd00b59380c9949d1f9b11d1339307f8a2b86ad14e1ffa4d`.
Provenance, all six native DLL hashes, license/notices and all three UWP binary
property checks passed. Screenbox's exact-package x64 build and 86/86 logic
tests passed; fork PR #8 merged as `31013d5e` with the pins. Native PR #40
merged as `a29212b` after all 14 checks passed at `772b850`.
Development layout 1.0.0.2 is registered in place, preserving app data.
The running app's DLLs were inspected after the user opened the Cast/device
flyout and reported the Mac listed. Loaded native SHA-256:
`3a81ef19bbbf7288a9fc71b3264445a5eaa787dd952d10e86fb4d22591f8d8e7`;
loaded crypto SHA-256:
`4f6db0813f6405a92cf7c661e6df6e3aef4edc98b06ef21446b67ffbd357fd68`.
Both modules load from the verified ci.207 development layout and match the
published package. After this verification, the user performed the final Mac
checks and answered **"All checks passed"** to this explicit checklist:

- Leave consent unanswered, cancel the cast and confirm the prompt closes;
  cast again and accept consent.
- Confirm normal video/audio near 45 seconds, pause/resume and forward/backward seeks.
- Stop; confirm the Mac video closes and local playback stays paused near the
  last position.
- Let the first of two queued test clips finish naturally without Stop and
  confirm the second starts on the Mac.

These are **PASS by user report** for development Screenbox 1.0.0.2 using the
verified published ci.207 DLLs on one MacBook Pro (`Mac14,10`, user-reported
macOS 26.7.1) from Windows x64. No automated Screenbox cast log, API terminal
telemetry or exact action timings were captured. The loaded-module evidence
closes D63's provenance gap for this final run without changing the scope of
the earlier private-build observations. Final-package validation is complete
for this recorded combination; broader interoperability remains unvalidated.
Record: [D64 package checks](validation/nuget-macos-package-windows-2026-10-11.json).

## Mac receiver consent recurrence (D65, 2026-10-12)

The user authorized investigation after public documentation research and
confirmed the Mac remained on macOS 26.7.1. Main base `671e714` contains merged
PR #41; final Screenbox documentation fork PR #9 is also merged as `2cee2ea7`.
The D64 package/playback gate above is complete, not new work to repeat.

Eleven short desktop CLI casts reused the recorded Mac (`Mac14,10`), existing
pairing and authorized clip. Every cast reached playing, received scripted
Stop eight seconds after ready, exited 0 with `sender_stop`, cleaned up and
reported zero failed reads. Staged binaries were restored with hash checks.
No enrollment, credential export, receiver setting, firewall rule or sender
privacy-policy change was made. The temporary name-only comparison is documented
with its binary hash; no persistent C++ change remains.

Individually reported prompt outcomes: A/C/D/F appeared and were accepted;
B/E/G were prompt-free. The changed display name in E reached the receiver
(`send-airplay2-consent-probe`) without a new prompt, as confirmed by the log
and user. Source audit shows the baseline still generates new random device,
session/client/item identifiers and headers, so recent permission reuse survives
that established per-cast identity policy in these tests.

H/I/J/K used equal measured idle gaps of 45.078 seconds for each repeat.
The user reported more than one prompt in that chain; their final K report was
corrected to **no prompt**. The complete capture shows delegate/should-ask
sequences at H and J, but request-only logs at I and K. H-to-I receiver permission
requests were 60.640 seconds apart; I-to-J 55.517; H-to-J 116.157; J-to-K 57.475.
Request timestamps are not acceptance timestamps. An internal request-processor
permission message alone does not establish that a notification appeared.

**Interpretation:** H/J are log-derived prompt candidates, not individually
confirmed UI observations; the lack of a delegate sequence at I does not alone
prove a prompt-free UI. Only multiple prompts in the chain and corrected K=no
prompt were explicitly reported. Grant age/expiry is a working hypothesis
from that evidence and timing pattern, not a demonstrated J re-prompt rule.
The capture emitted no numeric grant duration or
reset/expiry value; exact timer, renewal rule and cache key remain unproven.
Name and random-device-ID changes are not demonstrated causes of every prompt.
No sender fix or permanent identity change is justified by this slice.

This adds consent/start/Stop evidence for one Mac/firmware/Windows/profile
combination, not new controls/seek/EOF or alternate-host interoperability.
Only timestamps, fixed event categories, scalar sender telemetry and the two
synthetic sender names are retained; raw log, endpoints and identifiers are not
committed. Further timer/key/context comparisons are optional, with no automatic
re-pairing or receiver access-policy changes.

Study: [public sources, audit and results](macos-receiver-consent.md).
Record: [D65 redacted artifact](validation/native-macos-consent-windows-2026-10-12.json).

## Screenbox Forget-device UI (D66, 2026-10-12)

User-selected follow-up in fork PR #10, implementation `a3d64831` from main
`2cee2ea7`, followed by validation documentation `35285224`. No native sender,
package pin, receiver settings or consent-policy change. The existing published
`0.3.0-ci.207` supports selected-profile removal.

96/96 Screenbox logic tests pass with synthetic profile storage and no XAML
objects. VS 2026 MSBuild Release x64 packaged build passed; private development
version 1.0.0.3 was registered in place and the source manifest restored.
Before UI testing, the running executable and both loaded AirPlay DLL paths/hashes
matched the extracted MSIX. See the artifact for exact fingerprints and the local
working-tree build provenance limitation; this is not a release package.

Computer Use observed the paired Mac's separate Cast/Forget actions, clicked
Forget and observed Pair with Forget hidden, selected the Apple TV and observed
its paired Cast/Forget actions retained, then selected Chromecast and observed
Forget hidden. These are sender UI observations, not raw-vault inspection or a
Chromecast/Apple TV playback regression. The user handled Mac consent/PIN and
answered "Done"; restored paired Cast/Forget actions were independently observed.
Computer Use started the authorized clip, observed Forget hidden during active
casting, operated pause/resume and Stop, then observed local paused playback near
the last remote position. The user reported "Video and audio are playing normally"
and confirmed pause/resume and Stop closing the Mac video.

**PASS for this recorded MacBook Pro/Windows x64 combination.** Receiver identity
and macOS 26.7.1 are the previously recorded user-reported configuration; receiver
pixels/audio were observed by the user. No new automated cast log, exact timing,
raw credential/PIN capture, committed screenshots or x86/ARM64 run. Failure,
idempotence, selection changes and busy guards are offline test evidence.
All three fork CI checks passed at implementation `a3d64831` and at final
head `47016474`; PR #10 merged as `8bfd3dd9`.

Record: [D66 artifact](validation/screenbox-forget-macos-windows-2026-10-12.json).

**PR #10 review follow-up (`f05c10e6`):** selecting Chromecast while Forget was
pending could bypass the Cast busy state. Eligibility now applies the Forget
guard before either provider. All 99 logic tests pass; reinstating the reviewed
bypass fails the two Chromecast cases, and the fixed source was restored
byte-for-byte before rebuilding the full passing suite. The review thread is
resolved; its x64 package passed as private version 1.0.0.4. This is offline command-eligibility
validation, not a new receiver observation; the Mac UI record above remains
attributed to `a3d64831`.

**Compact footer refinement (`4c346af8`):** the user requested label **Forget**
and placement beside Cast. The x64 package passed, was registered in place as
private version 1.0.0.5 and its source manifest restored. Running executable
and native module paths/hashes match the extracted MSIX. Computer Use observed
the paired Mac's Forget button on the left and Cast on the right, aligned on
the same row without overlap. This is sender-layout evidence only: no additional
Forget/Pair/Cast action or receiver playback was performed. Exact hashes are in
the D66 artifact; final-head CI and merge are recorded below.

**Grouped/padded footer (`1ba38e55`):** the user requested Forget immediately
beside Cast at the bottom right and space between the list and actions. The
right-aligned horizontal group uses 8-DIP spacing and a 12-DIP top margin.
VS 2026 MSBuild x64 packaged build passed; private development 1.0.0.6 was
registered in place, preserving data/pairings, and the source manifest restored.
Following written instructions to open the Cast flyout, select a paired AirPlay
device and scroll to the list bottom, the user answered **Yes** to correct
grouping and clear separation from device names (longer names if available).
No exact name-length case was recorded. This is user-observed sender-layout
confirmation, not new receiver playback or credential deletion/re-pairing.
The Computer Use skill was not used for this check; future visual confirmation
must use written checks and the user's report, per their preference.

**Merge checkpoint (2026-10-12):** native PR #42 merged as `8a40ec3` after all
14 checks passed at final head `8978188`. Fork PR #10 merged as `8bfd3dd9` after
test, XAML lint and package build passed at final head `47016474` (release skipped).
The review threads are resolved. This closes the D65/D66 review/merge gates;
it adds no receiver observation or additional host/firmware coverage.

**User clarification (2026-10-12):** no local Chromecast receiver accepting
video is currently available, so that regression is deferred. The user states
casting to a sleeping Apple TV was already verified earlier; the exact earlier
Screenbox build/date is not supplied in this clarification. Treat it as completed
user-reported behavior instead of scheduling a duplicate required gate. Preserve
the dated earlier candidate records' own not-run limitations; this clarification
does not retroactively add hardware evidence to those binaries. The subsequent
[audio-feature assessment](audio-features-assessment.md) adds no new receiver run.

**D68 offline codec/packaging gate (2026-10-12):** the separate stereo-AAC
development probe passes desktop codec/channel/window tests, x64 AppContainer
binary inspection and all eight offline Native AOT/MSIX codec/cache/lifecycle
cases. Five loaded module hashes match the extracted package. Baseline static
and shared CTest pass 35/35 and 36/36. See [the spike](stereo-aac-codec-spike.md)
and [artifact](validation/stereo-aac-codec-spike-windows-2026-10-12.json).
No receiver, speaker, surround-output or HLS-conversion playback test is added;
these results must not expand the existing Apple TV/Mac interoperability claims.

**D69 native source foundation (2026-10-12):** synthetic MP4/MKV inventory,
selection, video/packet-index and seek-clock cases pass, alongside local static
35/35 and shared 36/36 CTest. PR #48 merged as `5d46479` on 2026-10-13 after all
16 checks passed at final head `c816709` in run `38268265899`.
See [contracts and limits](audio-source-inventory.md).
The source timing helpers are not compressed-audio decoder seek proof; no stereo
HLS conversion, new packaged runtime or receiver observation is added.

**D70 selected decoder windows (2026-10-13):** actual compressed AAC/AC-3/E-AC-3/
DTS-core 5.1 fixtures pass identity, seek/timestamp/resampling and lifecycle checks
through the private adapter; the preserved codec probe also passes. UWP x64
compilation and codec DLL app CRT/import inspection pass. See
[contracts and evidence limits](audio-decoder-source.md). This is desktop offline
and UWP build evidence, not a new packaged adapter runtime, stereo HLS playback,
receiver interoperability result or package publication.

**D67 planning disposition (2026-10-12):** the user defers audio-file casting,
including audio-only speakers/HomePod, and DTS conversion preserving 5.1/7.1
because suitable local test hardware is unavailable. The next documented slice
is [video with surround audio downmixed to stereo AAC](stereo-aac-casting-plan.md).
No codec implementation, audio-speaker test, discrete-surround output test or
stereo-conversion receiver test has run. Future offline downmix/segment tests
must remain separate from manually observed receiver playback and output mapping.
Research PR #43 merged as `aa0f25e`, with 14 checks passed at `789f029`; fork
docs PR #11 merged as `f596fd5e` with no CI checks reported. Those merges add
documentation, not new receiver or firmware coverage.

**D67 planning merge checkpoint (2026-10-12):** native PR #44 merged as
`12c24a7`, with the selection review resolved and all 14 checks passed at final
head `bc4b381`. Fork PR #12 merged as `ce782356` at `0cd9ff56` (docs-only;
no CI checks reported). The planning gate is complete. No conversion backend,
new package or receiver run is added; the deferred hardware gates remain deferred.

**D70 merge checkpoint (2026-10-13):** PR #50 merged as `65bb8a7`, after all
16 CI checks passed at final head `8b1adf2` in run `38280966375`. This adds no
receiver or packaged runtime evidence.

**D71 offline HLS/cache increment (2026-10-13):** selected surround source PCM
becomes separate stereo AAC resources alongside copied video and subtitles.
Ten compressed movies pass global duration/joins, video payload, subtitles,
edits/short-source silence and selected cache lifecycle tests. Codec/decoder/HLS
CTest passes 3/3, baseline static/shared 35/35 and 36/36, and x64 UWP compilation
passes. See [cold synthesis, final padding, clock and authoring limits](stereo-aac-hls.md).
No receiver, speaker, discrete-surround, new packaged builder runtime or package
publication is included. Manually observed lip sync/seeks/controls/EOF remain
required after explicit public/packaged integration; offline PCM is not that proof.
PR #51 merged as `e09c57f` with all 16 checks passed at final head `475ae61`
in run `38330651001`, and both review threads resolved. The final declaration
follow-up in PR #52 merged as `c092c9b`, after all 16 checks passed at `6716f1a` in run `38331920914`. Master/audio independence claims
remain omitted for cold AAC synthesis; copied video keeps its tag.

**D72 offline readiness increment (2026-10-13):** a real 16.5-second long video
GOP retains its packet bytes while audio is split into three bounded segments.
Synthetic one-hour DTS callbacks with packet offsets beyond 4 GiB pass late and
backward cold decoding, exact global PTS/end and deterministic I/O bounds.
Terminal source/seal/read failures cancel audio work and retain fixed diagnostics;
request cancellation/admission/explicit Stop remain nonfatal. See
[contracts, measurements and remaining gates](stereo-aac-readiness.md).
These are offline native tests: no real hour-long file, new receiver observation,
packaged builder runtime, audio-only speaker or discrete-surround result is added.

**D72 merge checkpoint:** PR #53 merged as `25b84a1`, with all 16 checks
passed at final head `6bf1bd2` in run `38336511817`. No receiver evidence added.

**D73 offline authoring policy (2026-10-13):** contiguous-window bitrate
calculations and isolated first-period AAC estimates pass regular compressed MP4
fixture audits. Indexed-MKV cluster estimates and short/edited early-ending audio
fail whole-VOD accuracy audits explicitly. A real 33-second video GOP is refused
before calibration/sealing, with cache cleanup. See [policy and evidence](stereo-aac-authoring.md).
These checks do not establish Apple-wide authoring conformance, real-file/host
performance or a new receiver/packaged runtime result. Speaker/surround stays deferred.

**D97 native audio-only URL gate (2026-10-17):** an original 64-second AAC-LC
M4A with zero video streams passes on MacBook Pro/macOS 26.7.1 and Apple TV/tvOS
26.6, explicitly unchanged. The user confirms audible playback, pause/resume,
forward/backward seeks, natural end and separate Stop/no delayed restart on
both; Mac pending-consent cancellation also passes. Every native run exits zero
and releases its source once with media cleanup/no media failure. This uses
the existing progressive C API 5 at source `449457a`, with no production change.
See [fixture/runtime evidence and scope](audio-only-url-probe.md). Speaker,
UWP, Screenbox, other codecs and audio-only HLS remain separate gates.
