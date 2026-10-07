# Reference sender baseline (pyatv)

Status: steps 1-3 (scan and AirPlay pairing) PASS, user-reported on 2026-10-06; see
[receiver-validation.md](receiver-validation.md). Step 4 (playback) FAIL on
2026-10-06: pyatv 0.18.0 `play_url` gets HTTP 500 from `/playback-info` on
tvOS 26.6, and the receiver never fetches the URL, even a public Apple URL. This
is a known upstream incompatibility; see
[the observation](receiver-validation.md#pyatv-reference-playback-2026-10-06).
Repeating step 4 with pyatv 0.18.0 will not produce a baseline on tvOS 26.
Step 4 PASSED with the unmerged fix `robkochman/pyatv@8144c77c`, installed by the
user in a separate venv. Video and audio played, fetched from `airplay2-cli serve`; see
[the fork result](receiver-validation.md#reference-playback-with-unmerged-pyatv-fix-2026-10-06).
These steps require the user's Windows host on the same LAN as "Living Room".
A cloud session cannot reach that LAN.

This baseline uses an existing sender to establish what the receiver accepts
before native integration. It is reference evidence about the receiver.
Native session code and the failed standalone G1 experiment now exist; see the
receiver record. Further hypothesis tests should use native `cast`. pyatv is an external
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

## Step 4: reference URL playback with `airplay2-cli serve`

Use one unprotected H.264/AAC MP4 that the user owns or can redistribute. The
receiver fetches the media itself. `airplay2-cli serve` hosts the file with the
project's `MediaServer`, so this step also tests whether the Apple TV can fetch
from this library's server through the Windows firewall. That is separate from
native session code, which does not exist yet.

1. Build the CLI from the branch carrying `serve` (pinned vcpkg build, see README).
2. In one PowerShell window, start serving. Use the receiver's IPv4 address; only
   that address may fetch. Windows Defender Firewall may ask whether to allow
   `airplay2-cli.exe`: allow it on **private** networks only, and confirm the
   LAN's network profile is Private.

   ```powershell
   build\Release\airplay2-cli.exe serve --address 192.0.2.10 --file C:\path\to\clip.mp4
   ```

   It prints `Private URL (do not share or log): http://...`. Keep it private.
   pyatv also runs a UDP NTP timing server that the receiver contacts during
   SETUP. On a host without an inbound rule for the Python interpreter, SETUP
   timed out with `no response to SETUP`. Allow inbound UDP for that
   interpreter, scoped to the local subnet, for the duration of the test. For a
   Microsoft Store Python, the process image is under `WindowsApps`, not the venv.
3. In a second window, play and control with pyatv, pasting the private URL:

   ```powershell
   & $atv -s 192.0.2.10 play_url=<private URL>
   & $atv -s 192.0.2.10 device_state position total_time
   & $atv -s 192.0.2.10 set_position=30
   & $atv -s 192.0.2.10 pause
   & $atv -s 192.0.2.10 play
   & $atv -s 192.0.2.10 stop
   ```

4. Press Enter in the `serve` window. It stops and prints a summary such as
   `Stopped. reads=N bytes=M failed_reads=F span=[first,end)`.

Report back, sanitized: whether video and audio played, each command's output
and exit code (or error text without the URL), the `Stopped.` summary line, and
the media's SHA-256 (`Get-FileHash -Algorithm SHA256`), container, codecs,
duration and dimensions. `reads=0` after `play_url` means the receiver never
fetched from the server: check the firewall prompt, network profile and address.
Record the results in [receiver-validation.md](receiver-validation.md).
Run pyatv with `--debug` only to capture the request sequence, and sanitize the
output before sharing it: it includes the private URL, identifiers and addresses.
