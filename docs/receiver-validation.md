# Receiver validation record

Status: DISCOVERY AND PAIRING OBSERVED; built-in fresh-socket and separate-process
verification passed; pyatv 0.18.0 reference playback FAILED (known upstream
tvOS 26 incompatibility); reference playback PASSED with an unmerged pyatv fix
fetching from `airplay2-cli serve`. Native `cast` initially failed G1 alone;
the minimal native remote-control SETUP/event session then PASSED G1 without
pyatv: video/audio and home-screen return after sender shutdown were user-observed.
Native MRP command/telemetry checks passed; G2 visual confirmation is pending.
See the dated G1/G2 observations below. Fill out one record per
receiver firmware and sender platform.

The Boost HTTP media server is implemented with loopback tests on Windows
static/shared builds; see [media-server.md](media-server.md). Apple TV HTTP
fetch and firewall reachability passed in later reference and native runs.
Real-file >4-GiB seek remains NOT RUN. The original media-server slice itself
performed no receiver or credential operation. The development
`airplay2-cli serve` command now exposes that server for the pyatv reference
playback step. With pyatv 0.18.0 the receiver never connected to `serve`. With
an unmerged pyatv fix it fetched the whole file from `serve` while video and
audio played. See
[pyatv reference playback](#pyatv-reference-playback-2026-10-06) and
[the fork result](#reference-playback-with-unmerged-pyatv-fix-2026-10-06).
Real-file >4-GiB seek remains NOT RUN.

The environment below was supplied by the user on 2026-10-06 (Asia/Tokyo).
It identifies the intended test setup. A Windows discovery run subsequently
resolved this receiver. The user subsequently confirmed authenticated PIN pairing,
credential save and built-in fresh-socket verification after the M6 metadata fix.
Reference playback, the initial native failure and the subsequent native-only G1 pass are recorded below.

## Environment

- Record dates and library commits: dated observations below identify each tested build.
- Receiver manufacturer/model/generation: Apple TV 4K (user-provided), advertised model `AppleTV14,1`; pyatv 0.18.0 reports "Apple TV 4K (gen 3)". That label is pyatv's model-table mapping, not an independent hardware check.
- Firmware version and build: tvOS 26.6 (23L773), user-reported from Settings > General > About on 2026-10-06; consistent with advertised `osvers`/`ov=26.6`. Earlier results in this record predate the build report and assume the firmware was unchanged.
- AirPlay access policy and PIN/password settings (no secrets): access limited to people on the same network (user-reported); pyatv scan reports no password required and mandatory pairing for AirPlay/RAOP/Companion.
- Sender OS/version/architecture: Windows 11 x64; automated runner observes Windows build `10.0.26200`, AMD64.
- Host: native Windows desktop CLI; packaged Windows C# and Android NOT RUN.
- Network: host on Wi-Fi, same subnet as the receiver. Windows network category **Public** (the runbook expected Private; the user chose to proceed). Existing inbound Allow rules (Public profile) for the `airplay2-cli.exe` builds; pyatv timing needed a temporary user-created inbound UDP rule for the Python interpreter, LocalSubnet, Public profile. See the playback observation.
- Media SHA-256, container, codecs, duration, dimensions and bitrate: user-owned clip, 53,953,926 bytes, SHA-256 `a91fb5c781f4a6ecc90b780dc77793403b6aac7220af7c899ba3b217129b5e65`; MP4 (`isom`, `moov` before `mdat`), H.264 Main profile level 4.2 1280x720, AAC (`mp4a`) stereo 44.1 kHz, 131.6 s. Read with a standard-library box parser; ffprobe was unavailable. Bitrate not measured.
- Negotiated protocol/authentication path (reference sender): HAP pair-verify with stored credentials, encrypted RTSP control, NTP timing (sender UDP), event channel, type-130 control stream, `POST /command` queue commands; receiver fetches the URL over plain HTTP from the sender.
- Reference sender/version and baseline result: pyatv 0.18.0 AirPlay pairing PASS (user-reported); reference URL playback FAIL: `/play` 200, then `/playback-info` 500 and no media fetch, also with a public Apple HLS URL. Matches open upstream issues. Unmerged pyatv fix `robkochman/pyatv@8144c77c` (`/command` queue flow): playback PASS, video and audio (user-observed), served by `airplay2-cli serve`. See [reference-baseline.md](reference-baseline.md) and the observation below.
- Native sender path: two independently HAP-verified encrypted control/event
  sessions. Minimum remote-control-only SETUP/event connection retained alongside
  URL SETUP/NTP/RECORD/type-130 `/command` start and HTTP media fetch. Native-only
  G1 passed for one 45-second run; no MRP or pyatv was used.

## Required observations

| Test | Expected | Result/evidence |
|---|---|---|
| Discovery and departure | Correct identity; no duplicate/stale entries | Discovery observed; departure/interface changes NOT RUN |
| First pairing | PIN UI and credential save succeed | PASS, user-reported after M6 metadata fix; see observation below |
| Wrong PIN / revoked pairing | Explicit failure, no playback | NOT RUN |
| Reconnect after restart | Stored pairing works when receiver policy permits | Built-in fresh-socket and separate-process verification PASS; host/receiver restart NOT RUN |
| Repeated verification | Independent processes reuse stored pairing | PASS, baseline plus three reconnects and three fault-recovery verifications per static/shared run |
| Profile/input guards | Existing/missing profiles and redirected PIN input are refused | PASS, automated native CLI static/shared runs |
| Local forget | Local deletion and idempotence | Absent-profile no-op PASS; real disposable deletion SKIPPED (no opt-in) |
| Start MP4 | Both audio and video play | Reference: pyatv 0.18.0 FAIL (no fetch); unmerged pyatv fix PASS, video and audio from `serve` (user-observed). Native: initially headless (FAIL); minimum native remote SETUP/event session then PASS without pyatv, video/audio user-observed |
| Pause/resume | Receiver and host state agree | Reference (unmerged pyatv fix): PASS, `Paused` then `Playing`, user-observed. Native command/telemetry PASS; visual observation pending (dated G2 record) |
| Seek forward/back | Playback moves to requested position | Reference: forward seek to 30 s PASS (position 36 s about 7 s later), user-observed; backward NOT RUN. Native command/telemetry PASS; visual observation pending (dated G2 record) |
| Position/duration | Values follow receiver playback | Reference: PASS, positions 17/36/37/46 s against duration 131 s, consistent with timing. Native PASS: duration 131.6 s and positions follow pause/forward/backward seek |
| End-of-file | Correct ended state and resource cleanup | NOT RUN |
| Stop from sender/receiver | Correct state and resource cleanup | Reference sender `stop`: exit 0 and the TV returned to the home screen (user-observed), but `device_state` then reported `Paused` and the sender's URL session stayed open; see observation. Native sender shutdown: user observed home-screen return after minimum SETUP/event run; native MRP Stop accepted followed by teardown (visual result pending); receiver-side stop and protocol idle NOT RUN |
| Repeated casting | Ten start/stop cycles without stale sessions | NOT RUN |
| Network interruption | Bounded failure; next cast can recover | NOT RUN |
| Large file | Seek beyond 4 GiB without integer truncation | NOT RUN |
| Packaged Windows host | Discovery, native loading, file access, serving work | NOT RUN |

Use a personally owned or redistributable unprotected test clip. Capture sanitized
diagnostics: timestamps, state transitions, status codes and range requests.
Do not commit pairing secrets, PINs, private media URLs or raw credential logs.
Unit tests and mock receivers cannot substitute for this record.

The dated slice observations below preserve their original evidence. Statements
such as NOT RUN in those historical sections apply at that date/slice; the status
and required-observations table above summarize the latest result for each gate.

## Discovery observation: 2026-10-06 (Asia/Tokyo)

Windows 11 x64 / MSVC 19.51 Release CLI, discovery implementation on
`codex/receiver-discovery` (working-tree scan before publishing implementation
commit `48189897ba167b44c3da7c6e4a7857bf28120498` in
[PR #2](https://github.com/ilyalissoboi/send-airplay2/pull/2)). A 15-second scan outside the execution
sandbox received four DNS responses with zero rejected packets and no warnings.
It discovered "Living Room", model `AppleTV14,1`, on interface 24. Its AirPlay
and RAOP services were merged by matching advertised identity and both resolved
port 7000 with one IPv4 and four IPv6 addresses, including a scoped link-local
address. The feature mask was `0x3c177fde4a7fdfd5`; password and pairing
requirements remain unknown. A second device, a `Mac14,2`, was also resolved.

Discovery/identity resolution is observed. Real receiver departure, expiry,
interface changes and the complete "Discovery and departure" test above remain
pending; lifecycle behavior has only synthetic test coverage. Reference playback
and all media controls are NOT RUN. Later pairing evidence is recorded below.
This is not playback certification.
Raw LAN identifiers, addresses and advertised public keys are omitted here.

## Pairing transport foundation: 2026-10-06

The next slice adds private TLV8 and authenticated control-record codecs with
synthetic test coverage; see [pairing-transport.md](pairing-transport.md). It does
not perform a handshake or receiver I/O. No PIN, credential or playback operation
was attempted; all corresponding hardware results above remain NOT RUN.

## Peer-verification slice: 2026-10-06

Private verification of existing, pinned identity credentials is implemented with
synthetic RFC/transcript/failure tests; see [peer-verification.md](peer-verification.md).
No receiver HTTP/socket connection, PIN enrollment, credential change or playback
operation was attempted. Synthetic peer-rejection tests are not evidence of actual
Apple TV revocation behavior; all hardware authentication results remain NOT RUN.

## PIN-pairing slice: 2026-10-06

Private PIN/SRP provisioning message processing is implemented and tested with
public synthetic transcripts; see [PIN pairing](pin-pairing.md). No receiver
connection, PIN-display request, actual PIN entry, credential save or playback
operation was attempted. All hardware pairing/playback results remain NOT RUN.

## Receiver-transport slice: 2026-10-06

Private bounded HTTP/RTSP framing, native IPv4/IPv6 TCP, deadlines/cancellation and
pairing/verification-to-record integration are implemented; see
[receiver transport](receiver-transport.md). Synthetic peers and real loopback
sockets exercise these flows. No connection to "Living Room", PIN display/entry,
credential change/save or playback was attempted. The hardware table remains
NOT RUN for authentication and playback; loopback success does not validate tvOS.

## Credential storage / CLI slice: 2026-10-06

Windows desktop pairing, stored-credential verification and local forget commands
are implemented; see [credential storage](credential-storage.md). Tests use only
public synthetic credentials in a separate random test namespace. Actual Windows
Credential Manager save/load, cross-process reload, malformed-entry rejection,
concurrent create refusal and redirected-input refusal passed locally in static
and shared Release builds. Synthetic enrollment credentials survive serialization
and satisfy the independent peer-verification transcript/control-key oracle.
At that slice's merge, no Apple TV PIN-display request, actual PIN entry,
application credential save, receiver revocation or playback operation had been
attempted. Subsequent live enrollment and separate-process verification are
recorded below. Remaining tests include wrong PIN,
cancel/timeout and receiver-revoked credentials. Record the exact firmware build
and access settings without recording secrets.

## Live PIN pairing and M6 compatibility: 2026-10-06

Receiver: Living Room / Apple TV 4K, advertised `AppleTV14,1`, user-reported
tvOS 26.6. Host: Windows 11 x64, MSVC Release static CLI. Exact firmware/OS builds
and AirPlay access policy remain unspecified.

The user observed a PIN on the TV and entered it through the CLI's hidden prompt.
Initial enrollment failed with `Invalid pair-setup message`. Instrumented retries
isolated M6: HTTP 200, outer body 322 bytes, wire fields `5:255,5:60,6:1(state=6)`.
A later decrypted schema diagnostic reported 299 bytes and
`source=identity TLV=[1:36,3:32,10:64,17:159]`. Those are headers/lengths only;
no actual identity, keys, PIN, ciphertext or metadata contents were retained.
The failure occurred after mandatory M4 server-proof verification and successful
M6 AEAD decryption, but before accessory-signature validation/credential release.
The strict schema rejected the additional type-17 receiver metadata.

An agent-operated no-PIN/no-save LAN probe independently observed
`/pair-pin-start` HTTP 200 with an empty body, and M2 HTTP 200 (409 bytes): state
2, 16-byte salt and 384-byte SRP public value. This probe did not complete pairing.

The fix in [PR #8](https://github.com/ilyalissoboi/send-airplay2/pull/8), source
commit `f61698fa930893c139efcb5f5d2b0a40a93d0cae` on `codex/pairing-diagnostics`,
based on PR #7 merge
`6e83badfc5146371ee0c886e3f75fba492f9ab61`, accepts one optional opaque M6
type-17 value up to 256 bytes and discards it. Mandatory ID/key/signature checks,
server proof, duplicate rejection and rejection of other unknown tags remain.
Only public synthetic bytes were added as regression fixtures. Failed earlier
M6 processing did not save a local profile; receiver-side pairing may nevertheless
have been provisioned by M5. No receiver revocation was performed or asserted.

The user reran the rebuilt
`build-pairing-static/Release/airplay2-cli.exe` and reported:

```text
Pairing authenticated; credentials saved. Verifying a fresh connection.
Peer verification succeeded; encrypted control transport established. Playback is not implemented.
```

This establishes authenticated enrollment, Windows application credential save,
reload and built-in verification on a fresh socket for this receiver/host
combination. The user then launched `verify` separately with the saved profile
and reported the same peer-verification success message and `$LASTEXITCODE` of 0.
No PIN was required. The `pair` exit code itself was not supplied.
These results do not establish encrypted application request/response exchange,
restart/revocation behavior or playback. Physical-console echo/mode restoration,
wrong PIN, cancellation/timeout and host/receiver restart checks remain pending.

## pyatv reference pairing: 2026-10-06

The user ran pyatv 0.18.0 (MIT, external tool) on the Windows host, following
[reference-baseline.md](reference-baseline.md), and reported the following.

- Unicast scan: "Living Room", Apple TV 4K (gen 3) per pyatv's model table,
  tvOS 26.6, deep sleep false. Services: AirPlay port 7000, Companion port 49153
  and RAOP port 7000. Each reported `Requires Password: False` and
  `Pairing: Mandatory`, with no stored credentials before pairing.
- `atvremote --protocol airplay pair`: succeeded, exit code 0 (user-reported).
- `device_state` on a fresh pyatv invocation using stored credentials:
  `DeviceState.Idle`.

This shows that a second, independent controller can complete AirPlay HAP
pairing on this receiver/firmware alongside the existing `airplay2-cli` profile,
and that pyatv can reuse its credentials. It is not a playback result, and it
says nothing about this library's session code. The address, MAC address and
device identifiers printed by the scan are omitted here; credentials were not
shared. The user subsequently reported tvOS build 23L773 and AirPlay access
limited to the same network (see Environment). The Companion protocol was not
paired.

## pyatv reference playback: 2026-10-06

Step 4 of [reference-baseline.md](reference-baseline.md), run by the agent in a
local session on the user's Windows 11 x64 host (build `10.0.26200`), with the
user watching the TV. Receiver: Living Room / Apple TV 4K / `AppleTV14,1` /
tvOS 26.6 (23L773). Reference sender: pyatv 0.18.0 (MIT, external tool) on
Python 3.11.9, using its existing stored AirPlay credentials. Media server:
`airplay2-cli serve`, static Release build of PR #11 code head
`19a08d256fdd2092aecfc63438af2b6a07c95b8c`; media as listed under Environment.

The receiver address came from a fresh `discover` run, and the private URL came
from `serve` output. Both stayed in driver-script memory. Child output was
filtered before display: addresses, URLs, MAC addresses, identifiers, headers
and plist values are omitted, except allowlisted status fields. No raw debug
output was stored.

**Run 1: no Python firewall rule, commands per the runbook.**

- `play_url` exited 1 after `no response to SETUP` for the base RTSP SETUP.
  This SETUP advertises an NTP timing port on the sender.
- A concurrent `device_state` at about 15 s exited 1 after `no response to POST
  /pair-verify`. Later `set_position=30`, `pause`, `play` and `stop` exited 0,
  and every status read returned `DeviceState.Idle`, so those commands had
  nothing to act on.
- `play_url` logged power state On, Off, On. The user saw the TV wake from
  sleep with no playback UI.
- `serve`: `Stopped. reads=0 bytes=0 failed_reads=0`.

Windows Firewall had no rule for the Python interpreter, and the network
category is Public. The working hypothesis is that the receiver's inbound UDP
timing requests were dropped, which stalled SETUP. The user added a temporary
inbound UDP Allow rule for the interpreter (LocalSubnet, Public profile).

**Run 2: same, with the rule.** SETUP completed; `play_url` exited 1 about 2 s
later with `GET /playback-info` HTTP 500. Control commands were skipped.
`serve`: `reads=0`.

**Run 3: diagnostic, `--debug` with filtering; TCP connections to `serve`
polled every 200 ms.** Sanitized sequence, all within about 1.4 s:

| Request | Response |
|---|---|
| `POST /pair-verify` x2 | 200, 200 |
| RTSP `SETUP` (base, no timing) | 200, `eventPort` |
| `RECORD` | 200 |
| `SETUP` stream type 130 | 200, `streamID`, `dataPort` |
| `POST /pair-verify` x2 | 200, 200 |
| RTSP `SETUP` (base, `timingProtocol=NTP`) | 200, `eventPort` |
| `RECORD`, `POST /feedback` | 200, 200 |
| `POST /play` (bplist with `Content-Location`) | 200, empty body |
| `PUT /setProperty?isInterestedInDateRange`, `?actionAtItemEnd` | 200, `errorCode=0` |
| `POST /rate?value=1.000000` | 200 |
| `PUT /setProperty?forwardEndTime`, `?reverseEndTime` | 200, `errorCode=0` |
| `GET /playback-info` | **500**, empty body; connection then lost |

Inbound TCP connections to `serve`: **0**. `serve`: `reads=0`.

**Run 4: control.** The same filtered run with Apple's public HLS example URL
in place of `serve` produced the identical sequence and the same 500. The
failure therefore does not depend on this project's server, the URL or the
local firewall for TCP.

Interpretation:

- The receiver accepts pyatv 0.18.0's legacy `/play` request on tvOS 26.6, but
  it does not start media or fetch the URL. This matches open upstream issues
  [pyatv#2906](https://github.com/postlund/pyatv/issues/2906) (Apple TV 4K,
  tvOS 26.6, identical symptom) and
  [pyatv#2821](https://github.com/postlund/pyatv/issues/2821) (tvOS 26.2,
  regression of [pyatv#2512](https://github.com/postlund/pyatv/issues/2512)).
- An unmerged proposal, [pyatv#2846](https://github.com/postlund/pyatv/pull/2846)
  (head `8848ad3fd9ae46b8eb733bfc667b536a28f04c5a`, read on 2026-10-06), replaces
  `/play` with a type-130 stream SETUP (`controlType` 1), then
  `POST /command` with a binary plist wrapping queue commands such as
  `insertPlayQueueItem` (`mediaType` `file`, `Content-Location`), plus
  `setProperty`. It reads playback state from the event channel instead of
  polling `/playback-info`. Neither the proposal nor a reworked fork linked from
  it was run here; neither is maintainer-reviewed or verified on this receiver.
- Inbound UDP timing to the sender was needed before SETUP completed. A native
  session will need the same on Windows. Run 1 does not prove the cause; it was
  not confirmed by a packet capture.
- `serve` was started, announced and stopped cleanly in every run. Its fetch
  path and firewall reachability from the Apple TV remain NOT RUN, because no
  sender caused a fetch. `serve` counts reads only. A HEAD-only or aborted
  connection would also show `reads=0`, which is why run 3 polled connections.

Seek, pause/resume, position and stop results remain NOT RUN for any working
playback. Remove the temporary Python firewall rule after testing.

## Reference playback with unmerged pyatv fix: 2026-10-06

Same host, receiver, firmware, network, media and `serve` build as the previous
section, and the same temporary Python UDP rule. After that failure, the user
chose to try an unmerged upstream fix as the reference sender.

- **Sender:** `robkochman/pyatv` commit
  `8144c77c6cecbed4f9ba2adb5a350ad86a8f6604` ("Fix AirPlay URL playback on
  modern tvOS"). It is one commit on top of the maintainer's `Release 0.18.0`
  commit `b277a4c82`, linked from
  [pyatv#2846](https://github.com/postlund/pyatv/pull/2846). It is MIT like
  pyatv. It is not merged or maintainer-reviewed, and it reports its version
  as 0.18.0.
- **Review:** the agent read the four changed source files before the user
  installed it. Changes are confined to the AirPlay URL stream and
  event-channel code: no new hosts, processes, file access or dependencies.
  The user installed it from that commit's archive into a separate venv and
  ran the filtered diagnostic driver. The agent did not execute the fork.
  It is an external test tool only: not a dependency, and no code was copied
  into this repository.

Sanitized sequence, filtered as in the previous section:

| Phase | Requests and results |
|---|---|
| Remote-control session | pair-verify 200 x2; base SETUP (no timing) 200; RECORD 200; type-130 SETUP 200 |
| Stream session | pair-verify 200 x2; base SETUP with `timingProtocol=NTP` and `sessionCorrelationUUID` 200; `GET /info` 200; `RECORD` 200; type-130 SETUP (`controlType` 1) 200 with `streamID` |
| Start | four `POST /command` (`insertPlayQueueItem`, two `setProperty`, `setRate`), each 200 with an empty body |
| Events | `playbackState` `loading` x3, then `playing`; many `notification` and `updateInfo` events; `/feedback` 200 about every 2 s |

`serve` results:

- **TCP connections from the receiver:** 17. The first arrived about 3.7 s after
  `play_url` started.
- **Stop summary:** `reads=1466 bytes=95888702 failed_reads=0
  span=[0,53953926)`. The receiver requested the whole file range, and about
  1.8 times the file size in total within 60 s, so ranges overlap or repeat.
  Individual range requests were not logged.
- **Firewall:** fetch worked on the Public network with the existing inbound
  Allow rules for `airplay2-cli.exe`; no new rule was needed for `serve`.

The user reported that video and audio played without issues. `play_url`
blocks while media plays, so the driver ended it after 60 s. End-of-file,
sender stop, seek, pause/resume and position were not exercised in this run.

This result establishes:

- the receiver plays this H.264/AAC MP4 when it is fetched from this project's
  `MediaServer` through `serve`;
- the receiver can reach that server through the Windows firewall on this host;
- the `/command` flow works on tvOS 26.6 with this sender.

It is reference-sender evidence, attributed to unmerged third-party code. It
did not exercise native session code. Later native G1 observations below
supersede that implementation status, including the later native-only G1 pass.

### Control pass with the unmerged fix

The user ran the sanitized control driver with the same fork, `serve` build,
media and network. Each control is a separate `atvremote` process, which uses
pyatv's remote-control channel; the fork did not change that code.

| Time (s) | Command | Exit | Reported state / position / duration |
|---|---|---|---|
| 10.2 | `play_url` started (background) | | |
| 35.4 | `device_state position total_time` | 0 | `Playing` / 17 / 131 |
| 36.3 | `set_position=30` | 0 | |
| 43.2 | status | 0 | `Playing` / 36 / 131 |
| 44.1 | `pause` | 0 | |
| 51.0 | status | 0 | `Paused` / 37 / 131 |
| 51.9 | `play` | 0 | |
| 60.8 | status | 0 | `Playing` / 46 / 131 |
| 61.7 | `stop` | 0 | |
| 65.6 | `device_state` | 0 | `Paused` |
| 96.5 | `play_url` still running 30 s after `stop`; killed by the driver | | |

`serve`: `reads=1436 bytes=93970360 failed_reads=0 span=[0,53953926)`.

The user reported normal video and audio, and that seek, pause and play worked
as expected. Positions match wall-clock progress, the seek target and the
pause. The concurrent status connection that failed in the pyatv 0.18.0 run
worked here once playback was established.

After `stop`, the user saw the TV return to the home screen. However,
`device_state` reported `Paused` rather than idle, and the fork's `play_url`,
which waits for an `idle` or `stopped` event, did not return. The sender's
URL session stayed open until the driver closed its connections. For native
code, "stop" must therefore be defined explicitly: for example, a stop or
queue-removal command followed by session teardown, verified by observed
receiver state, not assumed from a 200 response. End-of-file, backward seek,
repeated casts and receiver-side stop remain NOT RUN.

## Native cast, hardware gate G1: 2026-10-07

The first casts by this library's own session code. Receiver: Living Room /
Apple TV 4K / `AppleTV14,1` / tvOS 26.6 (23L773). Host: Windows 11 x64, Public
Wi-Fi profile, existing inbound Allow rules for this `airplay2-cli.exe`.

- **Build:** static Release from `claude/url-playback-session`; step 6 commit
  `1630b61`, plus the diagnostic event log added after run 1.
- **Command:** `airplay2-cli cast` with the stored `airplay2-cli` profile and
  the same media as the reference runs.
- **Driver:** the agent ran it; the receiver address stayed in driver memory.
- **Observation:** the user watched the TV.

| Run | Setup | `cast` output (sanitized) | TV (user-observed) |
|---|---|---|---|
| 1 | `cast` alone, 45 s | `playing` within 1 s, then `paused`; `events=42 feedback=22 timing=21 failed=no reads=762 bytes=49859866 failed_reads=0 span=[0,47054848)`; exit 0 | Nothing appeared; home screen throughout |
| 2 | `cast --event-log` alone, 25 s | `loading` x3, `playing` with full playback info, then `loading`/`playing`/`loading` around 26-32 s; `events=47 reads=1103 bytes=72159904 span=[0,53953926)`; exit 0 | Nothing appeared |
| 3 | `cast` while pyatv 0.18.0 `atvremote push_updates` held its remote-control session open, 30 s | `playing`; `events=40 feedback=15 timing=14 reads=1219 bytes=79762080 span=[0,53953926)`; exit 0. pyatv's session saw device states including `Playing` | **Video and audio played correctly**, with one short buffering pause around 15 s |

Interpretation:

- The native sequence works at the protocol level on tvOS 26.6. Every request
  was accepted, and the receiver fetched the whole MP4 from this project's
  `MediaServer` through the Windows firewall, without errors. Event channel,
  feedback and timing all worked: 12-22 feedback requests and 13-21 timing
  answers per run. Stop was clean each time.
- **H5 is supported:** the receiver presents URL playback only when the sender
  also has a remote-control session. On its own, our session's playback
  happens headlessly. Run 3 used official pyatv 0.18.0 for that session (the
  `isRemoteControlOnly` SETUP plus its MRP data stream). Whether the SETUP and
  event channel alone are enough, or the MRP handshake is needed, is not yet
  known.
- **Event structure** (run 2, key names only):
  - `playbackState` events carry `params.playbackState`, `rate`, `position`,
    `duration`, `readyToPlay`, `stallCount`, buffer flags, and loaded and
    seekable time ranges.
  - Notifications carry `name`, `item.uuid` and sometimes `position` or
    `value`.
  - Some events (`updateInfo`) are bare dictionaries without the `params.data`
    envelope. The parser now accepts both shapes; earlier they were counted as
    unreadable and answered.
- The receiver read more than the file size in every run (up to 1.5x), so ranges
  are re-requested. The state also returned to `loading` mid-play; the buffering
  pause in run 3 may be related. Not yet investigated.
- **G1 status: FAIL** for `cast` alone. The remote-control session must be part
  of native start. H4 (stop behavior) could not be assessed, because nothing
  was on screen in runs 1 and 2.


### Native minimum remote-control session: G1 PASS, 2026-10-07

Same receiver, firmware, host and MP4 as the previous native runs. The new native
session opens its own stored-credential pair-verify and `isRemoteControlOnly`
SETUP (`timingProtocol=None`), connects the separately keyed event channel, then
starts the URL session. It retains remote control until URL teardown. No remote
RECORD, feedback, type-130 data stream, MRP or pyatv process was used.

- Windows MSVC static Release, based on `66b8a94` with this native minimum change.
  Implementation subsequently committed as `cd2c983`; source blobs in the record
  identify the tested code independently of later documentation changes.
  Tested CLI SHA-256 and sanitized output are in the
  [machine-readable record](validation/native-minimum-session-windows-static-2026-10-07.json).
- The agent ran native `cast` using the existing paired profile. Receiver address
  and discovery output remained in driver memory; no credential file was read.
- The user reported: video and audio played normally, and the device returned
  to the home screen after playback finished.
- The driver sent Enter after 45 seconds. Exit 0; summary:
  `state=playing events=49 remote_events=0 feedback=22 timing=21 failed=no
  reads=1317 bytes=86184608 failed_reads=0 span=[0,53953926)`.
- Existing firewall rules were reused by temporarily staging the tested executable
  in the stopped session's allowed build location. Its original executable was
  restored; no firewall rule was added or changed.

**G1 PASS for this run:** native-only visible video/audio, full-file fetch and
user-observed home-screen return after sender shutdown. The minimum SETUP/event
connection was sufficient on this receiver; an idle remote event channel is not
an error. MRP remains the selected controls path (D29/D31). The printed final
state is the last URL event, not proof of protocol idle. This was not EOF or a
receiver-side stop test. Native controls, repeated casting, extended session
lifetime and other firmware/hosts remain unvalidated.

## Native MRP controls: command/telemetry PASS, G2 observer pending, 2026-10-07

Same Apple TV 4K / tvOS 26.6 (23L773), Windows 11 x64 and MP4 as G1. The
normal native CLI now extends the independently verified remote session with
RECORD, type-130 controlType-2 data SETUP, keyed MRP framing and handshake.
No pyatv controller was running. Existing credentials and firewall rules were
reused; the temporarily staged executable was restored.

The [sanitized record](validation/native-mrp-controls-windows-static-2026-10-07.json)
contains final CLI SHA-256, tested source blobs, command times and allowlisted
scalar output. Commands were scoped to the full selected player path using D32
startup correlation; this firmware omitted our URL and queue UUID in MRP.

| Native command/check | Receiver evidence |
|---|---|
| Initial status | owned=yes, playing, position 10.2 s, duration 131.6 s |
| Pause | Correlated result accepted; URL and MRP paused, position 10.0 s after 5 s |
| Resume | Correlated result accepted; URL loading then playing |
| Seek forward to 45 s | Accepted; playing position 51.5 s about 5 s later |
| Seek backward to 15 s | Accepted; playing position 19.8 s about 5 s later |
| Heartbeat | One correlated acknowledgment after 30 s |
| MRP Stop then teardown | Accepted; URL paused; exit 0 and failed=no |

Final summary: `state=paused events=71 remote_events=9 feedback=19 timing=18
failed=no reads=1773 bytes=116021286 failed_reads=0 span=[0,53953926)`.
Repeated range reads explain total bytes exceeding file size. Command results and
telemetry pass for this run; visible pause/resume/seeks, resumed audio and home
screen return await the user's observation. **G2 remains pending that observation.**
The final paused URL event does not prove protocol idle or EOF.

Compatibility issues discovered and covered by independent synthetic fixtures:
receiver replies can contain a plist body; an idle readiness poll must preserve
the socket; the reference subscription yields a 2363-byte authenticated record;
configuration/heartbeat use correlated type-0 acknowledgments; URL duration is
a CMTime rational dictionary. MRP receive opts into a bounded 16-KiB record
budget while other channels retain 1024 bytes. Full command result types and
request IDs remain checked. D32 is an engineering fallback for cooperative
startup, not proof against simultaneous same-duration AirPlay senders.

Windows static/shared Release each passed 22/22 CTest targets (11.88/11.71 s),
including independent protobuf/frame/large-record oracles, split/coalesced
frames, malformed input, correlation/authentication/timeout/cancellation,
heartbeat, ownership/replacement guards and native idle-readiness regression.
Formatting and diff checks passed. Unit tests and CI are separate from G2/G3.
EOF, receiver-side stop, automatic failure cleanup, ten cycles, sleep/wake,
network loss/recovery and other hosts/firmware remain step 3 or later gates.

## Automated CLI E2E observation: 2026-10-06

The agent ran the [noninteractive runner](e2e-runner.md) from
[PR #9](https://github.com/ilyalissoboi/send-airplay2/pull/9), initially introduced
at `2bf31dd8f61d17e4475ec36c78f81bcac21f0009` and then hardened for closed-port
refusal portability at `650a0440a72d5e263bcca3fa8e6aa623cd628a7f`.
The report fingerprints identify the tested runner bytes.
The runs used Windows 11 x64,
observed Windows build `10.0.26200` / AMD64, using the existing user-paired profile
and the known Living Room Apple TV 4K / advertised `AppleTV14,1` / user-reported
tvOS 26.6. Static/shared Release CLIs contain the unchanged C++ code merged in
PR #8 (`f24ac4825a47b2b641caaac1d0f1c00dd1058c33`). No PIN entry, enrollment,
receiver revocation/settings change, reboot, network-interface change or primary
credential deletion occurred during these runs.

Each default run passed 16 checks with one skipped opt-in deletion case:

- **Live discovery:** two 5-second scans each found one matching target device
  without duplicate device/service records or warnings; advertised target identity
  was consistent. This does not test actual departure/interface changes.
- **Live authentication:** baseline verification, three independent reconnects
  and verification after each of three injected faults all passed with exit 0.
  These prove stored-credential reuse/fresh verification, not playback or encrypted
  application request/response exchange.
- **Local native CLI:** existing-profile refusal, random missing-profile refusal,
  redirected PIN refusal and absent-profile forget all passed with expected exact
  checkpoints/exit codes. No profile was created or deleted.
- **Loopback faults:** a selected/released closed port produced a network failure;
  an accepting silent peer produced the native deadline error; a peer closing
  after request bytes produced disconnect/network failure. These peers never
  forwarded to Apple TV and retained no payloads. Actual Apple TV network outage,
  reconnect after host/receiver reboot and established-session interruption remain
  NOT RUN; this evidence does not change those hardware gates.
- **Disposable deletion:** SKIPPED, not opted in. Real-profile deletion/idempotence
  remains a live gate; its orchestration has offline synthetic coverage.

Sanitized machine-readable artifacts contain runner/CLI SHA-256 fingerprints,
OS/build/architecture, endpoint address family/port/scope usage and constructed
case facts. Addresses, identities, profiles, paths, TXT/public keys, PINs, raw
child output and exception text are omitted:

- [Static CLI E2E report](validation/e2e-windows-static-2026-10-06.json).
- [Shared CLI E2E report](validation/e2e-windows-shared-2026-10-06.json).

These artifacts are selected-case evidence for this receiver/firmware/host only.
Offline runner tests and native/sanitizer CI remain distinct from device evidence.
