# send-airplay2

Experimental native sender library for supported, unprotected media on tested
AirPlay receivers. Target hosts: Windows, Linux, macOS and Android.

**Status: foundation only. This code cannot discover, pair with, or cast to a
receiver yet. No receiver compatibility has been established.**

The first implemented component is a dependency-free C++17 HTTP byte-range
resolver with a C interface. It will support local-file serving and seeking.
It is not an HTTP server or a protocol implementation. The pre-1.0 API is not frozen.

## Build and test

Requires CMake 3.20+ and a C++17 compiler:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Use `-DBUILD_SHARED_LIBS=ON` for a shared library. CI covers static and shared
builds on Windows, Linux and macOS. Android build/device validation is pending.

## Milestones

1. Standalone tested library, starting with local H.264/AAC MP4 playback on a
   specified Apple TV model/firmware. Prove pairing, reconnect, playback, pause,
   resume, seek, position reporting and stop through a CLI and a packaged Windows
   C# host. Expand host/receiver coverage only after explicit tests.
2. Integrate into a Screenbox fork alongside Chromecast through a shared casting
   abstraction, retaining the existing Chromecast implementation.

Initial exclusions: DRM, screen mirroring, system-audio capture, synchronized
multiroom, automatic transcoding, and universal receiver/codec compatibility.

See [design and implementation sequence](docs/design.md) and
[receiver validation checklist](docs/receiver-validation.md).

License: Apache-2.0, as established by the repository's original LICENSE.
No third-party implementation code is included in this foundation.
