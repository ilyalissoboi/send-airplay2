# send-airplay2

Experimental native sender library for supported, unprotected media on tested
AirPlay receivers. Target hosts: Windows, Linux, macOS and Android.

**Status: experimental discovery and HTTP range resolution. This code cannot
pair with or cast to a receiver yet. No playback compatibility has been established.**

The dependency-free C++17 core includes a byte-range resolver with a C interface
and a bounded mDNS/DNS-SD scanner with an experimental C++ interface and diagnostic
CLI. The range resolver will support local-file serving and seeking; an HTTP
server is not implemented. The pre-1.0 API is not frozen.

## Build and test

Requires CMake 3.20+ and a C++17 compiler:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Use `-DBUILD_SHARED_LIBS=ON` for a shared library. CI covers static and shared
builds on Windows, Linux and macOS. Android build/device validation is pending.

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
No third-party implementation code or runtime dependencies are included.
