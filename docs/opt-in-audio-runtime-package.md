# Opt-in audio runtime package (D89)

2026-10-16. The user's Proceed authorizes the packaging slice following merged
PR #69 (`64ed4db`, final head `e6a9970`, all 16 checks passed). The recorded
[D88 receiver results](automatic-audio-receiver-gate.md) remain specific to their
original binaries/host/receivers. This slice adds no new receiver result.

## Package contract

| Profile | Identity/version base | Native assets | Default behavior |
| --- | --- | --- | --- |
| Existing | `SendAirPlay2`, `0.3.0` | Core and crypto, supplied UWP architectures | Codec-free; published `ci.207` and Screenbox pin unchanged |
| Explicit `-StereoAac` | `SendAirPlay2.StereoAac`, `0.5.0` | Initially x64 only: core, crypto and four FFmpeg DLLs | Delivery still progressive until the host selects automatic/stereo HLS |

The new identity is a deliberate opt-in boundary; reference it instead of the
existing package. Both expose the same `SendAirPlay2` managed assembly/namespace.
A build target refuses direct references to both profiles and a selected runtime
identifier other than `win-x64`. A class library without a RID may compile;
the consuming app must choose the supported RID. The new version base matches
experimental API 5 without changing the old package's versioning/published assets.

The host still supplies `ICredentialStore`, brokered media callbacks,
`AacFragmentStore` and a quota. No Screenbox implementation/pin change, publication,
audio-only transport or discrete converted surround is included. x86/ARM64 audio
packages require their own build/runtime gates before this profile is widened.

## Build and provenance

Build a shared UWP x64 core with `SAP2_STEREO_AAC=ON` and
`SAP2_RUNTIME_VALIDATION=OFF`, using the existing pinned FFmpeg profile. Private
development validation exports are refused by the distribution check. The
codec-free profile refuses a DLL that imports the codec runtime; opting in cannot
silently produce the old two-DLL package with missing dependencies.

```powershell
./scripts/pack_nuget.ps1 -StereoAac -Native 'x64=build-audio-uwp/Release' `
  -FfmpegSourceArchive 'build-tools/vcpkg/downloads/ffmpeg-ffmpeg-n9.0.2.tar.gz'
```

The matching FFmpeg share directory is inferred from the build's selected Release
import library, or supplied with `-FfmpegShare`. Its installed-prefix DLL hashes
must match the staged DLLs, its SPDX port-file hashes must match the exact current
overlay, and the source archive must match the overlay's SHA-512. Existing native
source-stamp checks still reject stale or dirty native implementations.
`-VcpkgShare` continues to select the core OpenSSL/Botan/Boost notices separately.

The output contains `AUDIO-PROFILE.json` (configuration, hashes and source pin),
all six native DLLs, `THIRD-PARTY-NOTICES.txt`, and `source/ffmpeg`: the original
17-MB release archive, exact overlay/patches, manifest/triplet, installed port
records, notices and rebuild instructions. The local complete artifact is about
21 MB. Machine-specific compiler/install paths remain in the configuration;
sanitized committed evidence keeps hashes/categories rather than those paths.
This preserves the adopted source/notice requirements; it does not claim a new
license audit, bit-for-bit rebuild or Store certification.

## Validation

The profile checks cover the complete runtime, each missing codec DLL, missing
notice, source/prefix/overlay mismatch, GPL-enabled configuration, accidental
codec input to the default profile and private development exports. Modified
control DLLs are read only, never loaded. A package consumer compiles API-5
automatic-delivery options and verifies six copied native hashes; unsupported
RID and mixed direct references refuse. The default artifact is checked for
absence of codec DLLs/source/profile payload.

CI reuses the existing codec UWP job and cached dependencies, removes private
validation exports after development checks, then packs/uploads the separate
`nuget-stereo-aac-x64` artifact with sources/notices. A lightweight consumer job
checks it alongside `nuget-package`. No new architecture/codec compilation matrix
or release publication is added. Actual PR-head CI remains a separate gate.

Next after review/merge: decide publication of a clean-main candidate and plan
Screenbox consumption/storage/policy integration. No release is published or app
changed incidentally. Speakers and discrete converted surround remain deferred.
