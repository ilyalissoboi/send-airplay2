# Receiver validation record

Status: DISCOVERY AND PAIRING OBSERVED; built-in fresh-socket and separate-process
verification passed; playback NOT IMPLEMENTED. Fill out one record per
receiver firmware and sender platform.

The Boost HTTP media server is implemented with loopback tests on Windows
static/shared builds; see [media-server.md](media-server.md). Actual Apple TV HTTP
fetch, real-file >4-GiB seek and firewall reachability remain NOT RUN. No receiver
or credential operation occurred in the media-server slice.

The environment below was supplied by the user on 2026-10-06 (Asia/Tokyo).
It identifies the intended test setup. A Windows discovery run subsequently
resolved this receiver. The user subsequently confirmed authenticated PIN pairing,
credential save and built-in fresh-socket verification after the M6 metadata fix.
Playback results remain pending.

## Environment

- Date and library commit:
- Receiver manufacturer/model/generation: Apple TV 4K (user-provided), advertised model `AppleTV14,1`; generation not independently established.
- Firmware version and build: tvOS 26.6 (user-reported, consistent with advertised `osvers`/`ov=26.6`); exact build not supplied.
- AirPlay access policy and PIN/password settings (no secrets):
- Sender OS/version/architecture: Windows 11 x64; automated runner observes Windows build `10.0.26200`, AMD64.
- Host: CLI / packaged Windows C# / Android:
- Network: Ethernet/Wi-Fi; same subnet; firewall configuration:
- Media SHA-256, container, codecs, duration, dimensions and bitrate:
- Negotiated protocol/authentication path:
- Reference sender/version and baseline result:

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
| Start MP4 | Both audio and video play | NOT RUN |
| Pause/resume | Receiver and host state agree | NOT RUN |
| Seek forward/back | Playback moves to requested position | NOT RUN |
| Position/duration | Values follow receiver playback | NOT RUN |
| End-of-file | Correct ended state and resource cleanup | NOT RUN |
| Stop from sender/receiver | Correct state and resource cleanup | NOT RUN |
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
