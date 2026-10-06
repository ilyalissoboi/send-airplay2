# send-airplay2

Experimental native sender library for supported, unprotected media on tested
AirPlay receivers. Target hosts: Windows, Linux, macOS and Android.

**Status: experimental discovery and HTTP range resolution. This code cannot
pair with or cast to a receiver yet. No playback compatibility has been established.**

The C++17 core includes a byte-range resolver with a C interface
and a bounded mDNS/DNS-SD scanner with an experimental C++ interface and diagnostic
CLI. The range resolver will support local-file serving and seeking; an HTTP
server is not implemented. The pre-1.0 API is not frozen.
Private pairing TLV8 and encrypted control-record codecs are implemented using
OpenSSL, with independent vector and failure tests. Private authenticated peer
verification and first-time PIN/SRP message processing are also implemented.
Receiver transport and credential storage are still pending.
See [pairing transport foundation](docs/pairing-transport.md).
See [peer verification](docs/peer-verification.md) for trust and state contracts.
See [PIN pairing](docs/pin-pairing.md) for the private provisioning contract.

## Build and test

Requires CMake 3.20+, a C++17 compiler, OpenSSL 3.5+ and Botan 3.12+ development
libraries (Botan modules: ffi, srp6, sha2_64, system_rng). Building Botan itself
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
See [design and implementation sequence](docs/design.md) and
[receiver validation checklist](docs/receiver-validation.md).

License: Apache-2.0, as established by the repository's original LICENSE.
No third-party implementation source is copied into the repository. The new
cryptographic adapters link OpenSSL's Apache-2.0 `libcrypto` and BSD-2-Clause Botan.
