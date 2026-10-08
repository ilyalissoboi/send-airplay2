# Design and implementation sequence

Implementation snapshot: 2026-10-07 (Asia/Tokyo), after PR #12 merged into `main`. This document distinguishes proposed architecture
from implemented behavior. HTTP single-byte-range resolution and bounded
mDNS/DNS-SD discovery with a diagnostic CLI are implemented. Private pairing TLV8,
HKDF-SHA512 and authenticated control-record codecs are implemented, along with
private peer verification and PIN/SRP provisioning message processing. Bounded
HTTP/RTSP framing and native TCP now connect these flows privately; authentication
is tested with synthetic receivers and loopback I/O. Windows desktop credential
storage and CLI authentication are implemented. The user confirmed live PIN
pairing, built-in fresh-socket verification and separate-process credential reload
on Apple TV 4K / tvOS 26.6 after
the M6 metadata compatibility fix. Private channel keys, event I/O, timing,
session messages and the URL session/`cast` CLI are implemented. Standalone G1
initially failed. The minimal native remote-control SETUP/event session then
passed G1: user-observed video/audio and home-screen return after sender shutdown.
Broader authentication, native control validation and host gates remain.

## Scope and architecture

Use a portable C++ core with an eventual versioned C ABI. Keep protocol sessions,
discovery, pairing and credential serialization separate from host integration.
C# and JNI wrappers will adapt the same core for Screenbox and Android.

Media sources need read-at-offset and size callbacks, not just filesystem paths:
Windows brokered StorageFile access and Android content URIs must be supported.
Specify callback ownership, lifetime, cancellation and thread rules before exposing
them in a public ABI. No exceptions or C++ objects should cross that boundary.

Proposed components:

| Component | Responsibility | State |
|---|---|---|
| Discovery | mDNS/DNS-SD, service merging, TXT capability and identity parsing | Implemented bounded IPv4 scan; see discovery.md for limits and evidence |
| Pairing | PIN flow, authenticated peer verification, credential reuse | Private TLV8, PIN/SRP, peer verification, receiver I/O and Windows storage/CLI implemented; live enrollment/fresh socket passed, broader gates pending |
| Secure transport | Bounded framing, authenticated encryption, counters, timeouts | Private HTTP/RTSP/TCP and record integration implemented; see receiver-transport.md |
| Session | Setup/event/timing/feedback lifecycle and receiver error mapping | Private URL/MRP sessions and automatic ordered cleanup implemented; selected G3 checks and remaining manual gates recorded |
| Media server | GET/HEAD, byte sources, range responses, bounded streaming | Experimental Boost.Beast/Asio server implemented; loopback tested and Apple TV fetch observed; see media-server.md |
| Playback | URL start, pause/resume, seek, status, stop | URL/MRP and automatic terminal cleanup implemented; G1 passed; native EOF and ten short cycles passed; G2 passed by user report; remaining manual G3 checks pending |
| Public playback interface | Versioned C handle over session and media server | Experimental v1 implemented (D46, [public-api.md](public-api.md)); manual plan passed on one receiver/host (D48) |
| Platform adapters | Networking, credentials, file access, host lifecycle | Desktop native networking and Windows credentials implemented; common credential design (built-in plus host-provided stores) decided in D49, [credential-interface.md](credential-interface.md); other stores, packaged hosts and media access pending |
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
Broader hardware authentication, other OS store adapters and UWP packaging remain gates.

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

Screenbox's current agent instructions require Visual Studio 2026 MSBuild for UWP
builds. A desktop C# console success is insufficient: validate native loading,
brokered file access and inbound network serving in a packaged UWP host early.

## Ordered next changes and acceptance gates

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
   report), and waking it over MRP before play fixed that case in one run.
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
5. Expose the versioned session API and bindings. The experimental C playback
   interface ([public-api.md](public-api.md), D46) is implemented and unit-tested,
   and passed its manual plan on one receiver/host (D48); bindings remain. Prove packaged Windows C#
   loading, brokered file access and inbound networking, then Linux/macOS/Android
   device support and other credential stores. CI is separate from device evidence.
6. After the standalone gate, integrate Screenbox in a dedicated fork with
   Chromecast regression and local/remote handoff coverage. Reinspect its instructions.

Each implementation slice follows AGENTS.md: readable C++17, secret/resource RAII,
format checks, static/shared CMake/CTest and CI at the actual PR head. No automatic
re-pairing or credential deletion. Capture only sanitized receiver results.
