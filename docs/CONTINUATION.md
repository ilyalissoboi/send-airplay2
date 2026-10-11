# Separate-session development handoff

Checkpoint: 2026-10-11 (Asia/Tokyo). PRs #12-#40 are merged. `main` contains
the experimental C API version 3, C# binding, packaged UWP host and native
builds, Windows/macOS/Linux credential stores, wake before play, MP4/MKV HLS
remux with text subtitles and indexed MKV startup, and D62's URL controls
fallback for receivers that reject remote-control SETUP. The Screenbox fork
has implemented its current design (PRs #1-#8); merged `main` now pins
`0.3.0-ci.207`, including D62/D63. Receiver evidence remains specific to the recorded
Apple TV and MacBook Pro from Windows. D62 used the CLI; completed D63 checked
desktop C API and packaged UWP playback on macOS 26.7.1, and the user reports
the earlier private Screenbox UI checks passed. D64 completed final Screenbox
checks of published `ci.207` by user report with running native/crypto DLL
hashes verified; fork PR #8 merged as `31013d5e`. Final evidence documentation
in native PR #41 and fork PR #9 awaits review/merge with user approval.
Read `AGENTS.md`, then [HANDOFF.md](HANDOFF.md), [design.md](design.md) and
[receiver-validation.md](receiver-validation.md). This note is a focused restart
guide; the master handoff and dated artifacts preserve the longer history.

## Repository, branch and user authorization

- Repository: [ilyalissoboi/send-airplay2](https://github.com/ilyalissoboi/send-airplay2).
- **Completed slice (2026-10-11):** the user chose documentation reconciliation,
  then bringing D62 to Screenbox. PR #39 merged as `2829242`, final head
  `811c022` passed all 14 checks and its review thread is resolved. Continue
  final validation on `codex/mac-screenbox-final-validation` from current
  main `a29212b` (merged PR #40). C API,
  packaged UWP and private Screenbox checks are recorded below. Approved main
  package `ci.207` is now published and publicly hash-verified; fork PR #8
  merged its pins. Running DLL hashes match; final Screenbox receiver checks
  passed by user report. Review/merge the final evidence documentation with
  user approval; subsequent development choices remain unselected.
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

The active queue is [HANDOFF.md section 0](HANDOFF.md#0-resume-here): C API/UWP
and private Screenbox checks are recorded in completed D63; D64 published the
approved main prerelease and merged its fork pins. Running DLL hashes match,
and final-package receiver checks passed by user report. Review/merge the
final evidence documentation with user approval. Subsequent development is
unselected:
Mac consent research, fork follow-ups, MRP track/queue research, HDR and growing
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
7. **Screenbox integration:** implemented in the fork through PRs #1-#7.
   Re-read its current instructions before changes. The active slice brings
   D62 to its pinned package; Chromecast regressions and additional architecture
   and sleeping-TV checks remain follow-ups.

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

> Continue send-airplay2 from AGENTS.md, docs/HANDOFF.md, docs/design.md,
> docs/receiver-validation.md and docs/CONTINUATION.md. Resume the active D62
> C API/UWP/Screenbox slice on its actual PR head. Inspect `origin/main` and
> open PRs before writing. Preserve receiver-specific evidence and historical
> failures. Ask before merging and before publishing each package prerelease.
