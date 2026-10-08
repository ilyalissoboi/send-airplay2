# UWP native build (D55)

Status: **implemented; receiver-tested in the packaged UWP test host on one Apple
TV / Windows host, 2026-10-08 (x64 D55, x86 D57; ARM64 built in CI only).** This is D49 step 3: the native library built
for app (UWP) packages, against the app C runtime and without the desktop-only
Credential Manager adapter. Record:
[receiver-validation.md](receiver-validation.md#uwp-native-build-d55-2026-10-08)
and its [artifact](validation/native-uwp-native-build-windows-2026-10-08.json).

## Why

D53 found that the desktop-built DLLs load in the AppContainer only because the
desktop C runtime happens to be installed system-wide. A packaged app should
depend on the app C runtime (`VCRUNTIME140_APP.dll`, from the
`Microsoft.VCLibs.140.00` framework that the MSIX tooling already declares), be
marked AppContainer, and use only APIs available to apps. D49 also decided that
UWP builds compile out Credential Manager, so the binary carries no desktop-only
credential imports and packaged hosts supply their own store.

## Build

From the repository root, using the pinned vcpkg checkout. Use a separate vcpkg
installed directory: manifest mode prunes packages it was not asked for, so
sharing one with the desktop triplet would remove those packages.

```powershell
cmake -S . -B build-uwp -G "Visual Studio 18 2026" -A x64 `
  "-DCMAKE_SYSTEM_NAME=WindowsStore" "-DCMAKE_SYSTEM_VERSION=10.0" "-DBUILD_SHARED_LIBS=ON" `
  "-DCMAKE_TOOLCHAIN_FILE=build-tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_TARGET_TRIPLET=x64-uwp" "-DVCPKG_OVERLAY_PORTS=$PWD/vcpkg-overlays/ports" `
  "-DVCPKG_INSTALLED_DIR=$PWD/build-tools/installed-uwp"
cmake --build build-uwp --config Release
./scripts/check_uwp_binaries.ps1 -Directory build-uwp/Release
```

Quote every `-D` argument in Windows PowerShell 5.1: unquoted, it splits
`-DCMAKE_SYSTEM_VERSION=10.0` at the dot and CMake sees `10`.

`build-uwp/Release` then holds `send_airplay2.dll` and `libcrypto-3-x64.dll`,
everything a package needs. The UWP test host packages that directory by
default (`Sap2NativeDir`, [uwp-host.md](uwp-host.md)).

### Other architectures (D57)

The same configuration builds for x86 (`-A Win32`, `x86-uwp`, OpenSSL's DLL is
`libcrypto-3.dll`) and ARM64 (`-A ARM64`, `arm64-uwp`, `libcrypto-3-arm64.dll`).
Use one build and installed directory per architecture (`build-uwp-x86`,
`build-uwp-arm64` and matching `installed-uwp-*`), and pass `-Machine x86` or
`-Machine ARM64` to the check script, which also verifies the binaries'
architecture. ARM64 needs Visual Studio's ARM64 build tools component. CI builds
all three and runs the check (job `uwp`, one matrix entry per architecture).

## What differs from the desktop build

| Area | UWP build | Reason |
| --- | --- | --- |
| Targets | The library only; tools and tests are skipped | AppContainer programs cannot run outside a package. Receiver use is tested through the packaged host. |
| Credential Manager | Compiled out (`WINAPI_PARTITION_DESKTOP` check in `credential_store.cpp`); no `advapi32` | `wincred.h` is desktop-only. The built-in store reports `SAP2_ERROR_UNSUPPORTED`, as on platforms without one; hosts pass a `sap2_credential_store` (D49). |
| Botan | Static, linked into `send_airplay2.dll` | Botan's own `uwp` target builds static libraries only ("Shared libs not supported on uwp"). |
| Beast files | `BOOST_BEAST_USE_WIN32_FILE=0` | Beast's Win32 file backend calls `CreateFileW`, which apps lack. The library serves host callbacks and never opens files through Beast. |
| Asio | Select reactor instead of IOCP (Asio's own app-mode choice) | Unchanged library code; the desktop suite also passed with `BOOST_ASIO_DISABLE_IOCP` (32/32), as a check of that path. |
| Dependency DLLs | `libcrypto-*.dll` copied beside the library after the build | vcpkg copies dependency DLLs only next to programs. |

### Botan overlay port

`vcpkg-overlays/ports/botan` is vcpkg's `botan` port at the pinned vcpkg commit
`434307da09bc05b2c86996dccc8b2351fc0d5d37` (MIT-licensed port files, notice in
`vcpkg-overlays/LICENSE-vcpkg.txt`; Botan itself is BSD-2-Clause), with changes that apply only when the target is UWP:
`"supports": "!uwp"` removed, `port-version` 1, static linkage forced,
`--os=uwp`, a minimized build of the modules the library uses (`ffi`, `srp6`,
`sha2_64`, `system_rng`, `ed25519`) and no `botan-cli`. Botan's `uwp` target
uses `BCryptGenRandom` for the system RNG. Desktop builds do not pass the
overlay and keep the curated port.

On MSVC, Botan's `ffi.h` declares its functions `dllimport` unless `BOTAN_DLL`
is defined, and the static package config does not define it. CMake therefore
adds `BOTAN_DLL=` to the static Botan target on MSVC.

## Asio thread bug found on hardware

The first UWP-built library crashed the host at every Stop
(`0xc0000409` in `ucrtbase!abort`). The host's abort trace resolved the stack
to `sap2_cast_stop` → `MediaServer::Impl::stop` → `asio::thread_pool::join`. In
app builds Boost 1.92's Asio runs pool threads on `winapp_thread`, whose
`join()` waits but leaves the thread marked joinable, so its destructor calls
`std::terminate()`. Any joined `asio::thread_pool` aborts there; desktop builds
use `win_thread` and are unaffected.

The media server now runs source reads on its own `ReaderPool`: a fixed set of
`std::thread`s that drain one `io_context`, with the same semantics (`join()`
lets queued reads finish, then joins). Nothing else in the library uses
Asio-owned threads. Desktop tests cover the pool's behavior; only a packaged run
covers the app-mode difference.

## Certification kit (D57)

The user ran the Windows App Certification Kit (10.0.28000.2526) in an elevated
shell on the installed x64 test host 0.1.12.0, which packages this build:

```powershell
appcert.exe test -packagefullname <PackageFullName> -reportoutputpath <report.xml>
```

26 of 27 tests passed, including deployment and launch, crashes and hangs, the
binary analyzer and package sanity. **Supported APIs failed only for the host
executable**: six kernel32 functions (`CreateMemoryResourceNotification`,
`GetProcessGroupAffinity`, `IsProcessInJob`, `QueryInformationJobObject`,
`SetXStateFeaturesMask`, `VirtualAllocExNuma`) used by the .NET Native AOT
runtime compiled into `SendAirPlay2.UwpHost.exe`. No test reported
`send_airplay2.dll` or `libcrypto-3-x64.dll`. Whether the Store accepts those
runtime calls is a question for Screenbox's .NET toolchain, not the library.
Record: [artifact](validation/native-uwp-architectures-wack-windows-2026-10-08.json).

## Not covered

- ARM64 on a device: CI builds and checks `arm64-uwp`; no ARM64 run (D57).
- A Store-style certification of a signed package: D57 ran the kit on the
  sideloaded test host (below).
- A device without the VCLibs framework installed beforehand (sideloading
  normally installs it from the package's `Dependencies` folder).
- The upstream Asio report or fix; the workaround does not depend on one.
