# Integrated stereo AAC AppContainer gate (D75)

Authorized by the user's Proceed after PR #55 merged as `774198d` on 2026-10-13.
The slice validates the enabled production core inside a separate offline Native
AOT x64 UWP package. Speaker audio and discrete 5.1/7.1 output stay deferred.
Published packages and Screenbox's `0.3.0-ci.207` pin are unchanged.

## Contract and boundaries

`SAP2_RUNTIME_VALIDATION` defaults OFF, requires a shared stereo-enabled build,
and exports only the private `src/runtime_validation.h` hook. It reuses
the production preparation factory and C source/cache callback bridge; no copied
conversion implementation or additional runtime DLL is involved. The report is a
48-byte development layout, not a shipping playback API. Default builds contain
no validation symbol; the two existing codec CI jobs enable it explicitly.

The separate `tools/uwp-host/stereo-validation` package has its own identity and
no network capabilities. It uses the actual C# binding assembly (retaining that
assembly's marshalling settings); its AOT shell uses blittable pointer interop.
An internal friend grant allows the development harness to inspect callback
owners without adding a public API. The normal UWP host excludes the nested project.
The existing brokered `StorageFileMediaSource` now checks control before/after
reads and records fixed counters. `TemporaryAacFragmentStore` exclusively creates
one app-owned TemporaryFolder child before ownership transfer. It seals with
CreateNew/rename, supports positional short reads and removes only known files
and its empty child, preserving an independent sibling sentinel.

The offline run probes original ID 42 and checks a borrowed source remains alive;
then it validates DTS MP4/MKV and AAC MP4 through cold late/concurrent reads,
sealed byte-identical rereads, backward reads, all resources through the local
VOD end, cancellation inside a cache callback and a successful retry, terminal
cache/source failure, invalid original selection and idempotent Stop. Retained
audio callbacks must be inert after Stop. Public C API checks separately cover
successful create/destroy without start, missing host credentials before any
network work, and failed create with a zero cache quota. Source release, cache
clear/release, child removal and sibling preservation are asserted after each run.

Normal conversion uses 1,021-byte source reads and 997-byte sealed-cache reads.
A forced 97-byte DTS MP4 read case is an expected resource-limit refusal: the
existing decoder counts requested capacity toward its 256-MiB per-operation
budget, so very small successful reads can exhaust it. This slice preserves that
policy and records the limitation; it does not claim arbitrary tiny reads always
complete conversion. The initial harness run encountered this limit, then the
positive cases were aligned with D70's already tested short-read size.

Bundled fixtures establish authorized package-owned StorageFile access. The
optional `sap2-stereo-runtime.exe pick` activation opens a manual picker and
requires the owned DTS MP4 fixture's hash, allowing a distinct outside-package
grant check. Do not substitute a private movie into this fixed fixture harness.
No visual UI confirmation uses Computer Use.

This hook exercises preparation/resource/cache teardown, not the controller's
receiver supervision or receiver-observed natural EOF. Those retain D74's
scripted-controller evidence and remain manual Apple TV/Mac gates. Fixture timings
are not real-movie performance calibration or a package publication approval.

## Reproduction

Build the root shared UWP core with `SAP2_STEREO_AAC=ON`,
`SAP2_RUNTIME_VALIDATION=ON`, the pinned `stereo-aac` vcpkg feature and the
Release-only UWP codec triplet, as described in [the integration](stereo-aac-playback.md).
Run `scripts/check_uwp_binaries.ps1 -Directory <Release> -StereoAac` for all six DLLs.

From VS 2026 MSBuild with `vswhere.exe` on PATH:

```powershell
msbuild tools/uwp-host/stereo-validation/StereoRuntimeValidation.csproj /restore `
  /t:Publish /p:Configuration=Release /p:Platform=x64 `
  /p:GenerateAppxPackageOnBuild=true /p:AppxBundle=Never `
  /p:UapAppxPackageBuildMode=SideloadOnly `
  /p:Sap2NativeDir=<integrated-UWP-Release> `
  /p:Sap2FfmpegShare=<matching-triplet-share>/ffmpeg `
  /p:Sap2CoreShare=<matching-triplet-share>
```

Extract the unsigned MSIX with MakeAppx and register its manifest in place; never
uninstall the existing UwpHost/Screenbox or remove their profiles as a diagnostic.
Launch `sap2-stereo-runtime.exe`; inspect the fixed-field `stereo-runtime.txt` in
this package's LocalState. It stays alive for 30 seconds after writing results
so the six actually loaded module hashes can be compared with the registered
layout. Raise this private package's manifest version for subsequent updates.
The local registered package is not a published NuGet prerelease.

Enabled desktop CTest adds `runtime_validation_tests` (16 fixture scenarios).
Keep baseline static/shared and enabled shared checks, formatting/diff checks,
and actual-head CI as merge gates. Record the packaged source/binary/host/notices
and outcomes separately from desktop CTest and compile-only UWP inspection.

## Current evidence and next work

[PR #56](https://github.com/ilyalissoboi/send-airplay2/pull/56) is open for review.
The [dated artifact](validation/stereo-aac-uwp-runtime-windows-2026-10-13.json)
records a PASS bundled run at implementation `9371502`, package 0.1.0.2, on
Windows 11 x64 build 26200. All six actually loaded module hashes match the
registered layout. Its three borrowed probes, 16 preparation/resource scenarios
and nine public lifecycle cases pass. Child removal and sibling preservation
were asserted; no receiver was contacted. Package 0.1.0.3 at `5a4e5e3` fixes
bare picker activation. Its complete run passed on 2026-10-14 JST at
`2026-10-13T15:11:08.8621218Z`: three borrowed probes, 17 resource/preparation
cases (including the hash-verified picker-granted owned DTS MP4 outside the
package), and nine public lifecycle cases. The user confirmed fixture selection;
the fixed log records PASS and all six actually loaded DLL hashes match this
run's registered layout. This completes the x64 AppContainer source/cache gate
for these fixtures/providers. Package 0.1.0.2's earlier PASS stays separately
attributed; the initial harness corrections are retained in the artifact.

Baseline Windows Release static/shared CTest passes 36/36 and 37/37; enabled
shared passes 38/38. Named private-hook cases retain independent literal fragment
counts and failure codes. The existing UWP host compiles, all six UWP native DLLs
pass import inspection, and the default shared DLL has no validation export.
Formatting/diff checks pass. Implementation head `5a4e5e3` passed all 16 checks
in run `38415880324`; code/evidence head `8fb2bcc` also passed all 16 checks in
run `38421602489`. Actual final PR-head CI remains the merge gate.

Real-file startup/read-volume/cache/seek measurements follow, then manually observed stereo
video/audio, controls, Stop and natural EOF on available Apple TV/Mac receivers.
Approved package publication and Screenbox integration follow those gates.
