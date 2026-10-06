# Receiver validation record

Status: DISCOVERY OBSERVED; pairing/playback NOT RUN. Fill out one record per
receiver firmware and sender platform.

The environment below was supplied by the user on 2026-10-06 (Asia/Tokyo).
It identifies the intended test setup. A Windows discovery run subsequently
resolved this receiver; pairing and playback results remain pending.

## Environment

- Date and library commit:
- Receiver manufacturer/model/generation: Apple TV 4K (user-provided), advertised model `AppleTV14,1`; generation not independently established.
- Firmware version and build: tvOS 26.6 (user-reported, consistent with advertised `osvers`/`ov=26.6`); exact build not supplied.
- AirPlay access policy and PIN/password settings (no secrets):
- Sender OS/version/architecture: Windows 11 x64; exact OS build not supplied.
- Host: CLI / packaged Windows C# / Android:
- Network: Ethernet/Wi-Fi; same subnet; firewall configuration:
- Media SHA-256, container, codecs, duration, dimensions and bitrate:
- Negotiated protocol/authentication path:
- Reference sender/version and baseline result:

## Required observations

| Test | Expected | Result/evidence |
|---|---|---|
| Discovery and departure | Correct identity; no duplicate/stale entries | NOT RUN |
| First pairing | PIN UI and credential save succeed | NOT RUN |
| Wrong PIN / revoked pairing | Explicit failure, no playback | NOT RUN |
| Reconnect after restart | Stored pairing works when receiver policy permits | NOT RUN |
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
pending; lifecycle behavior has only synthetic test coverage. Reference playback,
pairing and all media controls are NOT RUN. This is not playback certification.
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
No Apple TV PIN-display request, actual PIN entry, application credential save,
receiver revocation or playback operation was attempted. Authentication/playback
hardware results remain NOT RUN. The next receiver gate is interactive hidden PIN
entry, first pairing and `verify` from a newly started CLI, including wrong PIN,
cancel/timeout and receiver-revoked credentials. Record the exact firmware build
and access settings without recording secrets.
