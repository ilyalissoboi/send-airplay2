# Reference sender baseline (pyatv)

Status: steps 1-3 (scan and AirPlay pairing) PASS, user-reported on 2026-10-06; see
[receiver-validation.md](receiver-validation.md). Step 4 (playback) NOT RUN.
These steps require the user's Windows host on the same LAN as "Living Room".
A cloud session cannot reach that LAN.

This baseline uses an existing sender to establish what the receiver accepts
before native session code is written. It is reference evidence about the
receiver, not evidence that this library can play media. pyatv is an external
MIT-licensed development tool. It is not a build/runtime dependency, and no
pyatv source is copied into this repository.

## Privacy rules

- `atvremote pair` prints the new credentials after success ("You may now use
  these credentials: ..."). Do not paste that line into chat, issues, PRs or
  commit messages.
- pyatv's default file storage keeps credentials in `%USERPROFILE%\.pyatv.conf`.
  Keep that file outside the repository and never commit it.
- When reporting results, omit IP/MAC addresses, device identifiers, PINs and
  private media URLs. Protocol names, ports, status values, pairing requirements,
  versions and error messages are fine.

## Step 1: install pyatv (Windows PowerShell)

Uses a dedicated virtual environment outside the repository. Version 0.18.0 is
the release inspected while preparing these steps.

```powershell
py -3 -m venv "$env:USERPROFILE\pyatv-baseline"
& "$env:USERPROFILE\pyatv-baseline\Scripts\python.exe" -m pip install pyatv==0.18.0
$atv = "$env:USERPROFILE\pyatv-baseline\Scripts\atvremote.exe"
& $atv --version
```

## Step 2: scan the receiver

Get the receiver's IPv4 address from `airplay2-cli discover`. Replace the
documentation address `192.0.2.10` with it. `-s` sends a unicast scan to that
host only.

```powershell
& $atv -s 192.0.2.10 --scan-timeout 5 scan
```

Report back from the output, sanitized: each listed protocol (AirPlay,
Companion, RAOP, ...), its port, and its "Pairing"/password requirement lines.

## Step 3: pair the AirPlay protocol (needs user at the TV)

```powershell
& $atv -s 192.0.2.10 --protocol airplay pair
```

A PIN appears on the TV. Type it at the `Enter PIN on screen:` prompt. Success
prints "Pairing seems to have succeeded" followed by the credentials line, which
must not be shared. This creates a separate pairing for the controller name
`pyatv`. It does not replace or remove the `airplay2-cli` profile.

Then confirm that a fresh connection reuses the stored credentials:

```powershell
& $atv -s 192.0.2.10 device_state
```

Report back: whether pairing succeeded and the exit status (`$LASTEXITCODE`).
On failure, report the error message without credentials. Also report
`device_state` output, the exact tvOS version and build from Settings > General >
About, and the AirPlay access settings without secrets (who may AirPlay; whether a
password is required).

## Step 4: reference URL playback (later; prepared separately)

After pairing, play one unprotected H.264/AAC MP4 that the user owns or can
redistribute:

```powershell
& $atv -s 192.0.2.10 play_url=http://<sender-lan-ip>:<port>/<path>
& $atv -s 192.0.2.10 device_state position total_time
& $atv -s 192.0.2.10 set_position=30
& $atv -s 192.0.2.10 pause
& $atv -s 192.0.2.10 play
& $atv -s 192.0.2.10 stop
```

The receiver fetches the URL itself. Serve the file from an HTTP server that
supports byte ranges and is reachable through the Windows firewall from the Apple
TV's address. The plan is a small `airplay2-cli serve <file>` command over the
existing `MediaServer`. It would also test the receiver's fetch and firewall
reachability independently of session code. Record the media SHA-256, container,
codecs, duration and dimensions in [receiver-validation.md](receiver-validation.md).
Run with `--debug` only to capture the request sequence, and sanitize the output
before sharing it: it may include identifiers, addresses and URLs.
