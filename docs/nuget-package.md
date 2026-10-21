# Packages and releases

The source builds two mutually exclusive package profiles with the same
`SendAirPlay2` namespace/assembly. Current source uses API/version base `0.6.0`.
Published packages keep their original tagged source and are distributed through
[GitHub Releases](https://github.com/ilyalissoboi/send-airplay2/releases), not nuget.org.

| Profile | Current published reference | Native scope |
| --- | --- | --- |
| `SendAirPlay2` | [0.3.0-ci.207](https://github.com/ilyalissoboi/send-airplay2/releases/tag/nuget-v0.3.0-ci.207), API 3 | Codec-free UWP x64/x86/ARM64 |
| `SendAirPlay2.StereoAac` | [0.6.0-ci.330](https://github.com/ilyalissoboi/send-airplay2/releases/tag/nuget-stereo-aac-v0.6.0-ci.330), API 6 | UWP x64 with optional codec runtime |

Reference one profile, including transitive dependencies. Mixed profiles are
rejected during asset resolution. Match managed/native files from the same
package; an API-6 source binding cannot use the older published API-3 runtime.

## Contents and consumption

Packages contain `lib/netstandard2.0/SendAirPlay2.dll`, XML documentation,
RID-specific `runtimes/win-<arch>/native` files, Apache-2.0 license,
`THIRD-PARTY-NOTICES.txt` and `BUILD-INFO.txt`. The native runtime is built for
AppContainer/app CRT; hosts supply a credential store and network capabilities.

The codec profile adds four dynamic FFmpeg DLLs, `AUDIO-PROFILE.json` and
`source/ffmpeg` with the original source archive, exact overlay/patches,
manifest/triplet, notices and installed port records. Keep these with redistribution.
It supports only `win-x64`; unsupported RIDs must fail rather than omit libraries.

Download the exact `.nupkg` and checksum from its release, verify SHA-256, and add
the containing directory to your application's package sources. CI must download
and verify the same version rather than depend on another machine's feed. Use a
RID-specific application build so native assets copy to the output. See
[C# integration](csharp-binding.md) and [UWP requirements](uwp-host.md).

## Build packages

Build [UWP libraries](uwp-native-build.md) first, one tree per architecture:

```powershell
./scripts/pack_nuget.ps1 -Native 'x64=build-uwp/Release','x86=build-uwp-x86/Release','arm64=build-uwp-arm64/Release' -Build
```

Omit architectures not built; applications targeting them cannot cast. For the
enabled x64 profile, use a matching conversion build with
`SAP2_RUNTIME_VALIDATION=OFF`:

```powershell
./scripts/pack_nuget.ps1 -StereoAac -Native 'x64=build-audio-uwp/Release' `
  -FfmpegSourceArchive 'build-tools/vcpkg/downloads/ffmpeg-ffmpeg-n9.0.2.tar.gz'
```

The script checks UWP imports/architecture, native source stamps and matching
source inputs. It rejects dirty/stale native implementations and private runtime
validation exports. Codec staging also checks installed DLL hashes, exact overlay
SPDX checksums and the source archive SHA-512. `-FfmpegShare`/`-VcpkgShare` select
matching notice directories when inference is inappropriate.

`-OutputDirectory` selects the feed directory. Version overrides must be unique;
the default generated suffix and CI run suffix avoid reusing cached package bytes.
The source stamp and BUILD-INFO identify the actual build, not a later docs commit.

## Release procedure

1. Run the workflow on the intended `main` commit using workflow_dispatch.
   Inspect all checks and select the correct artifact/profile from that run.
   CI creates artifacts; it does not publish releases.
2. Verify package source stamps, ABI/version, RID/native file hashes, notices,
   codec source/configuration and independent consumer restore/build/refusal checks.
3. Prepare exact notes/checksums for those retained bytes. Obtain maintainer
   approval for publication; do not rebuild under an already reviewed version.
4. Create a prerelease tag targeting the actual build source: `nuget-v<version>`
   for the codec-free package or `nuget-stereo-aac-v<version>` for the codec profile.
5. Verify the public tag and fresh unauthenticated asset downloads against the
   retained sizes/hashes. Do not overwrite an earlier release's assets.

Record release identity and validation in the PR/release rather than committing
machine-specific package staging logs. Package checks are not receiver playback
or Store certification. The separate Screenbox repository owns its player builds,
package pins and release process.
