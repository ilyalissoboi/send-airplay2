# NuGet package (Screenbox step 3)

Status: **`nuget-v0.3.0-ci.207` is published as a GitHub prerelease;
not on nuget.org (2026-10-11).** Its public-download hash is verified, and
[Screenbox fork PR #8](https://github.com/ilyalissoboi/Screenbox/pull/8) pins
that version and hash. The fork's merged `main` still restores `ci.196` until
that PR merges; final `ci.207` running-DLL and receiver validation remain pending.
The user chose GitHub releases over a machine-local package source, which CI
cannot reach. nuget.org publishing is still pending, and is needed before an
upstream Screenbox PR. This document covers the package, how to pack and
publish it, and how a host consumes it.

## Contents

`SendAirPlay2.<version>.nupkg`:

| Path | What |
| --- | --- |
| `lib/netstandard2.0/SendAirPlay2.dll`, `.xml` | The [C# binding](csharp-binding.md) and its documentation |
| `runtimes/win-x64/native/` | `send_airplay2.dll`, `libcrypto-3-x64.dll` |
| `runtimes/win-x86/native/` | `send_airplay2.dll`, `libcrypto-3.dll` |
| `runtimes/win-arm64/native/` | `send_airplay2.dll`, `libcrypto-3-arm64.dll` |
| `THIRD-PARTY-NOTICES.txt` | OpenSSL (Apache-2.0), Botan (BSD-2-Clause), Boost (BSL-1.0), from the vcpkg ports that built them |
| `LICENSE.txt`, `README.md` | Apache-2.0; the package readme (`bindings/csharp/SendAirPlay2/PACKAGE.md`) |
| `BUILD-INFO.txt` | Version, the binding's source commit (marked if the tree had uncommitted changes), architectures, each native library's source commit and SHA-256 |

The native libraries are the UWP builds ([uwp-native-build.md](uwp-native-build.md)):
AppContainer, the app C runtime from the `Microsoft.VCLibs.140.00` framework,
Botan linked in, no Credential Manager. A host must use a RID-specific build
(`win-x64`, `win-x86`, `win-arm64`) so NuGet copies them next to the app, and
supply its own `ICredentialStore`. The package has no NuGet dependencies.

**Versions** follow the C interface: `0.3.x` is `SAP2_PLAYBACK_API_VERSION` 3.
A local pack is `0.3.0-local.<UTC yyyyMMddHHmmss>` and a CI pack
`0.3.0-ci.<run number>`, so a host never restores a stale copy of an earlier
pack from NuGet's global package cache under the same version.

## Packing locally

Build the UWP libraries first (one configured tree per architecture,
[uwp-native-build.md](uwp-native-build.md)), then:

```powershell
./scripts/pack_nuget.ps1 -Native 'x64=build-uwp/Release','x86=build-uwp-x86/Release','arm64=build-uwp-arm64/Release' -Build
```

- `-Native` lists `arch=directory` entries; leave out an architecture that was
  not built. The script warns that an app built for a missing architecture
  cannot cast. ARM64 needs Visual Studio's ARM64 build tools.
- `-Build` rebuilds each tree with CMake first. Every directory must pass
  `check_uwp_binaries.ps1`.
- **Provenance:** the build writes `send_airplay2.source.txt` beside the
  library (`cmake/write_source_stamp.cmake`: the commit at link time, and
  whether native sources had uncommitted changes). The script refuses a library
  without a stamp, one built from uncommitted native changes, or one whose
  commit's native sources (`src`, `include`, `CMakeLists.txt`, `cmake`,
  `vcpkg.json`, `vcpkg-overlays`) differ from `HEAD`; rebuild it with `-Build`.
- The notices come from the vcpkg share directory recorded in the first tree's
  `CMakeCache.txt`; `-VcpkgShare` overrides it.
- `-OutputDirectory` defaults to `packages-local` (ignored by git); `-Version`
  or `-VersionSuffix` override the version.
- Needs the .NET SDK (`dotnet pack`) and Visual Studio's `dumpbin`.

## CI

The `uwp` job keeps each architecture's native libraries and notice files as
an artifact, and the `nuget` job packs all three into `nuget-package`
(`0.3.0-ci.<run number>`), kept for 14 days. CI itself publishes nothing.

## GitHub prereleases (for the Screenbox fork)

Until nuget.org publishing, the Screenbox fork's CI cannot reach a local feed,
so the user chose GitHub prereleases on this repository as the package's
source (2026-10-10):

1. Run the workflow on `main` (`gh workflow run build.yml --ref main`), so
   `BUILD-INFO.txt` names a commit on `main`.
2. Download that run's `nuget-package` artifact and check its
   `BUILD-INFO.txt` (commit and the three architectures).
3. With the user's approval for each one, create a prerelease tagged
   `nuget-v<version>` on that commit, with the `.nupkg` as its asset.

The fork pins the version in `Screenbox.Core.csproj`, and a script there
downloads that release asset into its feed folder before restore.

## Consuming from a local feed or a prerelease

A host adds the output folder as a package source next to nuget.org and
references the exact version:

```xml
<!-- nuget.config -->
<add key="send-airplay2-local" value="..\send-airplay2\packages-local" />
```

```xml
<PackageReference Include="SendAirPlay2" Version="0.3.0-local.20261009162323" />
```

A machine without that folder, such as a CI runner, cannot restore the
package; the Screenbox fork fills its feed folder from the GitHub prerelease
instead, and a developer can copy a fresh local pack into the same folder.

## Notices

Apps that redistribute the native libraries must keep the notices: the
package's `THIRD-PARTY-NOTICES.txt` covers OpenSSL, Botan and Boost
([dependencies.md](dependencies.md)). The package adds no dependency to the
library; packing uses only the .NET SDK.

## Results (2026-10-10)

- A local pack from `c8feb27` plus this change with x64 and x86 (this machine
  has no ARM64 build tools) produced the layout above; both architectures
  passed the UWP binary checks, and `csharp_binding_tests` still passes with
  the binding's pack properties.
- The CI pack with all three architectures: a `workflow_dispatch` run on
  `main` at `aa10119` (run 196) packed x64, x86 and ARM64, each built from
  that commit. The user approved publishing it as the prerelease
  `nuget-v0.3.0-ci.196` (SHA-256 `f9454330...2bb2c`); the public download
  matched.
- Screenbox restores it from that prerelease, in its CI and locally (fork
  PR #2). Its x64 app output carries the package's `send_airplay2.dll`, and
  the fork cast with it on the recorded receiver (fork PRs #3-#6).

Not covered: nuget.org publishing, package signing, an ARM64 pack on this
machine, and ARM64 or x86 builds of the host app.

## Published Mac update (D64, 2026-10-11)

PR #39 merged as main `2829242335280419a97b080ae2098d90f8a55b18`; its final
head `811c022` passed all 14 checks. Main workflow run
[38107556167](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38107556167)
produced `SendAirPlay2.0.3.0-ci.207.nupkg`, SHA-256
`e52affdf9f79e20dcd00b59380c9949d1f9b11d1339307f8a2b86ad14e1ffa4d`.
It includes D62 URL controls and D63 explicit HLS EOF handling plus the
event-channel closure race fix. The prior `ci.203` candidate lacks D63 and
was excluded from this update.

The binding and all three native source stamps name that clean main commit;
all six DLL payload hashes match `BUILD-INFO.txt`. Package layout, license and
dependency notices, and extracted x64/x86/ARM64 UWP binary checks passed.
Screenbox's prepared pin restored this exact artifact; VS 2026 MSBuild produced
an x64 MSIX with matching native/crypto DLL hashes and its 86 logic tests passed.
All 14 main CI jobs passed. The user approved publication, and
[ci.207 is published](https://github.com/ilyalissoboi/send-airplay2/releases/tag/nuget-v0.3.0-ci.207)
with its tag pointing to the exact main commit. Both the public download and a
fresh `Get-AirPlayPackage.ps1` download match the package hash above.
[Fork PR #8](https://github.com/ilyalissoboi/Screenbox/pull/8) contains the pins.
The matching development layout is registered as private version 1.0.0.2;
loaded-DLL verification and final receiver validation remain gates. No new
receiver observation is attributed to these package/build checks.
Record: [D64 package verification](validation/nuget-macos-package-windows-2026-10-11.json).
