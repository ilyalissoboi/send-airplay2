# Noninteractive E2E runner

`scripts/run_e2e.py` exercises the current CLI using an existing paired Windows
desktop profile. It requires Python 3.10+ and only the standard library; Python
is a development/test tool, not a native-library runtime dependency. Run it as
the Windows user who owns the saved credentials. Pair manually once first, then
use the resulting profile. The runner does not collect a PIN or enroll a device.

From the repository root, replace the documentation IP with the receiver's
numeric address from discovery:

```powershell
python scripts/run_e2e.py --cli build-pairing-static/Release/airplay2-cli.exe --address 192.0.2.10 --port 7000 --profile living-room-test --report build-e2e/report.json
```

Use the shared-build executable to validate that deployment too. For IPv6, pass
the address without a `%scope` suffix and provide `--scope-id N`; scoped
link-local IPv6 requires a nonzero numeric interface index. Authentication
storage currently supports Windows desktop. Offline runner contracts run on
Windows/Linux/macOS, which does not establish live authentication on other hosts.

## Actions and evidence

The default run contains 16 checks, plus an explicit skipped deletion case:

| Case | Expectation | Evidence kind |
|---|---|---|
| Two discovery snapshots | Exactly one device advertises the target AirPlay endpoint; target identity stays the same; no duplicate device/service records | Live discovery |
| Stored-profile verification | Exit 0 and the exact successful peer-verification checkpoint | Live receiver |
| Three repeated reconnects | Independent processes load the same profile and verify successfully | Live receiver |
| Existing-profile protection | `pair` refuses the saved profile before PIN prompting/enrollment | Local CLI |
| Missing-profile verification | A random unused profile is refused explicitly | Local CLI |
| Redirected PIN input | `pair` on that unused profile refuses noninteractive input | Local CLI |
| Forget absent profile | No-op success; no credential is deleted | Local CLI |
| Refused connection | A selected/released closed loopback port yields a native network failure | Loopback fault |
| Silent response | A loopback peer accepts/drains the request but never responds; native deadline expires | Loopback fault |
| Disconnection | A loopback peer closes after receiving request bytes; native disconnect/network failure | Loopback fault |
| Recovery after each fault | A fresh process verifies successfully against the actual receiver | Live receiver |

Fault peers never forward traffic to the Apple TV. They retain no request
payloads. These checks establish CLI failure handling and subsequent live
verification, not recovery from an actual Apple TV network outage or interruption
of an established encrypted application exchange. Discovery advertisements are
unauthenticated; pinned-key peer verification supplies the authentication gate.
Unrelated LAN devices may appear/disappear between snapshots.
The refusal test releases its ephemeral port before connecting because a
bound/non-listening socket produced timeouts on macOS. Another process could
claim that port; an unexpected CLI outcome fails the case rather than passing
as a timeout. Silent/disconnect peers keep their listening port until teardown.

If the baseline verification fails, profile-dependent cases are explicitly
skipped; they cannot pass merely because every command fails. A missing-profile
collision also blocks operations on that random profile. Every expected CLI
checkpoint is checked alongside the exit code; exit 0 alone is insufficient.

## Optional profile deletion

The primary `--profile` is never deleted. To test deletion, first manually pair
a **separate disposable profile**, then explicitly select it:

```powershell
python scripts/run_e2e.py --cli build-pairing-static/Release/airplay2-cli.exe --address 192.0.2.10 --profile living-room-test --forget-disposable-profile e2e-disposable
```

The runner verifies that disposable profile, deletes its local credentials,
checks that subsequent verification reports missing credentials, then checks
idempotent deletion. The disposable and primary names must differ. Receiver-side
pairing remains unchanged; this is not receiver revocation. Failed disposable
verification blocks deletion. This option is off by default and is never used
by CI; the runner neither clones nor exports credentials to create a profile.

## Bounds and reports

- `--repeat`: 1..100 additional independent verifications, default 3.
- `--timeout-ms`: native verification deadline, default 10000 ms.
- `--fault-timeout-ms`: 200..60000 ms, default 500 ms. Refusal uses at least
  3000 ms so Windows TCP has time to report a refused connection rather than
  reaching the short read deadline. The effective value is recorded per fault.
- `--process-slack-ms`: extra process startup/storage/teardown budget, default
  2500 ms. A runner-enforced kill is always FAIL, never proof of a native timeout.
- `--discovery-ms`: duration of each scan, default 5000 ms.
- `--skip-discovery`: explicitly skip discovery when multicast is unavailable;
  the report records that missing coverage.

Child stdin is redirected to the null device. Commands use argument lists without
a shell. Stdout/stderr are drained separately with an 8-MiB bound per stream.
Timeout/overflow kills and reaps the CLI, and unexpected output fails its case.
Raw child output is processed in memory and never printed or stored in reports.

JSON schema version 1 includes UTC start time, CLI/runner SHA-256 fingerprints,
host OS/build/architecture, receiver address family/port/scope usage, and case
PASS/FAIL/SKIP, controlled reasons, exit codes, durations and bounded numeric or
boolean observations. IPs, names, IDs, profiles, TXT/public keys, paths, credential
blobs, PINs and raw exception text are omitted. `playback_tested` is always false.
These reports establish only the selected cases for the explicitly documented
receiver/firmware/host; they are not a compatibility certificate.

Reports default to a unique `build-e2e/report-<UTC>-<suffix>.json` filename.
`--report` selects a new file; existing files are never overwritten. Case failures
retain the report. Unexpected infrastructure failures retain partial case results
and a sanitized `runner_error`. Exit status is 0 when enabled cases pass, 1 on
case/infrastructure failure, and 2 on argument/report I/O failure. Intentional
skips remain visible even when exit status is 0.

## Validation boundaries

Run offline runner tests with `python tests/e2e_runner_tests.py`. They use original
public synthetic subprocess fixtures and real ephemeral loopback faults. CI runs
these contracts on Windows/Linux/macOS, alongside the existing native static/
shared/sanitizer matrix. CI never accesses hardware or application credentials.
No third-party implementation or dependency was added.

Live static/shared results and sanitized artifacts are recorded in
[receiver-validation.md](receiver-validation.md). PIN enrollment, wrong PIN,
physical-console cancellation/echo restoration, host/receiver reboot, receiver
revocation, real departure/interface changes and live IPv6 require separate
validation. Playback, media serving and packaged-host proofs still need
implementation; this runner cannot claim them as tested.

At portable source commit `650a0440a72d5e263bcca3fa8e6aa623cd628a7f`, all native/
sanitizer/offline runner jobs passed in the
[PR CI run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37454270222)
and [push run](https://github.com/ilyalissoboi/send-airplay2/actions/runs/37454265231).
Verify the actual final PR head after documentation updates. The committed live
reports identify the tested Windows binaries and runner bytes separately.
