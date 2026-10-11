# Design and implementation sequence

Implementation snapshot: 2026-10-11 (Asia/Tokyo), after PRs #1-#39 merged.
The C++17 core implements discovery, PIN pairing, authenticated transport,
URL/MRP playback and joined cleanup, the experimental C API version 3 and C#
binding, Windows/macOS/Linux credential stores, packaged UWP builds, and
MP4/MKV HLS remux with text subtitles and indexed MKV startup. The Screenbox
fork has implemented its current design through PRs #1-#7.

Recorded receiver checks cover one Apple TV 4K / tvOS 26.6 from Windows
desktop and packaged hosts, and one MacBook Pro on macOS 26.7.1 from desktop
C API and packaged UWP hosts using D62's URL controls fallback. D63 recognizes
its explicit HLS EOF reason. The user reports the private x64 Screenbox UI
checks passed; see the current handoff for scope and the loaded-DLL evidence
limitation. Screenbox's merged `main` pin (`ci.196`) predates D62; fork PR #8
pins published, hash-verified `ci.207`, whose final receiver checks remain
pending. Additional host, receiver and firmware interoperability remains unvalidated. Current work is
the C API/UWP/Screenbox Mac path; see [HANDOFF.md](HANDOFF.md#0-resume-here).

## Scope and architecture

Use a portable C++ core with the experimental versioned C ABI. Keep protocol sessions,
discovery, pairing and credential serialization separate from host integration.
C# adapts the same core for Screenbox; Android/JNI remains future work.

Media sources need read-at-offset and size callbacks, not just filesystem paths:
Windows brokered StorageFile access and Android content URIs must be supported.
The C API specifies callback ownership, lifetime, cancellation and thread rules
([public-api.md](public-api.md)). No exceptions or C++ objects cross that boundary.

Components and current state:

| Component | Responsibility | State |
|---|---|---|
| Discovery | mDNS/DNS-SD, service merging, TXT capability and identity parsing | Implemented bounded IPv4 scan; see discovery.md for limits and evidence |
| Pairing | PIN flow, authenticated peer verification, credential reuse | Private TLV8, PIN/SRP, peer verification, receiver I/O and Windows storage/CLI implemented; live enrollment/fresh socket passed, broader gates pending |
| Secure transport | Bounded framing, authenticated encryption, counters, timeouts | Private HTTP/RTSP/TCP and record integration implemented; see receiver-transport.md |
| Session | Setup/event/timing/feedback lifecycle and receiver error mapping | Private URL/MRP sessions and automatic ordered cleanup implemented; selected G3 checks and remaining manual gates recorded |
| Media server | GET/HEAD, byte sources, range responses, bounded streaming | Experimental Boost.Beast/Asio server implemented; loopback tested and Apple TV fetch observed; see media-server.md |
| Playback | URL start, pause/resume, seek, status, stop | URL/MRP and automatic terminal cleanup implemented; G1 passed; native EOF and ten short cycles passed; G2 passed by user report; remaining manual G3 checks pending |
| Public playback interface | Versioned C handle over session and media server | Experimental v3: playback, host stores/pairing and HLS delivery; C# binding implemented; recorded Apple TV checks passed through desktop and packaged hosts |
| Platform adapters | Networking, credentials, file access, host lifecycle | Windows Credential Manager, macOS Keychain, Linux Secret Service and host stores implemented; UWP native builds, PasswordVault and brokered StorageFile reads tested on the recorded Windows/Apple TV combination; other sender platforms remain unvalidated |
| HLS remux | MP4/MKV to fMP4 VOD with WebVTT renditions | H.264/HEVC, AAC/AC-3/E-AC-3, MKV and MP4 text subtitles, indexed MKV startup implemented; HDR signaling and growing presentations remain work |
| Audio transport | Separate RAOP/AirPlay audio path when required by scope | Deferred beyond first video proof |

An AirPlay 2-capable receiver accepting an older protocol path is not proof of
an AirPlay 2 session implementation. Record the negotiated path in test results.
The first MP4 contains video and audio; this does not establish standalone audio
transport support for speakers.

## Findings that affect the first vertical slice

The UxPlay wiki is a useful reference index, not a complete sender specification.
pyatv provides an executable sender reference. The originally inspected legacy
`/play` and `/playback-info` path failed on tvOS 26.6. The unmerged reference fix
validated type-130 `/command` queue start and event state. Native code implements
that URL flow and now retains the native remote-control SETUP/event session
that passed G1. Native MRP controls now extend that session; see
[mrp-controls.md](mrp-controls.md).
Use [session-design.md](session-design.md) for D27-D31 and the current sequence. Do not treat a successful
unauthenticated /play HTTP response as an interoperability result.

References inspected:

- https://github.com/FDH2/UxPlay/wiki/AirPlay2-protocol
- https://pyatv.dev/documentation/protocols/
- https://github.com/postlund/pyatv/blob/master/pyatv/protocols/raop/protocols/airplayv2.py
  (inspected blob 3d61a34e00559554477cd2ff97d9c337a9e7ccff)
- https://github.com/postlund/pyatv/blob/master/pyatv/protocols/airplay/player.py
  (inspected blob e2c073cb0ba3e2a1c29f3882db88ed257bd9e2ef)

The implementation references are evidence of an approach, not evidence that
this new library works with any firmware. Select crypto and serialization
dependencies after checking maintained platform support and licensing; record
attribution before incorporating any external implementation.
OpenSSL is now selected for the private control crypto adapter; see
[dependency provenance](dependencies.md) and [transport contracts](pairing-transport.md).
The private authentication slice now implements [peer verification](peer-verification.md)
using EVP X25519/Ed25519 and pinned identity credentials. Private
[PIN pairing](pin-pairing.md) uses Botan SRP through its C FFI, preserving C++17.
Private [receiver I/O](receiver-transport.md) and Windows desktop
[credential storage/CLI authentication](credential-storage.md) are implemented.
Broader hardware authentication and sender interoperability remain gates. OS store
adapters and UWP packaging are implemented; their evidence is recorded separately.

## HTTP range contract

The resolver consumes a field value after the HTTP parser has enforced request
and header size limits. It supports a single closed, open-ended or suffix range
over a representation with a known 64-bit size. It uses half-open output intervals
to avoid overflow when computing lengths. Units are case-insensitive.

| Result | Server action |
|---|---|
| FULL or IGNORED | 200, Content-Length equal to representation size |
| PARTIAL | 206, selected length, Content-Range `bytes first-last/size` |
| UNSATISFIABLE | 416, Content-Range `bytes */size` |
| INVALID_ARGUMENT | Internal caller error; do not start a response |

Malformed, overflowing, unknown-unit and multipart range values are deliberately
ignored and use a full response. An empty file has no satisfiable byte ranges.
Apply Range only to GET, and apply If-Range/preconditions in the future server
before calling this function. Do not advertise the resolver as full HTTP compliance.

## Screenbox integration audit

Historical upstream audit preceding integration. The fork has since implemented
its provider/session boundary, PasswordVault pairing, HLS casting, queue and
system media controls, local handoff and cosmetic follow-ups (PRs #1-#7).
Its pinned package predates D62; extending its path to the Mac is active work.
Reinspect the fork's current instructions and design before editing it.

Inspected main commit: 46aadf6b20ef5d8348a6049c16c882c22ec0f84e.

- `Screenbox.Core/Services/CastService.cs` accepts only VlcMediaPlayer for discovery
  and calls VlcPlayer.SetRenderer for selection.
- `Screenbox.Core/Services/ICastService.cs` returns the concrete RendererWatcher.
- `Screenbox.Core/Models/Renderer.cs` wraps LibVLC RendererItem.
- `Screenbox/Package.appxmanifest` already declares privateNetworkClientServer.
  That alone does not prove discovery, inbound serving or native DLL loading works
  in a packaged app.

Consequently, introduce a provider-neutral device/session boundary and adapt the
existing LibVLC behavior behind it. AirPlay owns its media server and remote
session. Route transport controls and displayed position to that active session;
do not keep local playback running concurrently. Preserve the local item and
position for handoff and disconnect recovery. UI and lifetime changes require
inspection of RendererWatcher, CastContext, CastControlViewModel and playback
coordination before modifying Screenbox.

D53's packaged test host measured the network side: Screenbox's
`privateNetworkClientServer` lets the receiver reach the app's media server on a
Private network but not on a Public one, where `internetClientServer` is needed
([uwp-host.md](uwp-host.md)). Choosing that capability is a decision for the fork.
D54 added a C discovery snapshot (`receivers.h`) that a RendererWatcher-style
AirPlay source can poll. Its scan found the receiver inside the same
AppContainer with Screenbox's capabilities on a Private network.
D55 builds the library for app packages ([uwp-native-build.md](uwp-native-build.md)):
app C runtime, AppContainer flag, no Credential Manager, Botan static. The fork
should package that build, supply a `PasswordVault` store and keep Asio's
`thread_pool` out of app builds (D55 found it aborts on join there).

Screenbox's current agent instructions require Visual Studio 2026 MSBuild for UWP
builds. A desktop C# console success is insufficient: validate native loading,
brokered file access and inbound network serving in a packaged UWP host early.

## Ordered next changes and acceptance gates

Current gate: validate the published, hash-verified `0.3.0-ci.207` package in
Screenbox and review the fork's version/hash pin PR #8. D63 completed the C API
and packaged UWP Mac checks; D64 published the verified main package with the
user's approval. Earlier private Screenbox observations predate the review fix.
Mac consent research, fork regression/architecture checks,
MRP track/queue research, HDR/growing HLS, nuget.org and additional host/receiver
proofs remain choices in [HANDOFF.md section 0](HANDOFF.md#0-resume-here).
Remote-Stop classification is deferred by the user.

The following first-slice sequence is historical, retained to explain its gates;
it is not the current development queue.

The reference baseline, Windows pairing/reuse, media fetch and private URL
session are implemented or observed as recorded in receiver-validation.md.
The minimal native remote-control-only SETUP/event session is implemented and
passed G1 without RECORD or a data stream. MRP framing, handshake and controls
now extend that retained session. D29 requires MRP controls, and D31 selects the
in-tree bounded protobuf codec regardless of that minimum experiment's result.

1. G1 passed for the recorded native-only run; retain the independently verified
   remote session through URL teardown. Broader presentation proof is separate.
2. MRP framing, message mapping, handshake, correlation and heartbeat are implemented;
   native status/control telemetry passed; G2 visual confirmation passed by user report on 2026-10-07.
3. Automatic terminal cleanup is implemented (D33); native EOF and ten short
   start/stop cycles passed, as did user-confirmed sleep/wake recovery. Receiver
   remote stop returned home and cleaned up but classified connection_lost/exit 1.
   Extend G3 beyond the selected D42 network-loss/fresh-recovery pass; investigate
   normal stop reason and buffering. D34 diagnostics reproduce URL event EOF during user-confirmed
   remote Stop without a terminal state; retain failure classification until
   intent is validated. One full clip reached natural EOF, but the user observed
   video frozen after a buffering stop near 18 s while audio continued normally
   through clip end. Preserve this historical failure; playing telemetry and
   cleanup success do not prove moving video. D35 diagnostics now correlate
   stalled read-ahead requests occupying four slots with buffering. Minimal/four
   and MRP/16 comparisons passed full video/audio/Home; a default MRP/four repeat
   buffered but recovered. D36 passed user-observed controls, ten fresh-process
   stop/teardown cycles and a natural-EOF repeat at 16 slots; normal `cast` now
   defaults to 16 (override 1..16), while generic server/`serve` defaults stay four.
   Another 16-slot run paused at zero without transport commands or remote input;
   investigate startup ordering/state before further reliability claims. Per user
   decision D37, frozen video is low priority for now, with insufficient media
   connections the likely cause; raise priority if it recurs in later testing.
   D38 adds bounded startup traces and a one-second eligible-playing confirmation
   within the original deadline, with synthetic interruption/cancellation tests.
   This improves readiness reporting; the physical startup pause remained unresolved
   until D56: it was observed while the receiver was waking from sleep (user
   report), and waking it over MRP before play fixed that case in three sleep cycles.
   D39 completed the [manual batch](manual-validation.md): startup, controls/full
   EOF and post-wake presentation/Home passed. Remote Stop/Home and automatic
   cleanup passed, but protocol intent still reports connection_lost/exit 1.
   Startup pause and longer reliability remain active work.
   D40 adds fixed-label remote-event observations and retained final MRP
   received-state diagnostics after cleanup. They do not change classification;
   see the separate [validation record](receiver-validation.md) for the observed
   Stop/sleep comparison and the unobserved first attempt.
   D41's [pyatv audit](pyatv-stop-reference.md) found no validated Stop discriminator.
   D42 subsequently passed selected Ethernet interruption cleanup and fresh
   explicit same-credential playback after reconnection/Home, with user-confirmed
   video/audio/Home at near-end EOF. Connection failures retain their existing
   classification; normal Stop intent, startup pause and longer reliability remain open.
   D43's [PR review](pr-review.md) fixed arbitrary peer text in URL diagnostics
   and exception-path erasure of decoded MRP payloads. Static/shared and offline
   checks passed; this follow-up has no new hardware observation.
4. Complete hardware authentication/restart/revocation and discovery/interface
   checks, real-file >4-GiB seeking and neutral sender-identity validation (D30).
5. The versioned C API, C# binding, packaged Windows loading/brokered access,
   inbound networking and built-in credential stores are implemented (D46-D61).
   Linux/macOS/Android sender interoperability remains unvalidated; CI is separate
   from device evidence.
6. Screenbox integration is implemented in the fork. Chromecast regression and
   additional device checks remain; reinspect its instructions before changes.

Each implementation slice follows AGENTS.md: readable C++17, secret/resource RAII,
format checks, static/shared CMake/CTest and CI at the actual PR head. No automatic
re-pairing or credential deletion. Capture only sanitized receiver results.
