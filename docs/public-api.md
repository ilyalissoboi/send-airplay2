# Public playback interface (version 1)

Status: **implemented, experimental; manual plan passed on one receiver/host (D48).**
Snapshot 2026-10-07 (Asia/Tokyo), decision D46. The header is
[`include/send_airplay2/playback.h`](../include/send_airplay2/playback.h); it is the
authoritative contract. This page records why it has that shape and what it
does not cover yet.

The interface wraps the private `UrlPlaybackSession` and `MediaServer` without
changing the validated protocol sequence (session-design.md section 2, D28-D38).
Unit/CI success here is not receiver interoperability. The D48 manual batch is the
only receiver evidence for this interface: one Apple TV 4K (tvOS 26.6) and one
Windows 11 x64 host; see [receiver-validation.md](receiver-validation.md#public-playback-interface-manual-batch-d48-2026-10-08).

## User decisions (D46)

**Revised by D49 (2026-10-08):** credentials may also come from a host-provided
store passed through the C interface, and pairing will join the interface. See
[credential-interface.md](credential-interface.md). The D46 bullets below are
kept as recorded, with the superseded statements marked.

- **Credentials by profile name.** The host passes a profile name. The library
  loads it from its own platform store (Windows Credential Manager today), so
  pairing secrets never cross the ABI. *Superseded by D49:* the guarantee now
  holds for built-in stores only, and packaged UWP and Android hosts supply a
  host-provided store instead of needing adapters inside the library. Platforms
  with neither still report `SAP2_ERROR_UNSUPPORTED`.
- **Playback only for the first slice.** Discovery already has a C++ interface.
  Diagnostics stay in the CLI for now. *Superseded by D49:* pairing and local
  profile removal will join the C interface; until that is implemented they
  remain CLI-only.

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
| A sleeping receiver is woken first (D56) | When MRP reports no logical devices, start sends `WAKE_DEVICE` and waits up to 10 s, outside `start_timeout_ms`, for the receiver to settle; casting into a waking receiver lost the item to tvOS's return to Home ([mrp-controls.md](mrp-controls.md#receiver-power-and-wake-d56)). Not a host option. |
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

## Session teardown fix (D47)

The MRP fake exposed a session ordering bug that predates this interface. With
MRP on, the feedback loop also posts `/feedback` on the remote connection.
Cleanup cancels feedback first, and `ReceiverConnection::request` closes its
connection on any exception, cancellation included. So a remote request in
flight at stop closed the remote session *before* the URL session, contrary to
"retain remote control through URL teardown". It reproduced 3/3 with a held
request, and it made the MRP controller tests fail 7 of 18 runs under load.

At the user's request it is fixed in this PR: the loop no longer starts a remote
request once stop has begun, and an in-flight remote request is waited for, not
cancelled. Its request deadline bounds the wait. This changes receiver-validated
session code: every earlier hardware record used the old teardown, so the manual
plan must run on a build that contains this fix. New regression tests in
`url_playback_session_tests` cover both cases:

- A held request at stop: stop waits without closing either session, then closes
  URL then remote once the receiver answers. Without the fix this test fails
  with `[remote,URL]`.
- An unanswered request at stop: stop takes under 1.5 s with a 300 ms deadline,
  and is not reported as a failure.

## Credential stores and pairing (API version 2, D50)

Implements step 1 of D49 ([credential-interface.md](credential-interface.md)).
**Receiver result (D51):** `sap2_pair()` paired the recorded Apple TV into the
built-in store with hidden console PIN entry, and a cast with the new profile
passed; see [receiver-validation.md](receiver-validation.md#pairing-through-the-public-interface-d51-2026-10-08).
Host stores, wrong and cancelled PINs, and profile removal are unit-tested only.

- [`credentials.h`](../include/send_airplay2/credentials.h): the
  `sap2_credential_store` callback table (`load`, `save_new`, `erase`) with
  `SAP2_STORE_*` results and `SAP2_CREDENTIAL_RECORD_MAX` (203 bytes). Records
  are opaque; the library's buffers are fixed-size and erased after each call.
- `sap2_cast_options.credential_store` (new last field): a host store for that
  cast, or NULL for the built-in store. Version 1 option structs are still
  accepted through `struct_size`. The store is called only inside
  `sap2_cast_start()`, once, before any network work.
- [`pairing.h`](../include/send_airplay2/pairing.h): `sap2_pair()` wraps the CLI's
  existing pairing workflow (refuse an existing profile, ask the receiver to show
  its PIN, call the host's `read_pin`, save only authenticated credentials,
  reload them, verify a fresh connection). `sap2_forget_profile()` removes a
  local profile without touching the receiver.
- New results: `SAP2_ERROR_PROFILE_EXISTS` (pairing an existing profile, or
  `save_new` losing a race) and `SAP2_ERROR_PIN_TIMEOUT` (`read_pin` returned
  after `pin_timeout_ms`). A wrong PIN reports `SAP2_ERROR_AUTHENTICATION`; a
  cancelled PIN entry, `SAP2_ERROR_CANCELLED`. `SAP2_PLAYBACK_API_VERSION` is 2.
- Threading and lifetime follow credentials.h: callbacks run on the calling
  thread; a cast's store must stay valid until `sap2_cast_destroy()` returns,
  and a pairing call's store and `read_pin` until it returns.
  Callbacks must not re-enter the library for the operation that invoked them
  (for example `sap2_cast_stop()` from a cast's store would wait for the very
  start that is calling it); stop a cast from another thread.
- **Size-aware initializers.** `sap2_cast_options_init_sized(&options,
  sizeof options)` writes at most the caller's size and stores what the library
  knows in `struct_size`. The version 1 `sap2_cast_options_init()` now writes
  only the version 1 fields, so a host built against version 1 can never be
  overrun by a newer library; the library then ignores `credential_store`.
  `sap2_pair_options_init(&options, sizeof options)` is sized from the start.

Engineering choices made while implementing: `read_pin` is a synchronous
callback rather than a two-step API, because the existing workflow keeps the
provisioning socket open between the PIN request and the proof; its deadline is
checked when it returns, since a callback cannot be interrupted. Pairing has no
cancellation besides `read_pin`; each network phase is bounded by `timeout_ms`.
On a platform without a built-in store, a NULL store reports
`SAP2_ERROR_UNSUPPORTED`. UWP builds are such a platform: they compile out
Credential Manager (D55, [uwp-native-build.md](uwp-native-build.md)).

## Receiver discovery (D54)

The user asked for a C discovery interface after the D53 host, which still
needed a pasted address. [`receivers.h`](../include/send_airplay2/receivers.h)
wraps the C++ `discover()` (discovery.md) without changing the scanner:

- `sap2_discover(duration_ms, &list)` blocks for one bounded scan
  (1..`SAP2_MAX_DISCOVERY_MS`, default `SAP2_DEFAULT_DISCOVERY_MS` = 5000, as in
  C++). It starts no background work and touches no credentials.
- The result is an opaque `sap2_receiver_list` that owns every string;
  `sap2_receiver_list_count`, `sap2_receiver_list_get(list, index, &info)` with a
  caller-set `struct_size`, and `sap2_receiver_list_free`.
- `sap2_receiver_info`: `flags` (`PASSWORD_REQUIRED`, `HAS_FEATURES`), `id`,
  `name` with `name_length` (advertised bytes, possibly not UTF-8 and possibly
  with NULs), `model`, `address`, `port` and `features`.
- Results: argument refusals are `SAP2_ERROR_INVALID_ARGUMENT` before any socket
  opens; a transport failure (`std::runtime_error`) is `SAP2_ERROR_CONNECTION`;
  allocation failure is `OUT_OF_MEMORY`; anything else is `INTERNAL`.

Engineering choices (proposals, not user decisions):

| Choice | Reason |
|---|---|
| Only devices with an `_airplay._tcp` service with a nonzero port | The interface casts video; RAOP-only devices are speakers. |
| One address per receiver: the first IPv4 of the first AirPlay service that has a castable address, else its first IPv6 that is neither `fe80::/10` nor scoped | The media server rejects link-local IPv6, and a scope id is meaningless to the receiver. A device with no castable address is kept with `address = ""`, so hosts can still show it. |
| A snapshot list, not callbacks | Matches the C++ scan; no library threads call host code (D28), and bindings need no delegates. |
| Separate header, same result codes; `SAP2_PLAYBACK_API_VERSION` unchanged | The additions are new symbols only; a host checks for them by linking. A future version bump can name them. |
| Advertised data is labelled unauthenticated | Pairing pins identity; a name or id from the network proves nothing. |

Tests: `receiver_list_tests` compiles the C boundary with a scripted
`discover()` (no network): address and service selection, metadata and
byte-exact names, failure mapping, list ownership after the snapshot is gone,
bounds and short-struct refusals. `c_receivers_smoke` compiles the header as C
and checks the layout size and refusals. `api_host_arguments` covers the
host's `--discover` refusals. Receiver evidence:
[the D54 record](receiver-validation.md#c-discovery-interface-d54-2026-10-08).

## Threading and lifetime

- `start` blocks its caller. `get_status`, `wait_for_change`, `command` and `stop`
  are safe from any thread, alongside each other and a pending start.
- `destroy` must follow the return of every other call on the handle.
- `read_at` runs on up to `media_connections` library threads at once. It must not
  call any `sap2_cast_*` function for its own handle: `stop` joins those threads.
- After `stop` returns, no `read_at` runs.

## Manual validation plan

**Result (D48, 2026-10-08): checks 1-7 and the ten-cycle check passed** on the D47
runtime at `8a7050d`, with the observer confirming each TV result; see
[the D48 record](receiver-validation.md#public-playback-interface-manual-batch-d48-2026-10-08).
Check 6 used receiver sleep rather than a cable pull. The unreachable-address
check and Task Manager handle counts were not run. The optional past-4-GiB
seek passed through `cast --media-log` with a 6.32 GB remuxed film.

The plan uses `airplay2-api-host` (README), which
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

1. **Broader receiver validation.** D48 covers one receiver, firmware and host,
   and the unreachable-address and cable-interruption cases were not run through
   the interface.
2. **Silent receiver during stop.** The session ordering issue the MRP fake
   found is fixed (D47 below), but one edge remains: if the receiver never
   answers a remote `/feedback` request that is in flight at stop, that request
   reaches its deadline. `ReceiverConnection` closes on that failure, so the
   remote connection still closes before the URL session. Stop is delayed by at
   most the request timeout (5 s by default). The receiver is unresponsive by
   then, so no ordering guarantee is claimed for that case.
3. **C# binding and packaged UWP proof:** done for one receiver and host (D52-D55,
   [uwp-host.md](uwp-host.md)), including a UWP-built library; Store certification
   and x86/ARM64 remain.
4. **Credential stores for other platforms**, per D49
   ([credential-interface.md](credential-interface.md)): built-in macOS Keychain
   and Linux Secret Service adapters, and host-provided stores for UWP
   (`PasswordVault`) and Android (Keystore). Plus the Botan UWP packaging question.
5. **Pairing on hardware beyond D51's single pairing:** into a host-provided
   store, and wrong or cancelled PINs. Diagnostics (start trace, event and media
   logs) may come in a later interface version, if hosts need them.
6. **IPv6 link-local receivers** (scope IDs): the media server rejects them today,
   so discovery reports such a receiver with an empty address.
7. **Discovery beyond one snapshot (D54):** no live updates, departure events or
   IPv6-only networks; one receiver and host observed.

## Validation

Windows 11 x64, MSVC (Visual Studio 18 2026), Release, static and shared: 29/29
CTest targets each after D50. D50 targets:

- `c_credentials_smoke` (C): a one-slot C memory store behind the real library.
  Covers store-table validation; a cast's store being loaded once in start and
  never at create; absent, unavailable, malformed, zero-length and oversized
  records; profile removal (stored, absent, invalid profile, and an absent
  profile in the built-in store); pairing defaults and argument refusals; and
  pairing stopping before any PIN request or save on a malformed existing record
  or an unreachable receiver.
- `host_credential_tests` (C++, scripted sessions and the fake receiver): record
  round trip through the C table; `already_exists`, `invalid_record` and
  `unavailable` mapping; pairing success (PIN called once, leading zeros kept,
  one save, verification of the reloaded record on a fresh connection), wrong
  PIN, cancelled entry, invalid PINs (3 digits, 9 digits, a letter), late PIN,
  an existing profile (no PIN or connection), a lost `save_new` race and a save
  failure; removal; and a cast authenticated from a host store. Disabling the
  PIN deadline check fails the late-PIN case.
- `c_playback_smoke` now also checks that version 1 option structs are accepted.

Earlier targets:

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
  caught. After the D47 fix, `cast_controller_tests` and
  `url_playback_session_tests` each passed 10 sequential runs and 30 runs as six
  concurrent copies, with the normal 30 ms test feedback interval.

These are synthetic loopback checks. They establish neither receiver behavior nor
packaged-host behavior.
