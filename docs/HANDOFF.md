# Project handoff: send-airplay2

Snapshot: 2026-10-11 (Asia/Tokyo). Audience: the next human developer or model.
Read this first, then [design.md](design.md) and
[receiver-validation.md](receiver-validation.md). This is a continuation record,
not a claim that the sender has been completed.
The focused [separate-session handoff](CONTINUATION.md) contains the current
checkpoint, review disposition, validation commands and ordered development queue.

## 0. Resume here

**Active slice (2026-10-11): documentation reconciliation, then Screenbox on
the Mac (step 1 below), chosen by the user.** PR #39 merged as `2829242` on
2026-10-11 after all 14 checks passed at its final head `811c022`; its review
thread is resolved. The package continuation branch `codex/mac-screenbox-package`
starts at that main commit. The validation branch started at `a0fd5b9`.
Completed checks: C API static/shared and packaged UWP host on the recorded Mac,
including HLS remux, controls, start position, natural end and native consent-time
cancellation; the user also reports the private Screenbox UI checks passed.
Remaining gates: validate the published package in Screenbox and review/merge
the fork pin PR and this documentation checkpoint with user approval.
Preserve the loaded-DLL evidence limitation
in the D63 record when describing the private Screenbox checks.
Do not resume phases marked "Next" in the history below.
Main workflow run `38107556167` (run number 207) was dispatched at `2829242`
to prepare `0.3.0-ci.207` with D62/D63 and the review race fix. Clean source
stamps, all architecture payload hashes, licenses/notices and local UWP binary
checks passed. Candidate SHA-256:
`e52affdf9f79e20dcd00b59380c9949d1f9b11d1339307f8a2b86ad14e1ffa4d`.
All 14 jobs in the main workflow passed. The user approved publication;
[ci.207 is published](https://github.com/ilyalissoboi/send-airplay2/releases/tag/nuget-v0.3.0-ci.207).
The public download and a fresh fork-script download match that SHA-256.
Screenbox's `codex/airplay-mac-package` worktree under ignored
`build-review/screenbox` has prepared version/hash pins and a successful x64
packaged build whose native/crypto DLLs match the artifact. Logic tests: 86/86.
The pins/docs are on fork branch `codex/airplay-mac-package`, with
[draft PR #8](https://github.com/ilyalissoboi/Screenbox/pull/8) open for review.
Private development app version 1.0.0.2 was used only for the built test layout;
the manifest edit was restored. The matching layout is registered in place as
development version 1.0.0.2. Its loaded DLL and receiver behavior still need
verification. See the [D64 package record](validation/nuget-macos-package-windows-2026-10-11.json).

**D63 completed and merged:** on the user's MacBook Pro (`Mac14,10`, macOS 26.7.1),
the static progressive C API and shared HLS C API controls passed visually.
HLS finished naturally but reported `receiver_stop` through both the C API
and packaged UWP host. The Mac omits the final position and sends a stopped
playback-state event with the exact root `reason: ended`. Native commit
`0ad1e36` normalizes that explicit signal to `ended`; it does not infer EOF
from extrapolated time or unknown reasons. The corrected shared C API and
UWP 0.1.26.0 now report `media_end`, with the user's playback checks passing.
Static/shared CTest: 35/35 and 36/36. A private x64 package from this native
commit was prepared for Screenbox; it is not a published release. Screenbox's
86 logic tests passed. Same-version registration left the older Screenbox DLL
loaded; a rebuilt development version 1.0.0.1 now registers the corrected layout.
Its installed DLL hash is verified; the running DLL was not captured before the
app closed. Computer Use lost the UWP window's ownership during input, then the
user stopped it with Escape. The user subsequently reported all remaining UI
checks worked as expected: consent-time cancellation/recovery, current-position
start, video/audio, pause/resume, forward/backward seeks, Stop/local paused
handoff and natural queue advance. This is user-observed evidence, not automated
Screenbox telemetry. Consent-time cancellation through the static C API passed:
start cancelled in 5.068 s, no reads, one release, and the user observed the
unanswered Mac prompt close.
Record: [D63 receiver checks](receiver-validation.md#mac-c-api-packaged-uwp-and-hls-eof-d63-2026-10-11).
**PR #39 review follow-up:** the review identified a race between an accepted
explicit EOF and the next event-channel read returning EOF. The reader now
commits `media_end` with the playback state under the same mutex and exits;
the supervisor still owns ordered cleanup. An earlier recorded failure or
terminal reason is preserved. Regression coverage includes immediate closure
with native `ended` and Mac stopped/reason events in MRP and URL-only sessions,
and a later EOF after an earlier Stop or remote-channel failure. This follow-up
adds offline validation, not a new receiver observation; the hardware and
private Screenbox hashes above describe the earlier `0ad1e36` sources.
Final review-fix CTest: static 35/35, shared 36/36 including C#; format/diff checks
passed. The regressions fail against the prior `b8bf806` implementation.
The earlier main package candidate `0.3.0-ci.203` passed all 14 CI jobs but
does not contain this fix, so it will not be used for the Screenbox update.

Current slice and remaining choices (dependencies first):
1. **Screenbox on a Mac.** Screenbox's package (`0.3.0-ci.196`) predates D62,
   so a Mac it lists cannot be cast to yet. Steps:
   - **done in D63:** check D62 through the C interface and the UWP test host's script mode;
   - **done in D64:** publish approved `ci.207` from `main` and verify its
     public hash; version/hash pins are in fork PR #8;
   - test the final package from Screenbox to the MacBook Pro, which also pairs after
     an on-screen consent.
2. **The Mac's per-cast consent prompt:** find what makes the Mac ask again
   (D62 found the device ID is probably not the reason).
3. **Fork follow-ups:**
   - "Forget this AirPlay device";
   - x86 and ARM64 builds on a device;
   - a Chromecast regression check;
   - a sleeping TV from Screenbox.
4. **MRP research:** choosing subtitle (and audio) tracks from the sender,
   and whether the TV remote's next/previous can reach the sender, which
   currently cannot skip queue items.
5. **HLS:**
   - HDR `VIDEO-RANGE` in the playlists (HDR10 is announced as SDR now);
   - Dolby Vision;
   - growing presentations (phase 4), which a transcoding path for codecs
     the remux refuses (DTS, VP9, AV1, MPEG-2) would need.
6. **nuget.org publishing,** before an upstream Screenbox PR.
7. **Deferred or untested:**
   - remote-Stop classification (deferred by the user);
   - Android/JNI (D04);
   - casting from Linux and macOS hosts;
   - receivers other than the recorded Apple TV and MacBook Pro.

**Latest: casting to a Mac (D62, merged as PR #37).** The user asked whether
video can be cast to a Mac as to the Apple TV, then chose an automatic
URL-only fallback. A MacBook Pro (`Mac14,10`) pairs
after an on-screen consent and a PIN, rejects the remote-control SETUP with
500, and plays the URL session alone. With the fallback, start, status,
pause/resume, seek, start position, natural end (`media_end`) and stop passed
from the CLI (seek after the Mac's receiver strings showed it needs the item
UUID; user observed). Open: the per-cast consent prompt, the C interface/UWP
host/Screenbox on the Mac (next steps 1 and 2). See the D62 record in section 4 and
[receiver-validation.md](receiver-validation.md#macos-airplay-receiver-url-controls-d62-2026-10-10).

**Before D62 (2026-10-10):** HLS delivery (D60) and the Screenbox integration
were done for their current scope; the last entries of this section record
them.

**History: HLS delivery (D60)** ([hls.md](hls.md)). The user set the Screenbox
integration aside (design PR ilyalissoboi/Screenbox#1, since merged) and asked to
add HLS support to the base library, choosing "serve plus built-in remux".
Phase 1 (media server resource sets, development `cast --hls`, the `near_end`
end-of-media rule) is merged (PR #26), and so is phase 2 (PR #27): the
library's own MP4-to-fMP4 remux, `cast --file PATH --remux`. Phase 3a (branch
`claude/hls-mkv`): MKV input to the remux passed on the recorded Apple TV for
H.264 + AAC, HEVC + AC-3 5.1 and H.264 + E-AC-3, and with a user-provided
1 h 54 min H.264 + E-AC-3 Atmos film (user observed; packet-identical
offline). Next: 3b, the
C/C# delivery option; the MKV startup scan cost is a known follow-up; then
growing presentations (phase 4). See the D60 record in section 4.

**Phase 3b (branch `claude/hls-api`):** PR #28 (3a) is merged. The C
interface gains `sap2_cast_options.delivery` (`SAP2_DELIVERY_HLS_REMUX`), API
version 3, `MEDIA_UNSUPPORTED`/`MEDIA_MALFORMED` results, C#
`CastOptions.Delivery`, and the remux moves into the library. It passed on the
recorded Apple TV through `airplay2-api-host --remux`, static and shared (user:
video and audio normal, seeks worked, Home at the end). The user approved subtitles (3c) as the next step
after 3b; see [hls.md](hls.md#6-subtitles-phase-3c-approved-2026-10-09).
Local environment: the network now reads Private, and the earlier shared
staging path has a Private Block firewall rule (read, not changed); run the
shared host from the worktree's `build-shared/Release`.

**Phase 3c (branch `claude/hls-subtitles`):** PR #29 (3b) is merged. MKV text
subtitles (SubRip, WebVTT, ASS as text) become WebVTT renditions of a
multivariant `main.m3u8` with codec strings, bandwidth, resolution and frame
rate. The user's film was offered, selected and shown in sync on the
recorded Apple TV (user: yes to all four checks); offline, its 2,048 cues
matched ffmpeg's conversion. Open: sender-side subtitle selection over MRP,
MP4 text tracks, HDR VIDEO-RANGE. See [hls.md](hls.md#phase-3c-result-text-subtitles-2026-10-09).

**Indexed MKV start (branch `claude/hls-lazy-mkv`):** PR #30 (3c) is merged.
MKVs with Cues are planned from the index and each segment is built on its
first request (media server `size_on_request`). The film's startup fell from
4.9 s to under 0.1 s, its packets, times, continuity and subtitles stay
identical offline, and it played, seeked and ended normally on the recorded
Apple TV (user: everything worked as expected). Files without Cues keep the
full scan. Merged as PR #31.

**MP4 text subtitles (branch `claude/hls-mp4-subtitles`):** `read_mp4` reads
tx3g (with bold/italic/underline) and wvtt tracks, timed by their edit lists,
with language, DEFAULT and FORCED; they feed the WebVTT renditions. A
10-minute tx3g MP4 matched ffmpeg's cues exactly offline and showed subtitles
in sync, with italics, on the recorded Apple TV (user). Merged as PR #32; the
README rewrite followed as PR #33.

**Screenbox step 1 (branch `claude/uwp-host-remux`, D61):** after reviewing
the Screenbox design against the HLS work, the user chose HLS remux for every
file (option B) and the order: (1) UWP test host with remux and start
position, TV checks, certification kit; (2) update the Screenbox design and
answer its PR's two review threads; (3) packaging. The test host gained the
remux option, a start position, remembered files and a script mode
([uwp-host.md](uwp-host.md#script-mode-d61)). On the TV it found that tvOS
26.6 ignores the queue item's start position; the library now seeks there
before `Start` returns, which passed (user). The certification kit on 0.1.22.0
matched D57 (only the AOT runtime in the host executable fails Supported
APIs). Merged as PR #34. Record:
[receiver-validation.md](receiver-validation.md#uwp-host-hls-remux-and-start-position-d61-2026-10-10).

**Screenbox step 2 (fork PR #1, `b0efd6e0`):** the design now casts every
file through the remux (user decision 7), routes the system media transport
controls through the active `IMediaPlayer`, and has explicit end-of-item rules
for the queue; both review threads were answered and resolved.

**Screenbox step 3 (merged as PR #35):** `scripts/pack_nuget.ps1` packs the
binding (now 0.3.0, following API version 3) with the UWP-built native
libraries per architecture and the OpenSSL, Botan and Boost notices. Each
library carries a build-time source stamp, and CI packs all three
architectures ([nuget-package.md](nuget-package.md)). The fork's CI cannot
reach a local feed, so the user chose GitHub prereleases on this repository
(fork decision 8). `nuget-v0.3.0-ci.196`, built by a workflow run on `main` at
`aa10119`, is published with the user's approval; the fork downloads it by
pinned version and SHA-256.

**Screenbox integration (fork PRs #2-#6, merged 2026-10-10):** the fork
casts with AirPlay per its design, all user observed on the recorded Apple
TV from the packaged app.
- Phase 1: the package and its notices.
- Phase 2: receivers in the cast flyout.
- Phase 3: PIN pairing into PasswordVault, with Pair in place of Cast.
- Phase 4a: casting the loaded item through the remux from its position;
  controls follow the TV, and every end returns to local playback paused at
  the last position.
- Phase 4b: the play queue (next item, Next/Previous, repeat one, end of the
  queue), the Windows media controls and a "Casting to" overlay.

Receiver behaviour found there, worked around in the fork, and relevant to
any host:
- after a seek, and after a start position's seek, the receiver briefly
  reports the old position;
- it reports Loading after each seek;
- its HLS duration differs from the source's by a fraction of a second;
- each queue item is its own session, so the TV shows no playlist and its
  remote cannot skip items;
- a file the remux refuses (DTS audio) cannot be cast, as designed.

Since then (fork PR #7, `feat/airplay-cosmetics`, 2026-10-11), the user
asked for cosmetic changes:
- a blurred backdrop under the "Casting to" overlay;
- a 4-digit PIN dialog;
- a device icon and a product-family line per AirPlay receiver.
The icon and family come from the advertised model (`Receiver.Model`), and
Apple-silicon `MacNN,M` identifiers come from a table of Apple's published
identifiers. They were checked on screen, except the family line, which
awaits the user. The fork's design doc now holds its implementation status,
how to test a development build, its limitations and next steps. The
consolidated next steps are at the top of this section.
Record: [receiver-validation.md](receiver-validation.md#screenbox-integration-2026-10-10).

### Standalone items (D58-D59, merged)

**History: the standalone items (all merged).** The user merged PR #21 (D56) as
`99942d1` and asked to finish the standalone items except remote-Stop detection
(deferred). UWP for other architectures with a certification kit run (D57) is
merged (PR #22, `8ecb8b0`): x86 passed on the TV, ARM64 is built in CI only, and
the kit failed only Supported APIs for the .NET AOT runtime in the host
executable. D58 (branch `claude/macos-keychain`) adds the macOS login-keychain
built-in store, which passed its native-store tests on CI's macOS runner; the
Linux Secret Service store (D59, branch `claude/linux-secret-service`) is
stacked on it and passed against a throwaway GNOME Keyring in CI. See the D57,
D58 and D59 records in section 4.

**Wake before play (D56) is merged.** The user had asked whether a cast can wake
the receiver and chose the order: reproduce, check pyatv, fix. It records the receiver's MRP power reports in the start
trace and, when the handshake reports it asleep, sends `WAKE_DEVICE` and waits
for it to settle before starting playback. A cast to the sleeping Apple TV then
played normally; see the D56 record in section 4 and
[mrp-controls.md](mrp-controls.md#receiver-power-and-wake-d56).

**The UWP native build (D55, D49 step 3) is merged** (PR #20, `796a595`). The
user had merged PR #19 (the C discovery interface, D54) as `ac7717f` and asked
for it. It builds the library for app packages
(CMake WindowsStore, vcpkg `x64-uwp`, a Botan overlay port), compiles out
Credential Manager, fixes a Stop crash specific to app builds and adds a CI
build check. The packaged UWP host cast, controlled and stopped repeatedly with
it on the recorded Apple TV; see [uwp-native-build.md](uwp-native-build.md) and
the D55 record in section 4.

The C discovery interface (D54) is merged; see
[public-api.md](public-api.md#receiver-discovery-d54).

The packaged UWP test host (D53) is merged; see [uwp-host.md](uwp-host.md).

The C# binding (D52) is merged; see [csharp-binding.md](csharp-binding.md).

D49 step 1 (D50, API version 2: host credential stores and pairing) is merged;
`sap2_pair()` paired the Apple TV into the built-in store and a cast with the new
profile passed (D51).

D49 (common credential design) is recorded in
[credential-interface.md](credential-interface.md).

The public playback interface's manual plan **passed on the TV on 2026-10-08
(D48)** for one receiver/host, with D47's teardown fix; see
[public-api.md](public-api.md) and the D46 record in section 4.

**PR #12 is merged (D45).** The user merged
[#12](https://github.com/ilyalissoboi/send-airplay2/pull/12) into `main` on
2026-10-07 as `2bb25df4b4f58ef0a2c6936ed6f44161b815890c`. Its final head
`923d8efbee066a5a7ce37dd6e83cb891ede58a06` (D44, documentation only) passed all
ten [CI checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37587291158)
and its single review thread was resolved. No PR is open at this checkpoint.
Start each new slice on a fresh branch from `origin/main` with its own PR; ask
the user before merging. The rest of this section is the PR #12 record, kept for
its evidence; its branch and "open PR" instructions are superseded.

Work continues in a local session on the Windows 11 host that
shares a LAN with "Living Room". A cloud session cannot reach that LAN.
Implementation/test head `ab7ec0170dbc364bfbec0e52a1b71ae92a0974a5` passed all ten
[CI checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37584220175).
D44 addressed the single automated review comment: README and the current
network-observation table now consistently mark selected D42 Ethernet cleanup /
fresh explicit recovery PASS, with broader network reliability pending and
automatic reconnect/resume unimplemented.
Latest runtime follow-up: D43's [PR review](pr-review.md) fixes URL diagnostic
redaction and decoded plaintext erasure on exception paths. Windows
static/shared Release passed 24/24 CTest targets each; runner contracts passed
10/10; all 52 PR-changed C++ files passed formatting. Inspect the actual PR head
and CI after publication. This follow-up has no new receiver observation.
Latest receiver-tested implementation/evidence head (D40):
`4a18b2662150768a00154ca17e222f2e701fd192`;
[exact-head CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37577586084).
It adds bounded remote-event/final MRP diagnostics with separate observed
Stop/Home, sleep and fresh wake playback/EOF/Home evidence. Classification
remains conservative. D42 reused that runtime; D43 changes diagnostic/ownership
source. Inspect the actual PR head/checks rather than reusing an earlier result.
Lifecycle implementation/code-test head:
`2f77f622acbaff3e6579cb8c997c927a6ea12206` (D33). Native runtime source blobs
and CLI SHA-256 are recorded in the [lifecycle artifact](validation/native-lifecycle-windows-static-2026-10-07.json).
All ten checks passed in its [CI run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37553605053),
and at code-equivalent documentation head `759de2d30043aa7bd6873f9e2700adceffa8e187`
in [CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37553955465).
CI is separate from hardware gates; inspect PR #12's actual head/checks,
including documentation-only follow-ups. Observer head `c13d59ff485bb5883d91f9e05cd7369bf4bf88ef`
passed all ten [checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37557470097). Step 2's final documentation head
`dafb70a4bcec0236b8f20901d745a1546ff853ba` passed all ten checks before this work.
The observer-documentation head `3caad5ffedde8942102138ac774df23b015f5e94`
also passed all ten [checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37555176050).
The stop/buffering diagnostic code/test head
`27356a8279c25da30821af5c708e5d4bbed7baf4` passed all ten
[checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37556596080).
The full-clip observer follow-up below changes presentation evidence, not that runtime.
D35 code/evidence head `6ef958de3e8ff7a9968f9e247a3ec1739adf2441` passed all ten
[checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37559957331).
D36 below changes the normal cast admission budget; inspect its actual PR head
and checks rather than reusing a prior CI result.
The D37 documentation head `1d69560578fbbebafa5ab9c33d2cb56bd90b76cb` passed all ten
[checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37562737055).
D38 added startup code/tests below; CI remained a separate gate.
D38 startup code/test head is `6bed9febffb857050a9370bc34533add9abcf547`;
its [CI run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37564851802)
passed all ten checks, as did tested documentation head
`fa9b9189f599b0b6405b8c1b81327761900470c2` in
[CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37565052646).
Inspect the actual PR head, including subsequent documentation-only follow-ups.
D39 documentation/evidence head `5a2c58d9eec880c81e96ef264a489a709a7fa4a9`
passed all ten [checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37573362161).
D40 code/evidence and its CI are identified above. Inspect the actual PR head
after subsequent documentation-only updates rather than reusing an older run.
The D41 [pyatv remote-Stop source audit](pyatv-stop-reference.md) changes no
runtime behavior: upstream swallows URL polling connection loss as completion;
the tvOS fork's event waiter does not finish on socket EOF alone. Neither
provides a validated Stop-versus-network/sleep discriminator. This is offline
reference evidence, not a new hardware pass.
The subsequent D42 [Ethernet interruption/recovery check](validation/native-network-recovery-windows-static-2026-10-07.json)
passed selected automatic timeout cleanup and fresh same-credential playback
after reconnection/Home, with user-confirmed video/audio/Home at near-end EOF.
It uses the unchanged D40 runtime. The tested D41 documentation head `72b3059`
passed all ten [CI checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37579365861).
Inspect the actual PR head after this evidence-only follow-up.

PR #12 was developed first in a Claude session and then in a Codex checkout on
the same branch. The idle Codex checkout is now detached at `923d8ef` with no
changes, the main checkout `E:\work\send-airplay2` tracks `main`, and the merged
branch was deleted locally (D45).

- **PR #11 is merged** into `main` as `2b0e57c9d3ee44c5afc66418c23084ffada01d59`.
  Its final head `105a7b0ad8cbd179c3f06562e7ef9d2be3c126d8` passed all ten
  checks ([CI run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37476606466)).
  It contains `serve`, the reference playback records, the `bplist00` codec
  (D27) and the session design with decisions D28-D31.
- **PR #12 branch (merged):** `claude/url-playback-session`, created from that merge
  for the session implementation in [session-design.md](session-design.md).
  CI runs only for pull requests. PR #12 merged as `2bb25df`; new work uses a
  fresh branch from `main`.
- **Done on this branch:** steps 1-6 and the minimum native remote session.
  Channel key derivation, the event
  channel, the NTP timing responder, the session messages, and the
  `UrlPlaybackSession` orchestrator (section 5).
- **Initial step-6 G1 failure (superseded by the pass below):** `cast` ran the
  URL sequence on Living Room. The receiver fetched the full file and
  reported `playing`, but showed nothing. With pyatv 0.18.0's remote-control
  session held open (`atvremote push_updates`), the same native cast played
  video and audio correctly (user-observed). That supports H5: the
  remote-control session is required for presentation. See
  receiver-validation.md, "Native cast, hardware gate G1".
- **Native minimum now PASSED G1 (2026-10-07):** `cast` opens a separately
  verified remote-control-only SETUP/event session before URL start, with its own
  keys/counters/UUID. No remote RECORD, feedback, data stream, MRP or pyatv.
  The user observed normal video/audio and return home after the 45-second run
  was stopped by Enter. Full-file fetch, no failed reads, exit 0. See the new
  receiver record and sanitized artifact for exact counts and executable hash.
- **Step 2 implemented:** bounded in-tree protobuf/message codecs, data framing,
  remote RECORD/data SETUP, MRP handshake/correlation/heartbeat, ownership tracking
  and native status/pause/resume/absolute seek/stop. The native receiver accepted
  the controls and telemetry followed both seek directions; see the dated G2
  record for the observer status. D32 documents cooperative startup binding on
  firmware that omits the URL/item UUID.
- **Step 3 implemented (D33):** sole lifecycle supervisor, automatic ordered
  teardown, first end reason, command cancellation and pollable CLI stdin. Native
  EOF exited with stdin held open; ten short native cycles passed. The sanitized
  lifecycle record fingerprints the executable and runtime sources.
- **G2 and EOF observation passed:** the user confirmed visible controls, both
  seek directions, normal resumed audio and home after sender Stop; EOF also
  returned home with normal video/audio. Receiver-remote stop returned home and
  automatically cleaned up, but reported connection_lost/exit 1. Sleep likewise
  triggered bounded cleanup. Fresh casting after wake reused credentials and
  played normal video/audio; EOF again returned home (user-confirmed).
- **Stop/buffering investigation (D34):** fixed failure diagnostics identify URL
  event socket EOF on a repeated, user-confirmed remote Stop, with last state
  playing and no stopped/idle/ended event. Preserve failure classification until
  receiver intent is validated. A fresh full 131.6-second clip, with status-only
  input and no seek, reached natural media_end/exit 0 and four heartbeats. Two
  brief loading/playing transitions occurred near 18 s. **Full-clip video FAIL:**
  the user reports a short buffering stop around 18 s followed by frozen video
  through clip end; audio continued normally. EOF cleanup passed, but sustained
  video presentation did not. The original file plays normally past 18 s on the
  PC (user-confirmed). Home return for this particular run is unconfirmed.
  See the [stop/full-clip artifact](validation/native-stop-buffering-windows-static-2026-10-07.json).
- **HTTP/buffering comparison (D35):** opt-in socket-write/completion and receiver
  scalar diagnostics are implemented. A fresh default run paused at zero without
  remote input (first frame visible). Minimal remote/four slots passed the whole
  clip's video/audio/Home. A same-executable MRP/four-slot repeat buffered but
  recovered normal video/audio/Home; slot release closely preceded new range
  admission and playback resumption. MRP with 16 slots admitted up to six active
  requests and passed full video/audio/Home with no recorded loading transition.
  Admission starvation is a supported buffering hypothesis; the original
  persistent freeze and startup pause are unresolved. D35 retained the four-slot
  default pending the D36 checks below. See the [four-run artifact](validation/native-http-buffering-windows-static-2026-10-07.json).
- **Bounded cast policy (D36):** explicit 16-slot controls passed by user observation
  (pause/resume, forward/backward seek, resumed video/audio, Stop/Home). Ten new
  processes alternated MRP Stop and Enter; all were owned/playing and exited 0
  with joined cleanup, no session failure or failed file reads. A full-clip repeat
  reached natural media_end/exit 0 with stdin held open, four heartbeats, no recorded
  loading transition and up to five active media requests. The user confirmed
  normal full-clip video/audio and Home at EOF. A separate first attempt at 16 slots paused at zero
  before transport commands; user confirmed first frame and untouched remote.
  Cleanup passed, but this startup failure remains explicit. Normal `cast` now
  defaults to 16 bounded slots (override 1..16); generic server/`serve` remain four.
  See the [new artifact](validation/native-capacity-controls-lifecycle-windows-static-2026-10-07.json).
- **Frozen-video priority (user decision D37):** insufficient media admission
  capacity is the likely cause of the original frozen-video run. Keep its FAIL
  evidence and treat it as low priority for now; raise its priority and reopen
  active investigation if frozen video recurs in later testing. This is a triage
  decision, not a proven root cause or a retroactive playback PASS.
- **Startup investigation (D38):** bounded startup timing now shows all four
  queue-command acknowledgements preceding the first playing event. One unattended
  MRP trial reported rate 1 then paused at zero about 0.4 s later; rejecting only
  zero-rate playing was insufficient. `start()` now requires one continuous second
  of eligible URL playing, resetting on loading/pause/zero/reverse-rate states.
  The original deadline and cancellation still apply; there is no Play retry.
  Fixed 64-entry startup traces survive failures and include successful queue
  acknowledgements, allowlisted states/rates, relative times and cleanup status.
  Six short confirmed-startup comparisons and one after 300 s of sender inactivity
  returned successfully and cleaned up. Synthetic regressions pass; these native
  runs are telemetry-only. Exact exploratory/final fingerprints and traces are in
  the [startup artifact](validation/native-startup-confirmation-windows-static-2026-10-07.json).
  Submitted Windows static/shared Release passed 23/23 CTest targets each
  (15.41/15.22 s); offline runner contracts passed 10/10, format/diff checks passed.
  Inspect CI at the actual PR head before finalizing.
  The physical receiver pause remains unresolved.
- **Observed manual batch (D39):** the user returned and completed startup,
  controls/full EOF, remote Stop and sleep/wake checks on the submitted D38
  runtime. All six native casts confirmed startup and joined cleanup. Initial
  startup and fresh startup after sender Stop passed video/audio/Home; controls
  passed visibly, and a separate full clip passed normal video/audio/Home at EOF.
  Sleep was observed; wake reached Home, then fresh native playback reused
  credentials and passed video/audio/near-end EOF/Home. Remote Stop returned Home
  and cleaned automatically, but again had URL socket closure without a terminal
  event: connection_lost/exit 1 remains explicit. No startup-pause/frozen-video
  recurrence was reported in this selected batch; their historical evidence and
  limits remain. See the [batch record](manual-validation.md) and
  [exact runtime/observer artifact](validation/native-manual-batch-windows-static-2026-10-07.json).
- **Observed network interruption/recovery (D42):** the user removed the Ethernet
  cable during an owned/playing cast, then reconnected it and confirmed Home.
  URL feedback timed out; automatic cleanup exited connection_lost/1. A fresh
  explicit cast with the same credentials passed observed video/audio and Home
  at planned near-end EOF, exiting media_end/0. Pre-interruption presentation was
  asked about but not reported. See the [artifact](validation/native-network-recovery-windows-static-2026-10-07.json).
- **PR review complete locally (D43):** fixed two reproduced issues: raw peer
  strings/keys/targets in URL output, and decoded plaintext release without
  erasure on error paths. See [scope, regressions and limits](pr-review.md).
- **Merged (D45):** D44's actual head passed all ten checks with the review
  thread resolved, and the user merged PR #12.
- **Next:** continue development using the ordered queue in
  [CONTINUATION.md](CONTINUATION.md); its first item, a versioned public playback
  API design, is a proposal to confirm with the user. Keep ambiguous
  peer closure classified conservatively: the reference audit and single Ethernet
  comparison do not establish Stop intent. Investigate the unresolved receiver
  transition behind the intermittent startup pause using matched Home/idle
  comparisons if it recurs; selected successful starts do not establish its cause.
  Broader lifecycle reliability and other host/receiver gates remain. Preserve credentials,
  independent remote/URL sessions and ordered teardown; no automatic Play/seek.
- **Current support:** private native casting/control experiment on Apple TV 4K /
  tvOS 26.6 (23L773) / Windows 11 x64. No public playback ABI or packaged-host
  proof. The temporary Python firewall rule remains from reference testing and
  should be removed when that testing ends.

### PR #11 record (merged; historical)

The following dated actions and next-step notes describe the state at that merge.
Section 0 and section 7 supersede them for current implementation work.

- **Local session, 2026-10-06 (this update):** the branch was continued from a
  local Claude Code session on the Windows host. The user confirmed the cloud
  session had been stopped.
  1. Built static and shared Release with the pinned vcpkg toolchain
     (`434307d`, reusing the main checkout's installed packages). Both passed all
     15 CTest targets, including `file_source_tests` and `cli_serve`.
  2. Ran step 4 of [reference-baseline.md](reference-baseline.md) with
     `serve` and pyatv 0.18.0, driven by a script that kept the receiver
     address and private URL out of all output. **Result: FAIL, known upstream
     tvOS 26 incompatibility.** The receiver accepts pyatv's legacy `/play`
     (200) but returns 500 for `/playback-info` and never connects to `serve`.
     A public Apple HLS URL fails identically. SETUP first needed a temporary
     inbound UDP rule for pyatv's NTP timing server, created by the user.
     Details and sanitized sequence:
     [receiver-validation.md](receiver-validation.md#pyatv-reference-playback-2026-10-06).
  3. The user chose to try the unmerged fix `robkochman/pyatv@8144c77c`, linked
     from [pyatv#2846](https://github.com/postlund/pyatv/pull/2846). The agent
     reviewed its source diff; the user installed it into a separate venv
     (`%USERPROFILE%\pyatv-fork8144`) and ran the filtered driver. **Result:
     PASS.** Video and audio played (user-observed). The receiver opened 17
     connections to `serve` and read the whole file (`reads=1466
     bytes=95888702 failed_reads=0`). Flow: type-130 control stream, then
     `POST /command` queue commands, with `loading` then `playing` events.
     Details:
     [receiver-validation.md](receiver-validation.md#reference-playback-with-unmerged-pyatv-fix-2026-10-06).
  4. Recorded both results there and in section 5. No C++ changed.
  5. Control pass with the fork, run by the user: status, forward seek,
     pause, resume and position all PASSED (user-observed, values consistent).
     `stop` exited 0 and the TV returned to the home screen, but the receiver
     reported `Paused` and the sender's session stayed open until its
     connections closed. See the control table in
     receiver-validation.md.
  6. Implemented the bounded in-tree `bplist00` codec (D27) with plistlib
     fixtures; see section 5. Not yet used by the library.
- **Next actions:**
  1. Native session layer: the proposal is in
     [session-design.md](session-design.md). It separates validated facts from
     hypotheses H1-H4. The user decided D28 (threads), D29 (MRP controls now)
     and D30 (identity), then D31 (in-tree protobuf wire codec). Next: follow
     the staged plan, starting with channel key derivation.
     Note: the seek, pause and status controls validated with the fork used
     pyatv's MRP data stream, not `/command`. The plist codec (D27) is in
     place. Use the fork's observed sequence as protocol
     reference, not as copied code. Define stop as explicit teardown verified
     by receiver state.
  2. Small `serve` diagnostic: count connections and requests in the stop
     summary.
  Ask the user before marking the PR ready or merging. Merge commits have been
  the convention.
- **Local-session rules that matter here:**
  - Never print, log or commit the `serve` URL, the Apple TV's IP/MAC/identifiers,
    PINs or anything from `.pyatv.conf` or Windows Credential Manager. Do not read
    the credential files at all.
  - Run pyatv with `--debug` only if needed, and sanitize before quoting.
  - Pairing was already completed for both `airplay2-cli` (profile chosen by the
    user) and pyatv; do not re-pair or `forget` unless the user asks.
  - Apply AGENTS.md (C++ readability, clang-format, `git diff --check`, CTest
    static/shared) to any further code change.
- **Cloud-session leftovers:** the user confirmed on 2026-10-06 that the cloud
  session was stopped.
- **Temporary firewall rule:** the user created an inbound UDP Allow rule,
  "pyatv-baseline timing (temporary)", for the Store Python interpreter. Remind
  the user to remove it when reference testing ends.

## 1. Goal and user requirements

The originating discussion was titled **Assess AirPlay 2 Feasibility**. The user
asked whether a library could cast media from any device to an AirPlay 2 receiver,
such as Apple TV, and provided the UxPlay AirPlay2-protocol wiki as a starting point.
They clarified the requirements as follows:

- "Any device" means a native application on mainstream Windows, Linux, macOS
  and Android. It does not mean a browser-only or every possible device solution.
- Cast supported, unprotected media to explicitly tested AirPlay receivers.
- Milestone 1: a tested standalone library that supports that use case.
- Milestone 2: integrate the library into a fork of
  [huynhsontung/Screenbox](https://github.com/huynhsontung/Screenbox), similarly to
  its existing Chromecast casting support.
- GitHub access under `ilyalissoboi` was authorized. The user requested continued
  work, then requested this documentation in the currently open PR.

The implementation repository is
[ilyalissoboi/send-airplay2](https://github.com/ilyalissoboi/send-airplay2).
It was created with an Apache-2.0 LICENSE before implementation began.

## 2. Current repository and PR state

The table below is the historical PR #12 checkpoint. For the current slice,
use section 0: `main` was `a0fd5b9` (PRs #1-#38 merged), the C API is version 3,
and Screenbox fork PRs #1-#7 are merged. D62's final PR head `ed24ae4` passed
all 14 CI jobs; inspect live refs and the active slice's actual head again.

| Item | Snapshot |
|---|---|
| Foundation PR | [#1: feat: establish portable AirPlay sender foundation](https://github.com/ilyalissoboi/send-airplay2/pull/1) |
| Foundation PR state | Merged on 2026-10-06; verified through GitHub CLI |
| Discovery PR | [#2: feat: add receiver discovery and diagnostic CLI](https://github.com/ilyalissoboi/send-airplay2/pull/2), merged on 2026-10-06 |
| Pairing/transport PR | [#3: feat: add pairing TLV8 and authenticated control record codecs](https://github.com/ilyalissoboi/send-airplay2/pull/3), merged on 2026-10-06 |
| Peer-verification PR | [#4: feat: add authenticated peer verification for existing credentials](https://github.com/ilyalissoboi/send-airplay2/pull/4), merged on 2026-10-06 |
| PIN-pairing PR | [#5: feat: add authenticated PIN pairing message flow](https://github.com/ilyalissoboi/send-airplay2/pull/5), merged on 2026-10-06; verified merge `24bb2b86a1a725ea82a4a32d5a33fd2c22ef7e9d` |
| Receiver-transport PR | [#6: feat: add bounded authenticated receiver transport](https://github.com/ilyalissoboi/send-airplay2/pull/6), merged on 2026-10-06; verified merge `8c4ce3e47a3fd6d2cf73ab4197892a9004803d99` |
| Credential-storage/CLI PR | [#7: feat: add Windows credential storage and authentication CLI](https://github.com/ilyalissoboi/send-airplay2/pull/7), merged; verified merge `6e83badfc5146371ee0c886e3f75fba492f9ab61` |
| Compatibility PR | [#8: fix: accept bounded Apple TV pair-setup metadata](https://github.com/ilyalissoboi/send-airplay2/pull/8), merged; verified merge `f24ac4825a47b2b641caaac1d0f1c00dd1058c33` |
| E2E PR | [#9: test: add noninteractive CLI e2e runner](https://github.com/ilyalissoboi/send-airplay2/pull/9), merged; verified merge `d70a143a97f06b30c8b5bd266c03c36d8ed07ec9` |
| Media-server PR | [#10: feat: add bounded Boost HTTP media server](https://github.com/ilyalissoboi/send-airplay2/pull/10), merged; verified merge `6b9680237184741100415aeb21d440825662ba37` on `main`. Final head `f55b9db1a95f0c0c66084b05c1b3ca345b607270` passed all ten checks in [PR CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37461887701) |
| Media-server source commit | `c4e73156bffadacac3860a72df328c46a5fe5986`; subsequent documentation commits record PR/check evidence |
| E2E source commit | `2bf31dd8f61d17e4475ec36c78f81bcac21f0009`; subsequent documentation commits record checks |
| Portable E2E source commit | `650a0440a72d5e263bcca3fa8e6aa623cd628a7f`; closed-port correction and refreshed live artifacts |
| Compatibility source commit | `f61698fa930893c139efcb5f5d2b0a40a93d0cae`; subsequent documentation commits record checks |
| Credential-storage/CLI source commit | `37f5e3fe90136be25d89ede9c150bcd0f582969b`; subsequent documentation commits record checks |
| Receiver-transport implementation commit | `ffbe86f3e5d4aa6bc590d30c61ec70d42720615f`; subsequent IPv6 authority fix at `979ef0829248203684939274eb3864b8241845cc` |
| Target | `main` |
| Serve/baseline/codec PR | [#11: feat: add serve command, reference playback records, bplist codec and session design](https://github.com/ilyalissoboi/send-airplay2/pull/11), merged as `2b0e57c9d3ee44c5afc66418c23084ffada01d59`; final head `105a7b0ad8cbd179c3f06562e7ef9d2be3c126d8` passed all ten checks ([CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37476606466)) |
| Native session PR | [#12: feat: native URL playback, MRP controls and lifecycle cleanup](https://github.com/ilyalissoboi/send-airplay2/pull/12), merged as `2bb25df4b4f58ef0a2c6936ed6f44161b815890c`; final head `923d8efbee066a5a7ce37dd6e83cb891ede58a06` passed all ten checks ([CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37587291158)) |
| Current branch | None open; start each slice on a fresh branch from `main` (D45) |
| Pairing/control implementation commit | `ee4afa80172d38300078fad0b5a2332898e95cd5`; later documentation commits record checks |
| Peer-verification implementation commit | `7b572a7b24d7242200e0cb1321c366a83932b3da`; all six platform/static/shared jobs and Linux ASan/UBSan passed |
| PIN-pairing implementation commit | `8770909ce66239c03664c324d966420b3a18adc0`; local static/shared checks passed; CI evidence below |
| Hardened PIN-pairing source commit | `4c7dcc4f6af6f5ebc391097dcc0998262436f761`; all platform/static/shared and sanitizer checks passed; subsequent documentation commits record evidence |
| Discovery implementation commit | `48189897ba167b44c3da7c6e4a7857bf28120498`; later commits add documentation and a C++ readability/ownership pass |
| C++ readability commit | `23f8ff08a12431fc0aba95a21aaf881aa7458674`; all platform/sanitizer CI jobs passed, subsequent commits record evidence only |
| Final foundation PR head | `034983ca095fd0803d3de2307c6d27fdf18db488` on `feat/portable-foundation` |
| Original main commit | `8c77b15d391e14b53a3591eea7d0ac6e28376813` (LICENSE only) |
| Foundation commit | `dbd654b1d92057b3208953226186c5c2b206ccff` |
| Crypto dependencies | OpenSSL 3.5+ libcrypto and Botan 3.12+ C FFI for private SRP; pinned vcpkg supplies 3.6.5/3.12.0. Public authenticated-session/packaged runtime loading is pending |
| Actual casting support | Private native URL start/CLI implemented in merged PR #12; native-only video/audio and sender stop/home-screen G1 PASS; native MRP controls implemented with dated G2 evidence; public API pending |
| Receiver validation | Windows discovery observed; user confirmed authenticated PIN enrollment, credential save/reload, fresh-socket and separate-process verification on Apple TV 4K / tvOS 26.6 (verify exit 0); pyatv 0.18.0 reference AirPlay pairing passed; pyatv 0.18.0 reference playback FAILED on tvOS 26.6 (known upstream issue, no media fetch); unmerged pyatv fix played video and audio fetched from `airplay2-cli serve`; native-only URL playback with minimum native remote SETUP/event session G1 PASS, user-observed video/audio and return home after sender stop |
| Screenbox changes | None; source audit only, no integration fork created in this session |

PRs #1 through #10 are merged and `main` contains the foundation/discovery/control codecs, peer verification, PIN setup, bounded receiver transport and Windows credential CLI, including M6 metadata compatibility, the noninteractive E2E runner and the bounded Boost media server. PRs #11 and #12 are merged; `main` now also contains the private native session, MRP controls and lifecycle cleanup (section 0). Verify current GitHub and
local branch state before further development; the original foundation SHA is
not the final PR head.

At the user's request, CI now triggers only on `pull_request`, removing duplicate
push and PR runs. Opening, updating or reopening a PR runs the existing ten jobs
once; a branch push without an open PR does not trigger this workflow.

### Local testing environment

User-provided on 2026-10-06 (Asia/Tokyo):

- Receiver: **Apple TV 4K**; subsequent LAN discovery advertised `AppleTV14,1`
  and name "Living Room". pyatv 0.18.0 maps it to Apple TV 4K (gen 3);
  generation not independently established beyond that model-table mapping.
- Firmware: **tvOS 26.6 (23L773)**, user-reported (build supplied 2026-10-06), consistent
  with advertised `osvers`/`ov`. AirPlay `srcvers` is not a tvOS build identifier.
- AirPlay access: limited to the same network (user-reported); no password;
  pairing mandatory per pyatv scan.
- Intended testing host: **Windows 11 x64**; observed build `10.0.26200`, AMD64.

The host is on Wi-Fi classified by Windows as a Public network; see
receiver-validation.md for firewall details. The pyatv 0.18.0 reference playback
attempt failed on this firmware. An unmerged pyatv fix then played the MP4 from
`airplay2-cli serve`; see section 5.
See [receiver-validation.md](receiver-validation.md) for the test record.

## 3. Feasibility findings and evidence levels

**Assessment:** the narrowed use case is technically plausible and manageable as
an incremental interoperability project. It is not established as working by
this repository. The evidence comes from existing sender implementations and
protocol research; only real receiver tests can establish this library's support.

Separate three things when reporting progress:

1. The receiver advertises AirPlay 2 capabilities.
2. A legacy-compatible media path works on that receiver.
3. A modern authenticated AirPlay 2 session path works.

Success at one does not prove the others. Record the actual path, receiver model
and firmware in every interoperability result. Avoid blanket "AirPlay 2 supported"
claims based on compilation, mock tests or an unauthenticated `/play` response.

Media playback by URL is distinct from screen mirroring, captured system audio,
multiroom synchronization and protected-media playback. The proposed first
approach lets the receiver fetch and decode an unprotected H.264/AAC MP4 from a
sender-hosted HTTP URL. That requires the receiver to reach the sender's media
server. Audio within the MP4 does not prove standalone speaker/audio transport.

### References already examined

| Reference | Finding and intended use | Evidence status |
|---|---|---|
| [UxPlay AirPlay2-protocol wiki](https://github.com/FDH2/UxPlay/wiki/AirPlay2-protocol) | Index of pairing, crypto, protocol and implementation references | Read; useful starting point, not a complete sender specification |
| [pyatv protocols](https://pyatv.dev/documentation/protocols/) | Discovery/authentication/protocol reference | Read; not a certification of this library |
| [pyatv AirPlayV2](https://github.com/postlund/pyatv/blob/master/pyatv/protocols/raop/protocols/airplayv2.py) | Executable reference for authenticated URL playback setup | Source inspected, blob `3d61a34e00559554477cd2ff97d9c337a9e7ccff` |
| [pyatv player](https://github.com/postlund/pyatv/blob/master/pyatv/protocols/airplay/player.py) | Playback polling, startup/error handling | Source inspected, blob `e2c073cb0ba3e2a1c29f3882db88ed257bd9e2ef` |
| [Screenbox](https://github.com/huynhsontung/Screenbox) | Host architecture and integration boundary | Selected files inspected at `46aadf6b20ef5d8348a6049c16c882c22ec0f84e` |

The inspected pyatv AirPlayV2 URL path verifies the connection, performs base RTSP
SETUP, establishes an event channel, starts periodic feedback, sends RECORD,
posts a binary-plist `/play` body, and sets the playback rate. Its player polls
`/playback-info` and interprets receiver errors. This sequence is a reference for
investigation; it must not be assumed to be a minimal or universally sufficient
sequence on every receiver. Reinspect source and obtain sanitized local traces
before implementing firmware-specific behavior.

The initial assessment also identified the following candidates. These are
research leads, not dependencies selected for the native core:

- **pyatv:** reference sender and proposed local baseline tool; a Python package
  is not the intended production native library. The earlier assessment treated
  its audio coverage as partial; do not infer universal audio support.
- **UxPlay:** a receiver/reference, not a drop-in sender library.
- **pair_ap:** an authentication building-block candidate linked from the wiki
  ([ejurgensen/pair_ap](https://github.com/ejurgensen/pair_ap)); not integrated.
- **libraop:** an audio/authentication research lead linked from the wiki
  ([philippe44/libraop](https://github.com/philippe44/libraop)); not integrated.
- **airplay2-sender-cpp:** an audio candidate mentioned in the earlier assessment;
  its exact repository/revision was not retained in the available continuation
  record. Resolve its provenance before relying on it. It is not hardware validated
  or adopted here.

No external implementation code has been copied into this project. Discovery
uses in-tree native sockets, documented in [discovery.md](discovery.md). OpenSSL is
now selected for private AEAD/HKDF adapters; see [dependencies.md](dependencies.md).
No plist or audio dependency has been selected. Check each candidate's
actual revision, license, maintenance and platform support before incorporating it;
do not assume all references share this project's Apache-2.0 license.

## 4. Decision record

User requirements are distinguished from engineering choices proposed in the
assessment and used to guide this first PR. Continued work authorized the first
foundation; it did not establish hardware compatibility or freeze the API.

| ID | Decision / working direction | Status and reason |
|---|---|---|
| D01 | Native Windows/Linux/macOS/Android, tested unprotected media | User requirement |
| D02 | Standalone library before Screenbox integration | User milestone order |
| D03 | Portable C++ core, eventual stable/versioned C ABI | Proposed architecture; C++17 foundation and experimental C API implemented |
| D04 | C# wrapper for Screenbox; JNI for Android | Planned; neither wrapper implemented |
| D05 | Begin with local H.264/AAC MP4 on one specified Apple TV model/firmware | Proposed first vertical slice; user supplied Apple TV 4K / tvOS 26.6 / Windows 11 x64; generation from pyatv model mapping; firmware build 23L773, host build 10.0.26200 recorded |
| D06 | CLI plus minimal packaged Windows C# host before Screenbox changes | Planned validation gate; packaged networking/file/native-loading risks must be tested early |
| D07 | Receiver-side URL playback with local HTTP serving | Implemented private URL session and server; native-only presentation G1 passed |
| D08 | Media source size/read-at callbacks, not filesystem paths alone | Experimental C++ callback contract and file adapter implemented; StorageFile/content URI adapters and public C ABI pending |
| D09 | Keep standalone audio transport separate from first MP4 proof | Planned; audio backend pending |
| D10 | AirPlay beside Chromecast through a provider-neutral boundary | Planned Screenbox integration; retain existing Chromecast behavior behind its adapter |
| D11 | No initial DRM, mirroring, system-audio capture, synchronized multiroom, auto-transcoding or universal codec/receiver support | Initial scope limits from assessment; unsupported media should fail explicitly |
| D12 | Preserve repository's Apache-2.0 license | Implemented; no relicensing decision made |
| D13 | Keep implementation reviewable on PR #1 | Foundation reviewed through PR #1; now merged |
| D14 | Ignore unsupported multipart/invalid Range fields and serve full content | Implemented resolver policy; documented in design.md |
| D15 | Bounded native IPv4 mDNS scan, portable DNS-SD cache/parser, no new third-party runtime dependency | Implemented; adapter isolated, IPv6-only discovery and Android device validation pending |
| D16 | Keep protocol-specific records and merge only by matching normalized advertised device identity | Implemented; hostname/friendly name alone is insufficient, advertisements remain unauthenticated |
| D17 | Experimental C++ discovery API plus CLI before versioned C discovery ABI/event API | Implemented; shared users require compatible C++ runtime. A C snapshot interface followed in D54; an event API is still pending |
| D18 | OpenSSL 3.5+ EVP primitives, private bounded pairing/control codecs before receiver handshake | Engineering choice, implemented; no homegrown cryptography, public pairing API deferred until peer authentication and ownership contracts are complete |
| D19 | Implement existing-credential HAP peer verification before first-time PIN provisioning | Implemented privately with EVP X25519/Ed25519; pinned ID/key, strict M2/M4 and one-time key release. Storage and bounded network transport are integrated; broader hardware authentication remains a gate |
| D20 | Botan 3.12 C FFI for fixed-profile HAP PIN/SRP and Ed25519 key validation | Engineering choice, implemented privately; C++17 core, maintained SRP/subgroup checks, mandatory server proof and accessory signature. vcpkg excludes UWP; resolve packaged-host integration before Screenbox work |
| D21 | Private synchronous native TCP with one outstanding request and a strict bounded HTTP/RTSP profile | Engineering choice, merged in PR #6; absolute deadlines/cancellation, terminal cleanup, HTTP ordered correlation with optional validated CSeq, mandatory RTSP CSeq, verified record transition. Hardware remains a gate |
| D22 | Private versioned credential envelope, trusted host store and Windows desktop CLI before a public auth ABI | Implemented and merged in PR #7; current-user/same-computer Credential Manager, create-only profiles, no plaintext fallback or automatic re-pair. Other OS stores and packaged hosts remain gates |
| D23 | Accept one optional opaque M6 type-17 metadata field, bounded to 0..256 bytes; discard and erase it | Live Apple TV M6 headers showed `17:159`. Required ID/key/signature, server proof, AEAD, duplicate and other unknown-field rejection remain enforced; metadata never influences trust, naming or storage. Sanitized phase/HTTP/TLV-header diagnostics retain no payloads |
| D24 | Standard-library Python CLI E2E runner using an existing paired profile; explicit evidence kinds and opt-in disposable deletion | User requested unattended actions available now. No PIN collection/enrollment, credential export/clone, receiver changes or primary-profile deletion; ephemeral loopback fault peers and fresh live verification distinguish synthetic failures from hardware evidence |
| D25 | Use Boost.Beast/Asio 1.92 for HTTP media serving behind an experimental C++ callback API | User explicitly selected Boost. Private Boost types, one concrete route-selected bind address, receiver-IP filtering, random per-session bearer URLs, bounded accepted connections/workers/buffers and absolute deadlines. No wildcard listener, scoped/link-local IPv6 URLs, firewall changes or playback commands; callbacks must cooperate with cancellation |
| D26 | Development `airplay2-cli serve` with a private file adapter that serializes reads of one `std::ifstream` | User chose option 1: serve the reference-baseline MP4 with this project's server rather than a third-party one, so receiver fetch/firewall reachability is tested too. Portable standard-library I/O over parallel positional reads; size snapshot, receiver-only access, private URL printed once, stdin-driven stop with aggregate read counts. Not a playback API |
| D27 | In-tree bounded `bplist00` subset codec for the session layer | User chose this on 2026-10-06 over libplist (LGPL-2.1) and other libraries. Apache-2.0, no new dependency. Covers only the types the `/command` flow needs, with strict bounds, tested against independent Python `plistlib` fixtures |
| D28 | Session threading: synchronous components with dedicated reader, feedback and timing threads | User decision 2026-10-06 over Boost.Asio async; reuses the deadline/cancellation transport. See session-design.md |
| D29 | Control path: implement MRP (remote-control session, data stream, protobuf messages) now | User decision 2026-10-06 over `/command`-only controls. The seek, pause and status validated with the fork used this path; `/command` still starts playback |
| D30 | Sender identity: reference SETUP values first, configurable, then a hardware test of neutral values; random per-session device ID, never the host MAC | User decision 2026-10-06 |
| D31 | MRP protobuf: in-tree bounded wire codec (varint, length-delimited, fixed32/64, unknown fields skipped) plus hand-written mapping of about 10 messages | User decision 2026-10-06 over protozero and Google protobuf/protoc. Apache-2.0, no dependency. Field numbers from pyatv's MIT `.proto` files, with provenance recorded before use; fixtures from Python `protobuf` with pyatv's compiled messages |

**D32 (engineering decision, 2026-10-07):** retain an explicit MRP player
path. Exact URL/queue UUID linkage is preferred; tvOS 26.6 omits those fields
in the observed item. Use cooperative startup correlation only for a newly
appeared item in the active TVAirPlay player after our URL start, with duration
matching within 0.5 s. Capture the pre-start baseline, bind once and refuse
stale, replacement or unrelated items. This is not proof against concurrent
AirPlay senders; see [MRP contracts](mrp-controls.md).

**D33 (engineering decision, 2026-10-07):** one lifecycle supervisor owns
automatic cleanup after startup. Preserve the first terminal reason and expose
completed joins/key erasure separately. Cancel pending commands before closing
MRP; retain URL-before-remote cleanup. EOF requires owned receiver telemetry at
duration while paused/stopped, never an extrapolated clock. URL stopped/idle
allows one second for final MRP evidence. Do not automatically reconnect,
resume, re-pair or adopt replacement players; a recovered network permits a
fresh explicit cast. The host owns media-server shutdown. See [contracts](mrp-controls.md).

**D34 (engineering decision, 2026-10-07):** retain fixed failure channel/operation
and error category through cleanup. Expose MRP's first terminal category without
peer descriptions, and distinguish URL/remote feedback and event failures.
The diagnostic is the first failure recorded by the session, not a total ordering
of independent socket events. Intentional normal-cleanup cancellation adds no
failure. A repeated user-confirmed remote Stop closed the URL event connection
with last state playing and no stopped/idle/ended event; preserve connection_lost
until explicit, validated receiver intent is available. Pause or socket EOF alone
must not be relabelled normal stop. Notification shapes are retained without their
unmapped values; do not guess their semantics. No dependency or copied source.

**D35 (engineering decision, 2026-10-07):** diagnose the sustained video freeze
before changing receiver/session behavior. Add optional fixed-size HTTP completion
records and allowlisted finite receiver scalar values. Count partial socket writes
even after cancellation; publish each record after pending callbacks drain.
Complete local TCP writes are not receiver receipt/decoding proof. Preserve the
10-minute cast request budget, all queue commands, session/control topology and
teardown order in the default mode. `--minimal-remote` explicitly selects the
existing SETUP/event-only comparison (omitting remote RECORD/data/MRP/feedback),
with no MRP ownership/control/EOF telemetry. It is not an automatic fallback.
No automatic Play/seek workaround, dependency or copied source.
Static/shared Release passed 23/23 CTest targets (13.74/13.39 s), offline runner
contracts passed 10/10, and touched C++ format/diff checks passed. The instrumented
normal-mode reproduction instead paused at zero without remote input; the user
saw its first frame. Preserve this separate startup failure. The explicit minimal
comparison subsequently played the full clip with normal video/audio/Home by
user report. A same-executable default-mode repeat buffered but resumed normal
video. HTTP admission/resume timing suggests comparing the bounded slot limit;
`--media-connections 1..16` exposed that explicit test while D35 retained default 4.
MRP/16 slots then passed full video/audio/Home with no recorded loading transition
and up to six active requests. At the D35 gate the default remained four pending
the additional control/lifecycle validation recorded under D36 below. The
persistent freeze itself has not been reproduced by these new runs.

**D36 (engineering decision, 2026-10-07):** choose a fixed, bounded 16-slot
normal `cast` admission budget after the explicit higher-capacity control and
selected lifecycle checks. Retain `--media-connections 1..16`; generic
`MediaServerOptions` and `serve` keep their four-slot default. The prior D35
full-clip video/audio/Home pass and D36 user-confirmed controls support this
choice. Ten fresh processes and another natural EOF passed cleanup at 16;
remote Stop and sleep/wake remain the earlier four-slot evidence, not new 16-slot
claims. The new artifact identifies the explicit-override pre-policy executable
and source blobs; rebuilt default-policy unit/CI results are separate.

This permits at most 16 source workers and 1 MiB of 64-KiB body buffers rather
than four workers/256 KiB, while preserving the 10-minute request deadline,
range policy, independent authenticated sessions, credentials and teardown order.
The new control trace used six active slots and the EOF trace five. No new queue,
eviction, automatic Play/seek, retry, reconnect or dependency is introduced.
Capacity does not fix every presentation failure: the first 16-slot attempt
paused at zero about 0.1 s after the CLI playing line, with two active requests
and no transport command. The user confirmed first frame/remote untouched.
Preserve that failure and the original frozen-video run; investigate startup
ordering/state next instead of interpreting successful telemetry as moving video.
Static/shared Release passed 23/23 CTest targets each (13.48/13.44 s);
offline runner contracts passed 10/10, and touched C++ format/diff checks passed.
See [receiver validation](receiver-validation.md#bounded-cast-admission-policy-and-controlslifecycle-checks-2026-10-07).

**D37 (user prioritization decision, 2026-10-07):** the user considers insufficient
media connections the most likely cause of the original frozen-video failure
and requests low priority unless it recurs in later testing. Retain the historical
FAIL and capacity evidence without claiming proven causality or a resolved
decoder fault. Monitor later playback tests; any recurrence restores active
investigation priority. The separate startup pause remains the next task.

**D38 (engineering decision and user testing constraint, 2026-10-07):** diagnose
startup rather than automatically reissuing Play. Rate-zero playing events are
not eligible startup. An unattended reproduction also played at rate 1 briefly
then paused at zero, so require a continuous 1,000-ms confirmation interval before
returning startup success. Reset it on every observed non-playing/zero/reverse
state; do not extend the existing deadline or ignore cancellation. Missing rate
retains compatibility with state-only receivers. This is a reporting/readiness
contract, not a fix for receiver pausing, visual proof or a future-playback guarantee.
The private option permits shorter synthetic intervals; normal `cast` uses 1 s.

Add opt-in fixed 64-entry startup traces, copied to caller-owned diagnostics on
return/rethrow; no user callback runs on transport threads. Keep only named
phases, fixed state enums, successful command HTTP statuses, finite rates and
steady elapsed milliseconds. Overflow drops new records without affecting
state processing. Failed `cast --event-log` startup emits this trace after
URL/remote cleanup, then stops/drains its media server before rethrowing. No
identity, URL, credential, metadata or arbitrary receiver text enters these rows.
All queue commands, MRP topology, media admission and ordered teardown remain.

During D38 the user was unavailable for manual testing for approximately one
hour and requested batched checkpoints afterward. At that gate, unattended
native telemetry and local/CI results were separate from pending visual gates.
The completed D39 batch is maintained in
[manual-validation.md](manual-validation.md). Frozen-video triage stays D37:
low priority unless it recurs; a paused-at-zero startup is a separate issue.

**D39 (observed validation gate, 2026-10-07):** the user announced availability
and completed the D38 manual batch. No runtime change. The submitted static CLI
SHA-256 is `f5f7fcd3ef15307c7e64294875f405e115d4452c6c71760cda84c6ee56970309`;
all recorded runtime source blobs match tested head
`fa9b9189f599b0b6405b8c1b81327761900470c2` (D38 code head `6bed9fe`).
Normal defaults were used: MRP, 16 media slots, 30,000-ms startup timeout,
1,000-ms confirmation, existing credentials and no startup recovery command.
Six starts confirmed after 1,011-1,027 ms of eligible playing. The user confirmed
startup/controls/full-clip and post-wake presentation/Home results. Initial Home
and untouched remote were not explicitly answered for the first run; the next
cast followed user-confirmed sender Stop/Home. Keep that condition limit explicit.

Remote Stop again returned Home but ended via URL socket EOF while still playing;
normal protocol intent remains unresolved and classification is unchanged.
Sleep reported paused then URL disconnect; cleanup completed, and fresh playback
after wake reused credentials successfully. Neither scenario establishes actual
network interruption/recovery. The physical startup pause was not reproduced
in this selected batch, not proven fixed. Frozen video retains low priority under
D37 unless it recurs; retain its original FAIL. Longer reliability, other hardware,
public ABI and packaged-host gates remain open. Exact observer quotes, timing and
HTTP completion facts are in the new [artifact](validation/native-manual-batch-windows-static-2026-10-07.json).

**D40 (engineering decision, 2026-10-07):** remote Stop classification remains
conservative while gathering bounded remote-event and final MRP observations.
Remote bodies are acknowledged before inspection and erased by a noncopyable
ownership guard. Opt-in logs use fixed type/state/key labels and signed 32-bit
reason/error/status codes, share the 256-entry event bound and omit arbitrary
peer text, names, targets, metadata and identifiers. Malformed observations do
not end an otherwise healthy channel. Remote events never update URL state.
The final CLI diagnostic runs after joined cleanup and exposes retained MRP
ownership/state/EOF, received elapsed time (without extrapolation or clamping),
duration/rate and message/heartbeat counts. It is not a fresh receiver query and
cannot establish that every final message arrived before cancellation.

Windows Release static/shared builds and CTest passed 23/23 each (15.67/15.53 s);
offline runner contracts passed 10/10. Ten touched C++ files pass format checks.
New scripted coverage verifies redaction/code bounds, malformed acknowledged
observations, disabled logging, literal 256-entry overflow, remote/URL state
separation and retained MRP elapsed-time state after cleanup. No dependency or
copied implementation was introduced; Apache-2.0 remains unchanged.
The native comparison and observer scope are recorded separately in the
[D40 artifact](validation/native-stop-diagnostics-windows-static-2026-10-07.json).
The user could not observe the first attempt and requested a repeat; preserve
that attempt as unobserved, with no confirmed remote action or Home result.
The repeated remote-Stop checkpoint returned Home (user-confirmed), closed URL
events while last URL state was playing and retained MRP unowned/unknown.
Sleep was separately confirmed screen-off; URL/MRP paused, MRP retained ownership
with received elapsed time zero and at_end=false, and URL feedback disconnected
first. Both cleaned automatically with connection_lost/exit 1. Remote events in
these traces were allowlisted updateInfo outlines without terminal state/codes.
This single comparison is not a validated normal-stop discriminator: ownership
loss and independent first-failure ordering cannot establish intent. Do not
change failure classification from these snapshots alone.
Fresh playback after confirmed wake/Home reused existing credentials, accepted
the planned near-end seek/Play and ended media_end/exit 0. Final owned MRP paused
at received elapsed time/duration 131.567 s, rate zero, at_end=true. The user
confirmed normal video/audio and Home after EOF. No new freeze was reported;
D37 stays unchanged. Actual network interruption/recovery remains NOT RUN.
Next: review the current diff/readability and actual PR-head CI, then complete
the available G3 network-loss/recovery check. Further normal-Stop classification
needs validated receiver intent, potentially a fixed MRP message-kind/ownership
transition trace; do not infer it from ownership loss plus socket closure.

**D41 (source audit, 2026-10-07):** at the user's request, inspect pyatv before
continuing lifecycle work. Pinned upstream `b277a4c` catches RuntimeError and
ConnectionLostError from `/playback-info` and assumes playback stopped. The
tvOS reference fork `8144c77` selects its event waiter, completing only after
playing then idle/stopped; inherited URL-event socket closure only logs and
retains state. Six isolated offline method probes confirmed polling-error
completion, playing-to-idle/stopped completion, and pending waiters for paused
or socket EOF while playing. They do not exercise a full client or receiver.
The MRP transport distinguishes closure without an exception from an error,
not a validated receiver Stop action. Native D40 policy remains unchanged;
copying the upstream heuristic would be a deliberate API policy change.
Complete the actual network interruption/recovery comparison when available,
then decide how ambiguous peer closure should be exposed. See the
[audit and provenance](pyatv-stop-reference.md); no source vendoring, dependency,
runtime change or new hardware gate occurred.

**D42 (selected hardware gate and policy, 2026-10-07):** the user became available
for actual network interruption/recovery and confirmed removal of the Ethernet
network cable. The established cast had owned/playing telemetry before the cue;
it later reported paused and a URL feedback timeout, automatically joined cleanup
and exited connection_lost/1 with stdin held open and no sender Stop. Retained
MRP remained owned/paused, received elapsed zero, rate one and at_end=false.
The user confirmed Home after reconnection. A fresh process reused the stored
credentials, accepted planned seek 124/Play and ended media_end/0; the user
confirmed normal video/audio and Home after EOF. This is fresh explicit recovery,
not automatic reconnect/resume or a separate full-clip proof. Pre-interruption
video/audio was not reported; user action times were not synchronized with logs.
Exact quotes, source/binary fingerprints and scalar traces are in the
[artifact](validation/native-network-recovery-windows-static-2026-10-07.json).

Keep the existing conservative termination policy: timeout, ownership loss,
pause and connection closure are not remote-Stop intent. The observed network
timeout differs from D40's remote-Stop event EOF but independent channel ordering
and one sample per cause cannot validate a classifier. No pyatv polling heuristic
is adopted. This selected network cleanup/fresh recovery check is PASS; normal
Stop intent, intermittent startup pause and broader reliability remain open.
No native source, dependency or credential changed. The existing D40 static/shared
CTest 23/23 results remain applicable to this unchanged runtime; all ten CI checks
passed at tested head `72b3059`, and all 51 PR-changed C++ files passed formatting.
Inspect the evidence follow-up's actual head/checks. Every staged executable was
restored with a SHA-256 equality check; no sender firewall/network setting changed.
The user temporarily disconnected and restored the receiver's Ethernet connection.

The C API is pre-1.0 and explicitly experimental. "Stable C ABI" is a target,
not a promise about the current header. Define versioning, ownership, threading,
cancellation and errors before publishing production bindings.

**D43 (PR review and engineering fixes, 2026-10-07):** reviewed draft PR #12
against `origin/main` (`2b0e57c9d3ee44c5afc66418c23084ffada01d59`), beginning at
verified head `439133239d7fe73a6d4fa3acbcbc930597e4fe04` (all ten CI checks passed).
The [review record](pr-review.md) describes scope and the two reproduced findings.
URL event outlines/status now use fixed type/state labels and allowlisted key
paths, omitting peer request targets even for unreadable bodies. Event body
ownership uses RAII through parsing/logging failures. Decoded MRP extension
payloads now belong to move-only erasing owners, covering partially decoded
batches, allocation failures, rejected responses and pending response cleanup.
Event-channel body RAII also covers failed acknowledgments before caller ownership;
the combined plaintext regression observes erasure immediately before release.
Known state handling, wire messages, commands, startup policy and end reasons
remain unchanged. No third-party source/dependency was added.

Regression tests failed against the previous behavior, then passed after the
fixes, including the independently reproduced event acknowledgment failure.
Windows static/shared Release each passed 24/24 CTest targets (15.40/15.43 s);
runner contracts passed 10/10; all 52 PR-changed C++ files passed clang-format
dry-run and whitespace checks. Loopback UDP and runner temporary-file checks
required execution outside the restricted sandbox; no firewall/network policy
was changed. Initial D43 commit `d1011ab5b6a413f2e58a5e8587060d5866af6894`
passed all ten [CI checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37582693970).
The acknowledgment-guard head `880feb3` passed nine checks, including sanitizers;
macOS shared exposed an existing silent-feedback test fault armed before startup.
The fixture now arms silence after `start()` returns and waits for a new request;
startup confirmation deliberately spans the old fault window (120 ms versus
30 ms interval plus 80 ms timeout). Deadline and cleanup assertions stay intact.
The revised session test passed five repetitions under concurrent local load.
No production source changes in this test follow-up. Final D43 head
`ab7ec0170dbc364bfbec0e52a1b71ae92a0974a5` passed all ten
[CI checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37584220175).
No receiver test was
performed for this follow-up: D40/D42 source/binary fingerprints remain the
hardware evidence. Remote-Stop intent, intermittent startup pause and broader
reliability remain open; frozen video stays low priority unless it recurs (D37).

**D44 (review feedback and separate-session handoff, 2026-10-07):** the user
approved marking PR #12 ready, then asked to address review comments and prepare
a detailed continuation note. The actual open/ready head was verified as
`ab7ec01`, with all ten CI checks passing and a clean worktree. The automated
review had one unresolved P2 thread,
[network-recovery gate consistency](https://github.com/ilyalissoboi/send-airplay2/pull/12#discussion_r4204085497).
The README's current G3 summary and required-observations table now report the
selected D42 Ethernet interruption cleanup/fresh same-credential recovery PASS.
Automatic in-session reconnect/resume is still unimplemented; broader network
cases and longer reliability are pending. Do not rewrite historical NOT RUN
records or infer removal-detection latency from unsynchronized action cues.

The detailed [CONTINUATION.md](CONTINUATION.md) records current authorization,
branch/status, exact implementation/receiver-tested heads, D39-D43 evidence and
limits, review disposition, ordered follow-up work, build/tool paths, sandbox
behavior, privacy rules and a suggested next-session prompt. Live draft-status
notes were updated to ready for review; **merging still needs user approval**.
This follow-up adds no source/test/dependency changes or hardware run. Check
whitespace, links and status consistency, then publish to the same branch,
resolve the corrected thread and verify actual-head CI. The final PR description
and task report identify that documentation commit and CI run.
Local verification passed 141 documentation links/anchors and current gate/status
consistency, all 52 unchanged PR C++ format checks, and `git diff --check`.
Two pre-existing capacity-record links were corrected to the heading's actual
anchor. No rebuild/CTest or receiver rerun was needed for unchanged implementation;
the D43 results above remain tied to `ab7ec01`.

**D45 (post-merge handoff and branch workflow, 2026-10-07):** the user merged
PR #12 as `2bb25df4b4f58ef0a2c6936ed6f44161b815890c` after D44's head `923d8ef`
passed all ten [checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37587291158)
with the review thread resolved. A new Claude session found the handoff still
describing an open PR and, at the user's request, reconciled the current-status
text and cleaned up stale branches. All twelve local feature branches were verified
as ancestors of `origin/main` before deletion; the main checkout was switched to
`main` and the idle Codex checkout detached at `923d8ef`, both with no local
changes. Remote merged branches were left for the user to delete. Workflow
decision: start each new slice on a fresh branch from `origin/main` with its own
PR, because the prior "continue on the open PR branch" rule no longer applies; ask
before merging. The ordered queue in [CONTINUATION.md](CONTINUATION.md) now
proposes a versioned public playback API design as the next slice, pending user
confirmation. Documentation only: no source, test, dependency or receiver change.

**D46 (public playback interface, 2026-10-07):** the user asked to merge PR #13
and start the API slice. PR #13 merged as `b0b0f86` at verified head `5e0a98e`
with all ten checks passing. The user then chose two options offered as
questions: **credentials by profile name**, loaded from the library's own
platform store so secrets never cross the ABI, and a **playback-only** first
slice (no pairing, profile deletion or diagnostics in the interface yet).
Engineering proposals under those choices: an opaque `sap2_cast` handle owning
the media server and session in the CLI's validated order; `struct_size`
versioning plus `SAP2_PLAYBACK_API_VERSION` 1; host `read_at`/`release` media
callbacks with ownership transferred only on successful create; blocking
start cancelled by `sap2_cast_stop()`; polled and waited status without event
callbacks (keeps D28); end reasons unchanged, so remote Stop/Home still reports
connection_lost; no Play/seek retry. `export.h` now holds `SAP2_API`, and the
library includes the credential store sources and links `advapi32` on Windows.
Implementation is in `src/cast_controller.*` and `src/playback_api.cpp`; tests
are `cast_controller_tests` and the C-language `c_playback_smoke`. Windows
static/shared Release passed 26/26 CTest targets each. At the user's request a
development host, `airplay2-api-host` (`tools/airplay2_api_host.cpp`), was then
added; it calls the library only through `playback.h`, offers interactive,
cancel-during-start and repeated-cycle modes for the
[manual validation plan](public-api.md#manual-validation-plan), and has its own
offline `api_host_arguments` test (27/27 targets each). CI at `5a46749` then
failed once on macOS shared in the unchanged session test's silent-feedback
precondition: no healthy feedback had been answered when `start()` returned.
The test now waits (bounded) for one healthy answer before arming the fault;
its 80 ms deadline, 500 ms cleanup bound and post-arm assertions are unchanged.
It passed 5/5 per build locally and 18/18 with six concurrent copies. The
session tests' scripted fake receiver then moved unchanged into
`tests/fake_receiver.h`, and a test-only `CastDependencies::adjust_session` hook
lets `cast_controller_tests` drive it with MRP disabled: start, pause through
`wait_for_change`, stop ordering, natural end, event loss and concurrent stops.
At the user's request the fake then gained an optional MRP data stream
(`Behavior::mrp_fixtures`, `FakeMrpPeer`), and the controller tests now cover
ownership, accepted pause/play/seek/stop reaching the receiver with their wire
numbers, MRP-reported `media_end` and `ownership_lost`. That work found a
session teardown ordering bug, fixed under D47 below. Receiver results for the
interface are under D48. Contract, mapping tables, limits and next steps:
[public-api.md](public-api.md).

**D47 (remote feedback no longer cancelled at teardown, 2026-10-08):** the MRP
fake showed that cleanup, which stops feedback first, cancelled any remote
`/feedback` request in flight. `ReceiverConnection::request` closes its
connection on any exception, so the remote session closed before the URL
session, contrary to "retain remote control through URL teardown". It
reproduced 3/3 with a held request and broke 7 of 18 loaded MRP controller
runs. The user chose to fix it in PR #14. The feedback loop now skips the
remote request once stop has begun and does not cancel one already in flight;
its request deadline (5 s default) bounds the wait. A remote request that fails
at that deadline still closes its connection first; that is accepted for an
unresponsive receiver. New `url_playback_session_tests` cases cover a held
request (fails as `[remote,URL]` without the fix) and an unanswered one (stop
under 1.5 s with a 300 ms deadline, no failure reported). **This changes the
runtime used by both `airplay2-cli cast` and the C interface:** every recorded
hardware result predates it, so the next receiver run must use a build that
contains D47.

**D48 (public interface manual batch, 2026-10-08):** with the user at the TV,
the [manual plan](public-api.md#manual-validation-plan) ran on source head
`8a7050d` (D47 runtime) through `airplay2-api-host`, using a local driver that
kept the receiver address out of all output and restored staged executables with
hash checks. Checks 1-7 passed with observer confirmation: start/stop, controls,
full clip to media_end, cancel during start (a few frames, then Home), remote
Home button (connection_lost/exit 1, as known), receiver sleep then fresh cast
after wake, and the shared build. Ten cycles in one process passed 10/10. A
`cast --media-log` run with a user-provided 6.32 GB remuxed film passed a seek
past 4 GiB (37 requests at offsets of 4,490,723,328 bytes and above, picture at
about 1:20:00 with sound) and played E-AC-3 audio normally, without an Atmos
indication. One receiver, firmware and host only; unreachable-address, handle
counts and cable-pull checks were not run. Record:
[receiver-validation.md](receiver-validation.md#public-playback-interface-manual-batch-d48-2026-10-08)
and its [artifact](validation/native-api-host-manual-batch-windows-2026-10-08.json).

**D49 (common credential interface, 2026-10-08):** the user merged PR #14 and,
before the next slice, asked for the credential design to cover non-Windows
platforms and for the Windows facts to be researched. Research against the
installed SDK, Microsoft Learn and the built DLL found: Credential Manager is
desktop-only; `PasswordVault` is available to UWP but limited to 20 credentials
per app, can roam with the Microsoft account, stores strings and is keyed by
package identity; Screenbox is an AppContainer UWP app; and `send_airplay2.dll`
imports the desktop-only credential functions. The user approved recording the
resulting design: one `CredentialStore` seam with built-in adapters (Windows
desktop now; macOS Keychain and Linux Secret Service later) and a host-provided
store through the C interface (UWP `PasswordVault` from C#, Android Keystore
from Kotlin, headless Linux); profiles stay the key; D46's "secrets never cross
the ABI" holds for built-in stores only; pairing and profile removal join the C
interface; `PasswordVault` stays host-supplied; UWP builds exclude the Credential
Manager adapter; no plaintext fallback. The host-store contract is an
engineering proposal for the next slice. Details, sources and unverified items:
[credential-interface.md](credential-interface.md). Documentation only.

**D50 (host credential stores and pairing, 2026-10-08):** the user merged PR #15
and asked to proceed with D49 step 1. New public headers `credentials.h`
(`sap2_credential_store` callbacks, `SAP2_STORE_*` results, 203-byte record
bound) and `pairing.h` (`sap2_pair`, a size-aware `sap2_pair_options_init`,
`sap2_forget_profile`); `sap2_cast_options.credential_store` as a new last field,
with version 1 structs still accepted; results `PROFILE_EXISTS` and
`PIN_TIMEOUT`; `SAP2_PLAYBACK_API_VERSION` 2. `sap2_pair` drives the existing
`run_auth_workflow` through a host-store adapter and a PIN-callback prompt
(review follow-up: `sap2_cast_options_init_sized()` added and the version 1
initializer limited to the version 1 fields, so an older host's struct is
never overrun; callbacks must not re-enter the library), so
the CLI's save/reload/verify order and refusals are reused, not reimplemented.
Engineering choices: a synchronous `read_pin` callback whose deadline is checked
on return; no pairing cancellation besides `read_pin`; the shared result mapping
now covers pair-setup failures, and `already_exists` maps to `PROFILE_EXISTS`.
New tests `c_credentials_smoke` and `host_credential_tests`; Windows
static/shared Release 29/29 CTest each.

**D51 (pairing through the public interface, 2026-10-08):** the user asked to
proceed with a real pairing and chose hidden console PIN entry, the built-in
store followed by a cast, and the profile `living-room-api`.
`airplay2-api-host` gained `--pair` (hidden `read_pin` through CONIN$/CONOUT$,
written against the public contract). `sap2_pair()` returned ok at source head
`b87e1ae` (authenticated enrollment, save, reload, fresh-connection
verification), and a 20 s cast with the new profile started in 1.8 s and stopped
cleanly; the user confirmed the PIN prompt and normal video/audio/Home. One new
pairing entry now exists on the Apple TV. Not run on hardware: a host-store
pairing, a wrong or cancelled PIN, profile removal. Record:
[receiver-validation.md](receiver-validation.md#pairing-through-the-public-interface-d51-2026-10-08).

**D52 (C# binding, 2026-10-08):** the user merged PR #16 and approved splitting
D49 step 2 into PR A (binding, no TV) and PR B (packaged UWP test app). The
binding (`bindings/csharp/SendAirPlay2`, `netstandard2.0`, no package
dependencies) wraps casts in a `SafeHandle` that owns the callback delegates
until `sap2_cast_destroy` returns, releases media sources exactly once (also on a
failed create), contains every callback exception, clears its temporary record
and PIN arrays, takes the PIN as a `char[]`, and refuses a library older than API
version 2. Engineering choices: abstract `MediaSource` with a managed read
buffer per thread; `ICredentialStore` over byte arrays; `PinReader` delegate. A
`net8.0` runner (`csharp_binding_tests`, CTest on shared builds when dotnet is
found) checks layout through the native initializers, ownership, delegate
lifetime under forced collection and finalization, store results and pairing
refusals; removing the keep-alive fails it. Windows static 29/29 and shared
30/30. No receiver use through C# yet. Build-time .NET reference packages are
recorded in dependencies.md.

**D53 (packaged UWP host, 2026-10-08):** the user merged PR #17 and asked for PR
B; for the Public network question they chose to measure both capability sets,
and later switched the network to Private themselves for a third run.
`tools/uwp-host` matches Screenbox's toolchain settings (`UseUwp`,
`net10.0-windows10.0.26100.0`, Native AOT, `DisableRuntimeMarshalling`, MSIX,
CsWinRT 2.3.1) and holds a C# `PasswordVault` host store, a `StorageFile`
source, a PIN dialog and a fixed-field log. Results on the recorded Apple TV: the
AOT app loads the native DLLs; Credential Manager fails inside the AppContainer;
pairing into PasswordVault passes and survives in-place updates; with Screenbox's
`privateNetworkClientServer`, casting fails on a Public network (inbound blocked)
and passes on a Private one, including controls and Home after Stop;
`internetClientServer` passes on Public. **Open decision for the Screenbox fork:**
whether to add `internetClientServer` for Public networks. Findings for D49 step
3: the native DLLs depend on the desktop C runtime. Not run: roaming, desktop
provisioning of the locker, Store certification, x86/ARM64. Record:
[receiver-validation.md](receiver-validation.md#packaged-uwp-host-d53-2026-10-08).

**D54 (C discovery interface, 2026-10-08):** the user merged PR #18 and asked to
proceed with the C discovery API. `receivers.h` adds `sap2_discover` (one
blocking bounded scan, 1..60000 ms, default 5000) returning an opaque owning
`sap2_receiver_list`, read with `sap2_receiver_list_count`/`_get` (caller-set
`struct_size`) and freed with `sap2_receiver_list_free`. Engineering choices:
only devices with an AirPlay video service; one castable address per receiver
(IPv4 first, else non-link-local, unscoped IPv6; empty when neither); names as
length-delimited bytes; advertised data labelled unauthenticated; playback API
version unchanged (new symbols only). `castable_receivers()` is a pure
function; `receiver_list_tests` runs the C boundary against a scripted
`discover()`. C# `Receivers.Discover(TimeSpan)` copies the list. Windows static
31/31 and shared 32/32. Receiver evidence: `airplay2-api-host --discover`
(static and shared) and the UWP host's Discover button (0.1.4.0, Screenbox's
capabilities, Private network) each found "Living Room" with an IPv4 address;
the user saw the app's address box fill. Not run: Public networks, a cast from
the discovered address inside the app, departure/interface changes, several
receivers, other platforms. Record:
[receiver-validation.md](receiver-validation.md#c-discovery-interface-d54-2026-10-08).
*Corrected by D55:* the app's PasswordVault was not empty; the D53 credential
had survived.

**D55 (UWP native build, 2026-10-08):** the user merged PR #19 and asked to
proceed with the UWP native build (D49 step 3); for the hardware run they chose
the full procedure. Engineering choices: CMake `WindowsStore` with vcpkg's
community `x64-uwp` triplet and a separate installed directory; library only
(tools and tests skipped); a `vcpkg-overlays/ports/botan` overlay (vcpkg's port
plus, for UWP only, `--os=uwp`, a minimized module set and static linkage,
because Botan's uwp target has no shared libraries); `BOTAN_DLL=` for static
Botan on MSVC; Credential Manager compiled out by the `WINAPI_PARTITION_DESKTOP`
check, so the built-in store reports unsupported; `BOOST_BEAST_USE_WIN32_FILE=0`;
`libcrypto` staged beside the library; the UWP host packages `build-uwp\Release`
by default. Static checks: AppContainer flag, `VCRUNTIME140_APP`, no `ADVAPI32`
or Botan DLL (`scripts/check_uwp_binaries.ps1`, also a new CI job). **Found on
hardware:** every Stop aborted the app. A `SIGABRT` stack trace added to the
host resolved it to `asio::thread_pool::join`: Boost 1.92's app-mode
`winapp_thread::join()` leaves the thread joinable and its destructor calls
`std::terminate`. The media server now uses its own `std::thread` reader pool.
After the fix, casts with pause/play/seeks, repeated Stops in one process and a
remote-Home end all passed, and the host now frees the slot of a cast that ended
by itself. Windows static 31/31 and shared 32/32; the desktop suite also passed
with Asio's select reactor forced (32/32). Not run: WACK (needs elevation),
x86/ARM64, a device without VCLibs, Public networks with this build, pairing
through it. Record:
[receiver-validation.md](receiver-validation.md#uwp-native-build-d55-2026-10-08).

**D57 (UWP architectures and certification kit, 2026-10-08):** the user merged
PR #21 and asked to finish the standalone items except remote-Stop detection,
which is deferred. Engineering choices: CI builds the UWP library for x64, x86
and ARM64 (matrix) and the check script verifies each binary's architecture;
the UWP host builds for the three platforms, packaging `build-uwp`,
`build-uwp-x86` or `build-uwp-arm64`, and takes OpenSSL's DLL by pattern. This
Visual Studio has no ARM64 tools (installing them is the user's choice), so
ARM64 is CI-only. The x86 host 0.1.14.0 loaded the x86 library and app runtime
and passed discover, cast, controls and Stop on the TV (user: everything worked
as expected). The user ran the certification kit on the x64 host 0.1.12.0: 26 of
27 tests pass; Supported APIs fails for six kernel32 calls in the host
executable's .NET Native AOT runtime, none in the library. Record:
[receiver-validation.md](receiver-validation.md#uwp-architectures-and-certification-kit-d57-2026-10-08).

**D58 (macOS Keychain built-in store, 2026-10-08):** part of the standalone
items. Research (Apple TN3137 and the SecItem reference): the SecItem API
targets the file-based keychain by default; the data protection keychain needs
keychain-access-group entitlements from a provisioning profile, and library
code inherits the host's. Engineering choices: generic password items in the
login keychain (service `send-airplay2/v1`, account = profile), create-only
through `SecItemAdd`'s duplicate refusal, every other status unavailable;
signed hosts that want the data protection keychain pass their own store. The
native-store tests now run on macOS too and passed on CI's `macos-26-arm64`
image, static and shared. Not run: a developer Mac, rebuilt-binary prompts,
Intel Macs. Record:
[credential-storage.md](credential-storage.md#macos-login-keychain-d58).

**D59 (Linux Secret Service built-in store, 2026-10-08):** part of the
standalone items. Research (libsecret reference): the synchronous binary API
stores and looks up `SecretValue` secrets; storing **replaces** a matching item;
errors come back as `GError`. Engineering choices: optional system libsecret
(>= 0.19, pkg-config, Linux targets only, `SAP2_SECRET_SERVICE`), one item per
profile under schema `org.send-airplay2.Credential` (attributes `namespace`,
`profile`) in the default collection, a per-user `flock` in `$XDG_RUNTIME_DIR`
around check-then-store and erase, every error unavailable; tests skip without a
Secret Service unless `SAP2_REQUIRE_SECRET_SERVICE=1`. CI's Ubuntu jobs run them
against a throwaway GNOME Keyring (`tests/with_test_keyring.sh`); they passed at
`4176b7d`, static and shared, after a lock-path fix. libsecret is
LGPL-2.1-or-later (dependencies.md). Not run: a Linux desktop, a locked keyring,
other Secret Service providers. Record:
[credential-storage.md](credential-storage.md#linux-secret-service-d59).

**D56 (casting to a sleeping receiver, 2026-10-08):** the user asked whether a
cast can wake the receiver remotely and chose the suggested order. Reproduced:
the sleeping Apple TV accepted the session, reported `playing`, then paused or
stopped within about 1.6 s as tvOS returned to Home (user: first frame, then
Home). pyatv (`postlund/pyatv@b277a4c`) wakes over MRP with `WAKE_DEVICE_MESSAGE`
(type 41) and reads power from `DeviceInfoMessage.logicalDeviceCount` (field
22). Engineering choices: record that count from the handshake and from
`DEVICE_INFO_UPDATE` (type 37) as a startup diagnostic; when the handshake
reports 0, send `WAKE_DEVICE` and wait until the count is at least 1 and
unchanged for 2.5 s, at most 10 s and outside `start_timeout`, then start as
before; best effort, default on, not a host option (`UrlPlaybackOptions` only).
On hardware the count went 0, 1, 0, 1 within about 2.5 s of the wake; playback
started 2.5 s after the last change and played normally (user observed), and an
awake receiver is not delayed. Second and third sleep cycles through the C
interface (`airplay2-api-host`, static and shared) also passed (user: everything
worked as expected). Windows static 31/31 and shared 32/32. Not run: more sleep
cycles, the UWP host on a sleeping receiver. After the run the user reported that the intermittent startup pause seen
since D38 was observed when the Apple TV was waking from sleep, which makes D56
its likely cause and fix; the earlier runs were not repeated. Record:
[receiver-validation.md](receiver-validation.md#casting-to-a-sleeping-receiver-d56-2026-10-08).

**D60 (HLS delivery, 2026-10-09):** the user asked to add HLS support to the
base library and chose "serve plus built-in remux": serve HLS presentations,
and build them from local files with natively decodable codecs (MKV in
particular) by remuxing to fMP4 without transcoding. Research: Apple's HLS
authoring specification (HEVC only in fMP4, `avc1`/`hvc1`, IDR-started
segments, 6 s targets, `EXT-X-PLAYLIST-TYPE`, `EXT-X-MAP`, no redirects).
Engineering choices: fixed resource sets in `MediaServer` below the private
bearer path; fMP4 only; VOD first, with segment sizes computed from sample
tables so segments are generated on demand; original remux code from public
specifications, no new dependency; muxed audio/video first; gzip playlists
and TLS deliberately not used on the local network (to verify on the
receiver). Phase 1 passed on the recorded receiver: an ffmpeg-made fMP4 VOD
presentation played, paused, seeked and ended with MRP ownership through
`cast --hls` (user observed). Its natural end stopped 0.08-0.10 s short of
the playlist duration, so a URL stop within 0.5 s of the MRP duration now ends
as `media_end` (`near_end`); a pause alone never ends a session. Windows static
and shared suites pass. Not run: HEVC, Dolby audio, separate audio, `EVENT`,
the C interface. Records: [hls.md](hls.md),
[receiver-validation.md](receiver-validation.md#hls-phase-1-d60-2026-10-09).
Phase 2 (2026-10-09): original MP4 demux, fMP4 writer and segmenter (sample
entries and edit lists copied from the source; cuts on a 6 s grid of video
sync samples; segments generated on demand). Its remux of the test clip was
packet-identical to the source under ffmpeg and played, paused, seeked and
ended normally on the recorded receiver (user observed). Engineering choice:
an unremuxable audio track refuses the file rather than casting it silent.
Record: [receiver-validation.md](receiver-validation.md#hls-phase-2-d60-2026-10-09).
Phase 3a (2026-10-09): original Matroska demux and sample entries from codec
data. Engineering choices:
- Decode times are the sorted presentation times, with a reorder-delay edit
  list.
- The audio timeline is continuous, following block times only beyond
  rounding.
- CodecDelay becomes an audio edit.
- A default-flagged remuxable audio track is preferred.
- E-AC-3 with dependent substreams is refused.

Three ffmpeg-made MKVs and a user-provided 6.3 GB film were payload-identical
under ffmpeg and played, paused, seeked and ended normally on the recorded
receiver (user observed). Known cost: the startup scan reads every block
header (2.8 GB and 4.9 s cold for the film).
Record: [receiver-validation.md](receiver-validation.md#hls-phase-3a-mkv-d60-2026-10-09).

**D61 (start position as a seek; UWP host script mode, 2026-10-10):** a
nonzero `start_position_seconds` had never been tried on the receiver. Through
the UWP test host, tvOS 26.6 played from 0 for HLS and progressive media alike,
although it accepted the queue item's `Start-Position-Seconds` (the key pyatv
sends too). Engineering choice: keep sending the key and, when the start is
above zero, wait up to 5 s for MRP ownership and send one seek before
`UrlPlaybackSession::start` returns; ownership not arriving fails the start as
`not_owned`, so a host never gets a cast silently playing from the wrong place.
Tested: `cast_controller_tests` ("Start position") and, on the recorded Apple
TV, the MKV film at 1:00:00 and the MP4 at 5:00, 9:40 (to its end) and 2:00
(progressive), user observed. The test host also gained a script mode (an
`sap2-uwp-host` app execution alias running `LocalState\scripts`), so TV runs
need the user only to watch. User decision in the same review: Screenbox casts
every file through `CastDelivery.HlsRemux` (option B), not progressive MP4.
Record: [receiver-validation.md](receiver-validation.md#uwp-host-hls-remux-and-start-position-d61-2026-10-10).

**D62 (URL controls for receivers that refuse remote control, 2026-10-10):**
the user asked whether video can be cast to a Mac as to the Apple TV. Found on
a MacBook Pro (`Mac14,10`, AirPlay 960.13.25): pairing works after the user
accepts an on-screen request (the Mac then shows a PIN; unanswered, it answers
400 after about 15 s, so the CLI needs `--timeout-ms 60000`); the
remote-control SETUP is rejected with 500 although its pair-verify succeeds;
the URL session alone plays and reports state, position and duration on its
event channel. User decision: an automatic URL-only fallback rather than an
explicit host option. Engineering choices:
- Any non-2xx remote-control SETUP closes that session and starts with URL
  controls (`UrlPlaybackOptions::url_controls_fallback`, default on;
  `SessionStatus::url_controls` reports it). No wake, no ownership-loss
  detection without MRP.
- Controls are URL `/command`s: pause/play `setRate` 0/1 (H1, now confirmed
  on the Mac), `seek` as a request naming the queued item's UUID with a CMTime,
  `stop`. Wire names follow what a third-party receiver (DiPlay, GPL-3.0)
  documents an Apple sender sending, and the Mac receiver's own strings;
  names only, no code ([dependencies.md](dependencies.md)). Failures keep the
  MRP command categories, so the C interface is unchanged.
- Progress comes from URL events (`UrlPlaybackTracker`): the last reported
  position, extrapolated by rate while playing; at/near end use reported
  positions only, with the MRP tracker's 0.5 s tolerance.
- With URL controls the base SETUP may wait `consent_timeout` (30 s) for the
  on-screen consent.

Tested: unit/fake-receiver tests (fallback start, commands, progress, natural
end, start-position seek, opt-out); on the Mac, start, status, pause/resume,
natural end and stop passed. Seek first failed in every form (the `/command`
seek with and without request fields, legacy `/scrub`,
`Start-Position-Seconds`: accepted and ignored). The Mac's log and the
receiver strings the user extracted (`APRKMediaPlayer`: "Sender seek to time
is %f for item %@") showed the seek must name the item; with `item: {uuid}`,
seeks and the start position landed exactly (Mac log and user observed).
The Mac's consent is asked "for client" by name with a grant period, so D30's
random device ID is probably not why it prompts again. Open: the C interface,
UWP host and Screenbox on the Mac.
Record: [receiver-validation.md](receiver-validation.md#macos-airplay-receiver-url-controls-d62-2026-10-10).

**D63 (Mac API/UWP validation and explicit HLS EOF, 2026-10-11):** user scope
is the next Screenbox-on-Mac step. Hardware exposed an HLS natural-end
classification failure in both native C API and packaged UWP, despite normal
playback. Engineering fix: normalize the observed stopped event's exact root
`reason: ended` to the existing terminal state. Unknown reasons and
progress/extrapolation rules retain their existing behavior; this does not
reopen deferred receiver-remote Stop classification. Corrected shared C API
and UWP checks passed, with the observer confirming normal playback/controls
and natural end. The user reports the private x64 Screenbox UI checks passed;
its loaded DLL was not independently captured. A main-branch prerelease and
final fork pins/validation are still pending. See the
[D63 record](receiver-validation.md#mac-c-api-packaged-uwp-and-hls-eof-d63-2026-10-11).

## 5. Implemented code and verification

The table and section 0 summarize current components. Dated subsections preserve
the exact evidence at each historical slice; their future-work notes are not the
current task list. Current next steps are in section 7.

| File | Purpose |
|---|---|
| `include/send_airplay2/http_range.h` | Experimental length-delimited C interface, fixed-width status values, offset/length result |
| `src/http_range.cpp` | Allocation-free single byte-range resolver, overflow checks and empty-file handling |
| `tests/range_tests.cpp` | Boundary/argument/buffer cases and generated interval tests |
| `tests/c_abi_smoke.c` | Compile/link/use from a C11 caller |
| `CMakeLists.txt` | C++17 static/shared library and CTest targets |
| `.github/workflows/build.yml` | Windows/Linux/macOS static/shared build-and-test matrix |
| `docs/design.md` | Architecture, range contract, integration audit and implementation gates |
| `docs/CONTINUATION.md` | Focused separate-session restart checkpoint, review disposition, evidence, commands and ordered development queue |
| `docs/receiver-validation.md` | Per-platform/per-firmware hardware record; discovery, PIN enrollment and fresh-socket verification observed; remaining gates explicit |
| `include/send_airplay2/discovery.h` | Experimental synchronous C++ discovery records/options/result API |
| `src/discovery*.cpp` / internal headers | Bounded DNS-SD parser/cache, scan scheduler and native socket adapter |
| `src/cli.cpp` | Dispatch for discover, auth, serve and cast; readable/JSON discovery diagnostics |
| `tests/discovery_tests.cpp` / CLI fixtures | Synthetic parser/lifecycle/query tests, mutation corpus and CLI output validation |
| `docs/discovery.md` | Discovery contract, adapter limits, source provenance and local LAN observations |
| `src/pairing_tlv.*` | Bounded ordered TLV8 codec with strict fragment/separator handling |
| `src/control_crypto.*` / `src/control_records.*` | OpenSSL AEAD/HKDF, incremental authenticated records, directional counters and terminal failure |
| `tests/control_tests.cpp` / `tests/fixtures` | Independent synthetic wire/key fixtures and malformed/replay/fragmentation tests |
| `docs/pairing-transport.md` / `docs/dependencies.md` / `vcpkg.json` | Transport contracts, gates, dependency/license provenance and pinned package manifest |
| `src/pair_setup_crypto.*` / `src/pair_setup.*` | Private Botan SRP/HAP proof adapter and authenticated persistent PIN setup state machine |
| `tests/pair_setup_tests.cpp` / `tests/fixtures/pair-setup` | Independent SRP/message fixtures, rejection/cleanup cases and enrollment-to-verification round trip |
| `docs/pin-pairing.md` | Provisioning trust, bounds, ownership, dependency and remaining I/O/storage gates |
| `src/identity_crypto.*` / `src/pair_verify.*` | OpenSSL X25519/Ed25519, secret ownership and bounded existing-credential verification state machine |
| `tests/pair_verify_tests.cpp` / `tests/fixtures/pair-verify` / `docs/peer-verification.md` | RFC vectors, independent synthetic transcripts, failure/cleanup/record-handoff tests and trust contracts |
| `src/receiver_http.*` / `src/receiver_stream.*` / `src/receiver_connection.*` | Private bounded framing, native TCP ownership/deadlines/cancellation and authentication-to-record integration |
| `tests/receiver_tests.cpp` / `docs/receiver-transport.md` | Fragmented fake receiver transcripts, dynamic authenticated peers, native loopback and lifecycle/framing contracts |
| `src/credential_*`, `src/auth_*` / `tests/credential_tests.cpp` / `docs/credential-storage.md` | Private bounded credential codec, native Windows store, hidden-PIN CLI, enrollment/save/reload/reconnect orchestration and synthetic/OS persistence tests |
| `src/file_media_source.*`, `src/serve_cli.*` / `tests/file_source_tests.cpp`, `tests/cli_serve.cmake` | Private file-backed `MediaSource` and development `serve` command; adapter, loopback and real-CLI tests |
| `src/binary_plist.*` / `tests/plist_tests.cpp`, `tests/fixtures/plist` | Private bounded `bplist00` subset codec (D27); plistlib byte-exact fixtures, literal layouts, malformed/budget cases and mutation sweeps. Integrated into private session code and compiled into its fixture test |
| `src/channel_keys.*`, `src/event_channel.*`, `src/ntp_timing.*` | Private session keys, receiver event requests and UDP timing, with fixture/stream/loopback tests |
| `src/session_messages.*`, `src/url_playback_session.*`, `src/cast_cli.*` | Private URL start, state, local teardown and CLI; scripted receiver tests; native-only G1 PASS; MRP controls implemented, with separate G2 record; URL controls fallback (D62) |
| `src/url_playback_tracker.*` | URL-event progress (state, extrapolated position, at/near end) for URL controls (D62) |

The range resolver handles closed, open-ended and suffix ranges for a known
64-bit representation size. It consumes an HTTP field **value**, not a complete
request; it performs no I/O. It does not implement HTTP serving, GET/HEAD parsing,
If-Range, conditional requests, authentication, URLs or session control.
See the header and design.md for status-to-response mapping and lifetime rules.

Verified at foundation commit `dbd654b1d92057b3208953226186c5c2b206ccff`:

- Local Linux GCC 13.3 static and shared CMake builds: both CTest targets pass.
- 25 boundary cases, four argument/buffer checks and 26,240 generated range cases:
  pass. These verify interval logic, not a 4-GiB file transfer or receiver seeking.
- C11 compilation and linking against C++: pass.
- AddressSanitizer and UndefinedBehaviorSanitizer: pass with
  `ASAN_OPTIONS=detect_leaks=0`. Default LeakSanitizer failed to inspect processes
  in the execution environment; leak detection was not validated.
- GitHub CI: all six Windows/Linux/macOS x static/shared jobs pass, confirmed
  2026-10-06. [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37409502023)
  and [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37409476906).
- Android NDK build/device, packaged Windows host and receiver tests: not run.

Discovery slice validation on 2026-10-06:

- Windows 11 x64, Visual Studio 2026 / MSVC 19.51: static and shared Release
  CMake builds, all six CTest targets pass. Discovery tests cover malformed and
  compressed DNS, binary/duplicate TXT, feature masks, resolution, identity
  merging, interface isolation, updates, TTL, goodbye/flush grace and cache caps;
  10,000 deterministic parser mutations also run. CLI JSON tests verify binary
  preservation, 64-bit mask formatting and escaped terminal controls.
- A 10-second LAN scan resolved `Mac14,2` AirPlay/RAOP services; a fresh
  15-second scan resolved the user's "Living Room" `AppleTV14,1`, advertised
  OS 26.6, AirPlay/RAOP port 7000, IPv4 plus scoped IPv6. Four responses, none
  rejected, no warnings. Details are sanitized in receiver-validation.md.
- Real receiver departure/interface changes, exact tvOS build, pyatv reference
  playback, pairing, native playback and packaged/Android host validation remain
  pending. Unit success and discovery do not certify playback.
- Discovery CI at implementation commit `48189897ba167b44c3da7c6e4a7857bf28120498`:
  all six Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan passed,
  verified 2026-10-06. [Push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37419114646)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37419131062).
  Commits through `f9b9002` updated documentation only; later readability changes
  require their own checks. Inspect checks for the actual PR
  head before merging. Linux/macOS receiver interoperability remains untested.

Readability pass on PR #2, 2026-10-06:

- Added a repository `.clang-format` convention, expanded control flow and clear
  DNS constants/names, separated query scheduling, service resolution and JSON
  formatting, and organized cache tests by scenario.
- Public API and internal comments describe buffer ownership, byte order,
  interface scope, monotonic time, compression traversal, cache grace, metadata
  merging and binary/UTF-8 output. Socket and Winsock resource owners explicitly
  forbid copying; cleanup order and failure unwinding are documented.
- Windows static/shared Release builds passed all six CTest targets, including
  new valid/invalid UTF-8 cases. The pre-existing synthetic fixture JSON was
  byte-for-byte unchanged before adding the new test fields. The test harness
  now decodes CLI output explicitly as UTF-8 on Windows.
- Protocol behavior and JSON schema remain the same. Actual pairing/playback
  validation is still pending; verify CI for the readability commit separately.
- A post-refactor 10-second Windows LAN scan again resolved "Living Room"
  (`AppleTV14,1`) and `Mac14,2`, each with both services: four responses, zero
  rejected packets and no warnings. This remains discovery-only evidence.
- At readability commit `23f8ff08a12431fc0aba95a21aaf881aa7458674`, all six
  Windows/Linux/macOS static/shared CI jobs and Linux ASan/UBSan passed:
  [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37420813282)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37420816897).

Pairing/control foundation, 2026-10-06:

- Started from verified merge of PR #2 at `02c953a22c9503f1bf17bc14d6814e797cb04a1b`
  on `codex/pairing-transport`. The public API still cannot pair or cast.
- Implemented bounded private TLV8, OpenSSL ChaCha20-Poly1305/HKDF-SHA512 and
  directional record reader/writer. Authentication/length/backend errors close
  the direction; keys and transient failed plaintext are wiped. Counter wrap and
  resource copying are forbidden. Socket ownership/deadlines and peer verification
  are explicitly pending; see pairing-transport.md.
- Windows static/shared Release builds passed all seven CTest targets using OpenSSL
  3.6.5 from the pinned vcpkg manifest, MSVC 19.51 / Visual Studio 2026.
  At implementation commit `ee4afa80172d38300078fad0b5a2332898e95cd5`, all six
  Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan passed in the
  [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37422659514).
  Inspect checks for the final PR head before merging. Fixtures include RFC AEAD bytes,
  Python-generated control framing/HKDF, every two-part split, byte-at-a-time reads,
  corruption/replay/EOF/nonce limits and 3,000 TLV parser mutations.
- No receiver handshake, credential change, PIN or playback operation was attempted.
  Wrong PIN/revocation/signature tests remain gates of the next handshake slice.

Readability pass on PR #3, 2026-10-06:

- Named TLV fragment/header sizes and the HKDF info limit, clarified encoded-size
  calculations, renamed the pure nonce constructor to `nonce_for_counter`, and
  made the plaintext cleanup guard explicitly non-copyable/non-movable. Touched
  sources include the standard headers for the facilities they use directly.
- Split control tests into named AEAD/HKDF, framing/size, authentication/replay,
  EOF/counter and TLV scenarios. Failure diagnostics identify the group/case and
  relevant split, mutation or byte offset. Named final-fragment layout calculations
  replace unexplained tail offsets. Existing fixture files and boundary expectations
  are unchanged; all 3,000 TLV mutations and existing record split cases remain.
- Added project-wide C++ readability requirements to AGENTS.md so subsequent
  implementation/test changes apply the same standards before PR completion.
- Windows static/shared Release builds passed all seven CTest targets; clang-format
  dry-run and `git diff --check` passed. An intentional missing-fixture invocation
  confirmed the diagnostic includes the active scenario and fixture filename.
- At final readability source commit `85e2b8d48a4bf993ba4581f539f1103ed601ba4a`,
  all six Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan passed in
  the [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37425244505)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37425248739).
  Inspect checks for the final PR head, including documentation-only updates.
- Protocol behavior, public API and receiver validation status remain unchanged.
  No new hardware pairing/playback result is claimed.

Peer verification, 2026-10-06:

- Verified PR #3 merged at `360d74661e7cc703a82b7852f425215189217ad3`; started
  `codex/peer-verification` from that main commit. Existing-credential verification
  is the next bounded slice before first-time PIN enrollment, not a public pairing API.
- Added EVP X25519/Ed25519 and erasing secret/credential owners. Receiver identity
  is pinned to trusted credentials; no discovery-derived trust or TOFU. M2 must
  authenticate before the sender signs M3. HTTP 200/state M4/no error is required
  before deriving/releasing write/read keys once. Failures permanently close and
  clear owned handshake/control keys. See peer-verification.md for complete limits.
- Windows 11 x64 / MSVC 19.51 static/shared Release builds pass all eight CTest
  targets. Added RFC 7748/8032 vectors, independent Python wire/HMAC transcript
  fixtures, identity/signature/schema/replay/rejection cases, directional record
  handoff at every split, phase cancellation and 1,000 deterministic M2 mutations.
  Fixture regeneration reproduced committed bytes; clang-format dry-run and
  `git diff --check` passed. Existing control fixtures and Apache-2.0 LICENSE are unchanged.
- At implementation commit `7b572a7b24d7242200e0cb1321c366a83932b3da`, all six
  Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan passed in the
  [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37427502668)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37427549046).
  Inspect final PR #4 head checks, including documentation-only commits, before merging.
- Continued the AGENTS.md readability requirements: named scenario groups,
  diagnostic context, explicit non-copyable ownership and Doxygen contracts.
  OpenSSL remains the only crypto dependency; no third-party source was copied.
- No receiver I/O, PIN entry, credential change or playback operation was attempted.
  Wrong PIN, actual revocation/reconnect, firmware interoperability and packaged/
  Android loading remain untested. Next: vetted PIN/SRP provisioning with verified
  server proofs/signatures, trusted credential storage, bounded HTTP/socket I/O.

PIN pairing, 2026-10-06:

- Verified PR #4 merged at `54d63b81d5b9972c12418164aac2076fda1d1ff7`; started
  `codex/pin-pairing` from main. Added private persistent HAP setup using Botan
  SRP-6a (3072-bit/g=5/SHA512), with OpenSSL transcript/HKDF/AEAD/Ed25519.
  No third-party implementation source was copied; dependency provenance and
  BSD-2-Clause notices are recorded in dependencies.md. Botan builds as C++20;
  its C FFI preserves the core's C++17 requirement.
- M4 must authenticate before M5 is emitted. M6 AEAD and accessory signature
  must authenticate before credentials are returned once. Errors/cancellation
  close and erase owned secrets; no discovery trust or plaintext/transient fallback.
  See pin-pairing.md for the bounded profile and caller/transport/storage contracts.
- Native regression tests demonstrated that a validly encrypted M6 with an
  identity public key and a trivial forged signature could otherwise be accepted.
  The shared Ed25519 verifier now enforces canonical encoding, rejects identity
  aliases and uses Botan to check curve/prime-order subgroup membership before
  signature verification. This protects both enrollment and later peer verification.
  Regression fixtures and numeric weak-key cases fail before the fix and pass after;
  no curve arithmetic was copied or implemented.
- Independent Python integer/SHA512/HMAC fixtures cover a leading-zero shared
  integer, exact sender bytes, wrong PIN, every server-proof/ciphertext byte,
  validly encrypted signature/identity/schema failures, bounds, phase cancellation,
  single-use release and erasure. Enrollment credentials complete the existing
  peer-verification oracle and derive the expected control keys.
- Windows 11 x64 / MSVC 19.51 static/shared Release builds pass all nine CTest
  targets with OpenSSL 3.6.5 and Botan 3.12.0. clang-format dry-run and
  `git diff --check` pass; fixture regeneration is reproducible.
- At hardened source commit `4c7dcc4f6af6f5ebc391097dcc0998262436f761`, all six
  Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan passed in both the
  [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37436323914)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37436328684).
  Inspect checks for the actual final PR #5 head, including documentation commits.
- The full hardened Windows Debug pair-setup suite also passes (94.70 seconds).
  Its CTest limit is five minutes to accommodate repeated 3072-bit SRP with
  unoptimized backends; corruption coverage is retained. Release suite time
  is about three seconds. GCC/Clang warnings-as-errors exposed a test-loop string
  copy, fixed with a const reference; final-head CI must be rechecked.
- No receiver connection, PIN-display request, actual PIN entry, credential save
  or playback operation was attempted. Hardware authentication remains NOT RUN.
  Next: bounded HTTP/socket I/O and trusted host credential storage, followed by
  real PIN/reconnect/revocation validation. vcpkg Botan excludes UWP; packaged
  Windows/Screenbox and Android loading require separate proof.

Reproduction from a fresh checkout (requires OpenSSL 3.5+ and Botan 3.12+; see README for the
pinned vcpkg build when the host package is unavailable):

```sh
git clone https://github.com/ilyalissoboi/send-airplay2.git
cd send-airplay2
git switch codex/pin-pairing
cmake -S . -B build-static -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-static --config Release
ctest --test-dir build-static -C Release --output-on-failure
cmake -S . -B build-shared -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-shared --config Release
ctest --test-dir build-shared -C Release --output-on-failure
```

For Linux sanitizer reproduction:

```sh
mkdir -p build-sanitized
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -Iinclude src/http_range.cpp tests/range_tests.cpp -o build-sanitized/range_tests
ASAN_OPTIONS=detect_leaks=0 ./build-sanitized/range_tests
```

Disable leak detection only where that process-inspection limitation exists.

Receiver-transport slice on `codex/receiver-transport`, 2026-10-06:

- Verified PR #5 merged at `24bb2b86a1a725ea82a4a32d5a33fd2c22ef7e9d`
  and created this branch from that exact main head. No new dependency or copied
  implementation source; RFC/pyatv blob and license provenance is in
  [receiver-transport.md](receiver-transport.md).
- Private bounded HTTP/RTSP request encoding/response parsing and native numeric
  IPv4/IPv6 TCP now integrate PIN provisioning, pinned peer verification and the
  encrypted-record transition. One outstanding request, no retries/fallback,
  absolute deadlines, <=20 ms cancellation polling and terminal socket/key cleanup.
  HTTP correlation uses ordering and checks CSeq if present; RTSP requires CSeq.
  This is a narrow receiver profile, not a general HTTP implementation.
- Windows 11 x64 / MSVC 19.51 static/shared Release: all 10 CTests pass. The new
  receiver suite exercises independent PIN transcripts, fresh-randomness synthetic
  verification, encrypted RTSP, fragmented/coalesced input, limits/injection,
  bad proof/signature/tag, replay, partial trailing records, deadline/cancellation,
  sequence exhaustion and cleanup. Real IPv4/IPv6 loopback checks native I/O,
  timeout/cancellation, EOF, refused connect and queued unsolicited input.
- Readability/ownership pass applies the AGENTS.md rules, including Doxygen
  contracts, named bounds, focused scenarios and RAII sockets/secrets/threads.
  clang-format and diff checks pass. The Windows Debug receiver suite also passes.
  IPv6 scope IDs stay in the socket address rather than HTTP Host. Final source
  is `979ef0829248203684939274eb3864b8241845cc`. At subsequent documentation head
  `3360550b1ab7307a6b8e2014438d6e26f221255b`, all 14 checks passed: six native
  Windows/Linux/macOS static/shared jobs and Linux ASan/UBSan in each of the
  [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37441690978)
  and [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37441695377).
  Earlier Windows checks were superseded during dependency configuration; green
  merged-main setup took about 35 minutes, and its saved cache let the final checks
  finish normally. Subsequent evidence updates are documentation only; still verify
  checks at the actual PR head before merging.
- No connection to the Apple TV, PIN display/entry, credential change/save or
  playback attempted. Receiver authentication/playback and Android/UWP packaged
  loading remain untested. Next: trusted credential serialization/host storage
  and CLI pairing/reconnect, followed by explicit hardware validation.

Credential-storage/CLI slice on `codex/credential-cli`, 2026-10-06:

- Verified PR #6 is merged at `8c4ce3e47a3fd6d2cf73ab4197892a9004803d99`
  and based the new branch on that commit. GitHub CLI remains available and
  authenticated; there were no open repository PRs before this slice.
- Implemented a private exact-length/version credential envelope (maximum 203
  bytes), strict Ed25519 pin validation, erasing blob owners, host store interface
  and current-user/same-computer Windows Credential Manager adapter. Profile
  names are bounded/lowercase; named user/profile mutexes serialize cooperating
  create/delete operations and refuse existing/malformed slots. No plaintext
  fallback or other-platform adapter is introduced; see decision D22 and
  [credential-storage.md](credential-storage.md).
- Added CLI `pair`, `verify` and `forget`. Hidden interactive PIN input is required
  before receiver enrollment; no PIN argument/stdin/environment route. Pair
  saves only authenticated M6 credentials, closes provisioning, releases/reloads
  the credential owner, and verifies on a fresh connection. A saved checkpoint
  distinguishes reconnect failure; saved credentials are retained for explicit
  `verify`, never automatically replaced. Forget is local-only and idempotent.
- Windows 11 x64 / MSVC 19.51 static/shared Release passes all 12 CTests (6.18 s /
  6.07 s). New codec/workflow/native-store scenarios test exact schema, truncation,
  weak keys, leading-zero PIN, cancellation, no premature/duplicate saves,
  fresh connections and retained storage after reconnect failures. Real isolated
  synthetic Windows store tests pass create/load/delete, cross-process reload,
  concurrent create refusal and malformed-slot protection. Real CLI tests reject
  PIN arguments without echo and redirected input before receiver contact.
  The independent PIN-to-peer-verification oracle now passes through the codec
  and checks exact transcript/control keys after reloading.
- At source commit `37f5e3fe90136be25d89ede9c150bcd0f582969b`, all 14 push/PR
  checks passed: Windows/Linux/macOS static/shared and Linux ASan/UBSan, in the
  [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37444942542)
  and [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37444929031).
  Native Windows synthetic storage/redirection tests ran in CI too; Linux/macOS
  exercise portable code plus the explicit unsupported-store path. Touched C++
  passes clang-format dry-run and `git diff --check`. Documentation updates
  retrigger CI; verify the actual final PR #7 head before merging.
- No new third-party source/runtime dependency: original Apache-2.0 code uses
  Windows OS APIs (`Advapi32`); OpenSSL/Botan remain the crypto dependencies.
  Readability/RAII contracts, formatting and applicable checks are part of this PR.
- Actual console PIN entry/echo restoration, Apple TV PIN display/enrollment,
  hardware restart reconnect, wrong PIN/revocation and playback remain NOT RUN.
  Next assess this authentication gate on Living Room / Apple TV 4K / tvOS 26.6
  with the exact firmware build/access settings, then add bounded GET/HEAD media
  serving with size/read-at callbacks, >4-GiB ranges and lifecycle/cancellation.
  OS storage adapters, packaged Windows/UWP and Android loading remain pending.

M6 compatibility slice on `codex/pairing-diagnostics`, 2026-10-06:

- PR #7 merged at `6e83badfc5146371ee0c886e3f75fba492f9ab61`; its actual final
  head `e9057069ab6ce0199868ae92f1972198bf9f1c9b` passed all 14 push/PR CI checks.
- User observed TV PIN display and entered the PIN at the CLI prompt. Initial
  enrollment failed in M6. Sanitized inner headers were
  `source=identity TLV=[1:36,3:32,10:64,17:159]`, confirming an extra type-17
  receiver metadata field rejected by the original strict schema. M4 server proof
  and M6 AEAD had passed; accessory signature had not yet been checked.
- An agent no-PIN/no-save probe independently observed HTTP 200 PIN display and
  M2 (409 bytes, state 2, salt 16 bytes, public value 384 bytes). No raw receiver
  identifiers, secrets, ciphertext or metadata contents were retained.
- The fix accepts only the bounded opaque metadata field, without parsing it or
  altering the signed identity transcript. RAII erases decrypted bytes and field
  copies on success/schema failure, including partial generic TLV decoding.
- Original synthetic fixtures cover absent/empty/159-byte/fragmented 256-byte
  metadata, 257-byte rejection, duplicates and invalid signatures. Successful
  variants retain the independent serialized-credential/peer-verification/control
  key oracle. Diagnostics tests cover all phases and bounded header-only output.
- Final Windows Release static/shared each passed all 12 CTest targets
  (6.59/6.39 s), including the empty-metadata boundary fixture. Touched C++ passes
  clang-format dry-run; `git diff --check` passes.
  At source commit `f61698fa930893c139efcb5f5d2b0a40a93d0cae`, all six platform/
  static/shared jobs and Linux ASan/UBSan passed in both the
  [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37450506346)
  and [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37450491552)
  (14 successful checks). Verify the actual final PR head after documentation
  updates. No new dependency or copied implementation; re-inspected MIT pyatv
  reference provenance is in dependencies.md.
- User reran the rebuilt static CLI and reported authenticated enrollment,
  credential save/reload and built-in fresh-socket peer verification with encrypted
  control transport established. Separate-process `verify` also passed with exit
  code 0. The pair exit code was not supplied; its reported success checkpoints
  are recorded in receiver-validation.md.
- Host/receiver restart, wrong PIN, revocation, physical console echo/mode
  restoration and playback remain untested. Earlier M6 failures saved no local
  profile, but receiver-side provisioning may already have occurred during M5;
  no receiver revocation was performed. Playback remains unimplemented.
- Assess the restart/revocation authentication gates, then implement bounded
  GET/HEAD media serving with size/read-at callbacks and >4-GiB/lifecycle tests
  while arranging reference playback. Public auth ABI, other stores and packaged
  Windows/UWP/Android proofs remain pending.

Noninteractive E2E runner slice on `codex/e2e-runner`, 2026-10-06:

- Verified PR #8 merge `f24ac4825a47b2b641caaac1d0f1c00dd1058c33` and started
  from `origin/main`. No C++ or authentication protocol behavior changed.
- `scripts/run_e2e.py` uses Python 3.10+ standard library as a development tool.
  It runs CLI processes with null stdin, bounded wall-clock/output capture and
  exact checkpoint/exit-code assertions. It writes exclusive new sanitized JSON
  reports with executable/runner fingerprints, case results and timings; no PIN,
  identities, IPs, profiles, TXT/public keys or raw child/exception data is stored.
- Default actions: two discovery snapshots, baseline/repeated verification,
  existing/missing-profile guards, redirected PIN refusal, absent-profile forget,
  refused/silent/disconnected loopback peers and live verification after each
  fault. Primary credentials remain intact. A distinct previously paired
  disposable profile can be explicitly selected for deletion/idempotence tests;
  no credential is copied and no receiver revocation occurs.
- Offline contracts exercise independent subprocess fixtures, real loopback
  faults, false success, absent-profile collision, deletion boundaries, process/
  output bounds, report redaction, exclusive file creation and partial reports.
  All 10 tests pass locally. CI adds Windows/Linux/macOS offline runner jobs;
  these jobs have no hardware or application-credential access.
- Live default static/shared runs each pass 16 checks; disposable deletion is
  skipped. Receiver remains Living Room / Apple TV 4K, advertised `AppleTV14,1`,
  user-reported tvOS 26.6. Windows reports build `10.0.26200`, AMD64. Artifacts and
  precise evidence distinctions are recorded in receiver-validation.md.
- Existing Release static/shared CTest suites each pass all 12 tests (6.22/6.34 s).
  Native binaries were already built from the unchanged merged C++ sources;
  `git diff --check` passes. At portable source commit
  `650a0440a72d5e263bcca3fa8e6aa623cd628a7f`, all 20 checks passed in the
  [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37454270222)
  and [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37454265231):
  Windows/Linux/macOS native static/shared, Linux ASan/UBSan, and the three
  offline runner jobs. Check the actual final PR head after documentation updates.
- CI caught a refusal-fixture portability issue: a bound/non-listening macOS
  socket produced a timeout. The runner now selects/releases a closed loopback
  port before connecting; unexpected results fail the case. Windows live reports
  were refreshed after this change; both default suites still pass all 16 checks.
- No third-party source or dependency added; Python is not a native runtime/build
  requirement. Generated caches are ignored. See e2e-runner.md for invocation,
  deadlines, skips, report schema and opt-in deletion behavior.
- Host/receiver reboot, wrong PIN, receiver revocation, physical-console cancel/
  mode restoration, real departure/interface changes, actual receiver network
  outages and live IPv6 remain separate gates. Loopback failure/recovery does not
  prove those or encrypted application exchange. Playback remains unimplemented.
- Next implement bounded GET/HEAD media serving with size/read-at callbacks and
  >4-GiB/lifecycle tests while completing the manual authentication/reference
  playback gates. Public auth ABI, other stores and packaged hosts remain pending.

### Bounded HTTP media server: 2026-10-06

- Continued from verified PR #9 merge on `codex/media-server`. Boost.Beast/Asio
  replace an in-tree HTTP implementation at the user's explicit request. Boost
  1.92.0 is pinned through the existing vcpkg baseline, with source/license
  provenance in dependencies.md before adoption; Apache-2.0 remains unchanged.
- Added experimental `MediaServer`/`MediaSource` C++ API, immutable 64-bit size
  snapshot/read-at callbacks, route-selected numeric bind, receiver-IP restriction,
  random bearer paths, GET/HEAD/ranges, fixed streaming buffers and absolute
  request deadlines. Network and source execution are separate and bounded;
  cancellation retains source admission slots until callbacks finish. Shutdown
  joins all callbacks, which must cooperate. See media-server.md for contracts.
- Independent literal HTTP loopback tests cover exact lengths/binary bodies,
  >4-GiB offsets, uint64 upper boundary, partial reads, request limits, IP/path
  guards, source failures/truncation/recovery, concurrent reads, silent headers,
  blocked source/writes and idempotent shutdown. IPv6 loopback is tested when the
  host supports it; this does not validate actual receiver IPv6 fetching.
- Windows 11 x64 / MSVC Release static/shared builds each passed all 13 CTest
  targets (9.51/9.45 s), including shared-DLL media calls and existing auth tests.
  All ten offline E2E runner contract tests also passed. C++ format dry-run and
  whitespace checks passed. CI must be checked at the
  actual PR head; no LAN media fetch, private media or Apple TV operation occurred.
- This adds media-serving building blocks only. Reference pyatv playback, an
  actual file/brokered media adapter, receiver fetch/firewall reachability,
  authenticated session/playback and packaged hosts remain pending.
- Initial macOS CI exposed a test-only alias assumption: binding a client to
  unconfigured `127.0.0.2` fails there. The IP-filter fixture now uses the ordinary
  `127.0.0.1` client against an allowed peer of `127.0.0.2`, whose route still
  selects the configured loopback listener. No interface alias is created; the
  production server is unchanged. Check CI at the corrected PR head.
- At head `7b227a3aeae9b95d1ac7c0b79ba8f4948f84624b`, Linux static/shared
  and ASan/UBSan checks passed; macOS exposed the alias fixture failure above.
  The corrected media tests subsequently passed on Windows static/shared
  (2.73/2.74 s). This is partial historical evidence, not final-head CI approval.
- Adding Boost changed the manifest cache key and forced CI to rebuild unchanged
  OpenSSL/Botan packages. Both native and sanitizer cache lookups now restore a
  previous OS/architecture cache on an exact-key miss; vcpkg still checks package
  ABI hashes and builds missing/changed packages. The PR-only trigger is preserved.
- At validated head `130a71c490dde25699301915a7a509b72b95b5dc`, all ten checks
  passed in [PR CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37461159947):
  Windows/Linux/macOS static/shared native CTest, Linux ASan/UBSan and offline
  runner contracts on all three platforms. Both macOS configurations passed the
  corrected IP fixture and the rest of the media suite. Inspect checks for the
  final documentation head before merging. No receiver media interoperability
  result is added by this gate.
- PR #10 merged into `main` as `6b9680237184741100415aeb21d440825662ba37`. Its final documentation head
  `f55b9db1a95f0c0c66084b05c1b3ca345b607270` passed all ten checks in
  [PR CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37461887701).

### Continuation assessment and reference-baseline preparation: 2026-10-06

- Cloud session on `claude/modest-cannon-xa79s5`, started from `main` at `6b9680237184741100415aeb21d440825662ba37`.
  GitHub showed no open PRs; PRs #1-#10 are merged. No C++ source changed.
- Status at this point: discovery, private pairing/verification, encrypted
  receiver transport, Windows credential CLI and the media server exist. Nothing
  calls `ReceiverConnection::request` yet; no plist codec, RTSP session/event/
  feedback lifecycle, playback commands, file-backed `MediaSource` or `cast`
  CLI exists. The library still cannot cast.
- Checks run in the Linux cloud container (Python 3.13): all ten offline E2E
  runner tests pass. The native build and CTest were not run, because the
  container lacks Boost 1.92 and Botan 3.12 and the pinned vcpkg build is slow.
  clang-format 18 `--dry-run --Werror` reports violations only in four foundation
  files that predate `.clang-format`: `src/http_range.cpp`,
  `include/send_airplay2/http_range.h`, `tests/range_tests.cpp` and
  `tests/c_abi_smoke.c`. Reformat them only in a change that touches them, and
  check the result against CI's formatter version.
- The user requested pyatv pairing with "Living Room" as the reference-baseline
  first step. The cloud container cannot reach the user's LAN: it has one
  interface on an isolated documentation-range subnet, and a pyatv 0.18.0
  (MIT) `atvremote scan` found no devices. The pairing must run on the user's
  Windows host. The procedure, privacy rules and the sanitized facts to report
  back are in [reference-baseline.md](reference-baseline.md). pyatv remains an
  external reference tool; it is not a dependency and no source was copied.
- The user then ran the scan and AirPlay pairing on Windows. AirPlay, Companion
  and RAOP all report mandatory pairing and no password. pyatv AirPlay pairing
  succeeded, and `device_state` on a fresh invocation returned `Idle`. pyatv's
  model table labels the receiver Apple TV 4K (gen 3). The sanitized record is
  in receiver-validation.md. The user then reported pair exit code 0, tvOS build
  23L773 and AirPlay access limited to the same network. Reference playback is
  NOT RUN: it needs a range-capable HTTP server for the test MP4.

### Development `serve` command: 2026-10-06

- The user chose to serve the reference MP4 with this project's server (D26).
  Added `airplay2-cli serve --address IP --file PATH` and a private file-backed
  `MediaSource`. Contracts and limits are in
  [media-server.md](media-server.md#development-cli-serve). Step 4 of
  [reference-baseline.md](reference-baseline.md) now uses it. No playback
  command, credential, firewall rule or receiver setting is touched by `serve`.
- New tests: `file_source_tests` (exact bytes at literal offsets, EOF, stats,
  cancellation, concurrent readers, shrinking file, 5-GiB sparse offset on
  non-Windows hosts, loopback range through `MediaServer`) and `cli_serve`
  (real CLI argument refusals and a start/stop cycle driven by end-of-file).
- Local cloud-container evidence. The pinned vcpkg build could not run because
  the environment's egress policy returned 403 for GitHub archive downloads.
  Ubuntu's Boost 1.83 headers and OpenSSL 3.0 were used instead of the pinned
  1.92/3.6. With those, g++ 13 and clang 18 built the new tests with
  `-Wall -Wextra -Wpedantic -Werror`. `file_source_tests` passed, also under
  ASan/UBSan and TSan, and `cli_serve.cmake` passed against a scratch harness
  linking the real `serve` code. A manual loopback run served a 3,000,000-byte
  random file: a full GET and a tail range matched the file byte for byte, HEAD
  reported exact length/type/ranges, an unknown path returned 404, and the stop
  summary reported the reads. The full pinned CMake/CTest matrix is CI's
  responsibility; check it at the PR head.
- Receiver fetch from the Apple TV, firewall reachability and pyatv playback
  remain NOT RUN (user action).
- First CI run at `f821570ccc528a45151c988b2b6f9c5de922b0e6`: Linux/macOS
  static/shared, the sanitizer job and the runner contracts passed, including
  `file_source_tests` and `cli_serve` with the pinned dependencies. Both Windows
  builds failed: MSVC error C3493 rejected an uncaptured function-local
  `constexpr` used inside a test lambda. The constants moved to namespace scope.
  Check CI at the corrected head.
- Readability pass at the user's request, applying AGENTS.md; behavior unchanged:
  - The concurrent-reader test now reports the first failing reader, read index,
    offset and byte counts. Checked by injecting a fault into a scratch copy.
  - Renamed the shadowing `random` variables, and named and commented the
    lock-free min/max helpers and the `streamsize` clamp.
  - Split `serve` startup, announcement and stop-wait into named functions.
  - Local g++/clang `-Werror`, ASan/UBSan, TSan and `cli_serve.cmake` all pass
    again with the system Boost 1.83.
- Final CI for this slice: the MSVC fix head `ed9cd72da7946b97f78516b59ae8de326f9d4189`
  and the readability head `19a08d256fdd2092aecfc63438af2b6a07c95b8c` each passed all
  ten checks: Windows/Linux/macOS static/shared native CTest (including
  `file_source_tests` and `cli_serve`), Linux ASan/UBSan, and the three
  offline runner jobs. See the
  [CI run for `19a08d2`](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37468229857).
  This is build/test evidence only. The receiver's fetch from `serve` is NOT RUN.

### Reference playback attempt: 2026-10-06 (local session)

- Local Windows 11 x64 session on `claude/modest-cannon-xa79s5` at `20aaaee`.
  Static and shared Release builds (MSVC, Visual Studio 2026 generator, pinned
  vcpkg `434307d`) each passed all 15 CTest targets.
- The reference baseline with pyatv 0.18.0 and `airplay2-cli serve` failed.
  Every run produced `reads=0`, and polling saw zero TCP connections to `serve`.
  Legacy `/play` returns 200 and `setProperty`/`rate` succeed, but
  `/playback-info` returns 500 within about 1.4 s, with the same result for a
  public Apple HLS URL. This matches open upstream pyatv#2906/#2821. The
  unmerged pyatv#2846 proposal moves URL playback to `POST /command` queue
  commands on a type-130 stream, which matters for the native session design
  (section 7, item 5). It was read as reference only: no code was copied or run.
- The base SETUP timed out until the user allowed inbound UDP to pyatv's NTP
  timing server; Windows had no rule for Python, and the network is Public.
  A native session that advertises a timing port needs the same reachability.
- Diagnostic gap: `serve` reports successful reads only. A connection or request
  counter in the stop summary would separate "never connected" from "HEAD only"
  without external polling.
- No receiver setting, credential or pairing changed. No C++ changed.

- The user then chose the unmerged fix `robkochman/pyatv@8144c77c`, one
  commit on the maintainer's 0.18.0 release. The agent reviewed the source
  diff; the user installed and ran it. Playback PASSED: video and audio played
  (user-observed), and the receiver opened 17 connections to `serve` and read
  the full file range. This is the first evidence that the receiver fetches from
  this project's `MediaServer` through the Windows firewall, which needed no
  new rule for `serve`. It does not show native casting. Controls (seek,
  pause, stop) were then run by the user with the fork: seek, pause/resume and
  position passed. `stop` returned the TV to the home screen, but the receiver
  reported `Paused` and the sender's session stayed open.

### Binary plist codec (D27): 2026-10-06

- The user chose a bounded in-tree codec over libplist (LGPL-2.1) or another
  library. Original Apache-2.0 code; no third-party source or new dependency.
  The format layout follows Apple's CFBinaryPList as documented by CPython
  `plistlib`.
- `PlistValue` covers booleans, signed 64-bit integers, reals, dates, UTF-8
  strings, data, arrays and ordered dictionaries. The encoder matches
  plistlib's binary writer byte for byte (`sort_keys=False`): depth-first
  numbering, shared equal scalars, and minimal 1/2/4/8-byte widths. One
  documented difference: reals are shared by bit pattern, so -0.0 stays
  distinct.
- The decoder accepts that subset plus 4-byte reals and any 1-8-byte table
  width. It rejects null/fill/UID/set/16-byte integers, malformed or misplaced
  tables, out-of-range references, cycles, duplicate or non-string keys, and
  invalid ASCII/UTF-16. Limits: 1 MiB document, 16,384 objects, depth 32, and
  65,536 decoded values / 4 MiB decoded payload, because shared references can
  otherwise expand exponentially.
- Tests: 7 plistlib fixtures (all types and boundaries, sharing, wide tables,
  the `/command` and event envelopes) checked in both directions; hand-built
  literal layouts; 30+ malformed and budget cases; encoder UTF-8, duplicate-key,
  depth, object and size limits; every single-byte XOR, every truncation and
  2,000 seeded random mutations per fixture.
- Evidence: Windows MSVC static/shared Release passed all 16 CTest targets
  (`plist_tests` about 0.1 s). clang++ 22 with `-Wall -Wextra -Wpedantic
  -Werror` built and passed; clang AddressSanitizer passed. That clang check
  caught a C++20-only structured-binding lambda capture before CI. A
  fault-injected fixture produced `root.ascii` and byte-offset diagnostics.
  MSVC caught an unspecified-evaluation-order bug: length reads now happen
  before the payload cursor is passed on.
- This is a building block only. No receiver message is encoded or decoded
  by library code yet.

### Session channel keys (step 1): 2026-10-06

- **New:** `src/channel_keys.*`.
  - `ChannelKeyLabels`, with factories for the control, event and data-stream
    channels. The event infos are reversed for the sender; the data-stream
    salt carries the decimal SETUP seed.
  - `derive_session_key`.
  - `ChannelKeySource`: a non-copyable, non-movable, erasing owner of the
    verified shared secret. It hands out only derived keys.
- **PairVerifier:** keeps the shared secret from M4 until the one-time
  release. `take_control_keys` still erases it. The new `take_session_keys`
  moves it into an empty `ChannelKeySource` and refuses an occupied one.
  Outputs stay unchanged on failure.
- **ReceiverConnection:** `verify` uses the session release and keeps the
  owner. `derive_channel_keys` works only while verified, and a failure closes
  the connection, like every other method. `close()` erases the secret.
- **Tests:**
  - Literal label strings, including unsigned decimal seed formatting.
  - Session release equals the existing control-key fixtures.
  - Event and data-stream keys equal new independent Python HKDF fixtures.
  - Aliased outputs, an empty source, an occupied destination and
    after-close derivation are all refused.
  - In `receiver_tests`, keys derived after a real record transition match
    the fake accessory's own literal-label HKDF.
  - Regenerating with Python 3.11.9 / cryptography 50.0.2 kept every existing
    pair-verify fixture byte-identical.
- **Evidence:**
  - Windows MSVC static/shared Release passed all 16 CTest targets.
  - clang++ 22 `-Wall -Wextra -Wpedantic -Werror` syntax checks of the changed
    sources passed.
  - Swapping the two event-key fixtures failed with scenario-specific messages.
  - clang-format and `git diff --check` passed.
- **Provenance:** the label strings come from pyatv revision `b277a4c` (MIT);
  blobs are recorded in dependencies.md.
- **Not yet exercised:** no receiver traffic uses these keys yet. Event-channel
  interoperability is untested.

### Event channel (step 2): 2026-10-06

- **Framing** (`receiver_http.*`): `EventRequestParser` handles
  receiver-initiated requests, back to back, with bounded pending input.
  `encode_event_response` builds the reference reply: `200 OK`,
  `Content-Length: 0`, `Audio-Latency: 0`, with `Server` and `CSeq` echoed. The
  per-line field check is now shared with the response parser, which behaves
  as before (`receiver_tests` unchanged and passing).
- **`EventChannel`** (`event_channel.*`) owns the stream, both record directions
  and the parser. It answers each request before returning it. Every failure is
  terminal and erases state. One owning thread; other threads stop a blocked
  receive through the cancellation flag.
- **`ReceiverOperation::until_cancelled(flag)`:** a no-deadline operation for
  long-lived readers. `after()` stays capped at 60 s, and an expired deadline is
  terminal, so a silent receiver must not time out the event reader.
- **Tests** (`event_channel_tests`):
  - Parser: literal requests, HTTP without a length, coalesced and split input,
    byte-at-a-time input, and 14 rejection cases plus the pending limit and EOF
    rules.
  - Replies: literal bytes, and refusal of injected CRLF.
  - Channel: read fragments of 1, 7 and 4096 bytes; requests across records;
    3-byte partial writes; clean and partial EOF; truncated and tampered
    records; an oversized body; cancellation; a deadline; construction checks.
- **Evidence:**
  - Windows MSVC static/shared Release passed all 17 CTest targets.
  - clang++ 22 strict-warning syntax checks passed. They again caught a C++20-only
    structured-binding lambda capture, now fixed.
  - Reversing the reply field order failed six checks by name.
  - clang-format and `git diff --check` passed.
  - CI for step 1 (`c5a2734`) passed all checks.
- **Not yet exercised:** no Apple TV traffic. Event-channel interoperability is
  untested.

### NTP timing responder (step 3): 2026-10-06

- **Codec** (`ntp_timing.*`): `ntp_from_system_time`, `parse_timing_request`
  (exactly 32 bytes, type `0xd2`) and `encode_timing_response`, matching the
  reference reply (type `0xd3`, sequence 7, reference = request send time).
- **`TimingResponder`:** binds a numeric local address on an ephemeral port and
  answers only the receiver's host. Foreign or malformed datagrams are dropped
  and counted, never fatal. `serve()` runs on one thread until cancelled.
- **Refactor:** the native socket helpers moved unchanged from
  `receiver_stream.cpp` to the private `native_socket.*`. Two additions: a
  port-0 address parser and the shared poll-slice calculation.
  `NativeReceiverStream` is unchanged in behavior; the existing native loopback
  receiver tests pass.
- **Tests** (`ntp_timing_tests`):
  - Conversion known answers: epoch, 2026-10-06, fractions, and before 1970.
  - Literal request and reply bytes, and size and type rejections.
  - Real UDP loopback over IPv4 and IPv6: garbage ignored and counted; the
    reply equals the codec output; a foreign host is ignored; cancellation and
    serving after close; construction errors.
- **Evidence:**
  - Windows MSVC static/shared Release passed all 18 CTest targets.
  - clang++ 22 strict-warning syntax checks passed for the Windows branches.
  - Making the reply echo the wrong reference time failed the literal check.
  - CI for steps 1 and 2 passed all checks.
- **CI:** the first run at `094f91a` failed on Linux (GCC and the sanitizer
  job). `static_cast<decltype(sent)>` cast to a `const` type
  (`-Werror=ignored-qualifiers`), a warning neither MSVC nor clang raises.
  It was fixed in `2f46e34` by comparing the signed result as a size; macOS
  and Windows had passed. No Apple TV traffic yet; timing interoperability is
  untested.

### Session messages (step 4): 2026-10-06

- **New:** `session_messages.*` on the D27 plist codec.
  - `SenderIdentity` (D30): reference model and OS values, the display name
    `send-airplay2`, and a random locally administered device ID.
  - Random UUIDs, stream seeds below 2^63, and `SessionHeaders`: random
    DACP-ID and Active-Remote, RTSP headers, and `/command` headers with the
    `AirPlay/870.14.1` agent and session/stream IDs.
  - SETUP builders: base, remote-control-only, URL control stream, data stream.
  - Response parsers: `eventPort`; `streamID` and optional `dataPort`.
  - `/command` envelope and the four start commands.
  - `parse_session_event`: the event type, and the lower-cased playback state
    from `params.playbackState` or `name`.
- **Encoding rule:** RTSP plist bodies are sorted recursively by key, as
  plistlib's default `sort_keys=True` does in the reference. Command payloads
  keep the reference's key order.
- **`public_random_bytes`:** added to `control_crypto` (OpenSSL `RAND_bytes`)
  for non-secret identifiers.
- **Tests** (`session_messages_tests`):
  - Every builder is byte-exact against 14 plistlib fixtures.
  - Literal header lists.
  - Response and event parsing, with 16 rejection cases.
  - Shape, version, locality and uniqueness checks of random identifiers.
  - Without the recursive key sort, all four SETUP fixtures fail with byte
    offsets.
  - URL-stream (239 bytes) and data-stream (298 bytes) SETUP sizes match the
    sanitized tvOS 26.6 request log exactly. The base SETUP differs only by the
    longer display name.
- **Evidence:** Windows MSVC static/shared Release passed all 19 CTest targets;
  clang++ 22 strict syntax checks and clang-format passed.
- **Scope:** the plist codec and session messages remain test-only until the
  orchestrator uses them. No Apple TV traffic yet.

### URL playback session (step 5): 2026-10-06

- **New:** `url_playback_session.*`. `UrlPlaybackSession::start` runs the
  reference sequence: connect and pair-verify; timing responder on the
  route-selected local address; base SETUP; event channel with derived keys;
  feedback every 2 s; `GET /info` (errors tolerated); RECORD; URL-stream SETUP;
  the four `/command` start commands. It returns only when the receiver reports
  `playing`; `loading` is not success.
- **Threads (D28):** callers and the feedback thread share the control
  connection under one mutex. An event thread answers events and publishes
  state; a timing thread answers NTP.
- **Failures:** `SessionException` categories are `rejected` (with the status),
  `start_timeout` and `connection_lost`. Any start failure tears everything
  down.
- **Stop:** `stop()` runs in a fixed order (feedback, event channel, control
  connection, timing responder). It is idempotent and also runs from the
  destructor.
- **Supporting changes:**
  - `native::route_local_address`: a UDP connect plus `getsockname`, sending
    nothing.
  - `StreamConnector` injection for tests.
  - The plist codec, session messages and session are now part of the library
    (`sap2_session_sources`).
- **Tests** (`url_playback_session_tests`), against a thread-safe fake receiver
  that does accessory-side pair-verify, encrypted control responses, and
  encrypted events derived from literal labels:
  - Full happy path: request order, RTSP URI and session headers, `/command`
    headers and command order, the media URL in the queued item, a live
    timing port (a real UDP probe), periodic feedback, and every event
    answered.
  - Later state changes, ordered stop, and idempotent stop.
  - Rejected base SETUP (500), rejected `insertPlayQueueItem` (400), never
    `playing` (timeout), event channel lost during start, cancelled start, and
    event channel lost after start.
- **Evidence:**
  - Windows MSVC static/shared Release passed all 20 CTest targets.
  - 25 repeated runs of the threaded suite: no failures (0.6 s each).
  - clang strict syntax checks and clang-format passed.
  - Accepting any state as started failed three scenarios by name.
  - One test bug was found and fixed during development: a pointer into a
    temporary decoded plist.
  - CI passed for `2f46e34` (step 3 fix) and `f2cfd23` (step 4).
- **CI:** `5104f18`, `1630b61` and `ad6626b` failed on Linux, macOS and the
  sanitizer job. The new test's local `native::NetworkRuntime` is an empty
  struct on POSIX, so `-Wunused-variable` under `-Werror` rejected it; Windows
  and local MSVC builds passed. Fixed in `6c61433` with `[[maybe_unused]]`,
  matching the other runtime owners.
- **Not yet exercised:** no Apple TV traffic. The first hardware run is gate G1
  in step 6.

### `airplay2-cli cast` (step 6): 2026-10-07

- **New:** `cast_cli.*`. Steps, in order:
  1. Open the media file (a bad path is exit 2).
  2. Load the stored credential profile (absent: exit 1, before any network
     work).
  3. Start `MediaServer` for that receiver only, using the media server's
     maximum per-request budget.
  4. Run `UrlPlaybackSession::start`.
  5. Print `State: <state>` lines from a reporter thread until Enter or
     end-of-file.
  6. Stop the session, then the server, and print a sanitized summary: state,
     events, feedback, timing answers, whether the session failed, and the read
     counts and span.
- **Never printed:** the private URL, the receiver address, identifiers or
  payloads. Session failures print a category and, if rejected, the status.
- **Supporting change:** the credential error wording from the auth CLI is
  shared (`describe_credential_error`). The usage text lists `cast`; the
  `verify` success message the E2E runner pins is unchanged.
- **Tests** (`cli_cast`): six argument refusals with exit 2; an absent profile
  exits 1 with a `Credentials:` message; no output ever contains a URL.
- **Evidence:** Windows MSVC static/shared Release passed all 21 CTest targets;
  clang strict syntax checks and clang-format passed.

### G1 diagnostics: 2026-10-07

- **Event log:** `cast --event-log` prints value-free event outlines (method,
  target, type, playback state and key paths), via
  `UrlPlaybackOptions::record_event_structure`, `take_event_log()` and
  `describe_event_structure()`.
- **Parser fix:** bare-dictionary events (`updateInfo`) are now parsed instead
  of counted as unreadable.
- **Tests:** a literal outline for each fixture event; the session records
  outlines only when enabled; and a bare-event fixture.
- **Evidence:** Windows static/shared builds passed all 21 CTest targets.
- **Hardware:** see receiver-validation.md. The native protocol sequence works;
  presentation needs the remote-control session (H5).


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

### Minimum remote-session implementation and automated validation: 2026-10-07

- `UrlPlaybackSession` owns independent remote and URL control/event connections.
  Remote SETUP is authenticated first. Its event reader answers requests without
  overwriting URL state; event loss marks the composite session failed.
- Remote keys/UUID/CSeq/counters are separate. The connection remains until URL
  teardown, then the reader is cancelled/joined and remote secrets are erased.
  This experiment deliberately omits remote RECORD/feedback/data stream/MRP.
- Regression tests use distinct ephemeral secrets for the two fake sessions:
  independent encrypted replies, SETUP fields/order, lifetime, state isolation,
  remote SETUP rejection, event-connect failure, event loss during/after URL
  start, URL failure cleanup, and URL-before-remote close order.
- Windows MSVC 19.51 static/shared Release: 21/21 CTest targets passed in each
  final build. Existing offline E2E runner: 10/10 tests passed. Touched C++ passes
  clang-format dry-run/Werror and git diff --check. Native CI must be checked at
  the newly published head; baseline CI is not evidence for this change.
- No dependency, copied implementation, pairing change or firewall change.
  Reference provenance is recorded in dependencies.md. Historical test records
  are preserved; current summaries and the PR continuation instructions are updated.

### Native MRP implementation and receiver controls: 2026-10-07

Step 2 adds original bounded protobuf/frame codecs, correlated handshake,
commands and heartbeat on a verified data stream. Full player paths accompany
commands; stale/unrelated/replaced items are refused. D32 binds a new selected
AirPlay item once when its duration matches our URL event; simultaneous
same-duration AirPlay takeover remains outside that cooperative guarantee.

Windows MSVC static/shared Release each passed 22/22 CTest targets
(11.88/11.71 s), offline E2E contracts 10/10, clang-format dry-run/Werror and
`git diff --check`. The native receiver accepted pause/resume, seek to 45 s,
seek back to 15 s and Stop. Telemetry followed the changes and a heartbeat
was acknowledged; exit 0, full file span fetched, no session/failed-read errors.
Additional focused static/shared MRP tests passed for missing device payloads
and malformed correlated command results. Initial implementation CI rejected a
const vector copy in a test loop under GCC/Clang warnings-as-errors; the
const-reference follow-up fixed it without changing runtime code. All ten
checks passed at code/test head `8dca4a55c831563be952a5b3d6c8a353ad8b7807` in
[CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37502438877): six native platform/static/shared jobs,
ASan/UBSan and three offline runner jobs. Inspect the actual final PR head
including any documentation-only follow-up.
The final executable/source fingerprints and observer status are in the
[dated G2 record](receiver-validation.md#native-mrp-controls-g2-pass-2026-10-07).
G2 visual confirmation passed by user report on 2026-10-07. Do not substitute telemetry or CI for it.
The final URL event was paused, not proof of protocol idle. Next: step 3 / G3.

### Native lifecycle implementation and selected G3 checks: 2026-10-07

D33 adds automatic cleanup for terminal URL state, owned receiver EOF, loss of
ownership and connection failure. One supervisor starts after initialization,
cancels commands and retains URL-before-remote closure. Concurrent stop calls
join that owner; startup failures clean directly. CLI command input is polled
without a detached stdin worker, so a terminal session closes its media server
and exits even while stdin remains open. First end reason and completed cleanup
are separate status facts. The stop path does not allocate a status string,
including during allocation failures in startup.

Final Windows MSVC static/shared Release passed 23/23 CTest targets
(13.33/13.07 s), offline runner contracts 10/10, touched C++ formatting and
git diff --check. New tests cover real native pipe input, partial/overlong/EOF
lines, concurrent stops, silent established feedback deadlines, pending feedback
and MRP cancellation, and independent receiver EOF boundaries. Scripted peers
are not actual receiver outages.

Native EOF after seek to 124 seconds/Play exited automatically with stdin held
open: media_end, cleaned=yes, failed=no, no failed reads, full-file fetch, exit 0.
Ten short native casts alternated MRP Stop and direct teardown; all established
owned playing telemetry and completed cleanup without errors. Exact build/source
fingerprints and counts: [dated lifecycle record](receiver-validation.md#native-lifecycle-selected-g3-cases-pass-manual-gates-pending-2026-10-07).
G2 controls and EOF video/audio/home are user-confirmed. Receiver-side stop
returned home and automatically cleaned up, but reported connection_lost/exit 1;
normal stop classification remains unresolved. Sleep automatically cleaned up;
fresh playback after wake reused credentials and EOF returned home with normal
video/audio (user-confirmed). Actual network interruption/recovery is NOT RUN.
The sleep setup also emitted repeated loading/playing states. A preliminary
probe paused near startup; seek retained pause and explicit Play was necessary.
Later passing cycles do not resolve intermittent buffering/startup behavior.
Do not mark G3 or the standalone milestone complete. Next investigate normal
receiver-stop classification and buffering, then actual network-loss recovery
when available; recovery means a fresh explicit cast with the retained profile.

### Receiver-stop diagnostics and full-clip playback: 2026-10-07

D34 adds fixed failure channel/category snapshots and CLI output, preserving MRP's
first category through stop. Tests distinguish URL/remote event EOF from URL
feedback timeout and suppress intentional cleanup cancellation. An ordinary
pause followed by event EOF still fails, protecting outage detection. Windows
static/shared Release each passed 23/23 CTest targets (13.13/13.05 s), offline
runner contracts 10/10, touched C++ clang-format dry-run/Werror and diff checks.
No dependency or copied implementation was introduced.

The repeated user-confirmed remote Stop returned Home; the native URL event
connection closed without a terminal playback-state event. Summary:
connection_lost, cleaned=yes, failed=yes, failure_channel=url_events,
failure_reason=disconnected, exit 1. This narrows the cause beyond the previous
paused snapshot. Unknown notification values were omitted, and the inspected
reference handler ignores them; no terminal meaning was inferred from their shape.

A fresh full clip reached EOF without seek/play/pause/stop commands while stdin
remained open. Two brief loading/playing transitions occurred around 18 seconds
after playing began, followed by advancing status snapshots and four acknowledged
heartbeats. URL stopped at the natural end and cleanup returned media_end/exit 0,
failed=no, failed_reads=0, full-file span. **Observer follow-up: full-clip video
presentation FAIL.** The user reports a short buffering stop near 18 s, then video
remained frozen while audio played normally until clip end. Home return for this
run is unconfirmed. The loading/playing pairs correlate with that report; returned
playing state, extrapolated positions, full-file read span, heartbeats and EOF
cleanup do not prove advancing video frames. At this D34 gate, video recovery
investigation took priority over broader support work; D37 subsequently
lowers its priority unless it recurs. Earlier short/near-end
video/audio/Home observations remain evidence for those separate runs.
The user also confirmed that the original MP4 plays past 18 s with moving video
on the PC. This supports investigating the casting/receiver path; it does not
alone exclude a receiver-specific media/decoder problem.

Read-only follow-up inspection found that `FileReadStats` counts source reads
before `MediaServer::Session::write_chunk` completes asynchronous socket writes.
Consequently zero failed reads and a full-file read span cannot prove complete
HTTP responses or bytes received/decoded. `cast` already sets a 600000 ms absolute
media-request deadline, longer than this run; the default 30 s server budget did
not expire here. Startup queue commands match the pinned reference's reviewed
sequence. Existing sanitized event traces omit buffer/stall values and request
completion/write errors, so they cannot identify the cause. No behavior fix or
automatic Play/seek retry is supported by this evidence.

**Investigation implementation (D35):** bounded opt-in diagnostics with local request
numbers, GET/HEAD, numeric selected ranges/status, bytes written, response
completion and timeout/cancel/I/O categories, plus active-connection counts.
Implemented `cast --media-log` and allowlisted `--event-log` rate/position/duration/
readyToPlay/stallCount values. Correlate these finite receiver scalars; omit media URLs,
identifiers and raw payloads. Use the unchanged supplied clip, first a natural
reproduction, then controlled native-session comparisons. Require observed moving
video past the stall through clip end before calling recovery successful.
The [sanitized artifact](validation/native-stop-buffering-windows-static-2026-10-07.json)
contains exact executable/source fingerprints and event/state/position facts.
The initial missed remote-stop window was ended by the sender and contributes no
remote-stop evidence. The allowed executable was restored after every run.

### D35 instrumented HTTP/buffering follow-up

See the dated [receiver record](receiver-validation.md#instrumented-httpbuffering-investigation-2026-10-07)
and [four-run artifact](validation/native-http-buffering-windows-static-2026-10-07.json).
Socket writes are now distinguished from source reads, including partial failures,
with no addresses/URLs/metadata in scalar records. Four occupied slots coincided with new small range admission occurring only
when older read-ahead requests ended, supporting an admission-delay hypothesis. Increasing the
explicit limit to 16 while retaining MRP admitted up to six requests and passed
full video/audio/Home without recorded loading. Abandoned read-ahead requests also
occurred in successful runs, so I/O error counts alone are not a playback failure.
`stallCount=0` did not exclude visible buffering. The same-source minimum session
also passed; a default repeat buffered but recovered. Preserve the earlier video
FAIL and this separate capacity candidate, without claiming MRP caused the freeze
or declaring the startup pause solved. Default cast/server budget is still four.

## 6. Screenbox integration findings

Status (2026-10-10): implemented in the fork through its phase 4b (fork PRs
#2-#6, merged); see section 0. User decisions 1-9 are in the fork's
`docs/AIRPLAY_INTEGRATION.md`. Among them: every file is cast through
`CastDelivery.HlsRemux` (option B), and the remux's own refusal
(`MediaUnsupported`/`MediaMalformed`) decides eligibility. The notes below
are from the initial inspection.

At the inspected Screenbox commit:

- `CastService.CreateRendererWatcher` accepts only `VlcMediaPlayer` and returns
  a concrete `RendererWatcher`.
- `CastService.SetActiveRenderer` calls `VlcPlayer.SetRenderer`.
- `ICastService` exposes that watcher and the current concrete renderer model.
- `Renderer` owns a LibVLC `RendererItem`; it is not a general AirPlay device model.
- `Package.appxmanifest` already includes `privateNetworkClientServer`, but that
  does not prove inbound serving, discovery or native loading in a packaged app.

The working design is to wrap the existing LibVLC behavior with a Chromecast
adapter and add an AirPlay provider/session behind a common boundary. Do not try
to pass an AirPlay device to LibVLC's existing renderer setter. AirPlay should own
its remote session and media server. Controls and displayed position must follow
the active remote session, with local playback handoff/recovery defined.

Before touching Screenbox, read its current `AGENTS.md`, `.github/copilot-instructions.md`
and applicable C#/XAML instructions. The inspected instructions require Visual
Studio 2026 MSBuild for UWP builds, not `dotnet build`. They also distinguish
stateless services from stateful resource managers. Inspect RendererWatcher,
CastContext, CastControlViewModel and playback coordination before choosing where
to own a session. Follow current repository instructions rather than assuming
the recorded source snapshot is still current.

## 7. What is missing and what to do next

Historical PR #12 follow-up list, retained for its evidence and decisions.
It is superseded by section 0's current queue: public APIs, bindings, stores,
UWP packaging, HLS and Screenbox integration below have since been implemented.

1. **Native presentation (G1):** minimum native remote-control-only SETUP/event
   session implemented and PASS without pyatv. Keep its independently verified
   connection until URL teardown. Expand lifetime/firmware and stop coverage;
   a single video/audio/home-screen observation does not establish G2/G3.
2. **MRP controls (D29/D31, G2):** implemented with bounded framing/codecs,
   independent fixtures, correlated handshake/commands/heartbeat and player
   tracking. Native command/telemetry evidence is recorded, and the user
   confirmed the visual controls and home-screen result (G2 PASS).
   D32 fallback assumes cooperative AirPlay startup
   and does not guarantee ownership against a concurrent same-duration cast.
3. **Lifecycle (G3):** automatic EOF/terminal/failure cleanup implemented; native
   EOF and ten short cycles passed, with user-confirmed EOF video/audio/home.
   Receiver-remote stop and sleep triggered automatic cleanup; stop classified
   connection_lost/exit 1. D34 diagnostics reproduced URL event EOF while still
   playing, without an explicit terminal state, so normal stop intent remains
   unresolved. A full 131.6-second clip reached natural EOF without seeking, but
   **video froze after buffering near 18 s while audio continued to clip end**
   (user-confirmed); the same source file plays normally on the PC. D35 diagnostics and
   controlled comparisons are implemented: minimal/four and MRP/16 passed full
   video/audio/Home; MRP/four buffered but recovered. D36 passed user-observed
   controls and selected lifecycle checks at 16 slots and selected 16 as normal
   `cast` default (generic server/`serve` remain four). Another 16-slot attempt
   paused at zero with only two active requests before transport commands;
   D38 now traces command/event order and requires one second of eligible playing
   before startup success; the spontaneous receiver pause remains active. D39
   completed the [manual batch](manual-validation.md): startup, controls/full EOF
   and post-wake presentation/Home passed; remote Stop/Home and cleanup passed
   but protocol intent remains unresolved. D40 adds bounded remote-event/final
   MRP observations; the repeated Stop/Home and sleep trace differ but do not
   establish a reliable normal-stop rule. The first D40 attempt was unobserved.
   D41's [pyatv audit](pyatv-stop-reference.md) found a permissive legacy polling
   heuristic and a fork event-waiter disconnect gap, with no validated Stop
   discriminator. Separate any API termination-policy change from intent evidence.
   Per user decision D37, the original frozen-video failure is low priority:
   insufficient media connections are the likely cause; retain its evidence and
   raise priority if it recurs in later testing. D42 passed selected actual
   Ethernet interruption cleanup and fresh explicit same-credential recovery
   with observed video/audio/near-end EOF/Home. Broader network cases remain
   untested; remote-Stop intent remains open under the conservative policy.
   Investigate the
   buffering pause and renewed loading seen in the mixed native/pyatv run; one
   preliminary native EOF probe paused at startup and needed explicit play.
   Successful later cycles/full-clip completion do not resolve that intermittent behavior.
4. **Other hardware gates:** restart authentication, wrong PIN/revocation,
   disposable-profile deletion only when opted in, departure/interface changes,
   real-file >4-GiB seeking and neutral sender identities. Discovery, pairing/reuse,
   reference playback and server fetch already have selected-combination evidence.
5. **Library/hosts:** public versioned session ABI, bindings, other credential
   stores, packaged Windows C# loading/brokered files/inbound networking and
   Linux/macOS/Android device proofs. Botan UWP packaging remains unresolved.
6. **Screenbox:** after the standalone gate, read current repository instructions,
   introduce the provider/session boundary, and test Chromecast and local handoff.
7. **Small diagnostic:** add aggregate connections/requests to `serve`'s summary.

D27-D31 are settled: in-tree plist, synchronous session threads, MRP controls,
configurable reference identity and in-tree protobuf. Remaining choices concern
further remote-sequence reduction, broader lifecycle behavior, public ABI/cancellation,
other OS storage/discovery backends, capability/codec policy and packaging.
Standalone audio, DRM, mirroring, multiroom and transcoding remain outside the
first MP4 slice. Missing hardware access does not prevent independent implementation.

## 8. Continuation mechanics and known obstacles

Current local environment, verified on 2026-10-06: GitHub CLI 2.102.0 is available
at `C:\Program Files\GitHub CLI\gh.exe`, authenticated as `ilyalissoboi` through
the keyring. A GitHub API read confirmed push and admin permissions for this
repository. Network verification required execution outside the sandbox; the
initial sandbox authentication error was not evidence of invalid credentials.
Authenticated command-line Git push succeeded for PR #2 in this local chat.

Historical cloud environment: public clone/read worked. Command-line `git push` failed with
`could not read Username for 'https://github.com'`; GitHub connector authorization
does not automatically configure CLI credentials. Publishing resumed successfully
through the authorized connector's Git tree/commit/branch/PR operations. Do not
ask for the same authorization again merely because a CLI lacks credentials.
Use the available supported connector, or normally configured credentials in a
human developer's environment; never place credentials in the repository.

The original local foundation commit was
`f9bec098b83de5d38caab3696f935aa87ded6be4`; the equivalent published foundation
commit is `dbd654b1d92057b3208953226186c5c2b206ccff` because it was created through
the connector. In a carried-over workspace, inspect local versus remote history
before attempting a push. A clean checkout of the PR branch avoids that divergence.
Do not force-push over unfamiliar remote changes.

Local continuation (D45): PR #12 is merged and its branch deleted locally. Start
each new slice on a fresh descriptive branch from `origin/main` and open its own
PR; CI runs only for pull requests. Inspect the actual PR head/checks before
reporting results. Do not force-push. Ask the user before merging. The Windows host can reach the LAN;
hardware observations still need someone watching the receiver. Never read raw
credential files or expose the address, identifiers, PIN or private URL.

Claude Code cloud sessions (2026-10-06): the repository is cloned fresh into an
isolated container. Pushes use the session's Git proxy; GitHub reads and PR
operations use the GitHub MCP connector, because the `gh` CLI is not available
there. The container has no route to the user's LAN, so discovery, pairing and
playback against "Living Room" must run on the user's host. Native dependencies
(Boost/Botan) are not preinstalled.

No background implementation task was scheduled. CI is asynchronous; further
coding resumes when a developer/model actively continues the project.

## 9. Completion criteria and reporting rules

Milestone 1 requires the tested standalone native library across the requested
host platforms and an explicit receiver compatibility matrix. The first Apple TV
MP4 vertical slice is an intermediate gate, not the whole milestone. Milestone 2
requires the Screenbox fork integration and regression-tested Chromecast behavior.

When handing off again, update: branch/PR and current commit, actual implementation
coverage, exact tests and hardware results, new decisions and rationale, open
choices, and the next actionable change. Keep "planned", "implemented",
"build-tested" and "receiver-tested" distinct. Store sanitized fixtures/results
with provenance; do not commit pairing secrets or private media.
