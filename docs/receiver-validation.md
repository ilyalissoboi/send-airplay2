# Receiver validation record

Status: DISCOVERY AND PAIRING OBSERVED; built-in fresh-socket and separate-process
verification passed; pyatv 0.18.0 reference playback FAILED (known upstream
tvOS 26 incompatibility); reference playback PASSED with an unmerged pyatv fix
fetching from `airplay2-cli serve`. Native `cast` initially failed G1 alone;
the minimal native remote-control SETUP/event session then PASSED G1 without
pyatv: video/audio and home-screen return after sender shutdown were user-observed.
Native MRP command/telemetry checks passed; G2 visual confirmation passed by user report on 2026-10-07.
Native automatic EOF cleanup and ten short cycles passed, with user-confirmed
EOF video/audio/home and sleep/wake recovery. Receiver-remote stop returned home
and automatically cleaned up but classified connection_lost/exit 1. Normal stop
classification remains G3 work. D42's selected Ethernet interruption and fresh
same-credential recovery passed as recorded below. A historical full 132-second
native run reached EOF cleanup but **failed sustained video presentation**:
buffering near 18 s was followed by frozen video while audio continued to clip end.
D35 follow-up: the minimum remote session played the full clip normally; default
MRP/four slots buffered but recovered; MRP/16 slots played normal video/audio and
returned Home, with no recorded loading transition. Admission capacity is a
supported candidate, not proof that the original persistent freeze is fixed.
User decision D37 lowers that frozen-video issue to low priority for now, with
insufficient media connections the likely cause; raise priority if it recurs in
later testing. Preserve its original FAIL evidence. Startup pause remains active.
See the dated observations below; record each receiver/firmware/platform separately.

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
| Pause/resume | Receiver and host state agree | Reference (unmerged pyatv fix): PASS, `Paused` then `Playing`, user-observed. Native PASS: command/telemetry and user-observed pause/resume/audio (dated G2 record) |
| Seek forward/back | Playback moves to requested position | Reference: forward seek to 30 s PASS (position 36 s about 7 s later), user-observed; backward NOT RUN. Native PASS: forward/backward command/telemetry and user-observed movement (dated G2 record) |
| Position/duration | Values follow receiver playback | Reference: PASS, positions 17/36/37/46 s against duration 131 s, consistent with timing. Native PASS: duration 131.6 s and positions follow pause/forward/backward seek |
| End-of-file | Correct ended state and resource cleanup | Native near-end seek/play: automatic media_end and joined cleanup, exit 0 with stdin held open; user confirmed video/audio and home-screen return (dated lifecycle record) |
| Sustained full-clip video | Moving video and normal audio through the full clip | Original FAIL: video froze after buffering near 18 s; audio/EOF cleanup passed. D35 follow-up: minimal remote/four slots and MRP/16 slots PASS video/audio/Home; MRP/four slots buffered but recovered. Persistent-freeze resolution remains unproven. |
| Stop from sender/receiver | Correct state and resource cleanup | Reference sender `stop`: exit 0 and the TV returned to the home screen (user-observed), but `device_state` then reported `Paused` and the sender's URL session stayed open; see observation. Native sender shutdown: user observed home-screen return after minimum SETUP/event run; native MRP Stop accepted followed by teardown and user-confirmed home return; receiver-remote stop returned home and automatically cleaned up, but classified connection_lost/exit 1; protocol idle unresolved |
| Repeated casting | Ten start/stop cycles without stale sessions | Native PASS: ten short independent casts, alternating five MRP Stop and five direct teardown; owned playing status, exit 0, no session/failed-read errors in each. Long sessions and visible home-screen observation remain separate |
| Receiver sleep/wake | Terminal cleanup; fresh cast works after waking | PASS: user-confirmed sleep, automatic connection_lost/exit 1 cleanup, fresh cast after wake without pairing, normal video/audio and EOF return home |
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

## Native MRP controls: G2 PASS, 2026-10-07

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
telemetry pass for this run; the user subsequently confirmed visible pause/resume, both seek directions,
normal resumed audio and home-screen return after stop. **G2 PASS** for this
recorded combination; the final paused URL event still does not prove protocol idle.
This control run did not test EOF.

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
Formatting and diff checks passed. All ten [CI checks](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37502438877)
passed at code/test head `8dca4a55c831563be952a5b3d6c8a353ad8b7807`: six native
platform/static/shared jobs, ASan/UBSan and three offline runner jobs. The test
const-reference follow-up corrected GCC/Clang warnings-as-errors; native runtime
code and its tested executable remained unchanged. Inspect the actual PR head
after documentation-only follow-ups. Unit tests and CI are separate from G2/G3.
EOF, receiver-side stop, automatic failure cleanup, ten cycles, sleep/wake,
network loss/recovery and other hosts/firmware remain step 3 or later gates.

## Native lifecycle: selected G3 cases PASS, manual gates pending, 2026-10-07

Same Apple TV 4K / advertised AppleTV14,1 / user-reported tvOS 26.6 (23L773),
Windows 11 x64 and 131.6-second unprotected MP4 as G1/G2. Existing credentials
and firewall rules were reused. No pyatv controller was running; the temporarily
staged native executable was restored. The [sanitized record](validation/native-lifecycle-windows-static-2026-10-07.json)
contains CLI SHA-256, tested runtime source blobs, allowlisted scalar output and
per-cycle results. This evidence covers this combination only.

- **EOF cleanup:** after owned playing telemetry, native seek to 124 seconds and
  Play were accepted. Stdin remained open. URL stopped, then the supervisor
  recognized owned receiver-reported terminal position and completed ordered
  cleanup. Summary: media_end, cleaned=yes, failed=no, failed_reads=0, exit 0;
  the receiver fetched the entire file span. This tests the end boundary after
  a near-end seek, not uninterrupted full-duration playback. The user confirmed
  normal video/audio and return home both for the earlier native EOF checkpoint
  and for repeated EOF after wake using the fingerprinted final executable.
- **Ten short cycles:** each used a new process/session and existing credentials,
  reported owned playing status, then alternated five MRP Stops with five direct
  Enter teardowns. All exited 0, cleaned=yes, no session/failed-read errors and
  full-file read spans. Subsequent starts established fresh playback without
  observed stale ownership. These are protocol/process results; no long-session,
  simultaneous-sender or visual home-screen result is inferred.
- **Receiver-remote stop:** the user exited playback and confirmed normal return
  home. With stdin held open, the CLI automatically joined/closed its session
  and media server. Last URL state paused; end=connection_lost, cleaned=yes,
  failed=yes, exit 1. Cleanup passed, but a normal receiver_stop reason is
  unresolved. Do not classify every paused-plus-disconnected session as normal
  stop: that would conceal a real outage during pause.
- **Sleep:** the user put the receiver to sleep and confirmed its screen was off.
  The CLI automatically cleaned up with stdin open, connection_lost/exit 1,
  cleaned=yes and no failed file reads. Before sleep, this run emitted repeated
  loading/playing states; this is additional buffering evidence, not resolution.
- **Wake recovery:** after the user woke the receiver to Home, a fresh native
  cast reused the existing credentials and reported owned/playing at 4.1 s.
  Near-end seek and Play were accepted; it automatically ended with media_end,
  exit 0, cleaned=yes, failed=no and no failed file reads. The user confirmed
  normal video/audio and EOF return home. Sleep cleanup plus fresh wake recovery
  pass for this combination; this does not prove actual network-outage recovery.
- **Automated fault tests:** scripted URL and remote-event EOF now trigger joined
  automatic cleanup without operator input. A silent established feedback peer
  exercises its 80 ms test deadline; stopping during blocked feedback cancels
  rather than waiting the normal two-second test deadline. Ten synthetic
  concurrent-stop cycles verify one cleanup owner and URL-before-remote closure.
  Independent protobuf fixtures reject mid-item pause and playing/extrapolated
  EOF, and accept only owned paused-at-duration telemetry. Pending MRP command
  cancellation and real native-pipe partial/idle/EOF input are covered.
  These injected peers are separate from actual receiver/network interruption.

D33 preserves first terminal reason, cancellation and fixed cleanup order.
The CLI stops its media server after session cleanup and exits without blocking
on command input. Failure returns exit 1; a recovered network requires a fresh
explicit cast and retains credentials. A silent MRP-only peer may require the
next 30-second heartbeat plus its five-second default request deadline.

Final Windows static/shared Release each passed 23/23 CTest targets
(13.33/13.07 s), offline runner contracts 10/10, clang-format dry-run/Werror
and git diff --check. Actual-head CI is recorded in the handoff after publishing.
Unit/CI results do not close G3 interoperability gates.

**Still pending:** normal receiver-stop reason classification, actual established
receiver network loss then fresh-cast recovery, longer playback and other
receiver firmware/hosts. The user is available for remote/sleep checks but cannot
perform network disconnect/reconnect in this session.
A preliminary EOF probe paused near startup; seek preserved that pause and the
probe needed explicit Play. Later cycles reported playing. That intermittent
buffering/startup behavior remains unresolved; it is not evidence of EOF failure.

Completed remote-stop and sleep/wake checks are recorded above. The remaining
network continuation uses the same paired profile and supplied local MP4:

1. Start a fresh native cast, confirm video/audio and owned status, then
   disconnect the receiver's network during playback. Keep stdin open and wait
   for terminal failure/cleanup (a silent data-only peer may take about 35 seconds).
2. Restore the network, start a fresh cast with the retained profile, and confirm
   playback. Record actual interruption/recovery separately from scripted
   deadlines, receiver sleep/wake and short-cycle results.
3. Investigate receiver-stop classification with sanitized channel/event facts;
   do not infer normal stop solely from pause followed by disconnect. Preserve
   ordinary pause and genuine outage detection. Investigate repeated loading/
   playing and startup pauses during longer playback.

## Native receiver-stop diagnosis: EOF cleanup PASS, full-clip video FAIL, 2026-10-07

Same receiver/firmware, Windows static Release and supplied 131.6-second MP4.
The [sanitized artifact](validation/native-stop-buffering-windows-static-2026-10-07.json)
fingerprints the CLI and tested source blobs; its media hash was rechecked. No
pyatv controller, new credentials or firewall/network changes. The allowed
executable was restored after the checks. D34 diagnostics use fixed channel/error
categories; per-event state outlines prevent a transient terminal state being
hidden by the CLI's latest-state display.

**Repeated remote Stop:** the user exited playback and confirmed normal Home
return. URL events remained playing until the event socket reported disconnected
at 13.9 s after cast start. All received state events were loading/playing; none
were stopped, idle or ended. Ordered cleanup completed at 14.0 s: connection_lost,
failed=yes, cleaned=yes, exit 1, failure_channel=url_events,
failure_reason=disconnected, failed_reads=0 and full-file span. This identifies
the socket failure; it does not distinguish a user stop from an unexpected event
connection closure using the parsed protocol facts. Unmapped notification names
and reason values were omitted; shapes alone cannot establish terminal intent.
Keep connection_lost until explicit receiver intent is validated. A new scripted
pause-then-EOF regression preserves genuine outage detection during pause.
The earlier timed-out operator window ended locally and provides no remote-stop evidence.

**Full-clip natural EOF, protocol/process PASS:** a fresh cast received only
`status` input every five seconds, with stdin held open and no seek or transport
command. Owned playing snapshots progressed from 4.1 to 129.5 s; these snapshots
can extrapolate receiver timestamps and are not independent continuous clock
measurements. Four heartbeats were acknowledged. Two loading/playing pairs
occurred about 17.3 and 18.3 s after playing began: the first spanned about 0.4 s,
the second shared a tenth-second trace timestamp. There were no later loading or
paused state events in this run. URL stopped at 133.1 s after cast start, and
cleanup finished at 133.9 s: media_end, cleaned=yes, failed=no, exit 0,
reads=1652, bytes=108091430, failed_reads=0, full span [0,53953926).
That is a complete clip, rather than the earlier near-end seek experiment.
**Observer follow-up: sustained video presentation FAIL.** Around the 18-second
mark the user observed a short buffering stop. Afterwards the video remained
frozen, while audio continued playing normally until clip end. This agrees in
timing with the loading/playing pairs, but does not establish their cause. Returned
playing state, advancing/extrapolated position, full-file fetch and successful
heartbeats did not reveal the frozen picture. Natural EOF/cleanup remains a
protocol/process PASS; this run is not successful full-clip video playback.
Home return for this particular run remains unconfirmed. Earlier separate
near-end EOF and short-session observations remain unchanged. This D34 gate
initially prioritized video-freeze investigation; D37 subsequently lowers its
priority unless it recurs. No automatic retry/resume workaround has been validated.
The user confirmed moving video past 18 s during normal local PC playback of
the same original file. A receiver-specific media/decoder issue is still possible;
this comparison does not prove a particular casting fault.

**Diagnostic limits and next experiment:** file counters are updated before
asynchronous TCP writes complete. Full-file read span and failed_reads=0 therefore
do not establish completed HTTP bodies or successful receiver decoding. The
600000 ms media-request deadline used by `cast` exceeds this full run, so the
default 30 s server deadline is not an explanation. The recorded outlines omit
buffer/stall values and per-request completions/write failures. Next add bounded
numeric range/status/write/completion and concurrency facts, and reviewed buffer/
stall scalars, then correlate a controlled reproduction with visible video.
No root cause or recovery behavior is established by the current trace.

Final Windows static/shared Release each passed 23/23 CTest targets
(13.13/13.05 s), offline runner contracts 10/10, touched C++ formatting and
git diff --check. All ten checks passed at diagnostic code/test head
`27356a8279c25da30821af5c708e5d4bbed7baf4` in
[CI](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37556596080).
Inspect actual-head CI after this documentation-only observer follow-up. G3 remains
open for sustained video recovery, explicit normal-stop classification, actual network interruption and
fresh-cast recovery; the user cannot perform the network checkpoint this session.

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


### Instrumented HTTP/buffering investigation, 2026-10-07

D35 adds bounded per-request socket-write/completion facts and allowlisted finite
receiver scalars; neither constitutes decoder or visual playback proof. The same
53,953,926-byte MP4 and SHA-256 were retained, with no pyatv controller. Each run
fingerprints its own executable and source blobs; the explicit comparison flag
was added after the first run, so the executable fingerprints differ.

The first control-enabled reproduction did not reach sustained playback. It
reported playing briefly, then paused at zero about 1.6 s after cast start. The
user saw the first frame and confirmed no remote buttons were pressed. MRP
remained owned/paused at zero with five successful heartbeats. At the 175-second
observation deadline the driver sent Enter; ordered cleanup reported sender_stop,
cleaned=yes and no session failure. This is a separate startup failure, not a
reproduction of the earlier moving-video freeze near 18 s. Audio and Home return
were not confirmed. The driver timed out rather than reaching natural EOF; its
exit-code field was not captured on that path, so no exit code is inferred.

Eight GET range requests were recorded: six completed and two ended with I/O
errors after partial writes. Request 2 selected the full file but wrote 4,849,664
bytes; request 4 selected offset 3,158,760/length 23,842,072 and wrote 13,107,200.
Their last reported body progress was at 720/1,576 ms after server construction;
I/O completion arrived at 19,726/20,534 ms. No media request reached its 600,000 ms
deadline. Source reads totalled 58,908,600 bytes, while socket writes totalled
58,777,528 bytes: two read chunks were never reported written. Overlapping written
ranges covered the representation, which still does not prove receiver receipt,
per-request completion or decoding. Last-write timestamps describe completion
callbacks, not packet-level progress within an outstanding write.

A second full-clip comparison explicitly uses the existing minimum native remote
SETUP/event-only session, omitting remote RECORD/data/MRP/remote feedback. Logging,
file source, HTTP range policy and four-connection budget are unchanged. Its
user confirmed normal video for the whole clip without buffering/freezing, normal
audio and Home return at EOF. Its URL stopped event produced receiver_stop/exit 0
without MRP EOF evidence. No fallback or automatic Play/seek retry is introduced. Aborted read-ahead requests alone do not identify
which side intentionally cancelled a transfer or establish the freeze's cause.


The same-executable MRP/four-slot repeat instead buffered and recovered. URL
loading intervals were 56.0-62.4, 64.6-69.1 and 105.9-106.0 seconds on the driver
clock (cast began at 40.1). The user observed buffering near clip time 18 s, then
normal video/audio through EOF and Home return. All four slots were occupied at
server times 13,933-22,116 and 22,202-28,747 ms; a new small range was accepted at
22,116/28,747 ms exactly when an older stalled response ended. Natural media_end,
exit 0 and joined cleanup passed. `stallCount=0`/readyToPlay=yes on playing events
did not exclude these observed pauses. MRP position extrapolation ran ahead during
the loading interval and later corrected; it is not a video-progress measurement.

For the next comparison only the media limit increased to 16 (MRP stayed enabled;
the server API already supported 1..16). The trace admitted low-offset ranges
with five/six active requests and had no recorded loading transition. The user
confirmed normal video/audio for the entire duration and Home at EOF. Natural
media_end, exit 0 and cleanup passed. This supports admission-capacity starvation
as a cause of the buffering pauses: long-lived read-ahead transfers consume the
four-slot budget while new requests wait for acceptance. Backlog arrival times
are not measured, so the timing remains inference, not a packet-level proof.
At this D35 gate the default remained four; `--media-connections 16` was an
explicit comparison. D36 below records the subsequent policy decision.

| Run | HTTP requests | Complete / I/O / cancelled | Local body bytes written | Presentation / end |
|---|---:|---|---:|---|
| MRP/four, startup pause | 8 | 6 / 2 / 0 | 58,777,528 | First frame/paused at zero; sender deadline/Enter cleanup |
| Minimal remote/four | 30 | 12 / 17 / 1 | 100,423,718 | Normal full video/audio/Home; URL stopped cleanup, exit 0 |
| MRP/four repeat | 27 | 13 / 14 / 0 | 99,581,886 | Buffered then normal full video/audio/Home; media_end, exit 0 |
| MRP/16 | 29 | 14 / 15 / 0 | 125,410,732 | Normal full video/audio/Home; no loading transition; media_end, exit 0 |

No request hit its server deadline and no source read failed. Successful playback
also had I/O errors on abandoned read-ahead requests; those alone do not establish
failure. The original frozen-video/audio-continuing run remains a separate FAIL,
and the spontaneous startup pause remains unresolved. The next action at this
D35 gate was the controls/lifecycle capacity comparison recorded under D36 below;
do not reduce MRP functionality or add automatic Play/seek to conceal failures.
Static/shared Release passed 23/23 CTest targets (13.74/13.39 s), offline runner
contracts 10/10, touched C++ clang-format and diff checks passed. CI is separate
and must be inspected at PR #12's actual head. The four exact runtime/source and
observer records are in the [sanitized artifact](validation/native-http-buffering-windows-static-2026-10-07.json).
Each staged allowed executable was restored; no firewall or receiver settings changed.


### Bounded cast admission policy and controls/lifecycle checks, 2026-10-07

D36 repeats the explicit `--media-connections 16` configuration with MRP enabled
on the same receiver/firmware/host and 53,953,926-byte MP4. The native static CLI
SHA-256 was `0d77c4719c05a8d90ad36e1609c0be5ad6da3fac82276ecd8132bfd36e009641`.
The [sanitized artifact](validation/native-capacity-controls-lifecycle-windows-static-2026-10-07.json)
fingerprints its pre-policy source blobs and records scalar events, commands and
HTTP completion facts. These checks use an explicit override before changing
the default; rebuilt default-policy CTest/CI results are separate.

| Case | Automated result | Human observation |
|---|---|---|
| First 16-slot attempt | Playing line then paused at zero, before any transport command; status owned/paused; Enter cleanup, exit 0 | FAIL: first frame only; remote untouched |
| Fresh 16-slot controls | Six accepted commands; pause, Play, seek 65, seek 15, Play, Stop; owned positions 25.2, 26.0 paused, 34.1, 72.7, 22.7, 31.7; sender_stop/exit 0, joined cleanup | PASS: all requested operations performed as expected, including video/audio and Home after Stop |
| Ten fresh short processes | Five MRP Stop/five Enter; each owned/playing around 4.2-4.3 s, sender_stop/exit 0, cleaned=yes, failed=no, no failed file reads | Individual visual results not asserted for these ten cycles |
| Full 131.6-second repeat | No transport command; stdin open; natural media_end/exit 0, cleaned=yes, failed=no, four heartbeats, full-file source span, no loading transition | PASS: normal video/audio through the full clip and Home at EOF (user confirmed) |

The startup failure used only two active media requests; it appeared about 0.1 s
after the playing line, and receiver scalar rate was zero. The test did not
send Play to conceal it. Higher admission capacity does not eliminate this
intermittent failure. The original 18-second buffering/frozen-video failure also
remains in its earlier record. Successful selected runs do not establish longer
reliability or decoder progress from status alone.

The successful control run recorded 26 media requests (13 complete, 11 I/O ends,
two cancelled), with up to six active slots and 147,806,246 locally written body
bytes. The full-clip repeat recorded 38 (18 complete, 20 I/O ends), up to five
active slots and 125,148,588 locally written bytes. No media deadline or source
read failed. Abandoned read-ahead I/O ends also occur in successful runs. Socket
completion remains distinct from receiver receipt/decoding; request arrival in
the backlog is not measured.

Normal `cast` now defaults to 16 bounded media slots; an explicit 1..16 override
remains available. Generic `MediaServerOptions` and `serve` retain their four-slot
default. This allows at most 16 source workers and 1 MiB of body buffers, with
the existing 600,000-ms request budget and range/cleanup behavior. The choice
uses the earlier D35 full video/audio/Home pass, confirmed D36 controls and
selected lifecycle coverage; it is not a claim that capacity fixes the original
persistent freeze or startup pause. No automatic Play/seek, retry or new dependency.

The allowed executable was restored after every run; no receiver/firewall setting
changed. Receiver remote Stop and sleep/wake are the earlier four-slot evidence,
not repeated 16-slot observations. Normal receiver-stop intent remains open;
actual network interruption remains NOT RUN because the user cannot perform it
this session. Investigate startup ordering/state and sustained moving-video
reliability next. Local tests and actual PR-head CI remain separate from hardware.

Windows static/shared Release passed 23/23 CTest targets each (13.48/13.44 s),
offline runner contracts 10/10, touched C++ clang-format dry-run/Werror and
git diff --check passed. Inspect CI at the actual PR head.


### Frozen-video triage, user decision D37, 2026-10-07

The user considers insufficient media connections the most likely cause of the
original frozen-video failure and requests low priority for now, unless it recurs
in later testing. D35 admission timing and the successful 16-slot D35/D36 runs
support that working explanation. Causality is not proven and the original FAIL
record is retained. Monitor later playback tests and raise priority if video
freezes again. The separate paused-at-zero startup failure remains active and
is the next investigation. This changes task priority only; no runtime or
receiver behavior changed and no new hardware result is inferred.


### Startup confirmation and deferred manual batch, D38, 2026-10-07

During these D38 samples the user was unavailable for manual observation.
These new native runs therefore record **telemetry only**; moving video, audio
and Home return are NOT RUN. The existing Apple TV 4K/tvOS/Windows combination
and 53,953,926-byte MP4 are unchanged to our knowledge. All runs used the normal
16-slot media default and an explicit 10,000-ms startup deadline. Only native
status and Enter were sent; there was no Play/seek retry or pyatv control.

The first instrumented candidate rejected explicitly stationary/reverse-rate
playing, but still returned immediately on positive-rate playing. Twelve short
trials alternated MRP and minimal remote. In the first MRP trial, all four queue
commands had been acknowledged by 450 ms; playing/rate 1 arrived at 767 ms and
was immediately accepted. Paused followed about 0.4 s later, and owned status
remained paused at position zero. Enter cleaned up normally. The other five MRP
and six minimal-remote samples remained playing during short observations.
Rate-only readiness is therefore insufficient. Order/idle/transient-state
confounding prevents a conclusion that MRP causes the pause. All four command
acknowledgements preceded first playing in these and the final samples; this
does not exclude other receiver races.

D38 now requires 1,000 ms of continuous eligible URL playing before `start()`
returns. Every observed loading/pause/zero/reverse-rate state resets the interval;
missing rate retains state-only compatibility. Confirmation consumes the original
startup deadline and respects cancellation, with ordered cleanup on failure.
It neither resumes the receiver nor guarantees playback after startup returns.
`--event-log` retains a fixed 64-record startup trace, including failed starts:
relative steady times, local phases, successful queue-command HTTP statuses,
allowlisted states, finite numeric rates, truncation and cleanup status. No peer
metadata, URLs or authentication material are added to those records.

| Candidate / sample | Automated result | New human observation |
|---|---|---|
| Exploratory rate-only, six MRP/six minimal | First MRP returned then paused; other short samples remained playing; all Enter cleanups exited 0 | NOT RUN |
| Final 1,000-ms confirmation, three MRP/three minimal | All six returned after 1,002-1,025 ms of eligible playing and cleaned up with exit 0 | NOT RUN |
| Same final candidate, one MRP after 300 s without a native session | First playing/rate 1 at 1,138 ms, ready at 2,159 ms; short status/Enter cleanup passed | NOT RUN |

Every process ended by sender Enter with cleaned=yes, failed=no and no failed
source reads. These are short samples, not new full-clip EOF or lifecycle passes.
The final CLI SHA-256 was
`0e67c6e03dd09c4cdacf37678949b0fcd4c4f4e02305d4a228fac5a2903e22ae`;
the exploratory CLI had a distinct fingerprint. The
[sanitized artifact](validation/native-startup-confirmation-windows-static-2026-10-07.json)
preserves exact runtime/source fingerprints, per-trial scalar traces and the
variant distinction. After sampling, seven direct standard includes were added
to the final runtime source and regression timing/names were finalized; the
include-only runtime difference was verified against the recorded blob hashes.
The rebuilt submitted code is tested separately below. Every staged allowed
executable was restored; no firewall, credentials or receiver settings changed.

Windows static/shared Release passed 23/23 CTest targets each (15.41/15.22 s),
offline runner contracts 10/10, and touched C++ clang-format dry-run/Werror plus
git diff --check passed. New scripted cases cover stationary playing, positive
playing interrupted during confirmation, original deadline, cancellation,
invalid interval, positive recovery after zero-rate states and fixed-log overflow.
CI must be checked at the actual PR head; none of these checks establishes visible
receiver presentation.

The physical startup pause remains unresolved. Complete the
[deferred manual batch](manual-validation.md) when the user announces availability:
fresh startup, controls/full EOF, remote Stop and sleep/wake. Results were PENDING
at this gate; D39 below records the subsequent observed batch. Network interruption remains NOT RUN because the user
cannot perform it this session. D37's frozen-video issue stays low priority
unless it recurs; healthy scalar telemetry cannot establish that it did not recur.


### Observed normal-default manual batch, D39, 2026-10-07

The user returned and performed the [queued checks](manual-validation.md).
Six native casts used the submitted D38 runtime on the same recorded Apple TV
4K/tvOS 26.6/Windows 11 combination and 53,953,926-byte MP4. Receiver firmware
is assumed unchanged from the earlier user report. No pyatv, explicit media
capacity/start-timeout override, credential deletion, re-pairing or startup
recovery command. Normal MRP/16-slot/30,000-ms deadline/1,000-ms confirmation
defaults were used. Every start confirmed after 1,011-1,027 ms of eligible URL
playing, and every process joined cleanup.

The static CLI SHA-256 was
`f5f7fcd3ef15307c7e64294875f405e115d4452c6c71760cda84c6ee56970309`.
The [sanitized six-case artifact](validation/native-manual-batch-windows-static-2026-10-07.json)
records exact runtime/source fingerprints, scalar traces, commands and separate
observer quotes. All sampled runtime blobs match tested head
`fa9b9189f599b0b6405b8c1b81327761900470c2` (D38 code head `6bed9fe`), including
the final direct-standard-include adjustment. This is a newly rebuilt binary,
distinct from the earlier unattended D38 sampling fingerprint.

| Case | Native result | User observation |
|---|---|---|
| First startup, 45 s observation then Enter | Confirmed in 1,011 ms; owned/playing; sender_stop/exit 0, cleaned=yes | Normal video/audio until Stop, then Home |
| Fresh after sender Stop/Home, controls | Confirmed in 1,019 ms; six accepted controls; pause, Play, seek 65, seek 15, Play, Stop; sender_stop/exit 0 | Everything requested performed as expected: startup, visible pause/resume/both seeks, resumed presentation and Home |
| Separate full clip, status only, stdin open | Confirmed in 1,011 ms; natural media_end/exit 0, four heartbeats, no post-start loading transition, cleaned=yes | Normal video/audio for the clip, automatic Home at EOF |
| Receiver remote Stop, stdin open | Confirmed in 1,017 ms; URL event disconnect while last state playing, no terminal state; connection_lost/exit 1, cleaned=yes | Home appeared normally after the remote-Stop cue |
| Sleep, stdin open | Confirmed in 1,027 ms; paused then URL event disconnect; connection_lost/exit 1, cleaned=yes | Screen off after the sleep cue |
| Fresh after wake/Home, same credentials | Confirmed in 1,023 ms; owned/playing, planned seek 124/Play, media_end/exit 0, cleaned=yes | Home visible before cast; normal video/audio, Home after near-end EOF |

The first run's initial Home and untouched remote were not explicitly answered;
retain that condition limit. The next cast followed user-confirmed sender
Stop/Home. The full run has direct normal-video/audio/EOF/Home evidence; healthy
status is not used as a substitute. No new freeze was reported in this batch.
Selected successful starts do not establish the cause of the historical
intermittent startup pause or prove it fixed. No matched minimal-remote comparison
was triggered because these starts succeeded.

| Case | HTTP records | Peak active | Complete / I/O / cancelled | Local body bytes written |
|---|---:|---:|---|---:|
| First startup | 23 | 4 | 10 / 11 / 2 | 89,199,264 |
| Controls | 30 | 7 | 16 / 13 / 1 | 147,161,022 |
| Full clip | 30 | 6 | 15 / 15 / 0 | 124,624,300 |
| Remote Stop | 16 | 5 | 8 / 4 / 4 | 77,019,704 |
| Sleep | 27 | 7 | 14 / 10 / 3 | 89,264,800 |
| After wake | 23 | 5 | 12 / 8 / 3 | 83,764,542 |

No source read or HTTP request deadline failed. Abandoned read-ahead I/O ends
also occurred in the observed passing runs; they do not establish receiver
failure. Local writes and source spans remain distinct from receiver receipt
and decoder progress. Every staged allowed executable was restored; no firewall,
network or receiver settings changed.

Remote Stop/Home and automatic cleanup are observed, but explicit normal-stop
protocol intent remains OPEN: socket EOF alone conservatively stays a connection
failure. Sleep is separate from an actual network disconnect/reconnect, which
remains NOT RUN under the prior user constraint. D37's frozen-video issue remains
low priority unless it recurs, with the original FAIL retained. Longer reliability,
other receiver/firmware/host combinations, public ABI and packaged-host gates
remain open. This gate changes evidence and documentation only.

The unchanged D38 code already passed Windows static/shared Release CTest 23/23
each, offline runner contracts 10/10 and touched C++ format/diff checks. All ten
CI checks passed at both the [code head](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37564851802)
and [tested documentation head](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37565052646).
Inspect the actual PR head for this documentation/evidence follow-up. Local/CI
results remain separate from the receiver observations above.

## Remote-Stop diagnostic comparison (D40, 2026-10-07)

This slice adds bounded remote notification observations and retained final MRP
state under `--event-log`; termination priority and exit behavior are unchanged.
The selected host/receiver/media/profile and normal MRP/16-slot/30-second startup
budget/one-second confirmation are the same as D39. Receiver firmware is assumed
unchanged from the prior user report, not newly queried. Exact source blobs,
executable/media fingerprints, scalar traces and observer scope are in the
[D40 artifact](validation/native-stop-diagnostics-windows-static-2026-10-07.json).

The first attempt was unobserved: the user requested repetition. Its pause/URL
disconnect and owned/paused final MRP snapshot cannot be attributed to remote
Stop or counted as a Home/presentation result. It is retained separately. That
executable preceded a code-equivalent direct-standard-header adjustment; the
final sources were rebuilt and tested in both configurations before the repeat.

The repeated Stop, sleep and fresh post-wake cases used static CLI SHA-256
`da3f85a3f4b8f0595834b0462250859ee56c3bab89c38acdd632434d889f6e83`.

| Case | Native result after joined cleanup | Observer scope |
|---|---|---|
| Repeated remote Stop, stdin open | URL event disconnect while URL playing; final MRP unowned/unknown, at_end=false, 21 messages; connection_lost/exit 1, cleaned=yes | Home appeared normally in response to Stop cue; presentation before Stop was not separately answered |
| Sleep, stdin open | URL paused; URL feedback disconnected first; final MRP owned/paused, received position 0 s, duration 131.567 s, rate 1, at_end=false, 24 messages; connection_lost/exit 1, cleaned=yes | Screen off confirmed in response to sleep cue |
| Fresh cast after wake/Home, same credentials | Owned/playing; planned seek 124 s and Play accepted; media_end/exit 0, cleaned=yes; final MRP owned/paused at received position/duration 131.567 s, rate 0, at_end=true, 26 messages | Home before cast confirmed; normal video/audio and Home after EOF confirmed |

Remote observations were fixed-label updateInfo outlines (five in the Stop case,
six for sleep), without an allowlisted terminal state or reason/error/status code.
Retained MRP is the last processed snapshot, not a fresh receiver query or proof
that every final message arrived before cancellation. Loss of ownership and
pause alone are not normal-stop signals. Independent socket/feedback ordering
also changes which failure is observed first. This selected comparison therefore
does not justify converting connection failure to normal Stop. Classification
remains OPEN and conservative; actual network disconnect/reconnect remains
NOT RUN. No new dependency, credential replacement or receiver/network/firewall
setting change was introduced. Staged executables are restored after each run.
Fresh playback after wake reused the existing credentials and passed observed
video/audio/near-end EOF/Home. No new buffering or freeze was reported; D37's
historical frozen-video issue remains low priority unless it recurs. This is a
selected recovery observation, not an established-session network-loss proof.

Final Windows Release static/shared CTest passed 23/23 each (15.67/15.53 s), offline
runner contracts 10/10, touched C++ format checks and `git diff --check` passed.
Inspect CI at the actual published D40 PR head. Those checks are separate from
the selected receiver observations above.

## Ethernet interruption and fresh recovery (D42, 2026-10-07)

The user became available for the actual network check, previously NOT RUN.
Two native casts used the unchanged D40 runtime on Windows 11 x64 and the same
Living Room Apple TV 4K (AppleTV14,1), media file and stored profile. tvOS 26.6
(23L773) is assumed unchanged from the prior user report, not newly queried.
Normal MRP/16-slot/30-second-startup/one-second-confirmation defaults applied.
Static CLI SHA-256 was
`da3f85a3f4b8f0595834b0462250859ee56c3bab89c38acdd632434d889f6e83`.
Tested source blobs match D40 implementation head
`4a18b2662150768a00154ca17e222f2e701fd192` and the actual tested PR head
`72b3059c1a1fa11cd641c24f20764b5b99e6e535`. Exact hashes, traces and observer
quotes are in the [artifact](validation/native-network-recovery-windows-static-2026-10-07.json).

| Case | Native result | Observer scope |
| --- | --- | --- |
| Established cast, user removes Ethernet cable, stdin open | Owned/playing before cue; later URL paused and URL feedback timeout; automatic connection_lost/exit 1, cleaned=yes; final MRP owned/paused, received position 0 s, duration 131.567 s, rate 1, at_end=false, 24 messages and one heartbeat | Disconnection confirmed; cable explicitly confirmed as Ethernet/network. Video/audio before action was asked about but not reported. |
| Fresh process after reconnection/Home, same credentials | Owned/playing; planned seek 124/Play accepted; URL stopped then owned received EOF; media_end/exit 0, cleaned=yes; final MRP owned/paused, received position/duration 131.567 s, rate 0, at_end=true, 21 messages and one heartbeat | Home after reconnection confirmed. Normal video/audio and Home after EOF confirmed; separate buffering/freeze detail was not supplied. |

The interrupted process required no sender Stop/Enter; only `status` was sent
and stdin remained open. The user was cued to leave the network disconnected
until cleanup completed, then to reconnect. User action times were not synchronized
with process logs; the known cue-to-cleanup interval is not disconnect-detection
latency. This tests automatic failure cleanup and a fresh explicit cast after
reconnection, not automatic in-session reconnect/resume. The recovery cast ended
after a planned near-end seek; it adds no full-clip proof.

Both cases joined cleanup and restored the staged allowed executable, verified
by SHA-256 equality. No sender firewall/network setting, credential or runtime
source changed. The user temporarily removed and restored the receiver Ethernet
connection. The selected network cleanup/fresh recovery gate is PASS. D40 Stop
ended on URL event EOF while this network sample ended on feedback timeout; that
difference and retained ownership do not establish a reliable cause classifier.
The existing conservative policy remains: ambiguous connection failure stays
connection_lost/exit 1. Remote-Stop intent, intermittent startup pause, broader
network cases and longer reliability remain open. D37's frozen-video priority
and original FAIL evidence remain unchanged.

No rebuild or local CTest rerun was needed for this evidence-only slice. The
unchanged D40 runtime retains its Windows static/shared Release 23/23 results
and offline runner 10/10. All 51 PR-changed C++ files passed clang-format checks,
and `git diff --check` passed. All ten CI checks passed at tested head `72b3059`
in [this run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37579365861).
Inspect the actual PR head after publishing this evidence follow-up; CI remains
separate from the selected receiver observations above.
