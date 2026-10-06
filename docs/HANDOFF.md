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
| Foundation PR | [#1: feat: establish portable AirPlay sender foundation](https://github.com/ilyalissoboi/send-airplay2/pull/1) |
| Foundation PR state | Merged on 2026-10-06; verified through GitHub CLI |
| Current discovery PR | [#2: feat: add receiver discovery and diagnostic CLI](https://github.com/ilyalissoboi/send-airplay2/pull/2), open |
| Target | `main` |
| Current local branch | `codex/receiver-discovery`, based on merged `main` at `c61955f5074182e97b2828416175cce42763cc5f` |
| Discovery implementation commit | `48189897ba167b44c3da7c6e4a7857bf28120498`; subsequent handoff/CI evidence commits may follow |
| Final PR head | `034983ca095fd0803d3de2307c6d27fdf18db488` on `feat/portable-foundation` |
| Original main commit | `8c77b15d391e14b53a3591eea7d0ac6e28376813` (LICENSE only) |
| Foundation commit | `dbd654b1d92057b3208953226186c5c2b206ccff` |
| Runtime dependencies | No third-party libraries; native discovery uses OS socket/interface APIs |
| Actual casting support | None yet |
| Receiver validation | Windows LAN discovery observed for `AppleTV14,1` advertising OS 26.6 and `Mac14,2`; pairing/playback not run, no compatibility certification |
| Screenbox changes | None; source audit only, no integration fork created in this session |

PR #1 is now merged and `main` contains the foundation. Verify current GitHub and
local branch state before further development; the original foundation SHA is
not the final PR head.

### Local testing environment

User-provided on 2026-10-06 (Asia/Tokyo):

- Receiver: **Apple TV 4K**; subsequent LAN discovery advertised `AppleTV14,1`
  and name "Living Room". Generation not independently established.
- Firmware: **tvOS 26.6** (user-reported, consistent with advertised `osvers`/`ov`);
  exact build not yet supplied. AirPlay `srcvers` is not a tvOS build identifier.
- Intended testing host: **Windows 11 x64**; exact OS build not yet supplied.

AirPlay access settings, detailed network configuration and reference playback
results remain pending. Discovery resolved the receiver; playback compatibility
is untested.
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
uses in-tree native sockets, documented in [discovery.md](discovery.md). No crypto,
plist or audio dependency has been selected. Check each candidate's
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
| D05 | Begin with local H.264/AAC MP4 on one specified Apple TV model/firmware | Proposed first vertical slice; user supplied Apple TV 4K / tvOS 26.6 / Windows 11 x64; generation and exact builds pending |
| D06 | CLI plus minimal packaged Windows C# host before Screenbox changes | Planned validation gate; packaged networking/file/native-loading risks must be tested early |
| D07 | Receiver-side URL playback with local HTTP serving | Planned first media path; current code only resolves ranges |
| D08 | Media source size/read-at callbacks, not filesystem paths alone | Planned for Windows StorageFile and Android content URI access; callback ABI unfinished |
| D09 | Keep standalone audio transport separate from first MP4 proof | Planned; audio backend pending |
| D10 | AirPlay beside Chromecast through a provider-neutral boundary | Planned Screenbox integration; retain existing Chromecast behavior behind its adapter |
| D11 | No initial DRM, mirroring, system-audio capture, synchronized multiroom, auto-transcoding or universal codec/receiver support | Initial scope limits from assessment; unsupported media should fail explicitly |
| D12 | Preserve repository's Apache-2.0 license | Implemented; no relicensing decision made |
| D13 | Keep implementation reviewable on PR #1 | Foundation reviewed through PR #1; now merged |
| D14 | Ignore unsupported multipart/invalid Range fields and serve full content | Implemented resolver policy; documented in design.md |
| D15 | Bounded native IPv4 mDNS scan, portable DNS-SD cache/parser, no new third-party runtime dependency | Implemented; adapter isolated, IPv6-only discovery and Android device validation pending |
| D16 | Keep protocol-specific records and merge only by matching normalized advertised device identity | Implemented; hostname/friendly name alone is insufficient, advertisements remain unauthenticated |
| D17 | Experimental C++ discovery API plus CLI before versioned C discovery ABI/event API | Implemented; shared users require compatible C++ runtime, production bindings still pending |

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
| `docs/receiver-validation.md` | Per-platform/per-firmware hardware test record; discovery observed, pairing/playback not run |
| `include/send_airplay2/discovery.h` | Experimental synchronous C++ discovery records/options/result API |
| `src/discovery*.cpp` / internal headers | Bounded DNS-SD parser/cache, scan scheduler and native socket adapter |
| `src/cli.cpp` | `airplay2-cli discover`, readable and JSON diagnostics |
| `tests/discovery_tests.cpp` / CLI fixtures | Synthetic parser/lifecycle/query tests, mutation corpus and CLI output validation |
| `docs/discovery.md` | Discovery contract, adapter limits, source provenance and local LAN observations |

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
- Discovery CI runs (Windows/Linux/macOS static/shared plus Linux ASan/UBSan)
  are in progress; results must be verified independently of the foundation runs.

Reproduction from a fresh checkout:

```sh
git clone https://github.com/ilyalissoboi/send-airplay2.git
cd send-airplay2
git switch main
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

1. **Hardware baseline:** use the user-provided Apple TV 4K / tvOS 26.6 /
   Windows 11 x64 setup (discovery model `AppleTV14,1`). Obtain the exact
   tvOS build and AirPlay access settings without collecting secrets. Establish
   reference playback with pyatv on that LAN and record the real session path.
   Local discovery succeeded; reference and native playback have not been checked.
2. **Discovery + diagnostics:** implemented on this branch. Complete real receiver
   departure/interface-change checks and other platform/Android host coverage.
   The Windows Apple TV discovery gate has passed; see discovery.md for limits.
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

Remaining design choices: production OS discovery fallback/IPv6-only backend,
crypto/plist libraries, session socket/event
model, credential format/storage adapters, asynchronous C API and bindings,
timeouts/cancellation, capability policy, media-server access policy, unsupported
codec handling, Android packaging and audio transport. None is already implemented
or approved as a specific dependency choice. Keep these choices explicit in future
updates to this record.

## 8. Continuation mechanics and known obstacles

Current local environment, verified on 2026-10-06: GitHub CLI 2.102.0 is available
at `C:\Program Files\GitHub CLI\gh.exe`, authenticated as `ilyalissoboi` through
the keyring. A GitHub API read confirmed push and admin permissions for this
repository. Network verification required execution outside the sandbox; the
initial sandbox authentication error was not evidence of invalid credentials.
An authenticated command-line Git push has not been tested in this local chat.

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
