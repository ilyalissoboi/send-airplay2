# send-airplay2

Experimental native sender library for supported, unprotected media on tested
AirPlay receivers. Target hosts: Windows, Linux, macOS and Android.

**Status: experimental discovery, authentication, HTTP media serving and private
URL playback sessions with a development `cast` CLI. Native-only G1 passed on
Apple TV 4K / tvOS 26.6 (23L773) / Windows 11 x64: video/audio played and the TV
returned home after sender shutdown. The session retains a separate native
remote-control-only SETUP/event connection. MRP controls and the public playback
API remain pending; this is one tested combination, not universal support.**

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
reason and whether cleanup finished; connection failure returns exit 1. It
prints no private URL or receiver address. `--event-log` enables bounded event
outlines for diagnostics. A `playing` event does not prove visible playback:
G1 passed for the recorded native-only run, with user-observed video/audio and
return to the home screen after sender stop. Native EOF cleanup with stdin held
open and ten short start/stop cycles passed on the recorded receiver; receiver-side
stop, sleep/wake, network interruption/recovery and visual confirmations remain
separate gates. Native MRP controls are implemented; see the recorded
G2 result and [control contracts](docs/mrp-controls.md).
See [session design and gates](docs/session-design.md) and
[receiver results](docs/receiver-validation.md).

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
