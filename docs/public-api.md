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

## Not covered yet

1. **Receiver validation through the interface.** It needs an observed cast on the
   recorded Apple TV (start, controls, Stop/Home, natural EOF) with the binary's
   fingerprint. Until then only the CLI path has hardware evidence.
2. **Happy-path tests through the controller.** The fake receiver lives inside
   `url_playback_session_tests.cpp`. Extracting it into a shared test header would
   let `cast_controller_tests` cover active status, commands and natural end.
3. **C# binding and packaged UWP proof:** P/Invoke over this header, native loading,
   brokered file reads and inbound serving in a packaged app.
4. **Credential stores for other platforms** (Keychain, libsecret, Android
   Keystore, UWP PasswordVault), and the Botan UWP packaging question.
5. **Pairing, profile deletion and diagnostics** (start trace, event and media
   logs) in a later interface version, if hosts need them.
6. **IPv6 link-local receivers** (scope IDs): the media server rejects them today.

## Validation

Windows 11 x64, MSVC (Visual Studio 18 2026), Release, static and shared: 26/26
CTest targets each. New targets:

- `c_playback_smoke` (C): compiles the header as C and links the real library.
  Covers version and result names, option defaults, argument validation, source
  ownership on failed and successful create, the unstarted lifecycle, and a start
  with a deliberately absent profile (`PROFILE_NOT_FOUND` on Windows,
  `UNSUPPORTED` elsewhere). It reads that one absent profile and writes nothing.
- `cast_controller_tests` (C++): independent expected results for every start and
  command mapping; created/stopped phases; absent profile without a connection;
  credential-store failure; unreachable receiver; media-server argument
  rejection; prompt cancellation of a blocked start; seek argument bounds; source
  release exactly once. It passed 10 repeated local runs.

These are synthetic loopback checks. They establish neither receiver behavior nor
packaged-host behavior.
