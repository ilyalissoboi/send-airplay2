# send-airplay2

Experimental native sender library for supported, unprotected media on tested
AirPlay receivers. Target hosts: Windows, Linux, macOS and Android.

**Status: experimental discovery, authentication, HTTP media serving and private
URL playback sessions with a development `cast` CLI. Native-only G1 passed on
Apple TV 4K / tvOS 26.6 (23L773) / Windows 11 x64: video/audio played and the TV
returned home after sender shutdown. The session retains a separate native
remote-control session, now extended with native MRP status and controls. G2
controls and selected G3 lifecycle cases passed on that combination, including
Ethernet interruption cleanup and fresh same-credential playback after reconnection
(D42). Receiver-stop intent and the public playback API remain pending.
A historical full-clip run failed sustained video: after buffering near 18 seconds,
the picture froze while audio continued to EOF.**

The C++17 core includes a byte-range resolver with a C interface
and a bounded mDNS/DNS-SD scanner with an experimental C++ interface and diagnostic
CLI. A Boost.Beast/Asio media server now streams immutable byte-source callbacks
with GET/HEAD and byte ranges. Private session integration uses authenticated
SETUP, encrypted events, NTP timing, feedback and `/command` queue messages.
The pre-1.0 API is not frozen; playback has no public API yet.
See [media serving contracts and validation](docs/media-server.md).
Private pairing TLV8 and encrypted control-record codecs are implemented using
OpenSSL, with independent vector and failure tests. Private authenticated peer
verification and first-time PIN/SRP message processing are also implemented.
Private bounded HTTP/RTSP framing and native receiver TCP transport now connect
these flows, with deadlines, cancellation and the encrypted-record transition.
Windows desktop credential storage and CLI pairing/reconnect are implemented;
broader hardware authentication and storage adapters for other hosts are pending.
See [credential storage and CLI authentication](docs/credential-storage.md).
See [pairing transport foundation](docs/pairing-transport.md).
See [peer verification](docs/peer-verification.md) for trust and state contracts.
See [PIN pairing](docs/pin-pairing.md) for the private provisioning contract.
See [receiver transport](docs/receiver-transport.md) for framing, I/O and ownership.

## Build and test

Requires CMake 3.20+, a C++17 compiler, OpenSSL 3.5+ and Botan 3.12+ development
libraries (Botan modules: ffi, srp6, sha2_64, system_rng, ed25519), and Boost 1.92+
Beast/Asio development headers and CMake package configs. Building Botan itself
requires C++20. With maintained packages installed (set `OPENSSL_ROOT_DIR` and
`Botan_DIR` if needed):

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Use `-DBUILD_SHARED_LIBS=ON` for a shared library. CI covers static and shared
builds on Windows, Linux and macOS. Android build/device validation is pending.

For the pinned dependency build on Windows, from the repository root:

```powershell
git clone https://github.com/microsoft/vcpkg.git build-tools/vcpkg
git -C build-tools/vcpkg checkout 434307da09bc05b2c86996dccc8b2351fc0d5d37
.\build-tools\vcpkg\bootstrap-vcpkg.bat -disableMetrics
cmake -S . -B build -DBUILD_TESTING=ON "-DCMAKE_TOOLCHAIN_FILE=build-tools/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

On Linux/macOS, use `bootstrap-vcpkg.sh` with the same manifest/toolchain option.
vcpkg copies its dependency DLLs alongside Windows build targets. Redistributed
builds must include the appropriate OpenSSL/Botan runtimes and license notices; packaged
Windows/Android loading is still untested. See [dependency provenance](docs/dependencies.md).

## Receiver discovery

```sh
# Windows with the default Visual Studio generator:
build/Release/airplay2-cli.exe discover --timeout-ms 10000
build/Release/airplay2-cli.exe discover --json --timeout-ms 10000
# Linux/macOS with a single-config generator:
./build/airplay2-cli discover --json
```

Enable AirPlay and place the receiver on the same LAN as the sender. The scanner
uses active IPv4 multicast interfaces and UDP 5353; allow local-network access
for the CLI in the host firewall. It reports A and AAAA addresses, interface
indices, ports, model and raw advertised TXT fields. An empty scan does not prove
that no receiver exists. Discovery does not establish pairing or playback support.
See [discovery contracts and limits](docs/discovery.md), including JSON fields
and the separation between advertisements and tested compatibility.

## Windows desktop authentication

Choose the receiver's numeric address and port from discovery, then use a local
profile name (lowercase letters/digits/`._-`, starting with a letter/digit):

```powershell
# Replace the documentation address with the actual receiver address.
build/Release/airplay2-cli.exe pair --address 192.0.2.10 --profile living-room
build/Release/airplay2-cli.exe verify --address 192.0.2.10 --profile living-room
build/Release/airplay2-cli.exe forget --profile living-room
```

Pairing prompts for a hidden PIN in an interactive Windows console and stores
authenticated credentials in the current user's Windows Credential Manager.
Existing profiles are never overwritten automatically. Reconnect failure retains
saved credentials for `verify`; `forget` deletes local credentials only.
For scoped IPv6 use `--scope-id` with the numeric interface index. See the
[storage/CLI contract](docs/credential-storage.md) for deadlines, cancellation,
failure recovery and platform limits. See the
[receiver validation record](docs/receiver-validation.md) for the observed pairing
result and remaining restart/revocation/playback gates.

Automate the available noninteractive authentication/discovery checks with the
[E2E runner](docs/e2e-runner.md), using an already paired Windows profile. It runs
repeated verification, profile guards, loopback faults and live recovery, and
writes sanitized JSON results. It does not request a PIN or implement playback.

## Serving a file to a receiver (development)

```powershell
build/Release/airplay2-cli.exe serve --address 192.0.2.10 --file C:\media\clip.mp4
```

`serve` hosts one local file with the bounded media server so that a receiver can
fetch it, for example during the [pyatv reference baseline](docs/reference-baseline.md).
Only the given receiver address may fetch from it. It prints a private URL once,
serves until Enter or end-of-file on standard input, then prints aggregate read
counts. It sends no playback commands. See the
[media server CLI notes](docs/media-server.md#development-cli-serve) for options
and limits.

## Native playback experiment (Windows desktop)

```powershell
build/Release/airplay2-cli.exe cast --address 192.0.2.10 --profile living-room --file C:\media\clip.mp4
```

`cast` reuses a stored profile, starts the receiver-restricted media server and
an authenticated URL/MRP session. Type `status`, `pause`, `play`, `seek SECONDS`
(absolute position), or `stop`; Enter or stdin EOF tears down directly. Media
EOF, receiver terminal state, ownership loss and connection failure now trigger
automatic cleanup without command input. The summary reports the first terminal
reason, whether cleanup finished and fixed failure channel/category; connection
failure returns exit 1. It
prints no private URL or receiver address. `--event-log` enables bounded event
outlines, allowlisted buffering values and a fixed startup phase/state/rate trace,
including failed starts. It also records fixed-label remote notification
observations (shared 256-entry event log) and the retained final MRP state after
joined cleanup. URL and remote output omit arbitrary names/values and request targets;
URL state/type labels use fixed allowlists, with unknown strings reported as `other`.
URL outlines include only allowlisted key paths; malformed bodies report `URL unreadable=yes`.
Numeric remote reason/error/status codes have no inferred meaning. Final MRP
reports the received position rather
than wall-clock progress and is not a fresh receiver query. These observations
do not change terminal classification. Startup success requires one continuous
second of URL playing without an explicitly zero/reverse rate, within the
startup deadline.
A transient playing event is insufficient; no automatic Play retry is sent.
This confirmation is telemetry, not a proof of moving video.

D42's [network check](docs/receiver-validation.md#ethernet-interruption-and-fresh-recovery-d42-2026-10-07)
used a user-confirmed Ethernet cable removal during established playback. The
sender cleaned automatically on a URL feedback timeout, with connection_lost/exit 1.
A fresh cast after reconnection/Home reused credentials and passed user-observed
video/audio and Home at near-end EOF. Recovery starts a new session explicitly;
there is no automatic reconnect or resume. Ambiguous socket closure still does
not prove remote Stop intent; see the [pyatv audit](docs/pyatv-stop-reference.md).

D39's [manual batch](docs/manual-validation.md) passed selected startup,
controls/full EOF and sleep/wake presentation on the recorded receiver/host.
Remote Stop returned Home and cleaned up but still reports connection_lost/exit 1.
D40's [diagnostic comparison](docs/receiver-validation.md#remote-stop-diagnostic-comparison-d40-2026-10-07)
retained that classification: Stop/Home and sleep differed in final MRP state,
but did not establish normal protocol intent. Fresh post-wake video/audio/EOF/Home passed.
`--media-log` reports bounded HTTP range/status/socket-write/completion facts;
socket completion does not prove receipt or decoding (see [diagnostic contracts](docs/media-server.md#opt-in-request-diagnostics)). A `playing` event does not prove visible playback:
G1 passed for the recorded native-only run, with user-observed video/audio and
return to the home screen after sender stop. Native EOF cleanup with stdin held
open and ten short start/stop cycles passed on the recorded receiver; the user
confirmed G2 controls and near-end EOF video/audio/home. A separate full-clip run
froze video after buffering near 18 s while audio continued normally; its EOF
cleanup still passed. D35 diagnostics support media admission capacity as a
buffering cause: an explicit `--media-connections 16` comparison retained MRP and
passed full video/audio/Home with no recorded loading transition. D36 then passed
user-observed controls and ten fresh-process stop/teardown cycles at that budget;
a full-clip repeat reached automatic EOF with no recorded loading transition
and user-confirmed normal video/audio and Home return.
`cast` now defaults to 16 media connections; `--media-connections N` accepts 1..16.
The generic server and `serve` still default to four. The original freeze is low
priority unless it recurs in later testing; insufficient media connections are
the likely cause (user triage decision). The intermittent startup pause remains
active: another 16-slot run paused at zero
without transport commands or remote input. See the [capacity validation record](docs/receiver-validation.md#bounded-cast-admission-policy-and-controls-lifecycle-checks-2026-10-07).
`--minimal-remote` is a separate diagnostic comparison without MRP controls.
Receiver-remote stop and sleep
triggered cleanup; normal stop classification and actual network-loss recovery
remain open G3 work. Native MRP controls are implemented; see the recorded
G2 result and [control contracts](docs/mrp-controls.md).
See [session design and gates](docs/session-design.md) and
[receiver results](docs/receiver-validation.md).

The [D43 PR review](docs/pr-review.md) fixed URL diagnostic redaction and decoded
MRP/event plaintext cleanup on exception paths. Windows static/shared Release each pass
24 CTest targets; offline runner contracts pass 10 tests. These checks are
separate from the dated receiver observations above.

## Milestones

1. Standalone tested library, starting with local H.264/AAC MP4 playback on a
   specified Apple TV model/firmware. Prove pairing, reconnect, playback, pause,
   resume, seek, position reporting and stop through a CLI and a packaged Windows
   C# host. Expand host/receiver coverage only after explicit tests.
2. Integrate into a Screenbox fork alongside Chromecast through a shared casting
   abstraction, retaining the existing Chromecast implementation.

Initial exclusions: DRM, screen mirroring, system-audio capture, synchronized
multiroom, automatic transcoding, and universal receiver/codec compatibility.

Start with the [developer/model handoff](docs/HANDOFF.md) for project history,
decisions, verified state and continuation instructions.
See [design and implementation sequence](docs/design.md),
[reference sender baseline](docs/reference-baseline.md) and
[receiver validation checklist](docs/receiver-validation.md).

License: Apache-2.0, as established by the repository's original LICENSE.
No third-party implementation source is copied into the repository. The new
cryptographic adapters link OpenSSL's Apache-2.0 `libcrypto` and BSD-2-Clause Botan.
