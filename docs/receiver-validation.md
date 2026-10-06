# Receiver validation record

Status: DISCOVERY AND PAIRING OBSERVED; built-in fresh-socket and separate-process
verification passed; pyatv 0.18.0 reference playback FAILED (known upstream
tvOS 26 incompatibility); reference playback PASSED with an unmerged pyatv fix
fetching from `airplay2-cli serve`; native playback NOT IMPLEMENTED. Fill out one record per
receiver firmware and sender platform.

The Boost HTTP media server is implemented with loopback tests on Windows
static/shared builds; see [media-server.md](media-server.md). Actual Apple TV HTTP
fetch, real-file >4-GiB seek and firewall reachability remain NOT RUN. No receiver
or credential operation occurred in the media-server slice. The development
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
Playback results remain pending.

## Environment

- Date and library commit:
- Receiver manufacturer/model/generation: Apple TV 4K (user-provided), advertised model `AppleTV14,1`; pyatv 0.18.0 reports "Apple TV 4K (gen 3)". That label is pyatv's model-table mapping, not an independent hardware check.
- Firmware version and build: tvOS 26.6 (23L773), user-reported from Settings > General > About on 2026-10-06; consistent with advertised `osvers`/`ov=26.6`. Earlier results in this record predate the build report and assume the firmware was unchanged.
- AirPlay access policy and PIN/password settings (no secrets): access limited to people on the same network (user-reported); pyatv scan reports no password required and mandatory pairing for AirPlay/RAOP/Companion.
- Sender OS/version/architecture: Windows 11 x64; automated runner observes Windows build `10.0.26200`, AMD64.
- Host: CLI / packaged Windows C# / Android:
- Network: host on Wi-Fi, same subnet as the receiver. Windows network category **Public** (the runbook expected Private; the user chose to proceed). Existing inbound Allow rules (Public profile) for the `airplay2-cli.exe` builds; pyatv timing needed a temporary user-created inbound UDP rule for the Python interpreter, LocalSubnet, Public profile. See the playback observation.
- Media SHA-256, container, codecs, duration, dimensions and bitrate: user-owned clip, 53,953,926 bytes, SHA-256 `a91fb5c781f4a6ecc90b780dc77793403b6aac7220af7c899ba3b217129b5e65`; MP4 (`isom`, `moov` before `mdat`), H.264 Main profile level 4.2 1280x720, AAC (`mp4a`) stereo 44.1 kHz, 131.6 s. Read with a standard-library box parser; ffprobe was unavailable. Bitrate not measured.
- Negotiated protocol/authentication path (reference sender): HAP pair-verify with stored credentials, encrypted RTSP control, NTP timing (sender UDP), event channel, type-130 control stream, `POST /command` queue commands; receiver fetches the URL over plain HTTP from the sender.
- Reference sender/version and baseline result: pyatv 0.18.0 AirPlay pairing PASS (user-reported); reference URL playback FAIL: `/play` 200, then `/playback-info` 500 and no media fetch, also with a public Apple HLS URL. Matches open upstream issues. Unmerged pyatv fix `robkochman/pyatv@8144c77c` (`/command` queue flow): playback PASS, video and audio (user-observed), served by `airplay2-cli serve`. See [reference-baseline.md](reference-baseline.md) and the observation below.

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
| Start MP4 | Both audio and video play | Reference: pyatv 0.18.0 FAIL (no fetch); unmerged pyatv fix PASS, video and audio from `serve` (user-observed). Native NOT RUN |
| Pause/resume | Receiver and host state agree | Reference (unmerged pyatv fix): PASS, `Paused` then `Playing`, user-observed. Native NOT RUN |
| Seek forward/back | Playback moves to requested position | Reference: forward seek to 30 s PASS (position 36 s about 7 s later), user-observed; backward NOT RUN. Native NOT RUN |
| Position/duration | Values follow receiver playback | Reference: PASS, positions 17/36/37/46 s against duration 131 s, consistent with timing. Native NOT RUN |
| End-of-file | Correct ended state and resource cleanup | NOT RUN |
| Stop from sender/receiver | Correct state and resource cleanup | Reference sender `stop`: exit 0 but the receiver then reported `Paused`, and the URL session did not end; see observation. Native NOT RUN |
| Repeated casting | Ten start/stop cycles without stale sessions | NOT RUN |
| Network interruption | Bounded failure; next cast can recover | NOT RUN |
| Large file | Seek beyond 4 GiB without integer truncation | NOT RUN |
| Packaged Windows host | Discovery, native loading, file access, serving work | NOT RUN |

Use a personally owned or redistributable unprotected test clip. Capture sanitized
diagnostics: timestamps, state transitions, status codes and range requests.
Do not commit pairing secrets, PINs, private media URLs or raw credential logs.
Unit tests and mock receivers cannot substitute for this record.

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
does not show that this library can cast: native session code does not exist
yet.

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

After `stop`, the receiver reported `Paused` rather than idle, and the
fork's `play_url`, which waits for an `idle` or `stopped` event, did not
return. Ending the session took closing the sender's connections. For native
code, "stop" must therefore be defined explicitly: for example, a stop or
queue-removal command followed by session teardown, verified by observed
receiver state, not assumed from a 200 response. End-of-file, backward seek,
repeated casts and receiver-side stop remain NOT RUN.

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
