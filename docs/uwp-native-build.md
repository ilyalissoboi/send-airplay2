# UWP native build

Packaged hosts need WindowsStore/AppContainer libraries linked to the app C
runtime, not desktop DLLs that happen to load on a development machine. UWP
builds exclude desktop Credential Manager and require a host credential store.

## Configure and inspect

Use a Visual Studio developer PowerShell with C++/UWP tools, Windows SDK and
the [pinned vcpkg checkout](building.md). The commands use a separate build tree:

```powershell
cmake -S . -B build-uwp -A x64 `
  '-DCMAKE_SYSTEM_NAME=WindowsStore' '-DCMAKE_SYSTEM_VERSION=10.0' `
  '-DBUILD_SHARED_LIBS=ON' `
  "-DCMAKE_TOOLCHAIN_FILE=$PWD/build-tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  '-DVCPKG_TARGET_TRIPLET=x64-uwp' `
  "-DVCPKG_OVERLAY_PORTS=$PWD/vcpkg-overlays/ports"
cmake --build build-uwp --config Release
./scripts/check_uwp_binaries.ps1 -Directory build-uwp/Release -Machine x64
```

Quote `-D` arguments in PowerShell, especially dotted values. For x86 use
`-A Win32`, `x86-uwp` and `-Machine x86`; for ARM64 use `-A ARM64`, `arm64-uwp`
and `-Machine ARM64`. Keep separate build/installed directories. ARM64 also
requires the corresponding Visual Studio tools.

## Conversion variant

Use the [enabled build options](building.md) with WindowsStore flags and
`x64-uwp-codec-release` instead of the desktop triplet. Include
`SAP2_STEREO_AAC=ON`, `VCPKG_MANIFEST_FEATURES=stereo-aac`, overlay ports and
overlay triplets. Inspect with `check_uwp_binaries.ps1 -StereoAac`. Keep
`SAP2_RUNTIME_VALIDATION=OFF` for distributed builds; turn it on only for the
private offline runtime harness.

The codec-free output includes core and libcrypto; the enabled output adds four
FFmpeg DLLs. Botan is static in both UWP profiles. Use the packaging scripts to
stage notices/source, not an arbitrary DLL wildcard.

## Platform differences

- UWP targets build the library; ordinary CLI/test executables are skipped.
- Desktop credential imports are excluded. Pass a host store such as PasswordVault.
- The app CRT comes from the Microsoft.VCLibs framework. Hosts must declare and
  deploy the appropriate package framework dependencies.
- Botan's overlay forces its UWP target, required modules and static linkage.
- Beast's desktop Win32 file backend is disabled; host callbacks provide media.
- The HTTP server uses its own bounded `std::thread` reader pool. This avoids
  the app-mode Asio pool join/destructor failure encountered during integration.

CI builds/checks x64, x86 and ARM64, but a binary check is not packaged execution.
Use the [UWP host](uwp-host.md) for AppContainer file grants, store, loading and
callback tests. Device results are limited to the
[recorded architectures and receivers](receiver-validation.md).

AppContainer/import checks and the Windows App Certification Kit are different
gates. Passing this repository's binary script is not Store certification of
the host, its managed runtime or a signed distribution.
