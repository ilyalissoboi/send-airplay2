# Design and implementation sequence

Implementation snapshot: 2026-10-16 (Asia/Tokyo), after PRs #1-#73 merged.
The C++17 core implements discovery, PIN pairing, authenticated transport,
URL/MRP playback and joined cleanup, the experimental C API version 5 and C#
binding, Windows/macOS/Linux credential stores, packaged UWP builds, and
MP4/MKV HLS remux with text subtitles and indexed MKV startup. The Screenbox
fork has implemented its current design through PRs #1-#10, including the Mac
package pin, final validation record and Forget-device follow-up; PR #11 adds
the research checkpoint and PR #12 the next-slice planning checkpoint, without
changing runtime behavior.

**D92 HDR startup correction:** after fork #13 merged, a private long HEVC/DTS
cast reproduces a Mac grey screen. Offline preparation passes; declaring the
missing `VIDEO-RANGE=PQ` alone restores playback by user observation. The
[bounded SPS VUI correction](hevc-hdr-range.md) now carries PQ/HLG through remux
and automatic composition, including an HDR master without subtitles. The
private Screenbox override passes the user's Mac controls. A separate reported
Apple TV Screenbox failure and premature UWP `MediaEnd` remain unexplained;
native playback and visible-window 90-second UWP/Screenbox retries pass.
Final loaded hashes and Stop/cache cleanup pass. Published ci.282, audio policy,
decoder coverage and cache quota are unchanged; no full-film claim is made.

**D74 merged (PR #55, `774198d`):** [explicit stereo AAC playback integration](stereo-aac-playback.md)
extends the core to API 4 with original track probe/selection,
host fragment storage, binding/CLI options and terminal conversion supervision.
Its backend defaults OFF. **D75 merged (PR #56, `02d8be6`, x64 gate passed 2026-10-14):** [AppContainer runtime/storage gate](stereo-aac-uwp-runtime.md). D73's authoring PR #54 is merged; preserve its known
estimate limitations. D75's recorded x64 AppContainer source/cache checks pass;
real-file performance and packaged receiver playback remain later gates. This branch adds no package or Screenbox pin.

**D91 recorded host gate passed:** [Screenbox checkpoint](screenbox-automatic-audio-integration.md)
records fork PR #13's explicit x64 build option, default-off stereo permission,
finite per-cast app storage and serial drain/admission. Both Core/MSIX profiles,
113/127 logic suites and packaged payload controls pass. Version 1.0.0.9 adds
initial-start Cancel and passes AppContainer/six pinned loaded hashes plus user
Mac consent cancellation with paused/responsive handoff. The user confirms copied
and converted controls/Stop on both receivers, DTS refusal while off, natural and
rapid queue changes and transition Stop. Sampled/final caches are empty; conversion
is restored off/local paused. Inspect final PR heads/CI before merging. Ordinary ci.207 and
published package bytes remain unchanged.

**D90 published, public download verified:** [clean-main package](opt-in-audio-release-candidate.md)
`0.5.0-ci.282` uses main `e6acba1`; all 17 checks in run `38836333166` and local
artifact/consumer checks pass. The user approved publication; the GitHub prerelease
and sidecar download match the approved hash. A separate
[Screenbox integration proposal](screenbox-automatic-audio-plan.md) remains the
suggested host slice; no fork implementation or pin change is included.

**D89 merged (PR #70, `e6acba1`):** [opt-in runtime packaging](opt-in-audio-runtime-package.md)
adds a separate x64 UWP codec package with source/notice/provenance and consumer
guards. Default package, backend selection and Screenbox/publication boundaries
remain unchanged. No new receiver observation follows from these artifact checks.
All 17 checks passed at final head `4c4bfb8`, including the transitive profile
conflict regression. D90 above records the candidate and Screenbox proposal.

**D88 recorded gate complete (PR #69):** [automatic audio receiver gate](automatic-audio-receiver-gate.md)
adds original visible/audible controls and script-only automatic/stereo delivery
to the private UWP test host. Native AAC copy/DTS conversion pass on the Mac and
Apple TV; packaged nine-format matrices pass on both, with Stop/cancellation
observations and runtime hashes/token/cleanup evidence as recorded. The initial
Apple TV AAC disconnect and consent rejections remain distinct from retries.
Core policy/defaults, publication and Screenbox boundaries remain unchanged.

**D87 merged (PR #68, `f6c85ef`):** [bounded DTS seek coordinates](matroska-dts-bounded-seek.md)
use the nearer already parsed audio/video index with a maximum-lace backoff,
retaining selected audio and all existing budgets. Owned virtual read-amplification,
fixed-offset PCM/EOF and refusal controls pass. Package 0.1.0.8 at `731db15` passes
bundled/folder-granted 137-case matrices with loaded hashes verified. Three fresh
brokered first/middle/last owners pass in 138/314/64 ms cold with identical cached
repeats and cleanup, under unchanged read limits. Manual copied/converted receiver
checks follow; this is bounded one-input sampling, not whole-film playback proof.

**D86 merged (PR #67, `8a284ad`):** [DTS lace sample clocks](matroska-dts-frame-clock.md)
reproduce the frame-grid refusal with owned laces and count decoded continuation
samples within a known block. Existing tolerances remain at each new block;
gap-refusal and exact PCM/AAC controls pass. Bundled/folder-granted 137-case
matrices pass in private package 0.1.0.7 with loaded hashes verified. The brokered
video's first fragment takes 207 ms cold / 10 ms cached, but the middle request
refuses after 256 MiB requested source bytes; the last is not attempted. Overall
MEASURED_REFUSAL. Owned middle-seek/read amplification investigation is next,
preserving limits before repeating performance and receiver gates.

**D85 merged (PR #66, `1fe9f61`):** [Matroska presentation origins](matroska-presentation-origin.md)
rebase private converted video/text and source audio windows together. Native
timing/seek/EOF controls and bundled/folder-granted 126-case matrices pass in
private package 0.1.0.6 with loaded hashes verified. The brokered private video
passes its origin check, then refuses at the existing canonical DTS frame-grid
guard. Owned reproduction/phase investigation follows before repeating conversion
performance and receiver gates; source corruption has not been established.

**D84 merged (PR #65, `76abc3c`):** [expanded automatic AppContainer and bounded performance checks](automatic-audio-uwp-runtime.md)
pass the bundled and folder-granted 115-case/53-fixture matrices in private
package 0.1.0.5 with token/module hashes and storage cleanup verified. Runtime
DLL staging now follows the selected codec import libraries. The authorized
private video refuses at the zero-origin-video guard before fragment generation;
real-file conversion throughput remains open. Support for a bounded nonzero
Matroska presentation origin is the next proposed slice, before repeating
brokered performance and copied/converted receiver gates.

**D83 merged (PR #64, `2bdd397`):** [API 5 / offline checks / binding / CLI](automatic-audio-public.md)
exposes opt-in named-profile automatic audio with preservation choice and retained
decision reporting, preserving old prefixes/defaults. Copy/video-only need no
backend/cache; only accepted conversion clears storage. Offline checks borrow
source/cache and discard resources without credentials or receiver access.
Expanded packaged-runtime, performance and receiver gates remain next.

**D82 merged (PR #63, `6788583`):** [original-ID selected copy/conversion preparation](automatic-audio-policy.md)
adds a finite Apple HLS v1 copy table and bounded existing stereo conversion.
Copy/video-only work without the backend/cache; preservation refuses conversion.
No configuration/source/cache/network failure triggers a different track or route.
D83 above implements public integration/reporting; later packaged,
real-file/brokered performance and receiver gates. No package or Screenbox change.

**D81 merged (PR #62, `c69543a`):** [streamed packets and sparse restart anchors](matroska-compact-index.md)
replace full canonical audio packet tables. Recorded one-hour TrueHD/two-hour Opus
controls exceed the old packet cap with bounded retained memory; source reads,
callbacks, deadlines and decode budgets remain. This is synthetic offline evidence.
Selected copy/remux eligibility and automatic policy are next, followed by public
reporting, real-file/brokered performance, expanded packaged runtime and receivers.

**D80 merged (PR #61, `00c845d`):** [compressed durations and restart coordinates](matroska-compressed-index.md)
extend the native path to FLAC/Opus/Vorbis/TrueHD. The ten-second movie horizon is
removed within the bounded subset; D81 above replaces the packet-table scaling limit. Original absolute precision and decoded-frame
continuity have distinct tolerances. No public API, package or receiver result.

**D79 merged (PR #60, `531ff58`):** [selected PCM packets and exact long-file seeking](matroska-pcm-index.md)
remove the PCM movie horizon within the indexed subset. D80 extends this to
compressed durations/restarts; D81 adds compact anchors before automatic policy.

**D78 merged (PR #59, `9763189`):** [bounded coarse-clock reconstruction](coarse-matroska-audio.md)
counts decoded samples from fresh start-window owners for short variable-frame
Matroska presentations. The explicit ten-second horizon stays until bounded
long-file indexing/phase/scaling gates pass. Automatic policy and release follow.

**D77 merged (PR #58, `820e89c`):** [the private decoder/profile foundation](audio-decoder-expansion.md)
adds the documented FLAC/Opus/Vorbis/PCM/TrueHD matrix with owned offline
seeking/trim/AAC checks. D78 extends coarse Matroska timing in its short-file scope;
long-file indexing is next. Automatic policy/public reporting, expanded
packaged/runtime and receiver gates remain required.

**D76 user scope (merged PR #57):** automatic audio policy plus FLAC, Opus, Vorbis, PCM and
TrueHD, detailed in [the expanded plan](automatic-audio-transcoding-plan.md).
Proposed opt-in delivery resolves the original selected track, copies eligible
AAC/AC-3/E-AC-3 or converts validated inputs to stereo AAC. Eligibility belongs
to an explicit delivery profile, not discovery-derived receiver codec guesses.
The decoder/timeline foundation is next; no automatic mode or new decoder is
implemented by this plan. Audio-only/surround-output hardware deferrals persist.

Recorded receiver checks cover one Apple TV 4K / tvOS 26.6 from Windows
desktop and packaged hosts, and one MacBook Pro on macOS 26.7.1 from desktop
C API and packaged UWP hosts using D62's URL controls fallback. D63 recognizes
its explicit HLS EOF reason. The user reports the private x64 Screenbox UI
checks passed; see the current handoff for scope and the loaded-DLL evidence
limitation. Screenbox's merged `main` pin is published, hash-verified `ci.207`;
its running DLL hashes match that package. Final Screenbox cancellation/recovery,
playback/controls, Stop/local handoff and natural queue advance passed by user
report. Additional host, receiver and firmware interoperability remains
unvalidated. D65's initial consent investigation motivates a grant-age/expiry
hypothesis without establishing per-run H/J UI prompt attribution, a numeric
timer or cache key. The synthetic
name comparison and fresh random session identifiers retained short-term reuse;
no shipping sender-policy change is proposed. D65's research evidence and D66's
Screenbox follow-up and subsequent research checkpoint are merged;
see [HANDOFF.md](HANDOFF.md#0-resume-here).

The [audio-feature assessment](audio-features-assessment.md) records the
speaker-inclusive audio-file and DTS 5.1/7.1 research. D67 defers those
implementations for lack of suitable local test hardware and selects
[video with surround audio downmixed to stereo AAC](stereo-aac-casting-plan.md)
as the next slice. Planning PR #44 merged as `12c24a7` after all 14 checks passed
at `bc4b381`, including the resolved explicit-track-selection review.
The plan proposes an optional decoder/encoder,
separate stereo audio rendition and bounded cache while copying supported video.
Existing delivery defaults remain unchanged. This is planning authorization;
no backend is added or receiver support established by the documentation.

D68's subsequent authorized [codec/packaging spike](stereo-aac-codec-spike.md)
merged in PR #46 as `cb9d641` after all 16 checks passed at `579bb44`. It
adds a separate optional development DLL and offline packaged host. Existing
casting delivery and published packages are unchanged. Native HLS conversion
and receiver proof remain next gates; D69's selection foundations are below.
Offline PCM window tests are not evidence of complete movie seeking or receiver
compatibility.

D69's subsequent Proceed authorizes a private [source inventory/selection and
seek-metadata increment](audio-source-inventory.md), merged in PR #48 as `5d46479`
after all 16 checks passed at final head `c816709`. It separates supported-video
indexing from audio remuxability and preserves original audio identities/clocks
before decoder filtering. D70's subsequent authorized private
[decoder adapter](audio-decoder-source.md) matches those identities to actual
streams and tests compressed-input seek windows on edited timestamps, including
resampler phase and format changes; PR #50 merged as `65bb8a7` with 16 passing
checks at `8b1adf2`. D71 now implements private stereo HLS audio/cache in its
offline scope, merged in PR #51 with PR #52 correcting declaration scope.
D72 adds [native readiness](stereo-aac-readiness.md) checks; authoring, real-file
performance, explicit public, packaged and receiver gates remain next.

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
| Audio transport | Separate RAOP/AirPlay audio path for audio-only speakers | D67 deferred: suitable test hardware unavailable |
| Audio conversion | Surround source audio to stereo AAC alongside copied video | D68 codec spike and D69 source foundations merged; D70 selected-stream/seek-window adapter implemented in offline scope; D71 private stereo AAC HLS/cache implemented in offline scope; public/packaged/receiver gates remain |

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

Current queue: [D79's remaining compressed Matroska indexing gate](matroska-pcm-index.md),
selected remux/policy, then versioned public integration. Repeat packaged runtime
checks for the expanded matrix; real-file performance and converted-receiver
gates remain required for the existing stereo path and its expansion.

The published, hash-verified `0.3.0-ci.207` package passed final Screenbox checks
by user report with running DLLs verified. Final evidence in native PR #41 and
fork PR #9 is merged. The fork's version/hash pin PR #8 is merged. D63 completed the C API
and packaged UWP Mac checks; D64 published the verified main package with the
user's approval. Earlier private Screenbox observations predate the review fix.
D65 completed an initial controlled Mac consent study; its
[evidence and limits](macos-receiver-consent.md) do not justify a sender change.
The user selected D66, "Forget this AirPlay device" in Screenbox: remove only
the selected local pairing, offer Pair on success and retain paired state on
storage failure. Logic, x64 packaged build and forget → pair → cast UI validation
passed for the recorded Mac, with loaded binaries verified; fork PR #10 merged
as `8bfd3dd9`. D65 evidence PR #42 merged as `8a40ec3`. Other-device preservation was checked through
paired UI state, not a new Apple TV cast; no Chromecast playback regression is claimed.
Deeper grant research, fork regression/architecture checks,
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
6. Screenbox integration is implemented in the fork. Chromecast video regression
   is deferred for unavailable hardware; sleeping Apple TV casting is already
   verified by user report. Additional architecture/receiver checks remain;
   reinspect its instructions before changes.

Each implementation slice follows AGENTS.md: readable C++17, secret/resource RAII,
format checks, static/shared CMake/CTest and CI at the actual PR head. No automatic
re-pairing or credential deletion. Capture only sanitized receiver results.

## D71 private stereo AAC HLS composition

The latest Proceed connects D70 PCM windows through the fixed downmix/native AAC
encoder to separate HLS audio resources and a bounded host-backed lazy cache.
See [timing, storage, cancellation and evidence limits](stereo-aac-hls.md).
Video/subtitle payloads stay copied; the shipping remux defaults remain. Matroska
joins use an opt-in validated fixed-frame clock. Authoring/performance edges,
explicit public delivery/storage contracts, packaged runtime and receiver proof
remain separate gates; no audio-only speaker or discrete-surround work resumes.

## D72 native stereo AAC readiness

Long copied-video GOPs use additional bounded AAC audio segments rather than
oversized encoder epochs. The private builder reports its first terminal
conversion/cache failure for the future controller and stops further audio work.
Original fixture and synthetic one-hour callback tests check global timestamps,
exact EOF, bounded source I/O and offsets beyond 4 GiB. See [the evidence and
remaining gates](stereo-aac-readiness.md). Common rendition targets are retained;
long video segments, authoring bandwidth estimates and cold AAC synthesis remain
explicit limitations. Shipping APIs/packages and receiver evidence are unchanged.

## D73 stereo AAC authoring policy

The first-period AAC estimate is measured with an isolated decoder under the
same encode/fragment settings. Contiguous-window rate calculations include fMP4
and WebVTT bytes; indexed MKV size estimates stay explicitly identified. A
20-second copied-video span cap is an engineering limit, including the final span,
not Apple's nominal six-second recommendation. See [the measured and failed
audits](stereo-aac-authoring.md). Lazy conversion cannot promise whole-VOD accuracy
from one period. This policy and its validation preserve that limitation before
public integration; no broad authoring or receiver conformance is claimed.
