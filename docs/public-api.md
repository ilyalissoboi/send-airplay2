# Public playback interface (version 1)

Status: **implemented, experimental, not receiver-tested through this interface.**
Snapshot 2026-10-07 (Asia/Tokyo), decision D46. The header is
[`include/send_airplay2/playback.h`](../include/send_airplay2/playback.h); it is the
authoritative contract. This page records why it has that shape and what it
does not cover yet.

The interface wraps the private `UrlPlaybackSession` and `MediaServer` without
changing the validated protocol sequence (session-design.md section 2, D28-D38).
Unit/CI success here is not receiver interoperability: no cast has been run
through the C interface on hardware yet.

## User decisions (D46)

- **Credentials by profile name.** The host passes a profile name. The library
  loads it from its own platform store (Windows Credential Manager today), so
  pairing secrets never cross the ABI. Packaged UWP and Android hosts therefore
  need store adapters inside the library; other platforms report
  `SAP2_ERROR_UNSUPPORTED` until one exists.
- **Playback only for the first slice.** Discovery already has a C++ interface.
  Pairing, profile deletion and diagnostics stay in the CLI for now.

## Engineering choices

These are proposals made while implementing D46. They can change before the
interface leaves experimental status; record a new decision if they do.

| Choice | Reason |
|---|---|
| C ABI: opaque `sap2_cast` handle, `int32_t` results, fixed-width fields, no exceptions or C++ objects | design.md requires a versioned C ABI for C#/JNI bindings. Results are macros, like `http_range.h`, for FFI. |
| `struct_size` first in every extensible struct; `SAP2_PLAYBACK_API_VERSION` plus a runtime query | Lets later versions append fields without breaking older hosts. The version increments on any incompatible change while experimental. |
| One handle owns both the media server and the session | The CLI's required order (credentials first, server, session; session cleanup before the server stops) moves inside the library, where hosts cannot get it wrong. |
| Host media as a `read_at` + 64-bit `size` callback table with an optional `release` | Supports brokered `StorageFile` and Android content URIs, not only paths. The table maps directly onto the existing `MediaSource` contract (concurrent reads, short reads, cooperative stop). |
| Source ownership moves to the handle only on `SAP2_OK`; `release` runs exactly once, after the last read, from `sap2_cast_destroy()` | No double free on failed create; no read after release. |
| Status by polling (`get_status`) and blocking `wait_for_change`, no event callbacks | Keeps D28: no host code runs on session threads. Media reads are the only host callbacks, as before. A C# binding can wrap the wait in a task. |
| `sap2_cast_start()` blocks; `sap2_cast_stop()` from another thread cancels it | Reuses the session's existing cancellation flag. Stop waits for the cancelled start to return, so after stop nothing is running. |
| `SAP2_PHASE_ENDED` keeps the media listener until `stop`/`destroy` | Avoids an extra supervisor thread. Hosts must call `sap2_cast_stop()` or destroy after seeing an end reason. |
| End reasons are reported as-is; no reclassification | A remote Stop/Home still arrives as `SAP2_END_CONNECTION_LOST` (D34/D40/D41). Any termination policy needs its own user decision. |
| No automatic Play/seek retry; start confirmation unchanged (one second of forward playing) | Same as D38. |
| `export.h` holds `SAP2_API` for all public headers | Extracted from `http_range.h`; no change in behavior. |

### Result mapping

Start failures map private exception categories to fixed results; no receiver
text, address, identifier or URL is exposed.

| Source | Result |
|---|---|
| Profile absent | `PROFILE_NOT_FOUND` (before any listener or connection) |
| Credential store unavailable or record malformed / platform without a store | `CREDENTIAL_STORE` / `UNSUPPORTED` |
| Peer verification or record authentication | `AUTHENTICATION` |
| Non-2xx session response; MRP handshake rejected | `RECEIVER_REJECTED` (HTTP status in `rejected_status` when known) |
| No confirmed playing state in time | `START_TIMEOUT` |
| Network, timeout, disconnect, closed records | `CONNECTION` |
| Malformed message, correlation error | `PROTOCOL` |
| Media server setup | `MEDIA_SERVER`; its address/content-type validation gives `INVALID_ARGUMENT` |
| `sap2_cast_stop()` during start | `CANCELLED`, whatever the interrupted step raised |

Commands: `NOT_OWNED` when the receiver no longer plays our item, `ENDED` once the
session ended or stop began, `COMMAND_FAILED` for a rejected or unanswered command.

## Threading and lifetime

- `start` blocks its caller. `get_status`, `wait_for_change`, `command` and `stop`
  are safe from any thread, alongside each other and a pending start.
- `destroy` must follow the return of every other call on the handle.
- `read_at` runs on up to `media_connections` library threads at once. It must not
  call any `sap2_cast_*` function for its own handle: `stop` joins those threads.
- After `stop` returns, no `read_at` runs.

## Manual validation plan

None of these checks has been run yet. They use `airplay2-api-host` (README), which
calls the library only through `playback.h`. Record each run's commit, host
SHA-256 (`Get-FileHash`), static/shared linkage, receiver model and firmware,
and a yes/no for every observation, in a sanitized `docs/validation/` artifact.
Never record the receiver address, media URL or PIN. Results not confirmed on the
TV are telemetry only.

Observed on the TV (Apple TV 4K / tvOS 26.6, existing profile, `gas.mp4`):

| # | Procedure | Expected | Observer confirms |
|---|---|---|---|
| 1 | Interactive start, about 30 s, Enter | `Start: ok`, active/playing/owned; summary end `sender_stop`, cleaned, `releases=1` | Video and audio; Home after stop |
| 2 | `pause`, `play`, `seek` forward and back, then `stop` | Each `Control: ok`; position follows seeks | Visible pause/resume and seeks, audio returns, Home |
| 3 | Full clip, `status` only, stdin left open | Host ends itself; end `media_end`; `releases=1` | Moving video to the end (D37 freeze watch), Home |
| 4 | `--cancel-after-ms 1000` | `Cancel: start=cancelled`, start well under 30 s; a fresh cast then works | No stuck loading; Home |
| 5 | Stop or Home on the Apple TV remote | Host ends itself; end `connection_lost` (known); `releases=1` | Home |
| 6 | Sleep the TV or pull Ethernet once, then a fresh cast | End `connection_lost` with a failure channel; the fresh cast reaches `Start: ok` | Second cast plays and returns Home |
| 7 | Repeat 1 with the shared build | `linkage=shared`, same results | Same as 1 |

Telemetry only (no observer needed):

- `--cycles 10 --hold-ms 5000`: `Cycles: passed=10/10`, every cycle released once,
  no failed reads; Task Manager handle/thread counts return to the starting level.
- An unused LAN address: `Start: connection` within the request timeout.
- Optional, open gate: seek past 4 GiB in a larger unprotected H.264/AAC MP4.

## Not covered yet

1. **Receiver validation through the interface** (the plan above). Until it runs,
   only the CLI path has hardware evidence.
2. **Session ordering issue found by the MRP fake (not fixed).** With MRP on,
   the feedback loop also posts `/feedback` on the remote connection. Cleanup
   cancels feedback first, and `ReceiverConnection::request` closes its
   connection on any exception, cancellation included. So a remote feedback
   request in flight at stop closes the remote session *before* the URL
   session. That contradicts the documented "retain remote control through URL
   teardown" order. A test that silences remote feedback and stops while a
   request is pending reproduced `[remote, URL]` 3/3; at the 2 s production
   interval it needs stop to overlap a request. Fixing it changes
   hardware-validated session code and needs a decision; the MRP controller
   tests use a 60 s feedback interval meanwhile.
3. **C# binding and packaged UWP proof:** P/Invoke over this header, native loading,
   brokered file reads and inbound serving in a packaged app.
4. **Credential stores for other platforms** (Keychain, libsecret, Android
   Keystore, UWP PasswordVault), and the Botan UWP packaging question.
5. **Pairing, profile deletion and diagnostics** (start trace, event and media
   logs) in a later interface version, if hosts need them.
6. **IPv6 link-local receivers** (scope IDs): the media server rejects them today.

## Validation

Windows 11 x64, MSVC (Visual Studio 18 2026), Release, static and shared: 27/27
CTest targets each. New targets:

- `api_host_arguments`: runs `airplay2-api-host` offline. Covers argument
  refusals, range errors reported by `sap2_cast_create`, and an absent profile
  that stops before any network work with `releases=1`.

- `c_playback_smoke` (C): compiles the header as C and links the real library.
  Covers version and result names, option defaults, argument validation, source
  ownership on failed and successful create, the unstarted lifecycle, and a start
  with a deliberately absent profile (`PROFILE_NOT_FOUND` on Windows,
  `UNSUPPORTED` elsewhere). It reads that one absent profile and writes nothing.
- `cast_controller_tests` (C++): independent expected results for every start and
  command mapping; created/stopped phases; absent profile without a connection;
  credential-store failure; unreachable receiver; media-server argument
  rejection; prompt cancellation of a blocked start; seek argument bounds; source
  release exactly once. Against the shared scripted fake receiver (MRP
  disabled through the test-only `adjust_session` hook) it also covers a
  successful start, a receiver pause seen through `wait_for_change`, stop with
  URL-then-remote closure, natural end (`media_end`), event-channel loss
  (`connection_lost` on `url_events`), concurrent stops, and commands returning
  `ENDED` after each end. With `Behavior::mrp_fixtures` the fake also runs an
  encrypted MRP data stream (`FakeMrpPeer`): data-stream SETUP with the seed,
  keys derived from the `DataStream-Salt<seed>` labels, the handshake, and
  SET_STATE for the session's inserted item UUID on the fixture player path.
  Against it the controller tests cover ownership, accepted pause/play/seek/stop
  with their wire numbers (2, 1, 45, 45, 4) and seek positions (30.5, 0) at the
  receiver, state following pause and play, MRP-reported end (paused at the
  duration gives `media_end`) and ownership loss to another item
  (`ownership_lost`). A deliberate mutation of the expected command order was
  caught. It passed 10 sequential runs and 30 runs as six concurrent copies.

These are synthetic loopback checks. They establish neither receiver behavior nor
packaged-host behavior.
