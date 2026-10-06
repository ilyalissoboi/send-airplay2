# Receiver validation record

Status: NOT RUN. Fill out one record per receiver firmware and sender platform.

## Environment

- Date and library commit:
- Receiver manufacturer/model/generation:
- Firmware version and build:
- AirPlay access policy and PIN/password settings (no secrets):
- Sender OS/version/architecture:
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
