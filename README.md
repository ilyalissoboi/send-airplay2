# send-airplay2

A C++17 library for casting unprotected media files to AirPlay receivers such
as Apple TV and the macOS AirPlay Receiver. It discovers receivers, pairs by
PIN, serves media over HTTP and controls playback. The project provides an
experimental C API, a C# binding and command-line tools.

## Capabilities

- mDNS/DNS-SD discovery and PIN pairing with persistent credentials.
- Progressive file delivery, or on-demand MP4/MOV/Matroska remux to fragmented
  MP4 HLS with H.264/HEVC video, supported audio and text subtitles.
- Optional conversion of selected unsupported video audio to 48-kHz stereo
  AAC while copying the video. The finite input table includes DTS-core, FLAC,
  Opus, Vorbis, PCM and TrueHD configurations.
- An explicit audio-only profile: validated stereo AAC/M4A and CBR/VBR MP3
  play progressively; covered FLAC/PCM/Ogg inputs can convert to stereo AAC HLS.
- Play, pause, seek, status, cancellation and ordered session cleanup.
- Built-in credential stores for Windows desktop, macOS and Linux, plus host
  callbacks for packaged apps and other storage providers.

The API is experimental and not frozen. A codec name alone does not establish
input support: see the [video audio policy](docs/audio-transcoding.md) and
[audio-only admission table](docs/automatic-audio-only.md).

Receiver observations cover Apple TV 4K/tvOS 26.6 and MacBook Pro/macOS 26.7.1
from Windows 11 x64, including packaged UWP hosts. CI builds other host platforms;
that does not establish receiver interoperability. See
[compatibility and limits](docs/receiver-validation.md).

DRM, screen mirroring, system-audio capture, audio-only speakers/RAOP transport,
multiroom and general video transcoding are outside the implemented scope.
Converted audio is stereo; surround preservation is future work.

## Build and test

Install CMake 3.20+, a C++17 toolchain, OpenSSL 3.5+, Botan 3.12+ and Boost 1.92+.
Botan's own build requires C++20. With dependencies available to CMake:

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Add `-DBUILD_SHARED_LIBS=ON` for a shared library. Audio conversion defaults OFF;
enabling it requires the restricted FFmpeg profile and additional CMake options.
The [build guide](docs/building.md) covers pinned vcpkg dependencies, optional
conversion, platform requirements and tests. See the separate
[UWP build guide](docs/uwp-native-build.md) for AppContainer libraries.

## Try the CLI

These examples use the executable location from a Visual Studio Release build.
Other generators normally place it directly in the build directory. Replace the
documentation address with the numeric address reported by discovery. The CLI's
PIN prompt currently requires an interactive Windows console.

```powershell
build/Release/airplay2-cli.exe discover
build/Release/airplay2-cli.exe pair --address 192.0.2.10 --profile living-room
build/Release/airplay2-cli.exe cast --address 192.0.2.10 --profile living-room --file clip.mp4
build/Release/airplay2-cli.exe cast --address 192.0.2.10 --profile living-room --file clip.mkv --remux
```

During playback, enter `status`, `pause`, `play`, `seek SECONDS` or `stop`.
Run the CLI without arguments for its complete command and option list.
`airplay2-api-host` exercises the same workflow through the C API.

## Embed the library

| Interface | Entry point |
| --- | --- |
| Playback and media callbacks | [playback.h](include/send_airplay2/playback.h), [API guide](docs/public-api.md) |
| Pairing and trusted storage | [pairing.h](include/send_airplay2/pairing.h), [credentials.h](include/send_airplay2/credentials.h) |
| Receiver discovery | [receivers.h](include/send_airplay2/receivers.h), [discovery guide](docs/discovery.md) |
| C# binding | [bindings/csharp](bindings/csharp), [binding guide](docs/csharp-binding.md) |
| Packaged example | [UWP test host](docs/uwp-host.md) |

Current source implements API 6. Published packages are distributed as
[GitHub prereleases](https://github.com/ilyalissoboi/send-airplay2/releases),
not through nuget.org. `SendAirPlay2.StereoAac 0.6.0-ci.330` contains the API-6
x64 UWP codec runtime; the older codec-free `SendAirPlay2 0.3.0-ci.207` is API 3.
Do not mix a binding from one API version with another package's native files.
See [packaging and consumption](docs/nuget-package.md).

The separate [Screenbox fork](https://github.com/ilyalissoboi/Screenbox) provides
a player integration and downloadable AirPlay preview builds. Its UI, build and
release instructions belong to that repository.

## Contribute

Start with [CONTRIBUTING.md](CONTRIBUTING.md), the
[documentation index](docs/README.md) and [architecture](docs/design.md).
The [roadmap](docs/roadmap.md) distinguishes unfinished work from implemented
features. Report receiver failures with sanitized reproduction steps and the
receiver model/firmware; do not include pairing secrets or private media URLs.

## License

The core and binding use [Apache-2.0](LICENSE). Dependency licenses and source
provenance are documented in [dependencies.md](docs/dependencies.md).
The optional dynamically linked FFmpeg runtime has separate LGPL notices and
source/build-material requirements; preserve them when redistributing it.
