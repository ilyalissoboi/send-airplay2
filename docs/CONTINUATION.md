# Separate-session development handoff

Checkpoint: 2026-10-08 (Asia/Tokyo). PRs #12-#15 are merged; `main` contains the
receiver-tested public playback interface (D46-D48) and the D49 credential
design. D49 step 1 (host stores and pairing, D50) is on `claude/host-credential-store`.
Read `AGENTS.md`, then [HANDOFF.md](HANDOFF.md), [design.md](design.md) and
[receiver-validation.md](receiver-validation.md). This note is a focused restart
guide; the master handoff and dated artifacts preserve the longer history.

## Repository, branch and user authorization

- Repository: [ilyalissoboi/send-airplay2](https://github.com/ilyalissoboi/send-airplay2).
- **Active slice (D50):** host-provided credential stores, `sap2_pair()` and
  `sap2_forget_profile()` in the C interface (API version 2), on branch
  `claude/host-credential-store` with its own PR (find it with
  `gh pr list --state open`). A real pairing through `sap2_pair()` into the
  built-in store, then a cast with the new profile `living-room-api`, passed
  on the TV (D51).
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
- Latest implementation/test commit on `main`:
  `ab7ec0170dbc364bfbec0e52a1b71ae92a0974a5` (D43). Later commits change
  documentation only. Inspect `git rev-parse origin/main` rather than assuming
  these hashes are the latest tip.
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

The initial public interface remains experimental. Private native MP4 casting is
implemented; old statements that this repository cannot cast are superseded.
The public playback/session ABI and production bindings are not implemented.

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

D43 review fixes are in `d1011ab`, `880feb3` and `ab7ec01`: decoded MRP extension
payloads have move-only erasing owners; event bodies are erased if acknowledgment
fails before ownership reaches the caller; URL bodies are protected while
constructing diagnostics. The contract does not erase every metadata copy in
generic protobuf/plist trees. The silent-feedback test now arms its fault after
startup; its 80 ms request deadline and 500 ms cleanup assertion remain intact.
See [pr-review.md](pr-review.md) for reproduction and exact scope.

## Validation evidence and its limits

At `ab7ec01`, Windows Release static/shared CTest passed **24/24 each**
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

Latest receiver-tested code head (D40):
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

PR #12's review process is complete and merged; no implementation blocker was
identified for that private experimental slice. The items below are follow-up
development, not retroactive gate failures.

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
2. **Startup reliability.** Spontaneous pause at zero/first-frame-only occurred
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
   changes, actual-file seek beyond 4 GiB and neutral sender identity (D30).
   The selected D42 network check is already PASS; broaden it rather than claiming
   it has never run. Cooperative duration-based ownership can misidentify a
   concurrent same-duration takeover and does not guarantee exclusive ownership.
6. **Public library/host work, next implementation (D49 order).** Follow
   [credential-interface.md](credential-interface.md): (a) **done in D50:**
   host-store callbacks and pairing in the C interface, with test stores on all
   CI platforms, and one real pairing into the built-in store (D51); (b) the
   C# binding and a packaged UWP test app with a C# `PasswordVault` host store,
   also proving native loading, discovery, brokered file reads and inbound
   serving, and measuring D49's unverified items; (c) a UWP library build without
   the Credential Manager adapter; (d) macOS Keychain and Linux Secret Service
   adapters, then Linux/macOS/Android device support. Botan UWP packaging remains
   unresolved; desktop/CI success is not packaged-host proof.
7. **Screenbox integration.** After the standalone gate, re-read that repository's
   current instructions and work in its dedicated fork. Test Chromecast regressions
   and local/remote handoff. This session changed no Screenbox source or project.

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

No hardware cast or implementation automation is left running at this checkpoint.
Inspect live PR/CI status in the new session. The user previously could observe the TV within 30 seconds and later
performed Ethernet removal/reconnection, but future availability must be checked.
Batch manual checkpoints if they are unavailable; unattended telemetry never
substitutes for visual video/audio/Home observations.

Never read raw credential files or print/store PINs, private media URLs, receiver
addresses/MACs/identifiers or Windows Credential Manager contents. Reuse stored
credentials through the native APIs. Do not delete/re-pair credentials or alter
receiver/host settings as an incidental diagnostic step. All hardware results need
exact code/binary, host/firmware and observer scope recorded separately from CI.

## Suggested opening prompt for the next session

> Continue send-airplay2 from AGENTS.md, docs/HANDOFF.md, docs/design.md,
> docs/receiver-validation.md and docs/CONTINUATION.md. PR #12 is merged; check
> `origin/main` and any open PR before writing. Start the next slice on a fresh
> branch from `main` with its own PR; ask me before merging. Preserve D42 PASS and
> its limits. Use the ordered follow-up queue and confirm the proposed next slice
> with me without reopening settled decisions.
