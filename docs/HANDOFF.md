# Project handoff: send-airplay2

Snapshot: 2026-10-06 (Asia/Tokyo). Audience: the next human developer or model.
Read this first, then [design.md](design.md) and
[receiver-validation.md](receiver-validation.md). This is a continuation record,
not a claim that the sender has been completed.

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

| Item | Snapshot |
|---|---|
| Active PR | [#1: feat: establish portable AirPlay sender foundation](https://github.com/ilyalissoboi/send-airplay2/pull/1) |
| PR state | Open, draft, unmerged |
| Target | `main` |
| Work branch | `feat/portable-foundation` |
| Original main commit | `8c77b15d391e14b53a3591eea7d0ac6e28376813` (LICENSE only) |
| Foundation commit | `dbd654b1d92057b3208953226186c5c2b206ccff` |
| Runtime dependencies | None in the foundation |
| Actual casting support | None yet |
| Receiver validation | Not run; no model/firmware combination certified |
| Screenbox changes | None; source audit only, no integration fork created in this session |

This handoff is added after the foundation commit. Resolve the current branch
head from GitHub rather than assuming the foundation SHA is still the PR head.
The PR branch is the source of truth until merged; checking out `main` alone
does not retrieve the implementation.

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

No external implementation code has been copied into this foundation. No crypto,
plist, discovery or audio dependency has been selected. Check each candidate's
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
| D05 | Begin with local H.264/AAC MP4 on one specified Apple TV model/firmware | Proposed first vertical slice; receiver details still needed |
| D06 | CLI plus minimal packaged Windows C# host before Screenbox changes | Planned validation gate; packaged networking/file/native-loading risks must be tested early |
| D07 | Receiver-side URL playback with local HTTP serving | Planned first media path; current code only resolves ranges |
| D08 | Media source size/read-at callbacks, not filesystem paths alone | Planned for Windows StorageFile and Android content URI access; callback ABI unfinished |
| D09 | Keep standalone audio transport separate from first MP4 proof | Planned; audio backend pending |
| D10 | AirPlay beside Chromecast through a provider-neutral boundary | Planned Screenbox integration; retain existing Chromecast behavior behind its adapter |
| D11 | No initial DRM, mirroring, system-audio capture, synchronized multiroom, auto-transcoding or universal codec/receiver support | Initial scope limits from assessment; unsupported media should fail explicitly |
| D12 | Preserve repository's Apache-2.0 license | Implemented; no relicensing decision made |
| D13 | Keep implementation reviewable on PR #1 | Implemented; no merge performed |
| D14 | Ignore unsupported multipart/invalid Range fields and serve full content | Implemented resolver policy; documented in design.md |

The C API is pre-1.0 and explicitly experimental. "Stable C ABI" is a target,
not a promise about the current header. Define versioning, ownership, threading,
cancellation and errors before publishing production bindings.

## 5. Implemented code and verification

| File | Purpose |
|---|---|
| `include/send_airplay2/http_range.h` | Experimental length-delimited C interface, fixed-width status values, offset/length result |
| `src/http_range.cpp` | Allocation-free single byte-range resolver, overflow checks and empty-file handling |
| `tests/range_tests.cpp` | Boundary/argument/buffer cases and generated interval tests |
| `tests/c_abi_smoke.c` | Compile/link/use from a C11 caller |
| `CMakeLists.txt` | C++17 static/shared library and CTest targets |
| `.github/workflows/build.yml` | Windows/Linux/macOS static/shared build-and-test matrix |
| `docs/design.md` | Architecture, range contract, integration audit and implementation gates |
| `docs/receiver-validation.md` | Blank per-platform/per-firmware hardware test record |

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
- GitHub CI: all six Windows/Linux/macOS × static/shared jobs pass, confirmed
  2026-10-06. [PR run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37409502023)
  and [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37409476906).
- Android NDK build/device, packaged Windows host and receiver tests: not run.

Reproduction from a fresh checkout:

```sh
git clone https://github.com/ilyalissoboi/send-airplay2.git
cd send-airplay2
git switch feat/portable-foundation
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

## 6. Screenbox integration findings

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

The milestone is unfinished. **Do not interpret missing hardware access as a
reason to stop all implementation.** Build and mock/unit-test independent pieces
while arranging the hardware baseline in parallel with that work.

1. **Hardware baseline:** ask for the available Apple TV model/generation and exact
   tvOS firmware/build, plus the intended local testing host. Obtain AirPlay access
   settings without collecting secrets. Establish reference playback with pyatv on
   that LAN and record the real session path. The cloud workspace cannot reach the
   user's receiver simply because GitHub is connected.
2. **Discovery + diagnostics:** add bounded service/TXT parsing, merge `_airplay`
   and `_raop` identities where justified, handle disappearance/interface changes,
   and expose a CLI. Select the discovery/platform strategy explicitly.
3. **Pairing + secure transport:** inspect the reference authentication code,
   select vetted crypto/serialization dependencies, specify persistent credentials
   and host storage, and test fragmentation, wrong PIN, authentication failure,
   counters/replay, timeouts and revocation before claiming interoperability.
4. **HTTP media server:** define size/read-at callbacks and cancellation/lifetime;
   integrate the existing resolver into bounded GET/HEAD serving. Select the LAN
   address reachable by the receiver, use per-session URLs, and test concurrent
   reads, >4-GiB positions, exact lengths, shutdown and unreachable callbacks.
5. **Session/playback:** implement authenticated setup, event/timing/feedback
   lifecycle, URL start, status, pause/resume, seek and stop. Treat receiver status
   and disconnects explicitly. Use the same MP4 as the reference baseline.
6. **Host proofs:** implement the CLI and packaged Windows C# sample; test native
   loading, brokered media access and inbound networking. Add macOS/Linux/Android
   device coverage and bindings. Record failures separately from build success.
7. **Screenbox:** after the standalone gate, create/use a dedicated fork, introduce
   the casting adapter boundary, route active-session controls, and test Chromecast
   regression and local/remote handoff.

Remaining design choices: discovery backend, crypto/plist libraries, socket/event
model, credential format/storage adapters, asynchronous C API and bindings,
timeouts/cancellation, capability policy, media-server access policy, unsupported
codec handling, Android packaging and audio transport. None is already implemented
or approved as a specific dependency choice. Keep these choices explicit in future
updates to this record.

## 8. Continuation mechanics and known obstacles

Public clone/read worked in this session. Command-line `git push` failed with
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
