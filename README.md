# send-airplay2

A native C++17 library for casting local video files to AirPlay 2 receivers
such as the Apple TV. It discovers receivers, pairs with them by PIN, serves the
media over HTTP from the sending device and controls playback. It has a C API, a
C# binding and a development CLI.

> **Experimental.** The API is not frozen. Receiver testing so far covers one
> Apple TV 4K (tvOS 26.6) from a Windows 11 x64 host, as a desktop app and as a
> packaged UWP app, and one MacBook Pro's AirPlay Receiver from the CLI. Other
> receivers, firmware versions and host platforms are untested. See
> [what has been tested](#tested-so-far).

## What it does

- **Discovery:** an mDNS/DNS-SD scan for AirPlay receivers on the local network.
- **Pairing:** PIN pairing and reconnects with saved credentials. Credentials
  are kept in Windows Credential Manager, the macOS login keychain or the Linux
  Secret Service, or in a store the host app provides.
- **Playback:** casts an unprotected file through a private, receiver-only
  HTTP server, with play, pause, seek, status and stop, and cleans up when the
  media ends or the receiver stops.
- **HLS remux:** turns an MP4/MOV or MKV file into HLS on the fly, without
  re-encoding. Supported codecs are H.264 or HEVC video and AAC, AC-3 or E-AC-3
  audio. Text subtitles become selectable WebVTT tracks: SubRip, WebVTT and ASS
  from MKV files, and tx3g or WebVTT from MP4 files. A ready-made HLS playlist
  can also be cast as is.

Not in scope: DRM-protected media, screen mirroring, system audio capture,
multiroom audio and transcoding.

## Build

You need CMake 3.20+, a C++17 compiler and these development packages:

- OpenSSL 3.5+
- Botan 3.12+ with the modules ffi, srp6, sha2_64, system_rng and ed25519.
  Building Botan itself requires C++20.
- Boost 1.92+ Beast and Asio headers, with their CMake package configs.

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

- **Shared library:** add `-DBUILD_SHARED_LIBS=ON`. CI builds and tests both
  static and shared libraries on Windows, Linux and macOS.
- **Pinned dependencies:** pass
  `-DCMAKE_TOOLCHAIN_FILE=build-tools/vcpkg/scripts/buildsystems/vcpkg.cmake`
  after checking out vcpkg at `434307da09bc05b2c86996dccc8b2351fc0d5d37` in
  `build-tools/vcpkg` and bootstrapping it.
- **Linux:** install `libsecret-1-dev` for the Secret Service credential store.
  Its tests need an unlocked keyring on the session bus and skip without one; CI
  uses `tests/with_test_keyring.sh` to provide it.
- **UWP:** see [docs/uwp-native-build.md](docs/uwp-native-build.md).
- **Android:** not built or tested yet.

Redistributed builds must ship the OpenSSL and Botan runtimes and their license
notices. See [dependency provenance](docs/dependencies.md).

## Try it with the CLI

The CLI is a development tool, and pairing needs an interactive Windows console.
Commands are shown with Windows paths; replace `192.0.2.10` with your
receiver's address.

```powershell
# 1. Find receivers (same LAN; allow UDP 5353 for the CLI in the firewall).
build/Release/airplay2-cli.exe discover

# 2. Pair once; enter the PIN shown on the TV. Saved as profile "living-room".
build/Release/airplay2-cli.exe pair --address 192.0.2.10 --profile living-room

# 3. Cast a file. While it plays, type status, pause, play, seek SECONDS or stop.
build/Release/airplay2-cli.exe cast --address 192.0.2.10 --profile living-room --file C:\media\clip.mp4

# Cast an MKV, or an MP4 with subtitles, through the built-in HLS remux.
build/Release/airplay2-cli.exe cast --address 192.0.2.10 --profile living-room --file C:\media\film.mkv --remux
```

| Command | Purpose |
| --- | --- |
| `discover`, `pair`, `verify`, `forget` | Find receivers and manage saved credentials |
| `cast --file PATH [--remux]` | Play a file directly, or remuxed to HLS |
| `cast --hls PLAYLIST.m3u8` | Play a ready-made HLS presentation |
| `serve`, `remux` | Serve a file without playback, or write the remuxed HLS files to a folder |

Run `airplay2-cli` with no arguments for every option. `airplay2-api-host`
runs the same casts through the public C API only.

## Use it as a library

| Interface | Location |
| --- | --- |
| Playback (C) | [`playback.h`](include/send_airplay2/playback.h): cast from a host read callback, with controls and status. Set `delivery` to `SAP2_DELIVERY_HLS_REMUX` for the HLS remux |
| Pairing and credentials (C) | [`pairing.h`](include/send_airplay2/pairing.h), [`credentials.h`](include/send_airplay2/credentials.h) |
| Discovery (C) | [`receivers.h`](include/send_airplay2/receivers.h) |
| C# (.NET Standard 2.0) | [`bindings/csharp`](bindings/csharp); see [the binding notes](docs/csharp-binding.md) |
| NuGet package (binding + UWP native libraries) | `scripts/pack_nuget.ps1`, not published yet; see [docs/nuget-package.md](docs/nuget-package.md) |
| Packaged UWP example | [`tools/uwp-host`](tools/uwp-host); see [its notes](docs/uwp-host.md) |

The contracts and the version policy are in [docs/public-api.md](docs/public-api.md).

## Tested so far

Receiver: Apple TV 4K (AppleTV14,1), tvOS 26.6 (23L773). Host: Windows 11 x64.

**Passed, as observed by a user at the TV:**
- pairing, then reconnecting with the saved credentials
- playback of video and audio, pause, play and seek, including seeking past
  4 GiB, and starting at a chosen position
- the end of the media, and returning to the Home screen afterwards
- pulling the Ethernet cable during playback: the cast ends and cleans up, and
  a new cast works once the cable is back. The interrupted cast does not
  reconnect or resume.
- the C API, the C# binding and the packaged UWP app
- HLS remux of MP4 and MKV files: H.264, HEVC, AAC, AC-3 and E-AC-3, a
  1 h 54 min film, and text subtitles in sync, with italics

**Open:**
- When the receiver's remote stops playback, the session is still reported as
  a lost connection.
- Playback used to pause occasionally at startup. This was seen while the
  receiver was waking from sleep, and waking it before play most likely fixed
  it; watch for a recurrence on an awake receiver.
- Linux, macOS and Android hosts have not cast to a receiver.

**Mac as receiver** (MacBook Pro, Mac14,10, AirPlay Receiver set to "Anyone on
the same network"; CLI only):
- Pairing and each new cast need the user to accept a request on the Mac
  (unless it accepted this sender a few minutes earlier); pair with
  `--timeout-ms 60000` so there is time to answer.
- Passed: playback of video and audio, pause, play, seek, starting at a
  chosen position, status, the end of the media, and stop. The Mac has no
  remote-control session, so the library falls back to the URL session's own
  controls.

Passing unit tests and CI does not show compatibility with any receiver. Each
receiver result is listed in [docs/receiver-validation.md](docs/receiver-validation.md),
with the raw records in [docs/validation](docs/validation).

## Documentation

- **Start here to continue development:** [docs/HANDOFF.md](docs/HANDOFF.md)
  (history, decisions and current state) and
  [docs/CONTINUATION.md](docs/CONTINUATION.md) (the work queue).
- **Design:** [design.md](docs/design.md), [session-design.md](docs/session-design.md),
  [hls.md](docs/hls.md), [media-server.md](docs/media-server.md) and
  [mrp-controls.md](docs/mrp-controls.md).
- **Protocol pieces:** [discovery](docs/discovery.md),
  [PIN pairing](docs/pin-pairing.md), [peer verification](docs/peer-verification.md),
  [receiver transport](docs/receiver-transport.md) and
  [credential storage](docs/credential-storage.md).

## License

Apache-2.0 (see [LICENSE](LICENSE)). No third-party implementation source is
copied into the repository. The library links OpenSSL `libcrypto`
(Apache-2.0), Botan (BSD-2-Clause) and Boost (BSL-1.0).
