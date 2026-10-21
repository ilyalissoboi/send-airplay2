# Building and testing

Run commands from the repository root. Use separate build trees for static,
shared, UWP and codec-enabled configurations; do not share their vcpkg installed
directories because manifest resolution can prune another configuration's packages.

## Requirements

- CMake 3.20+ and a C++17 compiler. Botan's own build needs C++20 support.
- OpenSSL 3.5+ (`Crypto`), Botan 3.12+ with `ffi`, `srp6`, `sha2_64`,
  `system_rng` and `ed25519`, and Boost 1.92+ Beast/Asio CMake packages.
- Linux: optional `libsecret-1` 0.19+ and pkg-config for the built-in store.
  Without it, use a host credential store or enable the package before building.
- .NET SDK 8+ for optional binding tests; the UWP example requires its .NET 10
  Windows target, Windows SDK and Visual Studio UWP/MSIX tooling.

CI is the reference for tested toolchain configurations:
[build.yml](../.github/workflows/build.yml). Android is not built or tested.

## Pinned dependencies

The manifest pins vcpkg revision `434307da09bc05b2c86996dccc8b2351fc0d5d37`:

```sh
git clone https://github.com/microsoft/vcpkg.git build-tools/vcpkg
git -C build-tools/vcpkg checkout 434307da09bc05b2c86996dccc8b2351fc0d5d37
```

Bootstrap with `build-tools/vcpkg/bootstrap-vcpkg.sh -disableMetrics` on Unix,
or `build-tools/vcpkg/bootstrap-vcpkg.bat -disableMetrics` on Windows. Then pass
`-DCMAKE_TOOLCHAIN_FILE=build-tools/vcpkg/scripts/buildsystems/vcpkg.cmake`
when first configuring each tree. System packages can also be used when their
versions and CMake targets satisfy the requirements.

## Baseline configurations

```sh
cmake -S . -B build-static -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-static --config Release
ctest --test-dir build-static -C Release --output-on-failure

cmake -S . -B build-shared -DBUILD_TESTING=ON -DBUILD_SHARED_LIBS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-shared --config Release
ctest --test-dir build-shared -C Release --output-on-failure
```

Add the toolchain argument to both configure commands when using vcpkg.
Visual Studio places executables in `Release/`; single-configuration generators
normally place them directly in the build directory.

| Option | Default | Purpose |
| --- | --- | --- |
| `BUILD_SHARED_LIBS` | OFF | Build a shared library and binding-testable native runtime |
| `SAP2_SECRET_SERVICE` | ON | Use libsecret when found on Linux |
| `SAP2_DOTNET_TESTS` | ON | Include binding tests when a suitable SDK and shared library are available |
| `SAP2_STEREO_AAC` | OFF | Enable the optional FFmpeg conversion backend |
| `SAP2_AUDIO_CODEC_SPIKE` | OFF | Build the separate codec probe from the root project |
| `SAP2_RUNTIME_VALIDATION` | OFF | Export private offline runtime checks; requires shared conversion build; exclude from distributions |

## Optional conversion build

Use a Visual Studio developer PowerShell with the pinned vcpkg checkout:

```powershell
cmake -S . -B build-stereo -A x64 `
  '-DBUILD_SHARED_LIBS=ON' '-DBUILD_TESTING=ON' '-DSAP2_STEREO_AAC=ON' `
  '-DVCPKG_MANIFEST_FEATURES=stereo-aac' `
  "-DCMAKE_TOOLCHAIN_FILE=$PWD/build-tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_OVERLAY_PORTS=$PWD/vcpkg-overlays/ports" `
  "-DVCPKG_OVERLAY_TRIPLETS=$PWD/tools/audio-codec-spike/triplets" `
  '-DVCPKG_TARGET_TRIPLET=x64-windows-codec-release'
cmake --build build-stereo --config Release
ctest --test-dir build-stereo -C Release --output-on-failure
```

The dedicated triplets build Release dependencies only. Keep checkout/build paths
free of spaces for the FFmpeg build. The overlay restricts codecs, linkage and
features; see [provenance](dependencies.md). Enabling CMake conversion without
the manifest feature and matching FFmpeg package is insufficient.

For the separate codec suite, configure `tools/audio-codec-spike` as the source
directory in a different tree with the same overlay/triplet arguments. Build it,
then run CTest there. For AppContainer use the [UWP instructions](uwp-native-build.md).

## Test boundaries

CTest runs offline parser, crypto, storage, HTTP, session and API tests. Shared
builds add C# tests when a .NET SDK is available. The enabled codec suite checks
owned compressed fixtures and decoded sample/timeline behavior.

Linux credential tests need an unlocked Secret Service on a session bus. CI uses:

```sh
SAP2_REQUIRE_SECRET_SERVICE=1 dbus-run-session -- bash tests/with_test_keyring.sh ctest --test-dir build-shared -C Release --output-on-failure
```

Without an available keyring those tests can skip; inspect the result rather
than treating a skipped store test as a pass. macOS tests use isolated synthetic
login-keychain items. Run the hardware-free E2E runner contracts separately with
`python tests/e2e_runner_tests.py`.

CI includes Windows/Linux/macOS static/shared builds, Linux sanitizers, UWP
x64/x86/ARM64 binary checks and separate Windows codec/package jobs. Device
playback requires [manual observation](manual-validation.md); UWP executables
must run in a package rather than directly from their build directory.
