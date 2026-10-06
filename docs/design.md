# Design and implementation sequence

Research snapshot: 2026-10-06. This document distinguishes proposed architecture
from implemented behavior. HTTP single-byte-range resolution and bounded
mDNS/DNS-SD discovery with a diagnostic CLI are implemented.

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
| Pairing | PIN flow, authenticated peer verification, credential reuse | Pending |
| Secure transport | Bounded framing, authenticated encryption, counters, timeouts | Pending |
| Session | Setup/event/timing/feedback lifecycle and receiver error mapping | Pending |
| Media server | GET/HEAD, byte sources, range responses, bounded streaming | Range resolver only |
| Playback | URL start, pause/resume, seek, status, stop | Pending |
| Platform adapters | Networking, credentials, file access, host lifecycle | Pending |
| Audio transport | Separate RAOP/AirPlay audio path when required by scope | Deferred beyond first video proof |

An AirPlay 2-capable receiver accepting an older protocol path is not proof of
an AirPlay 2 session implementation. Record the negotiated path in test results.
The first MP4 contains video and audio; this does not establish standalone audio
transport support for speakers.

## Findings that affect the first vertical slice

The UxPlay wiki is a useful reference index, not a complete sender specification.
pyatv provides an executable sender reference. Its inspected AirPlayV2.play_url
implementation verifies the connection, establishes a base RTSP session and event
channel, starts feedback, sends RECORD, posts a binary-plist /play body, and sets
the playback rate. Its player polls /playback-info. Do not treat a successful
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

Screenbox's current agent instructions require Visual Studio 2026 MSBuild for UWP
builds. A desktop C# console success is insufficient: validate native loading,
brokered file access and inbound network serving in a packaged UWP host early.

## Ordered next changes and acceptance gates

Discovery and diagnostic CLI are now implemented. Windows discovery resolved
the user's "Living Room" (`AppleTV14,1`, advertised OS 26.6); pairing/playback and
real departure/interface-change checks remain pending. See
[discovery.md](discovery.md) for the adapter choice, provenance, API and test limits.
The next implementation slice is pairing and authenticated transport, while
establishing the pyatv playback baseline on this receiver.

1. Establish receiver baseline with an existing sender (pyatv) on the user's LAN.
   Record model, exact firmware/build and PIN/access settings. Test the same
   unprotected MP4 and document the actual session path. Keep credentials private.
2. Implement discovery and a diagnostic CLI; bounded parsers must reject malformed
   records, merge duplicate services and handle device departure/interface changes.
3. Implement pairing and secure transport using vetted crypto primitives, with
   transcript fixtures and independent known-answer tests. Test wrong PIN, bad
   signatures/tags, fragmented reads, replay/counter errors and credential revocation.
4. Implement local media serving with byte-source callbacks, request limits,
   unpredictable per-session URLs, exact HEAD/GET lengths and cancellation.
   Test a file larger than 4 GiB, concurrent reads and unreachable callback addresses.
5. Complete authenticated URL playback and control; validate session lifecycle,
   receiver-initiated stop, timeouts, network loss and repeat casting on hardware.
6. Prove the packaged Windows C# host, then Linux/macOS/Android hosts. Only claim
   individual tested combinations; publish CI results separately from device results.
7. Integrate Screenbox in a dedicated fork with Chromecast regression coverage.

The original cloud workspace did not run receiver tests. Local Windows LAN
discovery was observed on 2026-10-06. GitHub access is separate from LAN access;
hardware playback sign-off still requires reference and native sender results.
